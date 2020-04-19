#include "NvmHeuristics.h"

using namespace llvm;
using namespace std;
using namespace klee;

#include <sstream>
#include <utility>

#include "CoreStats.h"
#include "Executor.h"
#include "klee/ExecutionState.h"
#include "klee/TimerStatIncrementer.h"

/* #region CL Options */

namespace klee {
  cl::OptionCategory NvmCat("NVM options",
                            "These options control some of NVM-related features of this NVM-version of KLEE.");

  #define clNvmEnumValN(t) (clEnumValN(t, \
                              NvmHeuristicBuilder::stringify(t), \
                              NvmHeuristicBuilder::explanation(t)))

  cl::opt<NvmHeuristicBuilder::Type>
  NvmCheck("nvm-heuristic-type",
        cl::desc("Choose the search heuristic used by the NVM searcher."),
        cl::values(clNvmEnumValN(NvmHeuristicBuilder::Type::None),
                   clNvmEnumValN(NvmHeuristicBuilder::Type::Static),
                   clNvmEnumValN(NvmHeuristicBuilder::Type::InsensitiveDynamic),
                   clNvmEnumValN(NvmHeuristicBuilder::Type::ContextDynamic)
                   KLEE_LLVM_CL_VAL_END),
        cl::init(NvmHeuristicBuilder::Type::None),
        cl::cat(NvmCat));

  #undef clNvmEnumValN

  cl::alias PmCheck("pm-check-type",
                    cl::desc("Alias for nvm-check-type"),
                    cl::NotHidden,
                    cl::aliasopt(NvmCheck),
                    cl::cat(NvmCat));
}
/* #endregion */


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

