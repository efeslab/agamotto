#include "NvmHeuristics.h"

using namespace llvm;
using namespace std;
using namespace klee;

#include <sstream>

#include "CoreStats.h"
#include "Executor.h"
#include "klee/ExecutionState.h"
#include "klee/TimerStatIncrementer.h"


/**
 * NvmStackFrameDesc
 * 
 * (iangneal): should be essentially ported.
 */

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
   return lhs.caller_desc.get() == rhs.caller_desc.get() && 
          lhs.caller_inst == rhs.caller_inst &&
          lhs.return_inst == rhs.return_inst;
}

bool klee::operator!=(const NvmStackFrameDesc &lhs, const NvmStackFrameDesc &rhs) {
  return !(lhs == rhs);
}

/**
 * NvmValueDesc
 * 
 * (iangneal): should be essentially ported as well.
 */

std::shared_ptr<NvmValueDesc> NvmValueDesc::doCall(andersen_sptr_t apa, 
                                                   CallInst *ci, 
                                                   Function *f) const 
{
  NvmValueDesc newDesc;
  newDesc.caller_values_ = std::make_shared<NvmValueDesc>(*this);
  newDesc.call_site_ = ci;
  newDesc.global_nvm_ = global_nvm_;

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

    bool pointsToNvm = false;
    // We actually want the points-to set for this
    std::vector<const Value*> ptsSet;
    // errs() << "doing call instruction stuff\n";
    // errs() << *op << "\n";
    {
      TimerStatIncrementer timer(stats::nvmAndersenTime);
      bool ret = apa->getResult().getPointsToSet(op, ptsSet);
      assert(ret && "could not get points-to set!");
      for (const Value *ptsTo : ptsSet) {
        if (isNvm(apa, ptsTo)) {
          pointsToNvm = true;
          break;
        }
      }
    }

    if (!pointsToNvm) continue;

    if (i >= f->arg_size()) {
      assert(f->isVarArg() && "argument size mismatch!");
      newDesc.varargs_contain_nvm_ = true;
    } else {
      Argument *arg = (f->arg_begin() + i);
      assert(arg);
      // Scalars don't necessarily point to anything.
      if (!arg->getType()->isPtrOrPtrVectorTy()) continue;
      newDesc.local_nvm_.insert(arg);
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

std::shared_ptr<NvmValueDesc> NvmValueDesc::updateState(Value *val, 
                                                        bool isNvm) const 
{
  NvmValueDesc vd = *this;
  // Since the set is sparse, they're only contained if isNvm.
  if (isNvm && val->getType()->isPtrOrPtrVectorTy()) {
    if (isa<GlobalValue>(val)) {
      vd.global_nvm_.insert(val);
    } else {
      vd.local_nvm_.insert(val);
    }
  }

  return std::make_shared<NvmValueDesc>(vd);
}

bool NvmValueDesc::isNvm(andersen_sptr_t apa, const Value *ptr) const {
  //TODO: remove redundancy
  for (const Value *v : local_nvm_) {
    TimerStatIncrementer timer(stats::nvmAndersenTime);
    std::vector<const Value*> localSet, ptrSet, interSet;
    bool ret = apa->getResult().getPointsToSet(v, localSet);
    if (!ret) errs() << *v << "\n";
    assert(ret && "could not get points-to set!");

    ret = apa->getResult().getPointsToSet(ptr, ptrSet);
    if (!ret) errs() << *ptr << "\n";
    assert(ret && "could not get points-to set!");

    interSet.resize(std::max(localSet.size(), ptrSet.size()));

    auto it = std::set_intersection(localSet.begin(), localSet.end(),
                                    ptrSet.begin(), ptrSet.end(),
                                    interSet.begin());
    
    interSet.resize(it - interSet.begin());

    if (interSet.size()) {
      return true;
    }

    // if (llvm::AliasResult::MayAlias == apa->getResult().andersenAlias(v, ptr)) {
    //   return true;
    // }
  }

  for (const Value *v : global_nvm_) {
    TimerStatIncrementer timer(stats::nvmAndersenTime);

    std::vector<const Value*> localSet, ptrSet, interSet;
    bool ret = apa->getResult().getPointsToSet(v, localSet);
    if (!ret) errs() << *v << "\n";
    assert(ret && "could not get points-to set!");

    ret = apa->getResult().getPointsToSet(ptr, ptrSet);
    if (!ret) errs() << *ptr << "\n";
    assert(ret && "could not get points-to set!");

    interSet.resize(std::max(localSet.size(), ptrSet.size()));

    auto it = std::set_intersection(localSet.begin(), localSet.end(),
                                    ptrSet.begin(), ptrSet.end(),
                                    interSet.begin());
    
    interSet.resize(it - interSet.begin());

    if (interSet.size()) {
      return true;
    }
  }

  return false;
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
  s << "\tNumber of runtime global nvm values: " << global_nvm_.size();
  for (Value *v : global_nvm_) {
    std::string tmp;
    llvm::raw_string_ostream rs(tmp);
    v->print(rs);
    s << "\n\t\t" << tmp;
  }
  s << "\n\tNumber of runtime local nvm values: " << local_nvm_.size();
  for (Value *v : local_nvm_) {
    std::string tmp;
    llvm::raw_string_ostream rs(tmp);
    v->print(rs);
    s << "\n\t\t" << tmp;
  }
  return s.str();
}

bool klee::operator==(const NvmValueDesc &lhs, const NvmValueDesc &rhs) {
  return lhs.mmap_calls_ == rhs.mmap_calls_ &&
         lhs.global_nvm_ == rhs.global_nvm_ &&
         lhs.local_nvm_ == rhs.local_nvm_ &&
         lhs.varargs_contain_nvm_ == rhs.varargs_contain_nvm_ &&
         lhs.caller_values_ == rhs.caller_values_ &&
         lhs.call_site_ == rhs.call_site_;
}

/**
 * NvmInstructionDesc
 * 
 * (iangneal): working on this
 */

NvmInstructionDesc::NvmInstructionDesc(Executor *executor,
                                       andersen_sptr_t apa,
                                       KInstruction *location, 
                                       std::shared_ptr<NvmValueDesc> values, 
                                       std::shared_ptr<NvmStackFrameDesc> stackframe) 
        : executor_(executor),
          apa_(apa), 
          mod_(executor->kmodule.get()), 
          curr_(location), 
          values_(values), 
          stackframe_(stackframe) {}

NvmInstructionDesc::NvmInstructionDesc(Executor *executor,
                                       andersen_sptr_t apa, 
                                       KInstruction *entry) 
        : executor_(executor),
          apa_(apa), 
          mod_(executor->kmodule.get()), 
          curr_(entry), 
          values_(NvmValueDesc::staticState(mod_->module.get())), 
          stackframe_(NvmStackFrameDesc::empty()) {}

bool NvmInstructionDesc::isCachelineModifier(ExecutionState *es) const {
  Instruction *i = curr_->inst;

  Value *ptr = nullptr;
  if (StoreInst *si = dyn_cast<StoreInst>(i)) {
    // Case 2: store
    ptr = si->getPointerOperand();
  } else if (CallInst *ci = dyn_cast<CallInst>(i)) {
    // Case 3: flush
    Function *f = ci->getCalledFunction();
    if (f && f->isDeclaration()) {
      switch(f->getIntrinsicID()) {
        case Intrinsic::x86_sse2_clflush:
        case Intrinsic::x86_clflushopt:
        case Intrinsic::x86_clwb:
          ptr = ci->getOperand(0)->stripPointerCasts();
        default:
          break;
      }
    }
  }

  if (!ptr) return false;

  ptr = ptr->stripPointerCasts()->stripInBoundsOffsets();

  ref<Expr> addr;
  if (Instruction *iv = dyn_cast<Instruction>(ptr)) {
    KInstruction *target = mod_->getKInstruction(iv);
    addr = es->stack.back().locals[target->dest].value;
  } else if (const GlobalValue *gv = dyn_cast<GlobalValue>(ptr)) {
    addr = executor_->globalAddresses[gv];
  } else {
    errs() << *i << "\n";
    errs() << *ptr << "\n";
    errs() << *ptr->getType() << "\n";
    assert(false && "Don't know what to do!!");
  }
  
  if (addr.isNull()) {
    // hasn't been initialized to anything yet.
    return false;
  }

  ResolutionList rl;
  assert(!es->addressSpace.resolve(*es, executor_->solver, addr, rl));

  // This logic also makes sure that if it isn't persistent memory, we always skip it.
  for (ResolutionList::iterator it = rl.begin(), ie = rl.end(); it != ie; ++it) {
    if (isa<PersistentState>(it->second)) return true;
  }
     
  return false;
}

uint64_t NvmInstructionDesc::calculateWeight(ExecutionState *es) const {
  const static uint64_t kResolutionWeight = 1u;
  const static uint64_t kMmapWeight       = 2u;
  const static uint64_t kFenceWeight      = 1u;
  const static uint64_t kVarargWeight     = 1u;
  const static uint64_t kUnimportant      = 0u;
  const static uint64_t kDirtiesCacheline = 1u;

  // We use this as an incentive to resolve unresolved function pointers.
  if (!curr_ || needsResolution()) {
    return kResolutionWeight;
  }

  Instruction *i = curr_->inst;
  // First, we check if the instruction is inherently important.
  if (CallInst *ci = dyn_cast<CallInst>(curr_->inst)) {
    // Case 1: Contains an mmap call.
    if (values_->isMmapCall(ci)) {
      return kMmapWeight;
    }

    // Case 4: Is a memory fence.
    // TODO: also should cover mfences, in theory.
    Function *f = ci->getCalledFunction();
    if (f && f->isDeclaration() && 
        (f->getIntrinsicID() == Intrinsic::x86_sse_sfence ||
         f->getIntrinsicID() == Intrinsic::x86_sse2_mfence)) {
      return kFenceWeight;
    }
  }

  if (values_->isImportantVAArg(i)) {
    return kVarargWeight;
  }

  // Second, we see if a memory store/flush points to NVM.
  Value *ptr = nullptr;
  if (StoreInst *si = dyn_cast<StoreInst>(i)) {
    // Case 2: store
    ptr = si->getPointerOperand();
  } else if (CallInst *ci = dyn_cast<CallInst>(i)) {
    // Case 3: flush
    Function *f = ci->getCalledFunction();
    if (f && f->isDeclaration()) {
      switch(f->getIntrinsicID()) {
        case Intrinsic::x86_sse2_clflush:
        case Intrinsic::x86_clflushopt:
        case Intrinsic::x86_clwb:
          ptr = ci->getOperand(0)->stripPointerCasts();
        default:
          break;
      }
    }
  }

  if (!ptr) {
    return kUnimportant;
  }

  std::vector<const Value*> ptsSet;
  {
    TimerStatIncrementer timer(stats::nvmAndersenTime);
    bool ret = apa_->getResult().getPointsToSet(ptr, ptsSet);
    assert(ret && "could not get points-to set!");
  }

  for (const Value *v : ptsSet) {
    if (values_->isNvm(apa_, v)) {
      return kDirtiesCacheline;
    }
  }

  return kUnimportant;
}

NvmInstructionDesc NvmInstructionDesc::constructDefaultScion(void) const {
  Instruction *ni = curr_->inst->getNextNode();
  NvmInstructionDesc desc(executor_, apa_, mod_->getKInstruction(ni), values_, stackframe_);
  return desc;
}

NvmInstructionDesc::UnorderedSet 
NvmInstructionDesc::constructScions(void) {
  NvmInstructionDesc::UnorderedSet ret;
  // I essentially need to figure out the next instruction.
  // if (!isValid()) return ret; 
  assert(isValid() && "this is not the true way");

  Instruction *ip = curr_->inst;
  BasicBlock *bb = ip->getParent();

  if (ip == bb->getTerminator()) {
    // Figure out the transition to the next basic block.
    // -- Could be a return or a branch.
    if (ReturnInst *ri = dyn_cast<ReturnInst>(ip)) {
      // This is like main exit or something
      if (stackframe_->isEmpty()) {
        return ret;
      }

      Instruction *ni = stackframe_->getReturnLocation();
      std::shared_ptr<NvmValueDesc> new_values = values_->doReturn(apa_, ri);
      std::shared_ptr<NvmStackFrameDesc> ns = stackframe_->doReturn();

      NvmInstructionDesc desc(executor_, apa_, mod_->getKInstruction(ni), new_values, ns);
      ret.insert(desc);
      return ret;

    } else if (BranchInst *bi = dyn_cast<BranchInst>(ip)) {
      for (auto *sbb : bi->successors()) {
        // do loop detection.
        Instruction *ni = &(sbb->front());
        llvm::DominatorTree dom(*ip->getFunction());
        if (!dom.dominates(ni, ip)) {
          NvmInstructionDesc desc(executor_, apa_, mod_->getKInstruction(ni), values_, stackframe_);
          ret.insert(desc);
        }
      }
      return ret;

    } else if (SwitchInst *si = dyn_cast<SwitchInst>(ip)) {
      std::unordered_set<Instruction*> uniqueTargets;

      // The default isn't in the cases list, as it's a different parameter in
      // the IR: https://llvm.org/docs/LangRef.html#switch-instruction
      Instruction *ni = si->getDefaultDest()->getFirstNonPHIOrDbg();
      uniqueTargets.insert(ni);
      for (auto &c : si->cases()) {
        Instruction *ni = &(c.getCaseSuccessor()->front());
        uniqueTargets.insert(ni);
      }
      
      for (Instruction *ni : uniqueTargets) {
        NvmInstructionDesc desc(executor_, apa_, mod_->getKInstruction(ni), values_, stackframe_);
        ret.insert(desc);
      }

      return ret;

    } else if (isa<UnreachableInst>(ip)) {
      assert(ret.empty() && "Unreachable has no successors!");
      return ret;
    } else {
      errs() << *ip << "\n";
      assert(false && "Assumption violated -- terminator instruction is "
                      "not a return or branch!");
      return ret;
    }

  } else if (CallInst *ci = utils::getNestedFunctionCallInst(ip)) {
    Function *nf = utils::getCallInstFunction(ci);
    if (!nf) {
      errs() << *ci << "\n";
      klee_error("Could not get the called function!");
    }

    if (stackframe_->containsFunction(nf) && !ready_to_recurse_) {
    // if (curr_->inst->getFunction() == nf && !ready_to_recurse_) {
      can_recurse_ = true;
      return ret;
    }

    if (mod_->functionMap.find(nf) != mod_->functionMap.end()) {
      // Successor is the entry to the next function.
      Instruction *nextInst = &(nf->getEntryBlock().front());
      // Instruction *retLoc = ci->getNextNonDebugInstruction();
      Instruction *retLoc = ip->getNextNode();
      // errs() << "ret loc is " << *retLoc << "\n";
      assert(retLoc && "Assumption violated -- call instruction is the last instruction in a basic block!");
      assert(stackframe_.get() != nullptr);

      std::shared_ptr<NvmStackFrameDesc> newStack = stackframe_->doCall(stackframe_, ci, retLoc);
      std::shared_ptr<NvmValueDesc> newValues = values_->doCall(apa_, ci);

      NvmInstructionDesc desc(executor_, apa_, mod_->getKInstruction(nextInst), newValues, newStack);
      ret.insert(desc);
    }
    
    // We always want the default, because the instruction could be handled by
    // the special function handler. Even function pointers! 
    ret.insert(constructDefaultScion());

    // if (force_special_handler_call_fall_over_ || !ret.size()) {
    //   if (ret.size()) {
    //     // if it's specially handled, it should ALWAYS be.
    //     ret.clear();
    //   }

    //   ret.push_back(constructDefaultScion());
    // }
    
    return ret;

  } else if (CallInst *ci = dyn_cast<CallInst>(ip)) {
    // We can't immediately find the function. So, let's see if we can find it indirectly.
    // -- Inline assembly is skipped over.
    // -- Debug calls and other intrinsics
    if (ci->isIndirectCall()){   
      /**
       * Further analysis would be tedious and potentially unwanted. Since this
       * is potentially highly runtime dependent, we will defer and increase the weight
       * of the underlying instruction. We will then resolve this at runtime.
       * 
       * TODO: At some point, we may want to check a static-only heuristic. We
       * will require some sort of alias analysis for this.
       */

      if (!runtime_function_) {
        // Return no successors for now.
        need_resolution_ = true;
        return ret;
      }

      Instruction *retLoc = ip->getNextNode();
      assert(retLoc && "Assumption violated -- call instruction is the last instruction in a basic block!");
      assert(stackframe_);
      std::shared_ptr<NvmStackFrameDesc> newStack = stackframe_->doCall(stackframe_, ci, retLoc);
      std::shared_ptr<NvmValueDesc> newValues = values_->doCall(apa_, ci, runtime_function_);
      // We'll have to call update on these values later.

      NvmInstructionDesc desc(executor_, apa_, 
                              mod_->getKInstruction(&(runtime_function_->getEntryBlock().front())), 
                              newValues, newStack);
      ret.insert(desc);

      // We always want the default, because the instruction could be handled by
      // the special function handler. Even function pointers! 
      ret.insert(constructDefaultScion());
      // if (force_special_handler_call_fall_over_ || !ret.size()) {
      //   if (ret.size()) {
      //     // should ALWAYS be special
      //     ret.clear();
      //   }
      //   ret.push_back(constructDefaultScion());

      //   // errs() << "default node: " << *curr_->inst << " => " << *ret.back().inst() << "\n";
      // }
      
      return ret;
    } else {
      // Fallthrough to the default case.
      assert((ci->isInlineAsm() || isa<IntrinsicInst>(ci)) && "assumptions violated!");
    }
  }

  // Default case, the default scion
  ret.insert(constructDefaultScion());

  return ret;
}

std::shared_ptr<NvmInstructionDesc> NvmInstructionDesc::getRecursable() const {
  assert(can_recurse_ && "Trying to recurse a non-recursable function!");
  NvmInstructionDesc newDesc = *this;
  newDesc.ready_to_recurse_ = true;
  return std::make_shared<NvmInstructionDesc>(newDesc);
}

std::shared_ptr<NvmInstructionDesc> NvmInstructionDesc::getForceFallOver() const {
  assert(forceFallOverUnset() && "bad invocation!");

  NvmInstructionDesc newDesc = *this;
  newDesc.force_special_handler_call_fall_over_ = true;
  newDesc.successors_.clear();

  return std::make_shared<NvmInstructionDesc>(newDesc);
}

std::shared_ptr<NvmInstructionDesc> 
NvmInstructionDesc::resolve(KInstruction *nextPC) const 
{
  assert(need_resolution_ && !runtime_function_ && "Bad resolve call!");
  llvm::Function *f = nextPC->inst->getFunction();

  NvmInstructionDesc newDesc = *this; // explicit copy
  newDesc.runtime_function_ = f;
  newDesc.need_resolution_ = false;

  auto sptr = std::make_shared<NvmInstructionDesc>(newDesc);

  assert(sptr->runtime_function_ == newDesc.runtime_function_);
  assert(sptr->need_resolution_ == newDesc.need_resolution_);

  assert(sptr->successors_.size());
  assert(sptr->isValid());
  assert(!sptr->isTerminator());
  return sptr;
}

std::string NvmInstructionDesc::str(void) const {
  std::stringstream s;
  // {
  //   std::string tmp;
  //   llvm::raw_string_ostream rs(tmp);
  //   curr_->inst->getFunction()->print(rs);
  //   s << tmp;
  // }
  s << "\n----INST----\n";
  s << stackframe_->str();
  s << "\n" << values_->str();
  s << "\n"; 
  if (curr_) {
    std::string tmp;
    llvm::raw_string_ostream rs(tmp);
    curr_->inst->print(rs);
    s << tmp;
  }
  if (predecessors_.size()) {
    s << "\npred:";
    for (auto &psp : predecessors_) {
      std::string tmp;
      llvm::raw_string_ostream rs(tmp);
      psp->inst()->print(rs);
      s << "\n\t" << tmp;
    }
  }
  if (successors_.size()) {
    s << "\nsucc:";
    for (auto &psp : successors_) {
      std::string tmp;
      llvm::raw_string_ostream rs(tmp);
      psp->inst()->print(rs);
      s << "\n\t" << tmp;
    }
  }
  s << "\tFORCE=" << force_special_handler_call_fall_over_ << "\n";
  s << "\n----****----\n";
  return s.str();
}

bool NvmInstructionDesc::attach(std::shared_ptr<NvmInstructionDesc> &pptr, 
                                std::shared_ptr<NvmInstructionDesc> &sptr) 
{
  assert(pptr->getWeight() && "not sparse!");
  assert(sptr->getWeight() && "not sparse!");
  size_t pcount = std::count(pptr->successors_.begin(), 
                             pptr->successors_.end(),
                             pptr);
  size_t scount = std::count(sptr->predecessors_.begin(),
                             sptr->predecessors_.end(),
                             sptr);
  assert(pcount == scount && "asymmetry!");
  bool contained = !!pcount;

  if (!contained) {
    pptr->successors_.push_back(sptr);
    sptr->predecessors_.push_back(pptr);
  }

  return !contained;
}

bool NvmInstructionDesc::detach(std::shared_ptr<NvmInstructionDesc> &pptr, 
                                std::shared_ptr<NvmInstructionDesc> &sptr) 
{
  assert(pptr->getWeight() && "not sparse!");
  assert(sptr->getWeight() && "not sparse!");
  auto piter = std::find(pptr->successors_.begin(), 
                         pptr->successors_.end(),
                         pptr);
  auto siter = std::find(sptr->predecessors_.begin(),
                         sptr->predecessors_.end(),
                         sptr);
  bool pcon = piter != pptr->successors_.end();
  bool scon = siter != sptr->predecessors_.end();
  assert(pcon == scon && "asymmetry!");
  bool contained = pcon;

  if (contained) {
    pptr->successors_.erase(piter);
    sptr->predecessors_.erase(siter);
  }

  return contained;
}

bool NvmInstructionDesc::replace(std::shared_ptr<NvmInstructionDesc> &pptr, 
                                 std::shared_ptr<NvmInstructionDesc> &optr,
                                 std::shared_ptr<NvmInstructionDesc> &nptr) 
{
  assert(pptr->getWeight() && optr->getWeight() && nptr->getWeight() && "not sparse!");
  bool changed;

  changed = detach(pptr, optr);
  changed = changed || attach(pptr, nptr);

  return changed;
}

std::shared_ptr<NvmInstructionDesc> 
NvmInstructionDesc::createEntry(Executor *executor, KFunction *mainFn) {
  andersen_sptr_t anders = std::make_shared<AndersenAAWrapperPass>();
  {
    TimerStatIncrementer timer(stats::nvmAndersenTime);
    assert(!anders->runOnModule(*executor->kmodule->module) && "Analysis pass should return false!");
  }
  
  Instruction *inst = &(mainFn->function->getEntryBlock().front());
  KInstruction *start = mainFn->getKInstruction(inst);

  return std::make_shared<NvmInstructionDesc>(NvmInstructionDesc(executor, anders, start));
}

bool klee::operator==(const NvmInstructionDesc &lhs, const NvmInstructionDesc &rhs) {
  bool instEq = lhs.curr_ == rhs.curr_;
  bool valEq  = lhs.values_ == rhs.values_;
  bool stkEq  = lhs.stackframe_ == rhs.stackframe_;
  bool resEq  = lhs.runtime_function_ == rhs.runtime_function_;
  bool recEq  = lhs.ready_to_recurse_ == rhs.ready_to_recurse_;
  bool fallEq = lhs.force_special_handler_call_fall_over_ == rhs.force_special_handler_call_fall_over_;

  return instEq &&
         valEq &&
         stkEq &&
         recEq &&
         fallEq &&
         resEq;
}

bool klee::operator!=(const NvmInstructionDesc &lhs, const NvmInstructionDesc &rhs) {
  return !(lhs == rhs);
}

/**
 * NvmHeuristicInfo
 */

// Private
bool NvmHeuristicInfo::computeCurrentPriority(ExecutionState *es) {
  
  bool changed = false;
  for (std::shared_ptr<NvmInstructionDesc> &sptr : current_states_) {
    uint64_t prevWeight = sptr->getWeight();
    changed = changed || sptr->updateWeight(es);
    assert(prevWeight >= sptr->getWeight() && "assumptions violated!");
  }

  if (!changed) return false;

  std::list<
    std::pair<
      std::shared_ptr<NvmInstructionDesc>,
      NvmInstructionDesc::UnorderedSet
    >
  > toTraverse;

  std::unordered_set<std::shared_ptr<NvmInstructionDesc>> terminators;

  for (std::shared_ptr<NvmInstructionDesc> &sptr : current_states_) {
    toTraverse.emplace_back(sptr, sptr->constructScions());
    if (sptr->getWeight()) terminators.insert(sptr);
  }

  // Downward traversal.
  // TODO: Recursion check
  while (toTraverse.size()) {
    std::pair<
      std::shared_ptr<NvmInstructionDesc>,
      NvmInstructionDesc::UnorderedSet
    > current = toTraverse.front();

    toTraverse.pop_front();
    // There are cases when two basic blocks converge that an instruction which
    // used to be unique is no longer. So, we need to check the traversed
    // set on every iteration.
    // (iangneal): I think this is fine as we use sets elsewhere
    // if (traversed.find(instDesc) != traversed.end()) continue;

    // If there are no scions left, we don't add the successor back on.
    if (!current.second.size()) continue;

    NvmInstructionDesc::UnorderedSet next_scions;

    for (NvmInstructionDesc desc : current.second) {
      (void)desc.updateWeight(es);
      if (desc.getWeight()) {
        // This successor produced a second! So, it is not a terminator, and
        // they need to be attached.
        terminators.erase(current.first);
        auto succ_sptr = std::make_shared<NvmInstructionDesc>(desc);
        // We assume this is a terminator until it's first true successor is 
        // found.
        terminators.insert(succ_sptr);
        NvmInstructionDesc::attach(current.first, succ_sptr);
        // Since this is a new successor, it is also attached to this list
        toTraverse.emplace_back(succ_sptr, succ_sptr->constructScions());
      } else {
        auto ns = desc.constructScions();
        next_scions.insert(ns.begin(), ns.end());
      }
    }

    #if 0
    instDesc->calculateWeight(es);
    if (instDesc->)
    
    std::list<std::shared_ptr<NvmInstructionDesc>> succs = instDesc->getSuccessors(traversed);
    if (succs.size()) {
      for (std::shared_ptr<NvmInstructionDesc> &sp : succs) {
        std::list<std::shared_ptr<NvmInstructionDesc>> pred = sp->getPredecessors();

        bool found = false;        
        for (std::shared_ptr<NvmInstructionDesc> &p : pred) {
          // Won't be fully equal as adding the successor creates a new instance
          // that is no longer equal to instDesc;
          found = found || (p->inst() == instDesc->inst());     
        }
        assert(found && "The successor should have a predecessor equal to the current");

        toTraverse.push_back(sp.get());
      }
    } else if (instDesc->isTerminator() || !instDesc->isValid()) {
      // Invalid instructions serve as "terminator" points until they are
      // resolved.
      instDesc->calculateWeight(es);
      terminators.insert(instDesc);
    } else {
      // Convergent branches and loops.

      if (!instDesc->isValid()) assert(instDesc->isTerminator());
    }
    #endif
  }

  // Now, we propagate up.
  // -- we still need loop detection.
  std::unordered_set<std::shared_ptr<NvmInstructionDesc>> propagated;
  // -- we do this as a list so we can modify them.
  std::list<std::shared_ptr<NvmInstructionDesc>> level(terminators.begin(), 
                                                       terminators.end());

  while (level.size()) {
    std::list<std::shared_ptr<NvmInstructionDesc>> nextLevel;

    for (std::shared_ptr<NvmInstructionDesc> &sptr : level) {
      assert(sptr->getWeight());
      for (std::shared_ptr<NvmInstructionDesc> &pred : sptr->getPredecessors()) {
        assert(pred->getWeight());

        // This prevents loops.
        if (propagated.count(pred)) {
          continue;
        }

        uint64_t cur_priority = pred->getPriority();
        uint64_t pos_priority = pred->getWeight() + sptr->getPriority();
        pred->setPriority(max(cur_priority, pos_priority));

        // We only want to update the priority of current states, not past it.
        if (!std::count(current_states_.begin(), current_states_.end(), pred)) {
          nextLevel.push_back(pred);
        }
      }

      propagated.insert(sptr);
    }

    level = nextLevel;
  }

  current_priority_ = 0;
  for (auto &c : current_states_) {
    current_priority_ = max(current_priority_, c->getPriority());
  }

  return true;
}

// Public
NvmHeuristicInfo::NvmHeuristicInfo(Executor *executor, KFunction *mainFn, ExecutionState *es) {
  TimerStatIncrementer timer(stats::nvmHeuristicTime);

  current_states_.push_back(NvmInstructionDesc::createEntry(executor, mainFn));

  if (!computeCurrentPriority(es)) {
    klee_warning_once((const void*)this, "Empty heuristic state!");
  }
}

// Public
void NvmHeuristicInfo::updateCurrentState(ExecutionState *es, KInstruction *pc, bool isNvm) {
  TimerStatIncrementer timer(stats::nvmHeuristicTime);

  for (auto it = current_states_.begin(); it != current_states_.end(); ++it) {
    *it = (*it)->update(pc, isNvm);
  }

  computeCurrentPriority(es);
}

// Public
void NvmHeuristicInfo::stepState(ExecutionState *es, KInstruction *pc, KInstruction *nextPC) {
  TimerStatIncrementer timer(stats::nvmHeuristicTime);

  if (!current_states_.size()) {
    if (!last_state_) {
      klee_warning_once(0, "Empty heuristic state!");
      return;
    }
    
    assert(last_state_->isTerminator());
    return;
  }

  std::shared_ptr<NvmInstructionDesc> foundState(nullptr);
  for (std::shared_ptr<NvmInstructionDesc> &sptr : current_states_) {
    if (pc == sptr->kinst()) {
      assert(!foundState);
      foundState = sptr;
      break;
    }
  }

  if (!foundState) return;
  
  if (foundState->needsResolution()) {
    foundState = foundState->resolve(nextPC);
    assert(!foundState->needsResolution());

    computeCurrentPriority(es);
    
    assert(foundState->getSuccessors().size());
    assert(!foundState->isTerminator());
  }

  if (foundState->isTerminator() && foundState->isRecursable()) {
    // Lazy recursion evaluation
    foundState = foundState->getRecursable();
    computeCurrentPriority(es);
    assert(!foundState->isTerminator());
  }

  last_state_ = foundState;
  current_states_ = foundState->getSuccessors();

  // auto candidates = foundState->getMatchingSuccessors(nextPC);
  // if (!candidates.size() && isa<CallInst>(pc->inst) && foundState->forceFallOverUnset()) {
  //   foundState = foundState->getForceFallOver();
  //   assert(!priority.count(foundState.get()));
  //   errs() << "forced " << foundState->str() << "\n";
  //   computeCurrentPriority(es);
  //   candidates = foundState->getMatchingSuccessors(nextPC);
  // }

  // if (candidates.size() > 1) {
  //   klee_warning("Too many candidates! Wanted 1, got %zu", candidates.size());
  //   errs() << nextPC->inst->getFunction()->getName() << "@" << *nextPC->inst << "\n";
  //   for (auto &c : candidates) {
  //     errs() << c->str() << "\n";
  //   }
  //   klee_error("too many candidates!");
  //   return;
  // } else if (!candidates.size() && foundState->getSuccessors().size() > 1) {
  //   errs() << nextPC->inst->getFunction()->getName() << "@" << *nextPC->inst << "\n";
  //   errs() << "Does not match any of: \n";
  //   for (const auto &s : foundState->getSuccessors()) {
  //     errs() << s->str() << "\n";
  //   }

  //   // This can happen with function pointers, if they aren't handled carefully.
  //   assert(false && "no candidates! we need to recalculate");
  //   return;
  // } else if (!candidates.size() && foundState->getSuccessors().size() == 1) {
  //   candidates = foundState->getSuccessors();
  // }

  // std::shared_ptr<NvmInstructionDesc> &next = candidates.front();

  // if (!next->isValid()) {
  //   // We now know what the next instruction is, thanks to the symbolic execution
  //   // state.
  //   next->setPC(es, nextPC);
  //   computeCurrentPriority(es);
  // } else if (foundState->isCachelineModifier(es)) {
  //   // Update the weight if this instruction is known to modify NVM.
  //   // -- this is probably pretty inefficient.
  //   computeCurrentPriority(es);
  // }

  // current_state = next;
  // computeCurrentPriority(es);
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2: */
