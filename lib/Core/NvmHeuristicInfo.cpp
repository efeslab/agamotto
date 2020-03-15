#include "NvmHeuristicInfo.h"

using namespace llvm;
using namespace std;
using namespace klee;

#include <sstream>

#include "Executor.h"
#include "klee/ExecutionState.h"

/**
 * StaticStorage
 */

template<class X>
typename StaticStorage<X>::object_map_t StaticStorage<X>::objects_;

template<class X>
std::shared_ptr<X> StaticStorage<X>::getShared(const X &x) { 
  if (objects_.count(x)) {
    return objects_[x];
  }

  objects_[x] = std::make_shared<X>(x);
  return objects_[x];
}

/**
 * NvmStackFrameDesc
 */

uint64_t NvmStackFrameDesc::hash(void) const {
  uint64_t hash_value = 0;
  auto iter = return_stack.begin();
  for (uint64_t i = 0;
       i < return_stack.size() && iter != return_stack.end(); 
       ++i) 
  {
    uint64_t ptr_val = ((uint64_t)*iter);
    hash_value ^= (ptr_val << i) | (ptr_val >> (64llu - i)); // Rotational shift.
    iter++;
  }

  return hash_value;
}

std::shared_ptr<NvmStackFrameDesc> NvmStackFrameDesc::doReturn(void) const {
  NvmStackFrameDesc ns = *this;
  ns.caller_stack.pop_back();
  ns.return_stack.pop_back();
  return getShared(ns);
}

std::shared_ptr<NvmStackFrameDesc> NvmStackFrameDesc::doCall(
    Instruction *caller, Instruction *retLoc) const {
  NvmStackFrameDesc ns = *this;
  ns.caller_stack.push_back(caller);
  ns.return_stack.push_back(retLoc);
  return getShared(ns);
}

std::string NvmStackFrameDesc::str(void) const {
  std::stringstream s;

  s << "Stackframe: ";
  for (Instruction *i : caller_stack) {
    Function *f = i->getFunction();
    s << "\n\t" << f->getName().data() << " @ ";
    std::string tmp;
    llvm::raw_string_ostream rs(tmp); 
    i->print(rs);
    s << tmp;
  }

  return s.str();
}

bool klee::operator==(const NvmStackFrameDesc &lhs, const NvmStackFrameDesc &rhs) {
   return lhs.caller_stack == rhs.caller_stack && lhs.return_stack == rhs.return_stack;
 }

/**
 * NvmValueDesc
 */

uint64_t NvmValueDesc::hash(void) const {
  uint64_t hash_value = 0;

  for (Value *v : mmap_calls_) {
    hash_value ^= std::hash<void*>{}((void*)v);
  }
  for (Value *v : global_nvm_) {
    hash_value ^= std::hash<void*>{}((void*)v);
  }
  for (Value *v : local_nvm_) {
    hash_value ^= std::hash<void*>{}((void*)v);
  }

  hash_value ^= caller_values_ ? caller_values_->hash() : 0;
  hash_value ^= std::hash<void*>{}(call_site_);

  return hash_value;
}

std::shared_ptr<NvmValueDesc> NvmValueDesc::doCall(
  andersen_sptr_t apa, llvm::CallInst *ci) const 
{
  NvmValueDesc newDesc;
  newDesc.caller_values_ = getShared(*this);
  newDesc.call_site_ = ci;
  newDesc.global_nvm_ = global_nvm_;

  Function *f = utils::getCallInstFunction(ci); //->getCalledFunction();
  assert(f && "Don't know what do with a null function!");

  for (unsigned i = 0; i < (unsigned)ci->getNumArgOperands(); ++i) {
    Value *op = ci->getArgOperand(i);
    Argument *arg = (f->arg_begin() + i);
    assert(op && arg && "Unsure what to do!");

    // Scalars don't necessarily point to anything.
    if (!arg->getType()->isPtrOrPtrVectorTy()) continue; 
    
    // We actually want the points-to set for this
    std::vector<const Value*> ptsSet;
    errs() << "doing call instruction stuff\n";
    errs() << *op << "\n";
    bool ret = apa->getResult().getPointsToSet(op, ptsSet);
    assert(ret && "could not get points-to set!");
    for (const Value *ptsTo : ptsSet) {
      errs() << "\tpoints to " << *ptsTo << "\n";
      if (isNvm(apa, ptsTo)) {
        errs() << "\t\twhich is nvm!\n";
        newDesc.local_nvm_.insert(arg);
      }
    }

  }

  return getShared(newDesc);
}

