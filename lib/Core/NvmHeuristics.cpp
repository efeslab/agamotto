#include "NvmHeuristics.h"

using namespace llvm;
using namespace std;
using namespace klee;

#include <sstream>

#include "CoreStats.h"
#include "Executor.h"
#include "klee/ExecutionState.h"
#include "klee/TimerStatIncrementer.h"

/* #region NvmStackFrameDesc */

bool NvmStackFrameDesc::containsFunction(llvm::Function *f) const {
  const NvmStackFrameDesc *st = this;
  while (st && !st->isEmpty()) {
    if (st->caller_inst->getFunction() == f) return true;
    st = st->caller_desc.get();
  }

  return false;
}

std::shared_ptr<NvmStackFrameDesc> NvmStackFrameDesc::doReturn(void) const {
  assert(caller_desc && "We returned too far!");
  return caller_desc;
}

std::shared_ptr<NvmStackFrameDesc> 
NvmStackFrameDesc::doCall(const std::shared_ptr<NvmStackFrameDesc> &caller_stack,
                          Instruction *caller, 
                          Instruction *retLoc) const {
  return std::make_shared<NvmStackFrameDesc>(NvmStackFrameDesc(caller_stack, 
                                                               caller, 
                                                               retLoc));
}

std::string NvmStackFrameDesc::str(void) const {
  std::stringstream s;

  s << "Stackframe: ";
  const NvmStackFrameDesc *st = this;
  while (st && !st->isEmpty()) {
    Instruction *i = st->caller_inst;

    Function *f = i->getFunction();
    s << "\n\t" << f->getName().data() << " @ ";
    std::string tmp;
    llvm::raw_string_ostream rs(tmp); 
    i->print(rs);
    s << tmp;

    st = st->caller_desc.get();
  }

  return s.str();
}

bool klee::operator==(const NvmStackFrameDesc &lhs, const NvmStackFrameDesc &rhs) {
  bool callerEq = (!lhs.caller_desc && !rhs.caller_desc) || 
                  (lhs.caller_desc && rhs.caller_desc &&
                    (*lhs.caller_desc == *rhs.caller_desc));
  return callerEq && 
         lhs.caller_inst == rhs.caller_inst &&
         lhs.return_inst == rhs.return_inst;
}

bool klee::operator!=(const NvmStackFrameDesc &lhs, const NvmStackFrameDesc &rhs) {
  return !(lhs == rhs);
}

/* #endregion */

/* #region NvmValueDesc */

std::shared_ptr<NvmValueDesc> NvmValueDesc::doCall(andersen_sptr_t apa, 
                                                   CallInst *ci, 
                                                   Function *f) const 
{
  NvmValueDesc newDesc;
  newDesc.caller_values_ = std::make_shared<NvmValueDesc>(*this);
  newDesc.call_site_ = ci;
  newDesc.mmap_calls_ = mmap_calls_;
  newDesc.not_global_nvm_ = not_global_nvm_;

  if (!f) {
    f = utils::getCallInstFunction(ci);
    if (!f) {
      return std::make_shared<NvmValueDesc>(newDesc);
    }
  }

  for (unsigned i = 0; i < (unsigned)ci->getNumArgOperands(); ++i) {
    Value *op = ci->getArgOperand(i);
    if (!op->getType()->isPtrOrPtrVectorTy()) continue;
    assert(op);

    bool pointsToNvm = true;
    // We actually want the points-to set for this
    std::vector<const Value*> ptsSet;
    // errs() << "doing call instruction stuff\n";
    // errs() << *op << "\n";
    {
      TimerStatIncrementer timer(stats::nvmAndersenTime);
      bool ret = apa->getResult().getPointsToSet(op, ptsSet);
      assert(ret && "could not get points-to set!");
      for (const Value *ptsTo : ptsSet) {
        if (!isNvm(apa, ptsTo)) {
          pointsToNvm = false;
          break;
        }
      }
    }

    if (pointsToNvm) continue;

    if (i >= f->arg_size()) {
      // assert(f->isVarArg() && "argument size mismatch!");
      // newDesc.varargs_contain_nvm_ = true;
      break;
    } else {
      Argument *arg = (f->arg_begin() + i);
      assert(arg);
      // Scalars don't necessarily point to anything.
      if (!arg->getType()->isPtrOrPtrVectorTy()) continue;
      newDesc.not_local_nvm_.insert(arg);
    }

  }

  return std::make_shared<NvmValueDesc>(newDesc);
}

