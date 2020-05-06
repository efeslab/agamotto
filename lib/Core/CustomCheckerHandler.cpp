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

CustomChecker::CustomChecker(Executor &_executor) : executor(_executor) {}

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
  CounterChecker(Executor &_executor) : CustomChecker(_executor), ninvoke(0) {}
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
  PmemObjTxAddChecker(Executor &_executor) 
    : CustomChecker(_executor), in_tx(false), added_ranges() {}

  ~PmemObjTxAddChecker() {}

  void operator()(ExecutionState &state) override {
    Function *f = getFunction(state);
    if (!f) return;

    checkTxBegin(f, state);
    checkTxAdd(f, state);
    checkTxEnd(f, state);
  }
};

}

/* #endregion */

/* #region CustomCheckerHandler */

CustomCheckerHandler::CustomCheckerHandler(Executor &_executor) 
  : executor(_executor) {
  // Bind new custom checkers here!
#define addCC(T) checkers.emplace_back(new T(executor))
  addCC(CounterChecker);
  addCC(PmemObjTxAddChecker);
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
}

/* #endregion */