std::shared_ptr<NvmValueDesc> NvmValueDesc::doReturn(andersen_sptr_t apa, ReturnInst *i) const {
  std::shared_ptr<NvmValueDesc> retDesc = caller_values_;
  
  if (Value *retVal = i->getReturnValue()) {
    std::vector<const Value*> ptsTo;
    bool success = apa->getResult().getPointsToSet(retVal, ptsTo);
    if (success && ptsTo.size()) {
      errs() << "doRet " << *retVal << "\n";
      retDesc = retDesc->updateState(call_site_, isNvm(apa, retVal));
    } else {
       errs() << "doRet doesn't point to a memory object! " << success << " " << ptsTo.size() << "\n";
    }
  }

  return retDesc;
}

std::shared_ptr<NvmValueDesc> NvmValueDesc::updateState(Value *val, bool isNvm) const {
  NvmValueDesc vd = *this;
  // Since the set is sparse, they're only contained if isNvm.
  if (isNvm) {
    if (isa<GlobalValue>(val)) {
      vd.global_nvm_.insert(val);
    } else {
      vd.local_nvm_.insert(val);
    }
  }

  return getShared(vd);
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

  // assert(desc.mmap_calls_.size() && "No mmap calls?");

  // for (Value *v : desc.mmap_calls_) {
  //   errs() << *v << "\n";
  // }

  return getShared(desc);
}

std::string NvmValueDesc::str(void) const {
  std::stringstream s;
  s << "Value State:\n";
  s << "\tNumber of runtime global nvm values: " << global_nvm_.size();
  s << "\n\tNumber of runtime local nvm values: " << local_nvm_.size();
  return s.str();
}

bool klee::operator==(const NvmValueDesc &lhs, const NvmValueDesc &rhs) {
  return lhs.mmap_calls_ == rhs.mmap_calls_ &&
        //  lhs.nvmMmaps_ == rhs.nvmMmaps_ &&
        //  lhs.volMmaps_ == rhs.volMmaps_ &&
         lhs.global_nvm_ == rhs.global_nvm_ &&
         lhs.local_nvm_ == rhs.local_nvm_ &&
         lhs.caller_values_ == rhs.caller_values_ &&
         lhs.call_site_ == rhs.call_site_;
}