std::shared_ptr<NvmValueDesc> NvmValueDesc::doReturn(andersen_sptr_t apa, 
                                                     ReturnInst *i) const {
  std::shared_ptr<NvmValueDesc> retDesc = caller_values_;
  
  Value *retVal = i->getReturnValue();
  if (retVal && retVal->getType()->isPtrOrPtrVectorTy()) {
    TimerStatIncrementer timer(stats::nvmAndersenTime);
    // errs() << *retVal << "\n";
    std::vector<const Value*> ptsTo;
    bool success = apa->getResult().getPointsToSet(retVal, ptsTo);
    if (success && ptsTo.size()) {
      // errs() << "doRet " << *retVal << "\n";
      retDesc = retDesc->updateState(call_site_, isNvm(apa, retVal));
    } else {
      //  errs() << "doRet doesn't point to a memory object! " << success << " " << ptsTo.size() << "\n";
    }
  }

  return retDesc;
}

bool NvmValueDesc::mayPointTo(andersen_sptr_t apa, const Value *a, const Value *b) const {
  TimerStatIncrementer timer(stats::nvmAndersenTime);

  std::vector<const Value*> aSet, bSet, interSet;
  bool ret = apa->getResult().getPointsToSet(a, aSet);
  if (!ret) errs() << *a << "\n";
  assert(ret && "could not get points-to set!");

  ret = apa->getResult().getPointsToSet(b, bSet);
  if (!ret) errs() << *b << "\n";
  assert(ret && "could not get points-to set!");

  interSet.resize(std::max(aSet.size(), bSet.size()));

  auto it = std::set_intersection(aSet.begin(), aSet.end(),
                                  bSet.begin(), bSet.end(),
                                  interSet.begin());
  
  interSet.resize(it - interSet.begin());

  if (interSet.size()) {
    return true;
  }

  return false;
}

bool NvmValueDesc::pointsToIsEq(andersen_sptr_t apa, const Value *a, const Value *b) const {
  TimerStatIncrementer timer(stats::nvmAndersenTime);

  std::vector<const Value*> aSet, bSet, interSet;
  bool ret = apa->getResult().getPointsToSet(a, aSet);
  if (!ret) errs() << *a << "\n";
  assert(ret && "could not get points-to set!");

  ret = apa->getResult().getPointsToSet(b, bSet);
  if (!ret) errs() << *b << "\n";
  assert(ret && "could not get points-to set!");

  interSet.resize(std::max(aSet.size(), bSet.size()));

  auto it = std::set_difference(aSet.begin(), aSet.end(),
                                bSet.begin(), bSet.end(),
                                interSet.begin());
  
  interSet.resize(it - interSet.begin());

  if (!interSet.size()) {
    return true;
  }

  return false;
}

bool NvmValueDesc::matchesKnownVolatile(andersen_sptr_t apa, const Value *posNvm) const {
  if (isa<GlobalValue>(posNvm)) {
    for (const Value *vol : not_global_nvm_) {
      if (pointsToIsEq(apa, posNvm, vol)) {
        // errs() << "known is " << *vol << "\n";
        return true;
      }
    }
  } else {
    for (const Value *vol : not_local_nvm_) {
      if (pointsToIsEq(apa, posNvm, vol)) {
        // errs() << "known is " << *vol << "\n";
        return true;
      }
    }
  }
  
  return false;
}