NvmStackFrameDesc::Shared NvmStackFrameDesc::doCall(Instruction *caller, 
                                                    Instruction *retLoc) {
  NvmStackFrameDesc desc(shared_from_this(), caller, retLoc);
  return std::make_shared<NvmStackFrameDesc>(desc);
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

NvmValueDesc::Shared NvmValueDesc::doCall(CallInst *ci, Function *f) const {
  NvmValueDesc newDesc;
  newDesc.andersen_ = andersen_;
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
      bool ret = andersen_->getResult().getPointsToSet(op, ptsSet);
      assert(ret && "could not get points-to set!");
      for (const Value *ptsTo : ptsSet) {
        if (!isNvm(ptsTo)) {
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

NvmValueDesc::Shared NvmValueDesc::doReturn(ReturnInst *i) const {
  NvmValueDesc::Shared retDesc = caller_values_;
  
  Value *retVal = i->getReturnValue();
  if (retVal && retVal->getType()->isPtrOrPtrVectorTy()) {
    TimerStatIncrementer timer(stats::nvmAndersenTime);
    // errs() << *retVal << "\n";
    std::vector<const Value*> ptsTo;
    bool success = andersen_->getResult().getPointsToSet(retVal, ptsTo);
    if (success && ptsTo.size()) {
      // errs() << "doRet " << *retVal << "\n";
      retDesc = retDesc->updateState(call_site_, isNvm(retVal));
    } else {
      //  errs() << "doRet doesn't point to a memory object! " << success << " " << ptsTo.size() << "\n";
    }
  }

  return retDesc;
}

bool NvmValueDesc::mayPointTo(const Value *a, const Value *b) const {
  TimerStatIncrementer timer(stats::nvmAndersenTime);

  std::vector<const Value*> aSet, bSet, interSet;
  bool ret = andersen_->getResult().getPointsToSet(a, aSet);
  if (!ret) errs() << *a << "\n";
  assert(ret && "could not get points-to set!");

  ret = andersen_->getResult().getPointsToSet(b, bSet);
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

bool NvmValueDesc::pointsToIsEq(const Value *a, const Value *b) const {
  TimerStatIncrementer timer(stats::nvmAndersenTime);

  std::vector<const Value*> aSet, bSet, interSet;
  bool ret = andersen_->getResult().getPointsToSet(a, aSet);
  if (!ret) errs() << *a << "\n";
  assert(ret && "could not get points-to set!");

  ret = andersen_->getResult().getPointsToSet(b, bSet);
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

bool NvmValueDesc::matchesKnownVolatile(const Value *posNvm) const {
  if (isa<GlobalValue>(posNvm)) {
    for (const Value *vol : not_global_nvm_) {
      if (pointsToIsEq(posNvm, vol)) {
        // errs() << "known is " << *vol << "\n";
        return true;
      }
    }
  } else {
    for (const Value *vol : not_local_nvm_) {
      if (pointsToIsEq(posNvm, vol)) {
        // errs() << "known is " << *vol << "\n";
        return true;
      }
    }
  }
  
  return false;
}

NvmValueDesc::Shared NvmValueDesc::updateState(Value *val, bool isNvm) const {
  NvmValueDesc vd = *this;

  if (!isNvm && val->getType()->isPtrOrPtrVectorTy()) {
    // errs() << "UPDATE " << *val << "\n";
    if (isa<GlobalValue>(val)) vd.not_global_nvm_.insert(val);
    else vd.not_local_nvm_.insert(val);
  }

  return std::make_shared<NvmValueDesc>(vd);
}

bool NvmValueDesc::isNvm(const Value *ptr) const {

  TimerStatIncrementer timer(stats::nvmAndersenTime);

  std::vector<const Value*> ptsSet;
  bool ret = andersen_->getResult().getPointsToSet(ptr, ptsSet);
  if (!ret) {
    return false;
  }
  
  if (!mmap_calls_.size()) {
    errs() << "\t!!!!cannot point because no calls!\n"; 
  }

  bool may_point_nvm_alloc = false;
  for (const Value *mm : mmap_calls_) {
    if (mayPointTo(ptr, mm)) {
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
        if (pointsToIsEq(l, ptr)) return false;
      }
      for (const Value *l : not_global_nvm_) {
        if (pointsToIsEq(l, ptr)) return false;
      }
      
      #endif

    } else {
      // errs() << "\t----may NOT point to NVM!\n\t\t";
      // errs() << *ptr << "\n\t\t" << *mm << "\n";
    }
  }
  
  return may_point_nvm_alloc;
}

NvmValueDesc::Shared NvmValueDesc::staticState(SharedAndersen apa, llvm::Module *m) {
  NvmValueDesc desc;
  desc.andersen_ = apa;
  desc.mmap_calls_ = utils::getNvmAllocationSites(m, apa);

  assert(desc.mmap_calls_.size() && "No mmap calls?");

  return std::make_shared<NvmValueDesc>(desc);
}

std::string NvmValueDesc::str(void) const {
  std::stringstream s;
  s << "Value State:\n";
  s << "\n\tNumber of nvm allocation sites: " << mmap_calls_.size();
  for (const Value *v : mmap_calls_) {
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

/* #region NvmContextDesc */

NvmContextDesc::NvmContextDesc(SharedAndersen anders,
                               NvmStackFrameDesc::Shared stack, 
                               NvmValueDesc::Shared initialArgs,
                               NvmContextDesc::Shared p,
                               Function *f) 
  : andersen(anders),
    stackFrame(stack),
    valueState(initialArgs),
    parent(p),
    function(f) {}

NvmContextDesc::NvmContextDesc(SharedAndersen anders, 
                               Module *m, 
                               Function *main) 
  : andersen(anders),
    stackFrame(NvmStackFrameDesc::empty()),
    valueState(NvmValueDesc::staticState(anders, m)),
    parent(nullptr),
    function(main) {}

uint64_t NvmContextDesc::constructCalledContext(llvm::CallInst *ci, 
                                                llvm::Function *f) {
  assert(ci && f && "get out of here with your null parameters");
  Instruction *retLoc = ci->getNextNode();
  assert(retLoc && "could not get the return instruction");
  if (!ci->getNextNode()) errs() << *ci << "\n";
  NvmContextDesc calledCtx(andersen, 
                           stackFrame->doCall(ci, ci->getNextNode()), 
                           valueState->doCall(ci, f),
                           shared_from_this(),
                           f);
  auto sharedCtx = std::make_shared<NvmContextDesc>(calledCtx);
  sharedCtx->setPriorities();
  contexts[ci] = sharedCtx;

  return sharedCtx->getRootPriority();
}

uint64_t NvmContextDesc::constructCalledContext(llvm::CallInst *ci) {
  if (Function *f = ci->getCalledFunction()) {
    if (!stackFrame->containsFunction(f)) {
      return constructCalledContext(ci, f);
    } else {
      // Recursion risk.
      return hasCoreWeight ? 1lu : 0lu;
    }
  } 

  // We will wait until runtime to resolve this function pointer.
  return 1lu;
}

bool NvmContextDesc::isaCoreInst(Instruction *i) const {
  if (utils::isFence(i)) return true;

  if (utils::isFlush(i)) {
    CallInst *ci = dyn_cast<CallInst>(i);
    assert(ci && "flushes should always be some kind of function call");
    Value *v = ci->getArgOperand(0);
    assert(v && "no arg0!");

    return valueState->isNvm(v);
  }

  if (auto *si = dyn_cast<StoreInst>(i)) {
    return valueState->isNvm(si->getPointerOperand());
  }

  return false;
}

bool NvmContextDesc::isaAuxInst(Instruction *i) const {
  if (auto *ci = dyn_cast<CallInst>(i)) {
    if (auto *f = ci->getCalledFunction()) {
      if (f->isDeclaration() || f->isIntrinsic()) return false;

      // Has function body
      return true;
    }

    // Is a function pointer
    return true;
  }

  return isa<ReturnInst>(i);
}

uint64_t NvmContextDesc::computeAuxInstWeight(Instruction *i) {
  if (isa<ReturnInst>(i)) {
    if (parent && parent->hasCoreWeight) return 1lu;
    return 0lu;
  } else if (auto *ci = dyn_cast<CallInst>(i)) {
    return constructCalledContext(ci);
  }

  assert(false && "should never reach here!");
  return 0lu;
}

void NvmContextDesc::doPriorityPropagation(void) {
  std::list<BasicBlock*> toProp;

  for (BasicBlock &bb : *function) {
    if (succ_empty(&bb)) toProp.push_back(&bb);
  }

  // Now, we bubble up the priorities.
  std::unordered_set<BasicBlock*> traversed; // Loop detection
  while (toProp.size()) {
    BasicBlock *bb = toProp.front();
    toProp.pop_front();

    // Just in case this basic block is it's own predecessor or something.
    traversed.insert(bb);

    // Bubble up priorities along the instructions in this basic block.
    Instruction *curr = bb->getTerminator();
    if (!priorities[curr]) {
      priorities[curr] = weights[curr];
    }

    while(Instruction *pred = curr->getPrevNode()) {
      priorities[pred] = weights[pred] + priorities[curr];
      curr = pred;
    }

    // Now, we need to get the predecessor basic blocks
    for (BasicBlock *predBB : predecessors(bb)) {
      Instruction *pterm = predBB->getTerminator();
      uint64_t basePriority = weights[pterm] + priorities[curr];
      uint64_t currPriority = priorities[pterm];
      // For loops, if the predecessors have already been traversed, but this
      // block changed the bottom-most priority, then we add it to the list
      // to repropagate it anyways.
      if (!traversed.count(predBB) || (basePriority && !currPriority)) {
        priorities[pterm] = basePriority + weights[pterm];
        toProp.push_back(predBB);
      } 
    }
  }
}

void NvmContextDesc::setPriorities(void) {
  // I will accumulate these as I iterate so we don't have to re-iterate over
  // the entire function as we resolve core instructions.
  std::list<Instruction*> auxInsts;
  errs() << stackFrame->str() << "\n";

  for (BasicBlock &bb : *function) {
    for (Instruction &i : bb) {
      if (isaCoreInst(&i)) {
        weights[&i] = 1lu;
        hasCoreWeight = true;
      } else if (isaAuxInst(&i)) {
        auxInsts.push_back(&i);
      }

      assert(!isa<InvokeInst>(&i) && "we don't handle this!");
    }
  }

  for (Instruction *i : auxInsts) {
    weights[i] = computeAuxInstWeight(i);
  }

  doPriorityPropagation();
}

NvmContextDesc::Shared NvmContextDesc::tryGetNextContext(KInstruction *pc,
                                                         KInstruction *nextPC) {
  if (auto *ri = dyn_cast<ReturnInst>(pc->inst)) {
    auto retDesc = parent->dup();
    retDesc->valueState = valueState->doReturn(ri);
    return retDesc;
  }

  if (auto *ci = dyn_cast<CallInst>(pc->inst)) {
    if (pc->inst->getNextNode() != nextPC->inst) {
      errs() << "**********" << *pc->inst << "\n" << *nextPC->inst << "\n";
      if (!contexts.count(ci)) {
        constructCalledContext(ci, nextPC->inst->getFunction());
        setPriorities();
        return contexts.at(ci);
      }
      return contexts.at(ci);
    }
  }

  return shared_from_this();
}

NvmContextDesc::Shared NvmContextDesc::tryUpdateContext(Value *v, bool isValNvm) {
  NvmValueDesc::Shared newDesc = valueState->updateState(v, isValNvm);
  if (valueState->isNvm(v) != newDesc->isNvm(v)) {
    assert(false && "implement!");
  }

  return shared_from_this();
}

NvmContextDesc::Shared NvmContextDesc::tryResolveFnPtr(CallInst *ci, Function *f) {
  if (contexts.count(ci) && contexts.at(ci)->function == f) {
    return shared_from_this(); 
  }

  auto copy = dup();

  copy->constructCalledContext(ci, f);
  copy->doPriorityPropagation();
  return copy;
}

std::string NvmContextDesc::str() const {
  std::stringstream s;
  s << stackFrame->str() << "\n";
  s << valueState->str() << "\n";
  return s.str();
}

/* #endregion */

/**
 * NvmHeuristics
 */

/* #region NvmHeuristicInfo (abstract super class) */ 

NvmHeuristicInfo::~NvmHeuristicInfo() {}

/* #endregion */


/* #region NvmStaticHeuristic */

NvmStaticHeuristic::NvmStaticHeuristic(Executor *executor, KFunction *mainFn) 
  : executor_(executor),
    analysis_(utils::createAndersen(*executor->kmodule->module)),
    weights_(nullptr),
    priorities_(nullptr),
    curr_(mainFn->function->getEntryBlock().getFirstNonPHIOrDbgOrLifetime()),
    module_(executor->kmodule->module.get()),
    nvmSites_(utils::getNvmAllocationSites(module_, analysis_)) {}

bool NvmStaticHeuristic::modifiesNvm(Instruction *i) const {
  if (isa<StoreInst>(i) || utils::isFlush(i)) {
    Value *v = dyn_cast<Value>(i);
    if (utils::isFlush(i)) {
      assert(isa<CallInst>(i));
      v = dyn_cast<CallInst>(i)->getArgOperand(0);
    }
    std::vector<const Value*> ptsSet;

    TimerStatIncrementer timer(stats::nvmAndersenTime);
    bool ret = analysis_->getResult().getPointsToSet(v, ptsSet);

    if (!ret) {
      if (getCurrentNvmSites().count(v)) {
        return true;
      } else if (AllocaInst *ai = dyn_cast<AllocaInst>(i)) {
        if (ai->getType()->isFunctionTy()) return 0u;
      } else if (StoreInst *si = dyn_cast<StoreInst>(i)) {
        if (isa<GlobalValue>(si->getPointerOperand()->stripPointerCasts())) return 0u;

        assert(analysis_->getResult().getPointsToSet(si->getPointerOperand(), ptsSet));
      } else {
        errs() << *i << "\n";
        assert(false && "could not get points-to!");
      }
    }
    
    for (const Value *ptsTo : ptsSet) {
      if (getCurrentNvmSites().count(ptsTo)) {
        return true;
      }
    }
  }

  return false;
}

uint64_t NvmStaticHeuristic::computeInstWeight(Instruction *i) const {

  if (modifiesNvm(i) || isNvmAllocSite(i)) {
    return 1u;
  } else if (utils::isFence(i) && getCurrentNvmSites().size()) {
    return 1u;
  }

  return 0u;
}

void NvmStaticHeuristic::computePriority(void) {
  /**
   * Calculating the raw weights is easy.
   */
  resetWeights();

  std::unordered_set<llvm::CallInst*> call_insts;

  for (Function &f : *module_) {
    for (BasicBlock &b : f) {
      for (Instruction &i : b) {
        (*weights_)[&i] = 0lu;
        (*priorities_)[&i] = 0lu;
        if (mayHaveWeight(&i)) {
          (*weights_)[&i] = computeInstWeight(&i);
        } else if (CallInst *ci = dyn_cast<CallInst>(&i)) {
          if (!ci->isInlineAsm()) call_insts.insert(ci);
        }
      }
    }
  }

  /**
   * We also need to fill in the weights for function calls.
   * We'll just give a weight of one, prioritizes immediate instructions.
   */
  bool c = false;
  do {
    c = false;
    for (CallInst *ci : call_insts) {
      std::unordered_set<Function*> possibleFns;
      if (Function *f = utils::getCallInstFunction(ci)) {
        possibleFns.insert(f);
      } else if (Function *f = ci->getCalledFunction()) {
        possibleFns.insert(f);
      } else {
        if (!ci->isIndirectCall()) errs() << *ci << "\n";
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
            if ((*weights_)[&i] && !(*weights_)[dyn_cast<Instruction>(ci)]) {
              c = true;
              (*weights_)[dyn_cast<Instruction>(ci)] = 1u;
              goto done;
            }
          }
        }
      }

      done: (void)0;
    }
    errs() << "C " << c << "\n";
  } while (c);

  /**
   * Now, we do the priority.
   */

  for (Function &f : *module_) {
    if (f.empty()) continue;
    llvm::DominatorTree dom(f);

    // Find the ending basic blocks
    std::unordered_set<BasicBlock*> endBlocks, bbSet, traversed;
  
    BasicBlock *entry = &f.getEntryBlock();
    assert(entry);
    bbSet.insert(entry);

    while(bbSet.size()) {
      BasicBlock *bb = *bbSet.begin();
      assert(bb);
      bbSet.erase(bbSet.begin());
      traversed.insert(bb);

      if (succ_empty(bb)) {
        endBlocks.insert(bb);
      } else {
        for (BasicBlock *sbb : successors(bb)) {
          assert(sbb);
          if (traversed.count(sbb)) continue;
          if (!dom.dominates(sbb, bb)) bbSet.insert(sbb);
        }
      }
    }

    // errs() << "\tfound terminators" << "\n";
    bbSet = endBlocks;

    /**
     * Propagating the priority is slightly trickier than just finding the 
     * terminal basic blocks, as different paths can have different priorities.
     * So, we annotate the propagated with the priority. If the priority changed,
     * then reprop.
     */

    std::unordered_map<BasicBlock*, uint64_t> prop;

    while (bbSet.size()) {
      BasicBlock *bb = *bbSet.begin();
      assert(bb);
      bbSet.erase(bbSet.begin());

      Instruction *pi = bb->getTerminator();
      if (!pi) {
        assert(f.isDeclaration());
        continue; // empty body
      }

      if (prop.count(bb) && prop[bb] == (*priorities_)[pi]) {
        continue;
      }
      prop[bb] = (*priorities_)[pi];

      Instruction *i = pi->getPrevNode();
      while (i) {
        (*priorities_)[i] = (*priorities_)[pi] + (*weights_)[i];
        pi = i;
        i = pi->getPrevNode();
      }

      for (BasicBlock *pbb : predecessors(bb)) {
        assert(pbb);
        // if (!dom.dominates(bb, pbb)) bbSet.insert(pbb);
        // bbSet.insert(pbb);
        if (!prop.count(pbb)) bbSet.insert(pbb);

        Instruction *term = pbb->getTerminator();
        (*priorities_)[term] = std::max((*priorities_)[term], 
                                      (*weights_)[term] + (*priorities_)[pi]); 
      }

    }
  }

  /**
   * We're actually still not done. For each function, the base priority needs to
   * be boosted by the priority of the call site return locations.
   */
  bool changed = false;
  do {
    changed = false;
    for (Function &f : *module_) {
      if (f.empty()) continue;
      for (Use &u : f.uses()) {
        User *usr = u.getUser();
        // errs() << "usr:" << *usr << "\n";
        if (Instruction *i = dyn_cast<Instruction>(usr)) {
          // errs() << "\t=> " << (*priorities_)[i] << "\n";
          Instruction *retLoc = i->getNextNode();
          assert(retLoc);
          // errs() << "\t=> " << (*priorities_)[retLoc] << "\n";
          if ((*priorities_)[retLoc]) {
            for (BasicBlock &bb : f) {
              for (Instruction &ii : bb) {
                if (!(*priorities_)[&ii]) {
                  changed = true;
                  (*priorities_)[&ii] = (*priorities_)[retLoc];
                }
              }
            }
          }
        }
      }
    }

    for (CallInst *ci : call_insts) {
      Instruction *retLoc = ci->getNextNode();
      assert(retLoc);

      std::unordered_set<Function*> possibleFns;
      if (Function *f = utils::getCallInstFunction(ci)) {
        possibleFns.insert(f);
      } else if (Function *f = ci->getCalledFunction()) {
        possibleFns.insert(f);
      } else {
        if (!ci->isIndirectCall()) errs() << *ci << "\n";
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
            if (!(*priorities_)[&i] && (*priorities_)[retLoc]) {
              changed = true;
              (*priorities_)[&i] = (*priorities_)[retLoc];
            }
          }
        }
      }

    }
    errs() << "changed = " << changed << "\n";
  } while (changed);
  
} 

void NvmStaticHeuristic::dump(void) const {
  uint64_t nonZeroWeights = 0, nonZeroPriorities = 0;

  for (const auto &p : *weights_) nonZeroWeights += (p.second > 0);
  for (const auto &p : *priorities_) nonZeroPriorities += (p.second > 0);

  double pWeights = 100.0 * ((double)nonZeroWeights / (double)weights_->size());
  double pPriorities = 100.0 * ((double)nonZeroPriorities / (double)priorities_->size());

  for (auto *v : utils::getNvmAllocationSites(executor_->kmodule->module.get(), analysis_)) {
    errs() << *v << "\n";
  }

  fprintf(stderr, "NvmStaticHeuristic:\n"
                  "\tNVM allocation sites: %lu\n"
                  "\t%% instructions with weight: %f%%\n"
                  "\t%% instructions with priority: %f%%\n",
                  nvmSites_.size(), pWeights, pPriorities);
}

/* #endregion */

/* #region NvmInsensitiveDynamicHeuristic */

bool NvmInsensitiveDynamicHeuristic::needsRecomputation() const { 
  for (Function &f : *module_) {
    for (BasicBlock &b : f) {
      for (Instruction &i : b) {
        if (mayHaveWeight(&i)) {
          if ((*weights_)[&i] != computeInstWeight(&i)) {
            return true;
          }
        }
      }
    }
  }

  return false;
}

bool NvmInsensitiveDynamicHeuristic::modifiesNvm(Instruction *i) const {
  if (isa<StoreInst>(i) || utils::isFlush(i)) {
    Value *v = dyn_cast<Value>(i);
    if (utils::isFlush(i)) {
      assert(isa<CallInst>(i));
      v = dyn_cast<CallInst>(i)->getArgOperand(0);
    }
    std::vector<const Value*> ptsSet;

    TimerStatIncrementer timer(stats::nvmAndersenTime);
    bool ret = analysis_->getResult().getPointsToSet(v, ptsSet);

    if (!ret) {
      if (getCurrentNvmSites().count(v)) {
        return true;
      } else if (AllocaInst *ai = dyn_cast<AllocaInst>(i)) {
        if (ai->getType()->isFunctionTy()) return 0u;
      } else if (StoreInst *si = dyn_cast<StoreInst>(i)) {
        if (isa<GlobalValue>(si->getPointerOperand()->stripPointerCasts())) return 0u;

        assert(analysis_->getResult().getPointsToSet(si->getPointerOperand(), ptsSet));
      } else {
        errs() << *i << "\n";
        assert(false && "could not get points-to!");
      }
    }
    
    bool pointsToNvm = false;
    for (const Value *ptsTo : ptsSet) {
      if (getCurrentNvmSites().count(ptsTo)) {
        pointsToNvm = true;
        break;
      }
    }

    if (!pointsToNvm) return false;

    ValueSet truePtsSet(ptsSet.begin(), ptsSet.end());

    for (const Value *vol : knownVolatiles_) {
      std::vector<const Value*> volPtsSet;
      assert(analysis_->getResult().getPointsToSet(vol, volPtsSet));
      ValueSet trueVolSet(volPtsSet.begin(), volPtsSet.end());

      if (truePtsSet == trueVolSet) return false;
    }

    return true;
  }

  return false;
}

void NvmInsensitiveDynamicHeuristic::updateCurrentState(ExecutionState *es, 
                                                        KInstruction *pc, 
                                                        bool isNvm) {
  bool modified = false;
  if (nvmSites_.count(pc->inst)) {
    if (isNvm) {
      activeNvmSites_.insert(pc->inst);
    } else {
      activeNvmSites_.erase(pc->inst);
    }
    modified = true;
  } else {
    Value *v = nullptr;
    if (LoadInst *li = dyn_cast<LoadInst>(pc->inst)) {
      v = li->getPointerOperand();
    } else if (StoreInst *si = dyn_cast<StoreInst>(pc->inst)) {
      v = si->getPointerOperand();
    }

    if (v) {
      // We only want to add a known volatile to something that points to NVM
      std::vector<const Value*> volPtsSet;
      assert(analysis_->getResult().getPointsToSet(v, volPtsSet));

      if (nvmSites_.count(v)) {
        if (isNvm) {
          knownVolatiles_.erase(pc->inst);
        } else {
          knownVolatiles_.insert(pc->inst);
        }
        modified = true;
      }
    }
  }
  
  // I like lazy eval
  if (modified && needsRecomputation()) {
    computePriority();
    dump();
  } 
}

void NvmInsensitiveDynamicHeuristic::dump(void) const {
  uint64_t nonZeroWeights = 0, nonZeroPriorities = 0;

  for (const auto &p : *weights_) nonZeroWeights += (p.second > 0);
  for (const auto &p : *priorities_) nonZeroPriorities += (p.second > 0);

  double pWeights = 100.0 * ((double)nonZeroWeights / (double)weights_->size());
  double pPriorities = 100.0 * ((double)nonZeroPriorities / (double)priorities_->size());

  for (auto *v : utils::getNvmAllocationSites(executor_->kmodule->module.get(), analysis_)) {
    errs() << *v << "\n";
  }

  fprintf(stderr, "NvmInsensitiveDynamicHeuristic:\n"
                  "\tNVM allocation sites: %lu/%lu\n"
                  "\t%% instructions with weight: %f%%\n"
                  "\t%% instructions with priority: %f%%\n",
                  activeNvmSites_.size(), nvmSites_.size(), 
                  pWeights, pPriorities);
}

/* #endregion */

/* #region NvmContextDynamicHeuristic */

NvmContextDynamicHeuristic::NvmContextDynamicHeuristic(Executor *executor,
                                                       KFunction *mainFn)
  : contextDesc(std::make_shared<NvmContextDesc>(utils::createAndersen(*executor->kmodule->module), 
                                                 executor->kmodule->module.get(),
                                                 mainFn->function)),
    curr(mainFn->getKInstruction(mainFn->function->getEntryBlock().getFirstNonPHIOrDbg())) {}


void NvmContextDynamicHeuristic::stepState(ExecutionState *es, 
                                           KInstruction *pc, 
                                           KInstruction *nextPC) {
  if (auto *ci = dyn_cast<CallInst>(pc->inst)) {
    if (pc->inst->getFunction() != nextPC->inst->getFunction()) {
      contextDesc = contextDesc->tryResolveFnPtr(ci, nextPC->inst->getFunction());
    }
  } 

  contextDesc = contextDesc->tryGetNextContext(pc, nextPC);
  assert(contextDesc->function == nextPC->inst->getFunction() && "bad context!");
  curr = nextPC;
}

/* #endregion */

/* #region NvmHeuristicBuilder */

const char *NvmHeuristicBuilder::typeNames[] = {
  "none", "static", "insensitive-dynamic", "context-dynamic"
};

const char *NvmHeuristicBuilder::typeDesc[] = {
  "None: uses a no-op heuristic (disables the features)",
  "Static: this only uses the points-to information from Andersen's analysis",
  "Insensitive-Dynamic: this updates Andersen's analysis as runtime "
    "variables are resolved",
  "Context-Dynamic: this updates Andersen's analysis while being context"
    " (call-site) sensitive",
};

/* #endregion */

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2: */