/**
 * NvmInstructionDesc
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
          stackframe_(stackframe) {
        // weight_ = calculateWeight();
      }

NvmInstructionDesc::NvmInstructionDesc(Executor *executor,
                         andersen_sptr_t apa, 
                         KInstruction *location) 
        : executor_(executor),
          apa_(apa), 
          mod_(executor->kmodule.get()), 
          curr_(location), 
          values_(NvmValueDesc::staticState(mod_->module.get())), 
          stackframe_(NvmStackFrameDesc::empty()) {}

bool NvmInstructionDesc::isCachelineModifier(ExecutionState *es) {
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
          ptr = ci->getOperand(0)->stripPointerCasts();
        default:
          break;
      }
    }
  }

  if (!ptr) return false;

  KInstruction *target = mod_->getKInstruction(dyn_cast<Instruction>(ptr));
  ref<Expr> addr = es->stack.back().locals[target->dest].value;
  if (addr.isNull()) {
    klee_error("null?");
  }

  Executor::ExactResolutionList rl;
  executor_->resolveExact(*es, addr, rl, "isModifier");

  // This logic also makes sure that if it isn't persistent memory, we always skip it.
  for (Executor::ExactResolutionList::iterator it = rl.begin(), ie = rl.end(); it != ie; ++it) {
    const ObjectState *os = it->first.second;
    
    if (isa<PersistentState>(os)) {
      return true;
    }
  }
     
  return false;
}

uint64_t NvmInstructionDesc::calculateWeight(ExecutionState *es) {
  // We use this as an incentive to resolve unresolved function pointers.
  if (!curr_) {
    errs() << str() << " has weight 1u cuz unknown!\n";
    // errs() << "1u\n";
    weight_ = 1u;
    return weight_;
  }

  Instruction *i = curr_->inst;
  // First, we check if the instruction is inherently important.
  if (CallInst *ci = dyn_cast<CallInst>(curr_->inst)) {
    // Case 1: Contains an mmap call.
    if (values_->isMmapCall(ci)) {
      errs() << str() << " has weight 2u cuz mmap-class!\n";
      weight_ = 2u;
      return weight_;
    }

    // Case 4: Is a memory fence.
    // TODO: also should cover mfences, in theory.
    Function *f = ci->getCalledFunction();
    if (f && f->isDeclaration() && f->getIntrinsicID() == Intrinsic::x86_sse_sfence) {
      errs() << str() << " has weight 1u cuz sfence!\n";
      weight_ = 1u;
      return weight_;
    }
  }

  // Second, we see if a memory store/flush points to NVM.
  bool mustBeDirty = false;
  // TODO: clwb
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
          ptr = ci->getOperand(0)->stripPointerCasts();
        default:
          break;
      }
    }
    // Case 5: When we check for persistence.
    Function *unmap = mod_->module->getFunction("munmap");
    Function *check = mod_->module->getFunction("klee_pmem_check_persisted");
    if (f == unmap || f == check) {
      ptr = ci->getOperand(0)->stripPointerCasts();
      mustBeDirty = true;
    }
  }

  if (!ptr) {
    errs() << str() << " has weight 0u cuz no pointer!\n";
    weight_ = 0u;
    return weight_;
  }

  std::vector<const Value*> ptsSet;
  bool ret = apa_->getResult().getPointsToSet(ptr, ptsSet);
  assert(ret && "could not get points-to set!");
  errs() << "(weight stuff)\n";
  errs() << *ptr << "\n";
  // assert(ptsSet.size() && "this pointer points to nothing?!");

  for (const Value *v : ptsSet) {
    errs() << "\tpoints to " << *v << "\n";

    if (values_->isNvm(apa_, v)) {
      if (mustBeDirty) {
        errs() << "\t\tmust be dirty...\n";
        assert(isa<Instruction>(v));
        // Use the execution state to resolve this address.
        KInstruction *target = mod_->getKInstruction(dyn_cast<Instruction>(const_cast<Value*>(v)));

        // The target for the points-to set should always be some allocation site.
        // Need to make sure it's from the right stack as well.

        // returns if it found a memory object. (succes)
        const auto checkAllocas = [&] (bool &persisted) -> bool {
          for (StackFrame &sf : es->stack) {
            for (const MemoryObject *mo : sf.allocas) {
              if (mo->allocSite != v) continue;
              if (!es->addressSpace.findObject(mo)) continue;
              
              if (const PersistentState *ps = dyn_cast<PersistentState>(es->addressSpace.findObject(mo))) {
                persisted = ps->mustBePersisted(executor_->solver, *es);
              }

              return true;
            }
          }

          return false;
        };

        const auto findAddr = [&] () -> ref<Expr> {
          ref<Expr> null;
          for (StackFrame &sf : es->stack) {
            if (sf.kf->function != target->inst->getFunction()) continue;
           
            // This is for if the allocation was a function call location, like malloc or mmap
            ref<Expr> addr = sf.locals[target->dest].value;
            if (!addr.isNull()) return addr;
          }

          return null;
        };

        bool allocaPersisted;
        bool ret = checkAllocas(allocaPersisted);
        if (ret) {
          if (!allocaPersisted) {
            errs() << "weight 1 cuz dirty!\n";
            weight_ = 1u;
            return weight_;
          } else {
            errs() << "weight 0 cuz clean!\n";
            weight_ = 0u;
            return weight_;
          }
        }

        ref<Expr> addr = findAddr();

        if (addr.isNull()) {
          errs() << *target->inst << "\n";
          errs() << "\t NULL\n";
          continue;
        }

        Executor::ExactResolutionList rl;
        executor_->resolveExact(*es, addr, rl, "calculateWeight");

        // This logic also makes sure that if it isn't persistent memory, we always skip it.
        bool isPersisted = true;
        for (Executor::ExactResolutionList::iterator it = rl.begin(), 
              ie = rl.end(); it != ie; ++it) {
          const ObjectState *os = it->first.second;
          
          if (const PersistentState *ps = dyn_cast<PersistentState>(os)) {
            isPersisted = isPersisted && ps->mustBePersisted(executor_->solver, *es);
          }
        }

        if (isPersisted) continue;
        
      }
      // errs() << "1u\n";
      errs() << str() << " has weight 1u cuz cache-state modifying!\n";
      weight_ = 1u;
      return weight_;
    }
  }

  errs() << str() << " has weight 0u cuz default!\n";
  weight_ = 0u;
  return weight_;
}

std::list<NvmInstructionDesc> NvmInstructionDesc::constructSuccessors(void) {
  std::list<NvmInstructionDesc> ret;
  // I essentially need to figure out the next instruction.
  if (!isValid()) return ret; 

  Instruction *ip = curr_->inst;
  BasicBlock *bb = ip->getParent();

  // if (isa<ReturnInst>(ip)) {
  //   errs() << "return " << (ip == bb->getTerminator() ? "is terminator\n" : "is not terminator\n");
  // }

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
      ret.push_back(desc);
      return ret;

    } else if (BranchInst *bi = dyn_cast<BranchInst>(ip)) {
      for (auto *sbb : bi->successors()) {
        Instruction *ni = &(sbb->front());
        NvmInstructionDesc desc(executor_, apa_, mod_->getKInstruction(ni), values_, stackframe_);
        ret.push_back(desc);
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
        ret.push_back(desc);
      }

      return ret;

    } else if (isa<UnreachableInst>(ip)) {
      assert(ret.empty() && "Unreachable has no successors!");
      return ret;
    } else {
      errs() << *ip << "\n";
      assert(false && "Assumption violated -- terminator instruction is not a return or branch!");
      return ret;
    }

  } else if (CallInst *ci = utils::getNestedFunctionCallInst(ip)) {
    Function *nf = utils::getCallInstFunction(ci);
    if (!nf) {
      errs() << *ci << "\n";
      klee_error("Could not get the called function!");
    } 

    if (mod_->functionMap.find(nf) != mod_->functionMap.end()) {
      // Successor is the entry to the next function.
      Instruction *nextInst = &(nf->getEntryBlock().front());
      // Instruction *retLoc = ci->getNextNonDebugInstruction();
      Instruction *retLoc = ip->getNextNode();
      // errs() << "ret loc is " << *retLoc << "\n";
      assert(retLoc && "Assumption violated -- call instruction is the last instruction in a basic block!");

      std::shared_ptr<NvmStackFrameDesc> newStack = stackframe_->doCall(ci, retLoc);
      std::shared_ptr<NvmValueDesc> newValues = values_->doCall(apa_, ci);

      NvmInstructionDesc desc(executor_, apa_, mod_->getKInstruction(nextInst), newValues, newStack);
      ret.push_back(desc);
    } else {
      // Instruction *ni = ip->getNextNonDebugInstruction();
      Instruction *ni = ip->getNextNode();
      NvmInstructionDesc desc(executor_, apa_, mod_->getKInstruction(ni), values_, stackframe_);
      ret.push_back(desc);
    }
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
      // this->weight_ += 1;
      Instruction *retLoc = ip->getNextNode();
      // errs() << "ret loc is " << *retLoc << "\n";
      assert(retLoc && "Assumption violated -- call instruction is the last instruction in a basic block!");
      std::shared_ptr<NvmStackFrameDesc> newStack = stackframe_->doCall(ci, retLoc);

      NvmInstructionDesc desc(executor_, apa_, nullptr, values_, newStack);
      ret.push_back(desc);
      return ret;
    } else {
      // Fallthrough to the default case.
      // errs() << "Fallthrough call: " << *ci << "\n";
      assert((ci->isInlineAsm() || isa<IntrinsicInst>(ci)) && "assumptions violated!");
    }
  }

  // Default case

  // Instruction *ni = ip->getNextNonDebugInstruction();
  Instruction *ni = ip->getNextNode();
  NvmInstructionDesc desc(executor_, apa_, mod_->getKInstruction(ni), values_, stackframe_);
  ret.push_back(desc);

  return ret;
}

void NvmInstructionDesc::setSuccessors() {
  errs() << "setting successors for" << *curr_->inst << "\n";
  for (const NvmInstructionDesc &succ : constructSuccessors()) {
    std::shared_ptr<NvmInstructionDesc> sptr = getShared(succ);
    errs() << "\t" << *sptr->curr_->inst << "\n";
    successors_.push_back(sptr);
    sptr->addPredecessor(*this);
  }

  std::unordered_set<shared_ptr<NvmInstructionDesc>> uniq(successors_.begin(), successors_.end());

  if (uniq.size() != successors_.size()) {
    errs() << str() << "\nSuccessors:\n";
    for (const auto &s : uniq) {
      errs() << s->str() << "\n";
    }
  }
  assert(uniq.size() == successors_.size() && "Duplicate successors generated!");

  isTerminal = successors_.size() == 0;
}

std::list<std::shared_ptr<NvmInstructionDesc>> 
NvmInstructionDesc::getSuccessors(
    const std::unordered_set<std::shared_ptr<NvmInstructionDesc>> &traversed) 
{
  if (successors_.size() == 0 && !isTerminal) {
    setSuccessors();
    assert((successors_.size() > 0 || isTerminal) 
          && "Error in successor calculation!");
  }

  std::list<std::shared_ptr<NvmInstructionDesc>> currentSuccessors;
  for (auto sptr : successors_) {
    if (traversed.find(sptr) == traversed.end()) {
      currentSuccessors.push_back(sptr);
    }
  }

  return currentSuccessors;
}

std::list<std::shared_ptr<NvmInstructionDesc>> 
NvmInstructionDesc::getMatchingSuccessors(KInstruction *nextPC) {
  std::list<std::shared_ptr<NvmInstructionDesc>> ret;

  assert(successors_.size() || isTerminal);

  // errs() << __PRETTY_FUNCTION__ << " for " << *curr_->inst << "\n";
  // errs() << "Number of successors: " << successors_.size() << 
  //   (isTerminal ? " (terminal)" : " (not terminal)") <<"\n";
  // errs() << "Looking for " << *nextPC->inst << "\n";

  for (std::shared_ptr<NvmInstructionDesc> p : successors_) {
    if (p->curr_ == nextPC) ret.push_back(p);
    // else {
    //   errs() << "|succ| " <<*p->curr_->inst << " != |next| " << *nextPC->inst << "\n";
    // }
  }

  return ret;
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
    for (auto psp : predecessors_) {
      std::string tmp;
      llvm::raw_string_ostream rs(tmp);
      psp->inst()->print(rs);
      s << "\n\t" << tmp;
    }
  }
  if (successors_.size()) {
    s << "\nsucc:";
    for (auto psp : successors_) {
      std::string tmp;
      llvm::raw_string_ostream rs(tmp);
      psp->inst()->print(rs);
      s << "\n\t" << tmp;
    }
  }
  s << "\n----****----\n";
  return s.str();
}

std::shared_ptr<NvmInstructionDesc> NvmInstructionDesc::createEntry(Executor *executor, KFunction *mainFn) {
  andersen_sptr_t anders = std::make_shared<AndersenAAWrapperPass>();
  assert(!anders->runOnModule(*executor->kmodule->module) && "Analysis pass should return false!");
  errs() << "creatEntry begin\n";
  for (const auto &BB : *mainFn->function) {
    for (const auto &I : BB) {
      const Value *toChk;
      switch(I.getOpcode()) {
        case Instruction::Store:
          assert(isa<StoreInst>(&I));
          toChk = dyn_cast<StoreInst>(&I)->getPointerOperand();
          break;
        default:
          toChk = &I;
          break;
      }
      std::vector<const Value*> ptsTo;
      bool ret = anders->getResult().getPointsToSet(toChk, ptsTo);

      if (!ret) {
        errs() << I << " (" << *toChk << ")\n\treturned false!\n";
      } else if (ptsTo.empty()) {
        errs() << I << " (" << *toChk << ") points to nothing!\n";
      } else {
        errs() << I << " (" << *toChk << ") points to\n";
        for (const Value *v : ptsTo) {
          errs() << "\t" << *v << "\n";
        }
      }
    }
  }
  errs() << "creatEntry end\n";
  
  llvm::Instruction *inst = &(mainFn->function->getEntryBlock().front());
  KInstruction *start = mainFn->getKInstruction(inst);

  NvmInstructionDesc entryInst = NvmInstructionDesc(executor, anders, start);
  return getShared(entryInst);
}

bool klee::operator==(const NvmInstructionDesc &lhs, const NvmInstructionDesc &rhs) {
  bool instEq = lhs.curr_ == rhs.curr_;
  bool valEq  = lhs.values_ == rhs.values_;
  bool stkEq  = lhs.stackframe_ == rhs.stackframe_;
  // bool succEq = lhs.successors_ == rhs.successors_;
  // bool predEq = lhs.predecessors_ == rhs.predecessors_;
  // bool wEq    = lhs.weight_ == rhs.weight_;
  // if (lhs.curr_->inst == rhs.curr_->inst) {
    // errs() << *lhs.curr_->inst << " == " << *rhs.curr_->inst <<
    //     ": " << instEq << " " << valEq << " " << stkEq << " " << succEq
    //     << " " << predEq << " " << wEq << "\n";
    // errs() << *lhs.curr_->inst << " == " << *rhs.curr_->inst <<
    //     ": " << instEq << " " << valEq << " " << stkEq << " " << wEq << "\n";
  // }
  return instEq &&
         valEq &&
         stkEq;
        //  stkEq &&
        //  succEq &&
        //  predEq &&
        //  wEq;

}

/**
 * NvmHeuristicInfo
 */