std::shared_ptr<NvmValueDesc> NvmValueDesc::updateState(Value *val, 
                                                        bool isNvm) const 
{
  NvmValueDesc vd = *this;

  if (!isNvm && val->getType()->isPtrOrPtrVectorTy()) {
    // errs() << "UPDATE " << *val << "\n";
    if (isa<GlobalValue>(val)) vd.not_global_nvm_.insert(val);
    else vd.not_local_nvm_.insert(val);
  }

  return std::make_shared<NvmValueDesc>(vd);
}

bool NvmValueDesc::isNvm(andersen_sptr_t apa, const Value *ptr) const {

  TimerStatIncrementer timer(stats::nvmAndersenTime);

  std::vector<const Value*> ptsSet;
  bool ret = apa->getResult().getPointsToSet(ptr, ptsSet);
  if (!ret) errs() << *ptr << "\n";
  assert(ret && "could not get points-to set!");

  if (!mmap_calls_.size()) {
    errs() << "\t!!!!cannot point because no calls!\n"; 
  }

  bool may_point_nvm_alloc = false;
  for (const Value *mm : mmap_calls_) {
    if (mayPointTo(apa, ptr, mm)) {
      may_point_nvm_alloc = true;
      // errs() << "\t++++may point to NVM!\n\t\t";
      // errs() << *ptr << "\n\t\t" << *mm << "\n";
      /**
       * We are saying, if this could point to any NVM, we must prove that 
       * for every value it points to, they must all be known to be volatile.
       * Otherwise, they could still be NVM.
       * 
       * We should say, if this has the same points-to set as a known volatile,
       * it should be volatile. However, this doesn't necessarily play nice 
       * with loops. TODO
       */
      #if 0
      std::vector<const Value*> ptsSet;
      bool ret = apa->getResult().getPointsToSet(ptr, ptsSet);
      if (!ret) errs() << *ptr << "\n";
      assert(ret && "could not get points-to set!");
      for (const Value *q : ptsSet) {
        if (!matchesKnownVolatile(apa, q)) return true;
      }
      #else
      for (const Value *l : not_local_nvm_) {
        if (pointsToIsEq(apa, l, ptr)) return false;
      }
      for (const Value *l : not_global_nvm_) {
        if (pointsToIsEq(apa, l, ptr)) return false;
      }
      
      #endif

    } else {
      // errs() << "\t----may NOT point to NVM!\n\t\t";
      // errs() << *ptr << "\n\t\t" << *mm << "\n";
    }
  }
  
  return may_point_nvm_alloc;
}

std::shared_ptr<NvmValueDesc> NvmValueDesc::staticState(llvm::Module *m) {
  NvmValueDesc desc;
  #define N_FN 3
  static const char *fn_names[N_FN] = {"mmap", "mmap64", "klee_pmem_mark_persistent"};
  static Function* mmaps[N_FN];
  for (uint64_t i = 0; i < N_FN; ++i) {
    mmaps[i] = m->getFunction(fn_names[i]);
    if (!mmaps[i]) {
      klee_warning("Could not find function %s! (no calls)", fn_names[i]);
    }
  }
  
  for (Function *mmap : mmaps) {
    if (!mmap) {
      continue;
    }

    for (User *u : mmap->users()) {
      desc.mmap_calls_.insert(u);
    }
  }

  assert(desc.mmap_calls_.size() && "No mmap calls?");

  return std::make_shared<NvmValueDesc>(desc);
}

