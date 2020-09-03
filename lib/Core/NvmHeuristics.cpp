#include "NvmHeuristics.h"

using namespace llvm;
using namespace std;
using namespace klee;

#include <sstream>
#include <utility>

#include "llvm/IR/GlobalAlias.h"

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
                   clNvmEnumValN(NvmHeuristicBuilder::Type::Dynamic)
                   KLEE_LLVM_CL_VAL_END),
        cl::init(NvmHeuristicBuilder::Type::Static),
        cl::cat(NvmCat));

  #undef clNvmEnumValN

  cl::alias PmCheck("pm-check-type",
                    cl::desc("Alias for nvm-heuristic-type"),
                    cl::NotHidden,
                    cl::aliasopt(NvmCheck),
                    cl::cat(NvmCat));
}
/* #endregion */

/* #region NvmValueDesc */

bool NvmValueDesc::getPointsToSet(const Value *v, 
                                  std::unordered_set<const Value*> &ptsSet) const {
  /**
   * Using a cache for this dramatically reduces the amount of time spent here, 
   * as the call to "getPointsToSet" has to re-traverse a bunch of internal 
   * data structures to construct the set.
   */
  TimerStatIncrementer timer(stats::nvmAndersenTime);
  bool ret = true;
  if (!anders_cache_->count(v)) {
    std::vector<const Value*> rawSet;
    ret = andersen_->getResult().getPointsToSet(v, rawSet);
    if (ret) {
      for (const Value *v : rawSet) {
        if (nvm_allocs_.count(v)) ptsSet.insert(v);
      }

      (*anders_cache_)[v] = ptsSet;
    }
  } else {
    ptsSet = (*anders_cache_)[v];
  }

  return ret;
}

