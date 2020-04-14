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

NvmHeuristicInfo::~NvmHeuristicInfo() {}

SharedAndersen NvmHeuristicInfo::createAndersen(llvm::Module &m) {
  TimerStatIncrementer timer(stats::nvmAndersenTime);

  SharedAndersen anders = std::make_shared<AndersenAAWrapperPass>();
  assert(!anders->runOnModule(m) && "Analysis pass should return false!");

  return anders;
}

NvmHeuristicInfo::ValueSet NvmHeuristicInfo::getNvmAllocationSites(Module *m, const SharedAndersen &ander) {
  NvmHeuristicInfo::ValueVector all;
  NvmHeuristicInfo::ValueSet onlyNvm;
  ander->getResult().getAllAllocationSites(all);
  
  for (const Value *v : all) {
    if (isNvmAllocationSite(m, v)) {
      // errs() << "\tNVM: " << *v << "\n";
      onlyNvm.insert(v);
    }
  }

  return onlyNvm;
}

bool NvmHeuristicInfo::isNvmAllocationSite(Module *m, const llvm::Value *v) {
  if (const CallInst *ci = dyn_cast<CallInst>(v)) {
    if (const Function *f = ci->getCalledFunction()) {
      if (f == m->getFunction("klee_pmem_mark_persistent") || 
          f == m->getFunction("klee_pmem_alloc_pmem")) return true;
      
      if (f == m->getFunction("mmap") || f == m->getFunction("mmap64")) {
        Value *arg = ci->getArgOperand(4);
        if (Constant *cs = dyn_cast<Constant>(arg)) {
          const APInt &ap = cs->getUniqueInteger();
          if ((int32_t)ap.getLimitedValue() == -1) return false;
        }
        return true;
      }
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
  if (CallInst *ci = dyn_cast<CallInst>(i)) {
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

NvmStaticHeuristic::NvmStaticHeuristic(Executor *executor, KFunction *mainFn) 
  : executor_(executor),
    analysis_(createAndersen(*executor->kmodule->module)),
    weights_(std::make_shared<WeightMap>()),
    priorities_(std::make_shared<WeightMap>()),
    curr_(mainFn->function->getEntryBlock().getFirstNonPHIOrDbgOrLifetime()) {

  computePriority();
  dump();
}

void NvmStaticHeuristic::computePriority(void) {
  Module *module = executor_->kmodule->module.get();
  ValueSet nvmSites = getNvmAllocationSites(module, analysis_);
  ValueSet allSites;
  {
    ValueVector allSitesVec;
    analysis_->getResult().getAllAllocationSites(allSitesVec);
    allSites.insert(allSitesVec.begin(), allSitesVec.end());
  }
  
  /**
   * Calculating the raw weights is easy.
   */

  std::unordered_set<llvm::CallInst*> call_insts;

  for (Function &f : *module) {
    for (BasicBlock &b : f) {
      for (Instruction &i : b) {
        (*weights_)[&i] = 0lu;
        (*priorities_)[&i] = 0lu;
        if (isStore(&i) || isFlush(&i)) {
          Value *v = &i;
          if (isFlush(&i)) {
            assert(isa<CallInst>(&i));
            v = dyn_cast<CallInst>(&i)->getArgOperand(0);
          }
          std::vector<const Value*> ptsSet;

          TimerStatIncrementer timer(stats::nvmAndersenTime);
          bool ret = analysis_->getResult().getPointsToSet(v, ptsSet);

          if (!ret) {
            if (allSites.count(v)) {
              if (nvmSites.count(v)) (*weights_)[&i] = 1u;
              continue;
            } else if (AllocaInst *ai = dyn_cast<AllocaInst>(&i)) {
              if (ai->getType()->isFunctionTy()) continue;
            } else if (StoreInst *si = dyn_cast<StoreInst>(&i)) {
              if (isa<GlobalValue>(si->getPointerOperand()->stripPointerCasts())) continue;

              assert(analysis_->getResult().getPointsToSet(si->getPointerOperand(), ptsSet));
            } else {
              errs() << i << "\n";
              assert(false && "could not get points-to!");
            }
          }
          
          for (const Value *ptsTo : ptsSet) {
            if (nvmSites.count(ptsTo)) {
              (*weights_)[&i] = 1u;
            }
          }

        } else if (isFence(&i)) {
          (*weights_)[&i] = 1u;
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
  

  // errs() << "filled in!\n";
  // dump();

  /**
   * Now, we do the priority.
   */

  for (Function &f : *module) {
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
    for (Function &f : *module) {
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

  for (auto *v : getNvmAllocationSites(executor_->kmodule->module.get(), analysis_)) {
    errs() << *v << "\n";
  }

  fprintf(stderr, "NvmStaticHeuristic:\n"
                  "\tNVM allocation sites: %lu\n"
                  "\t%% instructions with weight: %f%%\n"
                  "\t%% instructions with priority: %f%%\n",
                  getNvmAllocationSites(executor_->kmodule->module.get(), 
                                        analysis_).size(),
                  pWeights, pPriorities);
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