std::string NvmValueDesc::str(void) const {
  std::stringstream s;
  s << "Value State:\n";
  s << "\n\tNumber of nvm allocation sites: " << mmap_calls_.size();
  for (Value *v : mmap_calls_) {
    std::string tmp;
    llvm::raw_string_ostream rs(tmp);
    v->print(rs);
    s << "\n\t\t" << tmp;
  }
  s << "\n\tNumber of known global runtime non-nvm values: " << not_global_nvm_.size();
  for (Value *v : not_global_nvm_) {
    std::string tmp;
    llvm::raw_string_ostream rs(tmp);
    v->print(rs);
    s << "\n\t\t" << tmp;
  }
  s << "\n\tNumber of known local runtime non-nvm values: " << not_local_nvm_.size();
  for (Value *v : not_local_nvm_) {
    std::string tmp;
    llvm::raw_string_ostream rs(tmp);
    v->print(rs);
    s << "\n\t\t" << tmp;
  }
  return s.str();
}

bool klee::operator==(const NvmValueDesc &lhs, const NvmValueDesc &rhs) {
  bool callerEq = (!lhs.caller_values_ && !rhs.caller_values_) ||
                  (lhs.caller_values_ && rhs.caller_values_ &&
                    (*lhs.caller_values_ == *rhs.caller_values_));
  return lhs.mmap_calls_ == rhs.mmap_calls_ &&
         lhs.not_global_nvm_ == rhs.not_global_nvm_ &&
         lhs.not_local_nvm_ == rhs.not_local_nvm_ &&
         lhs.call_site_ == rhs.call_site_ &&
         callerEq;
}

/* #endregion */

/**
 * NvmHeuristics
 */

/* #region NvmHeuristicInfo (abstract super class) */ 

SharedAndersen NvmHeuristicInfo::createAndersen(llvm::Module &m) {
  TimerStatIncrementer timer(stats::nvmAndersenTime);

  SharedAndersen anders = std::make_shared<AndersenAAWrapperPass>();
  assert(!anders->runOnModule(m) && "Analysis pass should return false!");

  return anders;
}

ValueSet NvmHeuristicInfo::getNvmAllocationSites(Module *m, SharedAndersen &ander) {
  ValueSet all, onlyNvm;
  ander->getResult().getAllAllocationSites(all);
  
  for (const Value *v : all) {
    if (isNvmAllocationSite(m, v)) onlyNvm.insert(v);
  }

  return onlyNvm;
}

bool NvmHeuristicInfo::isNvmAllocationSite(Module *m, const llvm::Value *v) {
  if (const CallInst *ci = dyn_cast<CallInst>(v)) {
    if (const Function *f = ci->getCalledFunction()) {
      if (f == m->getFunction("mmap") ||
          f == m->getFunction("mmap64") ||
          f == m->getFunction("klee_pmem_mark_persistent") || 
          f == m->getFunction("klee_pmem_alloc_pmem")) return true;
    }
  }

  return false;
}

bool NvmHeuristicInfo::isStore(llvm::Instruction *i) {
  return isa<StoreInst>(i);
}

bool NvmHeuristicInfo::isFlush(llvm::Instruction *i) {
  if (CallInst *ci = dyn_cast<CallInst>(i)) {
    Function *f = ci->getCalledFunction();
    if (f && f->isDeclaration()) {
      switch(f->getIntrinsicID()) {
        case Intrinsic::x86_sse2_clflush:
        case Intrinsic::x86_clflushopt:
        case Intrinsic::x86_clwb:
          return true;
        default:
          break;
      }
    }
  }

  return false;
}

bool NvmHeuristicInfo::isFence(llvm::Instruction *i) {
  if (CallInst *ci = dyn_cast<CallInst>(curr_->inst)) {
    Function *f = ci->getCalledFunction();
    if (f && f->isDeclaration()) {
      switch(f->getIntrinsicID()) {
        case Intrinsic::x86_sse_sfence:
        case Intrinsic::x86_sse2_mfence:
          return true;
        default:
          break;
      }
    }
  } else if (isa<AtomicRMWInst>(i)) {
    return true;
  }

  return false;
}

/* #endregion */


/* #region NvmStaticHeuristic */

