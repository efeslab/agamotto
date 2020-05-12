//===-- CustomCheckerHandler.cpp ---  -------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "CustomCheckerHandler.h"

#include "Executor.h"
#include "Memory.h"
#include "MemoryManager.h"
#include "Searcher.h"
#include "TimingSolver.h"

#include "klee/ExecutionState.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/Support/Debug.h"
#include "klee/Internal/Support/ErrorHandling.h"
#include "klee/MergeHandler.h"
#include "klee/OptionCategories.h"
#include "klee/Solver/SolverCmdLine.h"

#include "llvm/ADT/Twine.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Module.h"

#include <errno.h>
#include <sys/mman.h>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <fstream>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

using namespace llvm;
using namespace klee;

/* #region CustomChecker */

bool CustomChecker::isMemoryOperation(KInstruction *ki) {
  if (CallBase *cb = dyn_cast<CallBase>(ki->inst)) {
    Function *f = cb->getCalledFunction();
    if (!f) return false;

    switch(f->getIntrinsicID()) {
      case Intrinsic::vastart:
      case Intrinsic::x86_sse2_clflush:
      case Intrinsic::x86_clflushopt:
      case Intrinsic::x86_clwb:
        return true;
      default:
        return false;
    }
  } else if (isa<StoreInst>(ki->inst) || isa<LoadInst>(ki->inst)) {
    return true;
  }

  return false;
}

ResolutionList &CustomChecker::getResolutionList(void) {
  return handler->currentResList;
}

ref<Expr> &CustomChecker::getAddress(void) {
  return handler->addr;
}

Module *CustomChecker::getModule(void) {
  return executor.kmodule->module.get();
}

ref<Expr> CustomChecker::getOpValue(ExecutionState &state, int opNum) {
  return executor.eval(state.prevPC(), opNum, state).value;
}

ObjectPair &&CustomChecker::resolveAddress(ExecutionState &state, ref<Expr> addr) {
  ObjectPair op;
  bool success;
  executor.solver->setTimeout(executor.coreSolverTimeout);
  assert(state.addressSpace.resolveOne(state, executor.solver, addr, op, success));
  executor.solver->setTimeout(time::Span());
  assert(success);

  return std::move(op);
}

CustomChecker::CustomChecker(CustomCheckerHandler *_handler, Executor &_executor) 
  : handler(_handler), executor(_executor) {}

CustomChecker::~CustomChecker() {}

/* #endregion */

/* #region Custom Checker Implementations */

namespace klee {

/**
 * An example checker. Accumulates the number of times it was invoked and 
 * outputs the value on destruction.
 */
class CounterChecker final : public CustomChecker {
private:
  uint64_t ninvoke;
public:
  CounterChecker(CustomCheckerHandler *_handler, Executor &_executor) 
    : CustomChecker(_handler, _executor), ninvoke(0) {}
  
  ~CounterChecker() {
    klee_message("CounterChecker: invoked %lu times!\n", ninvoke);
  }
  void operator()(ExecutionState __attribute__((unused)) &state) override {
    ninvoke++;
  }
};

/**
 * It's purpose in life is to find ranges added to pmemobj transactions more
 * than once.
 * 
 * Flattens transactions because I believe that's what libpmemobj does.
 */
class PmemObjTxAddChecker final : public CustomChecker {
private:
  
  bool in_tx;

  // [addr, addr+offset)
  typedef std::pair<ref<Expr>, ref<Expr>> TxRange;
  std::list<TxRange> added_ranges;

  Function *getFunction(ExecutionState &state) {
    Instruction *i = state.prevPC()->inst;
    if (!isa<CallBase>(i)) return nullptr;

    CallSite cs(i);
    Value *fp = cs.getCalledValue();
    return executor.getTargetFunction(fp, state);
  }

  bool overlaps(ExecutionState &state, TxRange &r) {
    /**
     * If it may be true that r overlaps, meaning
     * added.first < r.second && r.first < added.second
     * 
     * then we report an overlap.
     */
    for (auto &added : added_ranges) {
      // 1. Construct bounds
      auto firstBound = UltExpr::create(added.first, r.second);
      auto secondBound = UltExpr::create(r.first, added.second);
      auto overlapBound = AndExpr::create(firstBound, secondBound);

      bool mayOverlap = false;
      bool success = executor.solver->mayBeTrue(state, overlapBound, mayOverlap);
      assert(success);

      if (mayOverlap) return true;
    }

    return false;
  }

  void checkTxBegin(Function *f, ExecutionState &state) {
    if (in_tx) return;

    if (f->getName() == "pmemobj_tx_begin") {
      in_tx = true;
    }
  }