NvmValueDesc::Shared NvmValueDesc::doCall(CallBase *cb, Function *f) const {
  NvmValueDesc newDesc(andersen_, anders_cache_, nvm_allocs_, not_global_nvm_);

  if (!f) {
    f = utils::getCallInstFunction(cb);
    if (!f) {
      return std::make_shared<NvmValueDesc>(newDesc);
    }
  }

  for (unsigned i = 0; i < (unsigned)cb->getNumArgOperands(); ++i) {
    Value *op = cb->getArgOperand(i);
    if (!op->getType()->isPtrOrPtrVectorTy()) continue;
    assert(op);

    bool pointsToNvm = true;
    // We actually want the points-to set for this
    std::unordered_set<const Value*> ptsSet;
    // errs() << "doing call instruction stuff\n";
    // errs() << *op << "\n";
  
    bool ret = getPointsToSet(op, ptsSet);
    assert(ret && "could not get points-to set!");
    for (const Value *ptsTo : ptsSet) {
      if (!isNvm(ptsTo)) {
        pointsToNvm = false;
        break;
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

NvmValueDesc::Shared NvmValueDesc::doReturn(NvmValueDesc::Shared callerVals,
                                            ReturnInst *ri,
                                            Instruction *dest) const {
  Value *retVal = ri->getReturnValue();
  if (retVal && retVal->getType()->isPtrOrPtrVectorTy()) {
    
    std::unordered_set<const Value*> ptsTo;
    bool success = getPointsToSet(retVal, ptsTo);

    if (success && ptsTo.size()) {
      // errs() << "doRet " << *dest << " <<<" << isNvm(retVal) << ">>>\n";
      return callerVals->updateState(dest, isNvm(retVal));
    } else {
      //  errs() << "doRet doesn't point to a memory object! " << success << " " << ptsTo.size() << "\n";
    }
  }

  return callerVals;
}

bool NvmValueDesc::mayPointTo(const Value *a, const Value *b) const {
  std::unordered_set<const Value*> aSet, bSet;
  std::vector<const Value*> interSet;
  bool ret = getPointsToSet(a, aSet);
  if (!ret) errs() << *a << "\n";
  assert(ret && "could not get points-to set!");

  ret = getPointsToSet(b, bSet);
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
  std::unordered_set<const Value*> aSet, bSet;
  std::vector<const Value*> interSet;
  bool ret = getPointsToSet(a, aSet);
  if (!ret) errs() << *a << "\n";
  assert(ret && "could not get points-to set!");

  ret = getPointsToSet(b, bSet);
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
    if (not_global_nvm_.count(posNvm)) {
      return true;
    }

    for (const Value *vol : not_global_nvm_) {
      for (const User *u : vol->users()) {
        if (isa<CallBase>(u)) continue;
        if (u == posNvm) return true;
      }
    }
  } else {
    if (not_local_nvm_.count(posNvm)) {
      return true;
    }

    for (const Value *vol : not_local_nvm_) {
      for (const User *u : vol->users()) {
        if (isa<CallBase>(u)) continue;
        if (u == posNvm) return true;
      }
    }
  }
  
  return false;
}

NvmValueDesc::Shared NvmValueDesc::updateState(Value *val, bool isNvm) {

  if (!val->getType()->isPtrOrPtrVectorTy()) {
    return shared_from_this();
  }

  if (isNvm && matchesKnownVolatile(val)) {
    /**
     * Global values can in theory flip-flop.
     */
    if (isa<GlobalValue>(val)) {
      NvmValueDesc vd = *this;
      vd.not_global_nvm_.erase(val);
      return std::make_shared<NvmValueDesc>(vd);
    } else {
      errs() << *val << " @ " << dyn_cast<Instruction>(val)->getFunction()->getName() << "\n";
      std::unordered_set<const Value *> ptsSet;
      getPointsToSet(val, ptsSet);
      for (const Value *v : ptsSet) errs() << "\t" << *v << "\n";
      for (const Value *v : not_local_nvm_) {
        // is it a user of any not local nvm?
        for (const User *u : v->users()) {
          errs() << "\t\tNOT LOCAL NVM: " << *v << ";\n\t\t\t User " << *u << "\n";
        }
      }
      assert(false && "we violated an assumption about how points-to works!");
    }
  } else if (!isNvm && !matchesKnownVolatile(val)) {
    NvmValueDesc vd = *this;

    if (isa<GlobalValue>(val)) {
      vd.not_global_nvm_.insert(val);
    } else {
      vd.not_local_nvm_.insert(val);
    }
    
    return std::make_shared<NvmValueDesc>(vd);
  }

  return shared_from_this();
}

bool NvmValueDesc::isNvm(const Value *ptr) const {

  std::unordered_set<const Value*> ptsSet;

  bool ret = getPointsToSet(ptr, ptsSet);
  if (!ret) {
    return false;
  }

  if (!nvm_allocs_.size()) {
    errs() << "\t!!!!cannot point because no calls!\n"; 
  }
  
  if (matchesKnownVolatile(ptr)) return false;

  bool may_point_nvm_alloc = false;
  for (const Value *mm : nvm_allocs_) {
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
      #elif 0
      for (const Value *l : not_local_nvm_) {
        if (pointsToIsEq(l, ptr)) return false;
      }
      for (const Value *l : not_global_nvm_) {
        if (pointsToIsEq(l, ptr)) return false;
      }
      #elif 0

      if (matchesKnownVolatile(ptr)) return false;

      #else
      return true;      
      #endif

    } else {
      // errs() << "\t----may NOT point to NVM!\n\t\t";
      // errs() << *ptr << "\n\t\t" << *mm << "\n";
    }
  }
  
  return may_point_nvm_alloc;
  // return false;
}

bool NvmValueDesc::mayModifyNvm(const Instruction *i) const {
  if (const StoreInst *si = dyn_cast<StoreInst>(i)) {
    return isNvm(si->getPointerOperand());
  } else if (utils::isFlush(i)) {
    const Value *v = dyn_cast<CallInst>(i)->getArgOperand(0);
    assert(v && "no arg0 in flush call!");
    return isNvm(v);
  }

  return false;
}

NvmValueDesc::Shared NvmValueDesc::staticState(SharedAndersen apa, llvm::Module *m) {
  NvmValueDesc desc;
  desc.andersen_ = apa;
  desc.anders_cache_ = std::make_shared<NvmValueDesc::AndersenCache>();
  desc.nvm_allocs_ = utils::getNvmAllocationSites(m, apa);

  assert(desc.nvm_allocs_.size() && "No mmap calls?");

  return std::make_shared<NvmValueDesc>(desc);
}

std::string NvmValueDesc::str(void) const {
  std::stringstream s;
  s << "Value State:\n";
  s << "\n\tNumber of nvm allocation sites: " << nvm_allocs_.size();
  for (const Value *v : nvm_allocs_) {
    std::string tmp;
    llvm::raw_string_ostream rs(tmp);
    v->print(rs);
    s << "\n\t\t" << tmp;
  }
  s << "\n\tNumber of known global runtime non-nvm values: " << not_global_nvm_.size();
  for (const Value *v : not_global_nvm_) {
    std::string tmp;
    llvm::raw_string_ostream rs(tmp);
    v->print(rs);
    s << "\n\t\t" << tmp;
  }
  s << "\n\tNumber of known local runtime non-nvm values: " << not_local_nvm_.size();
  for (const Value *v : not_local_nvm_) {
    std::string tmp;
    llvm::raw_string_ostream rs(tmp);
    v->print(rs);
    s << "\n\t\t" << tmp;
  }
  return s.str();
}

bool klee::operator==(const NvmValueDesc &lhs, const NvmValueDesc &rhs) {
  return lhs.nvm_allocs_ == rhs.nvm_allocs_ &&
         lhs.not_global_nvm_ == rhs.not_global_nvm_ &&
         lhs.not_local_nvm_ == rhs.not_local_nvm_;
}

/* #endregion */

/* #region NvmContextDesc */

std::unordered_map<NvmContextDesc::ContextCacheKey, 
                   NvmContextDesc::Shared,
                   NvmContextDesc::ContextCacheKey::Hash> NvmContextDesc::contextCache;

NvmContextDesc::NvmContextDesc(SharedAndersen anders,
                               Function *f,
                               NvmValueDesc::Shared initialArgs,
                               bool parentHasWeight) 
  : andersen(anders),
    function(f),
    valueState(initialArgs),
    returnHasWeight(parentHasWeight) {}

NvmContextDesc::NvmContextDesc(SharedAndersen anders, 
                               Module *m, 
                               Function *main) 
  : andersen(anders),
    function(main),
    valueState(NvmValueDesc::staticState(anders, m)),
    returnHasWeight(false) {}

uint64_t NvmContextDesc::constructCalledContext(llvm::CallBase *cb, 
                                                llvm::Function *f) {
  assert(cb && f && "get out of here with your null parameters");

  // First, we check the cache.
  ContextCacheKey key(f, valueState->doCall(cb, f));
  if (contextCache.count(key)) {
    contexts[cb] = contextCache[key];
    return contextCache[key]->getRootPriority();
  }

  Instruction *retLoc = utils::getReturnLocation(cb);
  assert(retLoc && "could not get the return instruction");

  NvmContextDesc calledCtx(andersen,
                           key.function,
                           key.valueState,
                           hasCoreWeight);
  auto sharedCtx = std::make_shared<NvmContextDesc>(calledCtx);

  auto auxInsts = sharedCtx->setCoreWeights();

  // We add it first to avoid infinite recursion.
  contexts[cb] = sharedCtx;
  contextCache[key] = sharedCtx;

  // We can do this later as it mutates the object
  sharedCtx->setAuxWeights(std::move(auxInsts));
  sharedCtx->setPriorities();
  
  return sharedCtx->getRootPriority();
}

uint64_t NvmContextDesc::constructCalledContext(llvm::CallBase *cb) {
  if (Function *f = cb->getCalledFunction()) {
    return constructCalledContext(cb, f);
  } 

  // We will wait until runtime to resolve this function pointer.
  return 1lu;
}

bool NvmContextDesc::isaCoreInst(Instruction *i) const {
  if (utils::isFence(i)) return true;

  if (utils::isFlush(i)) {
    auto *cb = dyn_cast<CallInst>(i);
    assert(cb && "flushes should always be some kind of function call");
    Value *v = cb->getArgOperand(0);
    assert(v && "no arg0!");

    return valueState->isNvm(v);
  }

  if (auto *si = dyn_cast<StoreInst>(i)) {
    return valueState->isNvm(si->getPointerOperand());
  }

  return false;
}

bool NvmContextDesc::isaAuxInst(Instruction *i) const {
  if (auto *cb = dyn_cast<CallBase>(i)) {
    if (auto *f = cb->getCalledFunction()) {
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
    return returnHasWeight ? 1lu : 0lu;
    // return 1lu;
  } else if (auto *cb = dyn_cast<CallBase>(i)) {
    return constructCalledContext(cb);
  }

  assert(false && "should never reach here!");
  return 0lu;
}

void NvmContextDesc::setPriorities(void) {
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

std::list<Instruction*> NvmContextDesc::setCoreWeights(void) {
  // I will accumulate these as I iterate so we don't have to re-iterate over
  // the entire function as we resolve core instructions.
  std::list<Instruction*> auxInsts;

  for (BasicBlock &bb : *function) {
    for (Instruction &i : bb) {
      if (isaCoreInst(&i)) {
        weights[&i] = 3lu;
        hasCoreWeight = true;
      } else if (isaAuxInst(&i)) {
        auxInsts.push_back(&i);
      }
    }
  }

  return auxInsts;
}

void NvmContextDesc::setAuxWeights(std::list<Instruction*> auxInsts) {
  for (Instruction *i : auxInsts) {
    weights[i] = computeAuxInstWeight(i);
  }
}

NvmContextDesc::Shared NvmContextDesc::tryGetNextContext(KInstruction *pc,
                                                         KInstruction *nextPC) {
  assert(!isa<ReturnInst>(pc->inst) && "Need to handle returns elsewhere!");

  if (auto *cb = dyn_cast<CallBase>(pc->inst)) {
    if (pc->inst->getNextNode() != nextPC->inst) {
      if (!contexts.count(cb)) {
        constructCalledContext(cb, nextPC->inst->getFunction());
        setPriorities();
        return contexts.at(cb);
      }
      return contexts.at(cb);
    }
  }

  return shared_from_this();
}

NvmContextDesc::Shared NvmContextDesc::tryUpdateContext(Value *v, bool isValNvm) {
  NvmValueDesc::Shared newDesc = valueState->updateState(v, isValNvm);

  if (valueState != newDesc) {
    
    // First, check the cache.
    ContextCacheKey cck(function, newDesc);
    if (contextCache.count(cck)) {
      return contextCache.at(cck);
    }
    // Else, update and add to the cache.
    auto updated = dup();

    updated->valueState = newDesc;
    auto auxInsts = updated->setCoreWeights();
    if (updated->weights != weights) {
      updated->setAuxWeights(std::move(auxInsts));
      updated->setPriorities();
    }

    contextCache[cck] = updated;
    return updated;
  }

  return shared_from_this();
}

NvmContextDesc::Shared NvmContextDesc::tryResolveFnPtr(CallBase *cb, Function *f) {
  if (contexts.count(cb) && contexts.at(cb)->function == f) {
    return shared_from_this(); 
  }

  auto copy = dup();

  copy->constructCalledContext(cb, f);
  copy->setPriorities();
  return copy;
}

std::string NvmContextDesc::str() const {
  std::stringstream s;
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
    nvmSites_(utils::getNvmAllocationSites(module_, analysis_)),
    valueState_(NvmValueDesc::staticState(analysis_, module_)) {
}

uint64_t NvmStaticHeuristic::computeInstWeight(Instruction *i) const {

  if (valueState_->mayModifyNvm(i) || isNvmAllocSite(i)) {
    return 3u;
  } else if (utils::isFence(i) && getCurrentNvmSites().size()) {
    return 2u;
  } else if (isa<ReturnInst>(i)) {
    return 1u;
  }

  return 0u;
}

void NvmStaticHeuristic::computePriority(void) {
  /**
   * Calculating the raw weights is easy.
   */
  resetWeights();

  std::unordered_set<llvm::CallBase*> call_insts;

  for (Function &f : *module_) {
    for (BasicBlock &b : f) {
      for (Instruction &i : b) {
        (*weights_)[&i] = 0lu;
        (*priorities_)[&i] = 0lu;
        if (mayHaveWeight(&i)) {
          (*weights_)[&i] = computeInstWeight(&i);
        } else if (CallBase *cb = dyn_cast<CallBase>(&i)) {
          if (auto *ci = dyn_cast<CallInst>(cb)) {
            if (ci->isInlineAsm()) continue;
          } 
          
          call_insts.insert(cb);
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
    for (CallBase *cb : call_insts) {
      assert(cb && "callbase is nullptr!");
      std::unordered_set<Function*> possibleFns;
      // errs() << *cb << "\n";
      auto *tmp = dyn_cast<CallBase>(cb->stripPointerCasts());
      if (tmp) cb = tmp;
      assert(cb && "could not strip!");
      if (Function *f = utils::getCallInstFunction(cb)) {
        possibleFns.insert(f);
      } else if (Function *f = cb->getCalledFunction()) {
        possibleFns.insert(f);
      } else if (auto *f = dyn_cast<Function>(cb->getCalledValue()->stripPointerCastsNoFollowAliases())) {
        possibleFns.insert(f);
      } else if (GlobalAlias *ga = dyn_cast<GlobalAlias>(cb->getCalledValue())) {
        Function *f = dyn_cast<Function>(ga->getAliasee());
        assert(f && "bad assumption about aliases!");
        possibleFns.insert(f);
      } else {
        if (!cb->isIndirectCall()) {
          errs() << *cb << "\n";
          errs() << cb->getCalledValue() << "\n";
          if (cb->getCalledValue()) errs() << *cb->getCalledValue() << "\n";
          if (cb->getCalledValue()) errs() << *cb->getCalledValue()->stripPointerCastsNoFollowAliases() << "\n";
        }
        assert(cb->isIndirectCall());

        for (Function &f : *curr_->getModule()) {
          for (unsigned i = 0; i < (unsigned)cb->getNumArgOperands(); ++i) {
            if (f.arg_size() <= i) {
              if (f.isVarArg()) {
                possibleFns.insert(&f);
              }
              break;
            }

            Argument *arg = f.arg_begin() + i;
            Value *val = cb->getArgOperand(i);

            if (arg->getType() != val->getType()) break;
            else if (i + 1 == cb->getNumArgOperands()) possibleFns.insert(&f);
          }
        }
      }

      for (Function *f : possibleFns) {
        for (BasicBlock &bb : *f) {
          for (Instruction &i : bb) {
            if ((*weights_)[&i] && !(*weights_)[dyn_cast<Instruction>(cb)]) {
              c = true;
              (*weights_)[dyn_cast<Instruction>(cb)] = 1u;
              goto done;
            }
          }
        }
      }

      done: (void)0;
    }
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
        if (auto *cb = dyn_cast<CallBase>(usr)) {
          // errs() << "\t=> " << (*priorities_)[i] << "\n";
          Instruction *retLoc = utils::getReturnLocation(cb);
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

    for (CallBase *cb : call_insts) {

      Instruction *retLoc = utils::getReturnLocation(cb);
      assert(retLoc);

      std::unordered_set<Function*> possibleFns;
      if (Function *f = utils::getCallInstFunction(cb)) {
        possibleFns.insert(f);
      } else if (Function *f = cb->getCalledFunction()) {
        possibleFns.insert(f);
      } else if (GlobalAlias *ga = dyn_cast<GlobalAlias>(cb->getCalledValue())) {
        Function *f = dyn_cast<Function>(ga->getAliasee());
        assert(f && "bad assumption about aliases!");
        possibleFns.insert(f);
      } else {
        if (!cb->isIndirectCall()) errs() << *cb << "\n";
        assert(cb->isIndirectCall());

        for (Function &f : *curr_->getModule()) {
          for (unsigned i = 0; i < (unsigned)cb->getNumArgOperands(); ++i) {
            if (f.arg_size() <= i) {
              if (f.isVarArg()) {
                possibleFns.insert(&f);
              }
              break;
            }

            Argument *arg = f.arg_begin() + i;
            Value *val = cb->getArgOperand(i);

            if (arg->getType() != val->getType()) break;
            else if (i + 1 == cb->getNumArgOperands()) possibleFns.insert(&f);
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
    assert(valueState_->isNvmAllocCall(dyn_cast<CallInst>(v)));
  }

  fprintf(stderr, "NvmStaticHeuristic:\n"
                  "\tNVM allocation sites: %lu\n"
                  "\t%% instructions with weight: %f%%\n"
                  "\t%% instructions with priority: %f%%\n",
                  nvmSites_.size(), pWeights, pPriorities);
}

/* #endregion */

/* #region NvmDynamicHeuristic */

NvmDynamicHeuristic::NvmDynamicHeuristic(Executor *executor,
                                         KFunction *mainFn)
  : contextDesc(std::make_shared<NvmContextDesc>(utils::createAndersen(*executor->kmodule->module), 
                                                 executor->kmodule->module.get(),
                                                 mainFn->function)),
    curr(mainFn->getKInstruction(mainFn->function->getEntryBlock().getFirstNonPHIOrDbg())) {}

void NvmDynamicHeuristic::updateCurrentState(ExecutionState *es, 
                                             KInstruction *pc, 
                                             bool isNvm) {
  TimerStatIncrementer timer(stats::nvmHeuristicTime);

  // Get the value we actually want to update.
  Value *memVal = pc->inst;
  if (LoadInst *li = dyn_cast<LoadInst>(pc->inst)) {
    memVal = li->getPointerOperand();
  } else if (StoreInst *si = dyn_cast<StoreInst>(pc->inst)) {
    memVal = si->getPointerOperand();
  } else if (CallBase *cb = dyn_cast<CallBase>(pc->inst)) {
    memVal = cb;
  } else {
    errs() << *pc->inst << "\n";
    assert(false && "we didn't capture all memory operations!");
  }

  auto newDesc = contextDesc->tryUpdateContext(memVal, isNvm);
  if (newDesc != contextDesc) {
    contextDesc = newDesc;
    // dump();
  }
}

void NvmDynamicHeuristic::stepState(ExecutionState *es, 
                                    KInstruction *pc, 
                                    KInstruction *nextPC) {
  TimerStatIncrementer timer(stats::nvmHeuristicTime);

  if (nextPC->inst->getFunction()->getName() == "pthread_exit") return;

  if (auto *cb = dyn_cast<CallBase>(pc->inst)) {
    if (pc->inst->getFunction() != nextPC->inst->getFunction()) {
      contextDesc = contextDesc->tryResolveFnPtr(cb, nextPC->inst->getFunction());
    }
    auto childCtx = contextDesc->tryGetNextContext(pc, nextPC);

    /**
     * Only push on stack if:
     * 1. We called a different function
     * 2. The next instruction is the current function's entry point (recursion).
     */
    if (childCtx->function != contextDesc->function || 
        nextPC->inst == contextDesc->function->getEntryBlock().getFirstNonPHI()) {
      contextStack.push_back(contextDesc);
      callInstStack.push_back(cb);
      contextDesc = childCtx;
    }

  } else if (auto *ri = dyn_cast<ReturnInst>(pc->inst)) {
    // errs() << __func__ << " returning " << *ri << " @ " << ri->getFunction()->getName() << "\n";
    // if (nextPC) errs() << __func__ << " next is: " << *nextPC->inst << " @ " << nextPC->inst->getFunction()->getName() << "\n";
    // else errs() << __func__ << " next is NULL\n";
    auto parentCtx = contextStack.back();
    contextStack.pop_back();
    auto retValDest = callInstStack.back();
    callInstStack.pop_back();
    parentCtx->valueState = contextDesc->valueState->doReturn(parentCtx->valueState, 
                                                              ri,
                                                              retValDest);
    parentCtx->setPriorities();
    contextDesc = parentCtx;
  }

  if (contextDesc->function != nextPC->inst->getFunction()) {
    errs() << *pc->inst << " @ " << pc->inst->getFunction()->getName() << "\n";
    errs() << *nextPC->inst << " @ " << nextPC->inst->getFunction()->getName() << "\n";
    if (contextDesc->function) {
      errs() << "CD: " << contextDesc->function->getName() << "\n";
    } else errs() << "CD NULL\n";

    errs() << nextPC->inst->getFunction()->getName() << "\n";

    if (contextDesc->function->getName() == "pthread_create") {
      contextStack.clear();
      callInstStack.clear();

      SharedAndersen sa = contextDesc->andersen;
      Module *m = nextPC->inst->getModule();
      Function *threadMain = nextPC->inst->getFunction();
      contextDesc.reset(new NvmContextDesc(sa, m, threadMain));
      computePriority();
    }
  }
  
  assert(contextDesc->function == nextPC->inst->getFunction() && "bad context!");
  curr = nextPC;
}

/* #endregion */

/* #region NvmHeuristicBuilder */

const char *NvmHeuristicBuilder::typeNames[] = { "none", "static", "dynamic" };

const char *NvmHeuristicBuilder::typeDesc[] = {
  "None: uses a no-op heuristic (disables the features)",
  "Static: this only uses the points-to information from Andersen's analysis",
  "Dynamic: this updates Andersen's analysis based on runtime information.",
};

std::shared_ptr<NvmHeuristicInfo> 
NvmHeuristicBuilder::create(Type t, Executor *executor, KFunction *main) {
  TimerStatIncrementer timer(stats::nvmHeuristicTime);
  TimerStatIncrementer timer2(stats::nvmOfflineTime);

  NvmHeuristicInfo *ptr = nullptr;
  switch(t) {
    case None:
      assert(false && "unsupported!");
      break;
    case Static:
      ptr = new NvmStaticHeuristic(executor, main);
      break;
    case Dynamic:
      ptr = new NvmDynamicHeuristic(executor, main);
      break;
    default:
      assert(false && "unsupported!");
      break;
  }

  assert(ptr);

  ptr->computePriority();
  ptr->dump();
  
  return std::shared_ptr<NvmHeuristicInfo>(ptr);
}

std::shared_ptr<NvmHeuristicInfo> 
NvmHeuristicBuilder::copy(const std::shared_ptr<NvmHeuristicInfo> &info) {
  TimerStatIncrementer timer(stats::nvmHeuristicTime);

  NvmHeuristicInfo *ptr = info.get();
  if (!ptr) {
    return info;
  }

  if (auto sptr = dynamic_cast<const NvmStaticHeuristic*>(info.get())) {
    // Since it's never updated, we don't have to copy it. Just share.
    return info;
  }

  if (auto cptr = dynamic_cast<const NvmDynamicHeuristic*>(info.get())) {
    ptr = new NvmDynamicHeuristic(*cptr);
  }

  assert(ptr && "null!");
  return std::shared_ptr<NvmHeuristicInfo>(ptr);
}

/* #endregion */

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2: */