NvmStaticHeuristic::NvmStaticHeuristic(Executor *executor, KFunction *mainFn) :
  : executor_(executor),
    analysis_(createAndersen(*executor->kmodule->module)),
    weights_(std::make_shared<WeightMap>()),
    priorities_(std::make_shared<WeightMap>()),
    curr_(mainFn->function->getEntryBlock().getFirstNonPHIOrDbgOrLifetime()) {

  computePriority();
}

void NvmStaticHeuristic::computePriority(void) {
  ValueSet nvmSites = getNvmAllocationSites(curr_->getModule(), analysis_);

  /**
   * Calculating the raw weights is easy.
   */

  std::unordered_set<llvm::CallInst*> call_insts;

  for (Function &f : *curr_->getModule()) {
    for (BasicBlock &b : f) {
      for (Instruction &i : b) {
        if (isStore(&i) || isFlush(&i) || isFence(&i)) {
          std::vector<const Value*> ptsSet;

          TimerStatIncrementer timer(stats::nvmAndersenTime);
          bool ret = analysis_->getResult().getPointsToSet(op, ptsSet);
          assert(ret && "could not get points-to set!");
          for (const Value *ptsTo : ptsSet) {
            if (nvmSites.count(ptsTo)) {
              weights_[&i] = 1u;
            }
          }

        } else if (CallInst *ci = dyn_cast<CallInst>(&i)) {
          call_insts.insert(ci);
        }
      }
    }
  }

  /**
   * We also need to fill in the weights for function calls.
   * We'll just give a weight of one, prioritizes immediate instructions.
   */

  for (CallInst *ci : call_insts) {
    std::unordered_set<Function*> possibleFns;
    if (Function *f = utils::getCallInstFunction(ci)) {
      possibleFns.insert(f);
    } else {
      assert(ci->isIndirectCall());

      for (Function &f : *curr_->getModule()) {
        for (unsigned i = 0; i < (unsigned)ci->getNumArgOperands(); ++i) {
          if (f.arg_size() <= i) {
            if (f.isVarArg()) {
              possibleFns.insert(&f);
            }
            break;
          }

          Argument *arg = f.arg_begin() + i;
          Value *val = ci->getArgOperand(i);

          if (arg->getType() != val->getType()) break;
          else if (i + 1 == ci->getNumArgOperands()) possibleFns.insert(&f);
        }
      }
    }

    for (Function *f : possibleFns) {
      for (BasicBlock &bb : *f) {
        for (Instruction &i : bb) {
          if (weights_[&i]) {
            weights_[dyn_cast<Instruction>(ci)] = 1u;
            goto done;
          }
        }
      }
    }

    done: 0;
  }

  /**
   * Now, we do the priority.
   */

  for (Function &f : *curr_->getModule()) {
    llvm::DominatorTree dom(f);

    // Find the ending basic blocks
    std::unordered_set<BasicBlock*> endBlocks;
    std::list<BasicBlock*> bbList;
    bbList.push_back(&f.getEntryBlock());

    while(bbList.size()) {
      BasicBlock *bb = bbList.front();
      bbList.pop_front();

      if (succ_empty(bb)) {
        endBlocks.push_back(bb);
      } else {
        for (BasicBlock *sbb : successors(bb)) {
          if (!dom.dominates(sbb, bb)) bbList.push_back(sbb);
        }
      }
    }

    bbList = endBlocks; // copy

    while (bbList.size()) {
      BasicBlock *bb = bbList.front();
      bbList.pop_front();

      Instruction *pi = bb->getTerminator();
      assert(pi);
      Instruction *i = pi->getPrevNonDebugInstruction();
      while(i) {
        priorities_[i] = priorities_[pi] + weights_[i];
      }

      for (BasicBlock *pbb : predecessors(bb)) {
        if (dom.dominates(bb, pbb)) continue;
        bbList.push_back(pbb);

        Instruction *term = pbb->getTerminator();
        priorities_[term] = std::max(priorities_[term], 
                                     weights_[term] + priorities_[pi]); 
      }
    }
  }
}

/* #endregion */

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2: */