std::unordered_map<std::shared_ptr<NvmInstructionDesc>, uint64_t> NvmHeuristicInfo::priority;

NvmHeuristicInfo::NvmHeuristicInfo(Executor *executor, KFunction *mainFn, ExecutionState *es) {
  current_state = NvmInstructionDesc::createEntry(executor, mainFn);
  current_state->calculateWeight(es);
  errs() << "init: " << current_state->str() << "\n";
  computeCurrentPriority(es);
}

void NvmHeuristicInfo::computeCurrentPriority(ExecutionState *es) {
  // errs() << "Looking for " << current_state->str();
  if (priority.count(current_state)) {
    // errs() << "Found!\n";
    return;
  }
  // errs() << "Not Found!\n";

  // fprintf(stderr, "pointer of current state: %p\n", current_state.get());
  // priority[current_state] = 0;
  // assert(priority.count(current_state) && "what now");

  std::list<std::shared_ptr<NvmInstructionDesc>> toTraverse;
  toTraverse.push_back(current_state);

  std::unordered_set<std::shared_ptr<NvmInstructionDesc>> traversed;
  std::unordered_set<std::shared_ptr<NvmInstructionDesc>> terminators;

  // Downward traversal.
  while (toTraverse.size()) {
    std::shared_ptr<NvmInstructionDesc> instDesc = toTraverse.front();
    toTraverse.pop_front();
    // There are cases when two basic blocks converge that an instruction which
    // used to be unique is no longer. So, we need to check the traversed
    // set on every iteration.
    if (traversed.find(instDesc) != traversed.end()) continue;

    instDesc->calculateWeight(es);

    // Instructions can be invalid if we don't have enough information to 
    // figure out their successors statically. In this case, we just need to
    // skip them.
    // -- We actually don't need to do this, as an invalid instruction is 
    //    considered a terminator.
    // if (!instDesc->isValid()) continue;

    // errs() << instDesc->str();
    // errs() << toTraverse.size() << " " << traversed.size() << "\n";

    // assert(traversed.find(instDesc) == traversed.end() && "Repeat!");
    // for (auto sptr : traversed) {
    //   assert(!(*instDesc == *sptr) && "I guess shared_ptr== doesn't do it");
    // }
    traversed.insert(instDesc);
    
    const auto &succ = instDesc->getSuccessors(traversed);
    if (succ.size()) {
      for (auto sptr : succ) {
        toTraverse.push_back(sptr);
      }
    } else if (instDesc->isTerminator()) {
      errs() << "TERMINATOR: " << *instDesc->inst() << "\n";
      terminators.insert(instDesc);
    } else {
      // Convergence branches and loops.
      // errs() << "OOPS\n";
      // errs() << instDesc->str();
      // assert(false && "what?");
    }
  }
  
  // Base state:
  for (auto sptr : terminators) {
    priority[sptr] = sptr->calculateWeight(es);
  }

  // Now, we propagate up.
  std::unordered_set<std::shared_ptr<NvmInstructionDesc>> level = terminators;

  // -- we still need loop detection.
  std::unordered_set<std::shared_ptr<NvmInstructionDesc>> propagated;

  // errs() << "Propagate up\n";
  while (level.size()) {
    std::unordered_set<std::shared_ptr<NvmInstructionDesc>> nextLevel;

    // errs() << "level size: " << level.size() << "\n";
    for (auto sptr : level) {
      // errs() << "\t" << "n preds: " << sptr->getPredecessors().size() << "\n";
      for (auto pred : sptr->getPredecessors()) {
        if (propagated.find(pred) != propagated.end()) {
          errs() << "||| skipping " << *pred->inst() << "\n"; 
          continue;
        }

        uint64_t current_priority = priority[pred];
        priority[pred] = max(current_priority, pred->calculateWeight(es) + priority[sptr]);

        errs() << "^Updating " << *pred->inst() << " from " << *sptr->inst() << "\n";
        errs() << "^\tfrom " << current_priority << " to " << priority[pred] << "\n";

        nextLevel.insert(pred);
      }

      propagated.insert(sptr);
    }

    level = nextLevel;
  }

  // for (const auto &p : priority) {
  //   errs() << p.first->str();
  //   if (*p.first == *current_state) errs() << "+++HEY+++ " << (p.first.get() == current_state.get())  <<"\n";
  // }
  errs() << priority.size() << " =?= " << NvmInstructionDesc::getNumSharedObjs() << "\n";
  // priority.at(current_state);
  // dumpState();
  // assert(priority.size() == NvmInstructionDesc::getNumSharedObjs() && "too few!");
  assert(priority.count(current_state) && "can't find our work!");
  dumpState();
}