  void checkTxAdd(Function *f, ExecutionState &state) {
    auto fnName = f->getName();

    if (fnName != "pmemobj_tx_add_common") return;

    // Get the range from the "struct tx_range_def".
    // -- field 0 is "offset", uint64_t
    // -- field 1 is "size", uint64_t

    // 1. Get the memory object from the stack.
    KFunction *kf = executor.kmodule->functionMap[f];
    ref<Expr> address = executor.getArgumentCell(state.stack().back(), kf, 1).value;

    // 2. Resolve the object 
    ObjectPair op;
    bool success;
    executor.solver->setTimeout(executor.coreSolverTimeout);
    assert(state.addressSpace.resolveOne(state, executor.solver, address, op, success));
    executor.solver->setTimeout(time::Span());
    assert(success);

    // 3. Read the offset and size from the object
    const ObjectState *cos = op.second;
    assert(cos);
    auto range_start = cos->read(0, Expr::Int64);
    auto size = cos->read(sizeof(uint64_t), Expr::Int64);

    // 4. Get end bound
    auto range_end = AddExpr::create(range_start, size);
    
    auto new_range = TxRange(range_start, range_end);

    // 5. Check for overlaps. If overlap, there's a bug!
    if (overlaps(state, new_range)) {
      executor.terminateStateOnError(state, 
          "libpmemobj: overlapping TX add!!", Executor::TerminateReason::PMem);
    }

    // 6. Add the new range.
    added_ranges.push_back(new_range);

  }

  void checkTxEnd(Function *f, ExecutionState &state) {
    auto fnName = f->getName();

    if (fnName == "pmemobj_tx_end") {
      in_tx = false;
      added_ranges.clear();
    }
  }

public:
  PmemObjTxAddChecker(CustomCheckerHandler *_handler, Executor &_executor) 
    : CustomChecker(_handler, _executor), in_tx(false), added_ranges() {}

  ~PmemObjTxAddChecker() {}

  void operator()(ExecutionState &state) override {
    Function *f = getFunction(state);
    if (!f) return;

    checkTxBegin(f, state);
    checkTxAdd(f, state);
    checkTxEnd(f, state);
  }
};

/**
 * It's purpose in life is to filter out volatile 
 */
class VolatileFilter final : public CustomChecker {
  private:
    std::string ignoreName = "struct.volatile_byte";

  public:
    VolatileFilter(CustomCheckerHandler *_handler, Executor &_executor) 
      : CustomChecker(_handler, _executor) {}

    ~VolatileFilter() {}

    void operator()(ExecutionState &state) override {
      // if (LoadInst *li = dyn_cast<LoadInst>(state.prevPC()->inst)) {
      //   Type *t = li->getPointerOperandType();
      //   errs() << *li << ": " << *t << "\n";
      // }
      // if (StoreInst *si = dyn_cast<StoreInst>(state.prevPC()->inst)) {
      //   Type *t = si->getPointerOperandType();
      //   errs() << *si << ": " << *t << "\n";
      // }
      Instruction *i = state.prevPC()->inst;
      // errs() << "NEXT INST: "<< *i << "\n";
      assert(i);
      for (unsigned opNum = 0; opNum < i->getNumOperands(); ++opNum) {
        Value *op = i->getOperand(opNum);
        assert(op);
        // if (!op) {
        //   errs() << *i << " OP " << opNum << "\n";
        //   assert(op);
        // }

        // errs() << op << " OP# " << opNum << "\n";
        Type *t = op->getType();
        if (!t) {
          // errs() << *i << "\n";
          assert(false);
        }
        // errs() << *t << "\n";
        if (PointerType *pt = dyn_cast<PointerType>(t)) {
          t = pt->getElementType();
        }
        if (StructType *st = dyn_cast<StructType>(t)) {
          if (st->getStructName() == ignoreName) {
            // Add to ignore
            ref<Expr> addr = getOpValue(state, opNum);
            ObjectPair op = resolveAddress(state, addr);
            if (op.second && isa<PersistentState>(op.second)) {
              auto *wos = state.addressSpace.getWriteable(op.first, op.second);
              auto *ps = dyn_cast<PersistentState>(wos);
              // Now do offset calculation
              auto offset = op.first->getOffsetExpr(addr);
              llvm::DataLayout* dl = new llvm::DataLayout(getModule());
              uint64_t structSz = dl->getTypeStoreSize(st) * 8;
              ps->addIgnoreOffset(offset, structSz);
              errs() << "Adding " << offset << " with size " << structSz << " to ignore!\n";
            }
          }
        }
      }
    }
};

}

/* #endregion */

/* #region CustomCheckerHandler */

CustomCheckerHandler::CustomCheckerHandler(Executor &_executor) 
  : executor(_executor), addr(nullptr) {
  // Bind new custom checkers here!
#define addCC(T) checkers.emplace_back(new T(this, executor))
  addCC(CounterChecker);
  addCC(PmemObjTxAddChecker);
  addCC(VolatileFilter);
#undef addCC
}

CustomCheckerHandler::~CustomCheckerHandler() {
  for (CustomChecker *cc : checkers) {
    delete cc;
  }
}

void CustomCheckerHandler::handle(ExecutionState &state) {
  if (checkers.empty()) {
    klee_warning_once(0, "CustomCheckerHandler: no registered checkers!\n");
    return;
  }

  for (CustomChecker *cc : checkers) {
    (*cc)(state);
  }

  // Delete the memory stuff so it isn't accidentally reused.
  addr = nullptr;
  currentResList.clear();
}

void CustomCheckerHandler::setMemoryResolution(ref<Expr> address, ResolutionList &&rl) {
  currentResList = std::move(rl);
  addr = address;
}

/* #endregion */