void NvmHeuristicInfo::updateCurrentState(ExecutionState *es, KInstruction *pc, bool isNvm) {
  current_state = current_state->update(pc, isNvm);
  computeCurrentPriority(es);
}

void NvmHeuristicInfo::stepState(ExecutionState *es, KInstruction *nextPC) {
  klee_warning("dumping");
  dumpState();
  // errs() << *current_state->kinst()->inst << "\n\t=> " << *nextPC->inst << "\n";
  if (current_state->isTerminator()) {
    assert(nextPC == current_state->kinst() && "assumption violated");
    return;
  }

  // errs() << "CURRENT " << current_state->str(); 
  // if (current_state->str().find("exit") != string::npos) {
  //   errs() << "succs\n";
  //   for (auto s : current_state->getSuccessors()) {
  //     errs() << s->str() << "\n";
  //   }
  // }

  auto candidates = current_state->getMatchingSuccessors(nextPC);

  if (candidates.size() > 1) {
    klee_warning("Too many candidates! Wanted 1, got %zu", candidates.size());
    errs() << nextPC->inst->getFunction()->getName() << "@" << *nextPC->inst << "\n";
    for (auto &c : candidates) {
      errs() << c->str() << "\n";
    }
    klee_error("too many candidates!");
    return;
  } else if (!candidates.size() && current_state->getSuccessors().size() > 1) {
    errs() << nextPC->inst->getFunction()->getName() << "@" << *nextPC->inst << "\n";
    errs() << "Does not match any of: \n";
    for (const auto &s : current_state->getSuccessors()) {
      errs() << s->str() << "\n";
    }

    // This can happen with function pointers, if they aren't handled carefully.
    assert(false && "no candidates! we need to recalculate");
    return;
  } else if (!candidates.size() && current_state->getSuccessors().size() == 1) {
    candidates = current_state->getSuccessors();
  }

  std::shared_ptr<NvmInstructionDesc> next = candidates.front();

  if (!next->isValid()) {
    // We now know what the next instruction is, thanks to the symbolic execution
    // state.
    next->setPC(es, nextPC);
    computeCurrentPriority(es);
  } else if (current_state->isCachelineModifier(es)) {
    // Update the weight if this instruction is known to modify NVM.
    errs() << "thing\n";
    // -- this is probably pretty inefficient.
    // This IS inefficient.
    priority.clear();
    computeCurrentPriority(es);
  }

  current_state = next;
  // klee_warning("tick.\n%s", current_state->str().c_str());
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2: */
