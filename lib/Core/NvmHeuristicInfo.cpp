#include "NvmHeuristicInfo.h"

using namespace llvm;
using namespace std;
using namespace klee;

#include <sstream>

/**
 * StaticStorage
 */

template<class X>
typename StaticStorage<X>::object_map_t StaticStorage<X>::objects_;

template<class X>
std::shared_ptr<X> StaticStorage<X>::getShared(const X &x) {
  
  if (objects_.count(x)) {
    // errs() << "getShared hit!\n";
    return objects_[x];
  }

  // errs() << "getShared, not found\n";
  // for (const auto &p : objects_) {
  //   errs() << "-------\n";
  //   errs() << x.str() << "\n";
  //   errs() << "items eq: " << (p.first == x) << " hash eq: " << (p.first.hash() == x.hash()) << "\n";
  //   errs() << "-------\n";
  // }
  // errs() << "getShared, end print\n"; 

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
  uint64_t count = 0;
  for (const auto &p : state_) {
    uint64_t val = (uint64_t)p.second;
    hash_value ^= (val << count) | (val >> (64llu - count));
    count++;
  }

  return hash_value;
}

void NvmValueDesc::mutateState(Value *val, NvmValueState vs) {
  if (vs == DoesNotContain) return; // Make sparse
  state_[val] = vs;
}

// TODO: policies for speculating on MMAP
NvmValueState NvmValueDesc::getOutput(Instruction *i) const {
  bool contains = false;
  for (auto iter = i->op_begin(); iter != i->op_end(); iter++) {
    Use *u = &(*iter);
    NvmValueState nvs = state_.find(u->get()) != state_.end() 
        ? state_.at(u->get()) : DoesNotContain;
    if (nvs != DoesNotContain) {
      contains = true;
      break;
    }
  }

  if (!contains) return DoesNotContain;
  return ContainsPointer;
  // if (i->getType()->isPtrOrPtrVectorTy()) return ContainsPointer;
  // return ContainsDerivative;
}

std::shared_ptr<NvmValueDesc> NvmValueDesc::updateState(Value *val, NvmValueState vs) const {
  NvmValueDesc vd = *this;
  vd.mutateState(val, vs);
  return getShared(vd);
}

std::shared_ptr<NvmValueDesc> NvmValueDesc::speculateOnNext(
  std::shared_ptr<NvmStackFrameDesc> sf, KInstruction *pc) const 
{
  std::shared_ptr<NvmValueDesc> retDesc = getShared(*this);
  NvmValueState outState = getOutput(pc->inst);

  switch (pc->inst->getOpcode()) {
    
    // Return instructions should mutate the call location's value.
    case Instruction::Ret:
      return retDesc->updateState(sf->getCaller(), outState);
    // Control flow -- doesn't mutate values.
    case Instruction::Br:
    case Instruction::IndirectBr:
    case Instruction::Switch:
    case Instruction::Invoke:
    case Instruction::Call:
    // Compare -- not modifying 
    case Instruction::ICmp:
      return retDesc->updateState(pc->inst, DoesNotContain);

    // Value assignments
    case Instruction::PHI:
    case Instruction::Select:
    // Arithmetic / logical
    case Instruction::Add:
    case Instruction::Sub:
    case Instruction::Mul:
    case Instruction::UDiv:
    case Instruction::SDiv:
    case Instruction::URem:
    case Instruction::SRem:
    case Instruction::And:
    case Instruction::Or:
    case Instruction::Xor:
    case Instruction::Shl:
    case Instruction::LShr:
    case Instruction::AShr:
      return retDesc->updateState(pc->inst, outState);

    // Memory Instructions -- potentially modifying
    case Instruction::Alloca:
      return retDesc->updateState(pc->inst, DoesNotContain);
    case Instruction::Load:
    case Instruction::Store:
    case Instruction::GetElementPtr:
    case Instruction::Trunc:
    case Instruction::ZExt:
    case Instruction::SExt:
    case Instruction::IntToPtr:
    case Instruction::PtrToInt:
    case Instruction::BitCast:
      return retDesc->updateState(pc->inst, outState);

    // Floating point instructions...
    case Instruction::FAdd:
    case Instruction::FSub:
    case Instruction::FMul:
    case Instruction::FDiv:
    case Instruction::FRem:
    case Instruction::FPTrunc:
    case Instruction::FPExt:
    case Instruction::FPToUI:
    case Instruction::FPToSI:
    case Instruction::UIToFP:
    case Instruction::SIToFP:
    case Instruction::FCmp:
      return retDesc->updateState(pc->inst, outState);

    // ???
    case Instruction::InsertValue:
    case Instruction::ExtractValue:
    case Instruction::Fence:
    case Instruction::InsertElement:
    case Instruction::ExtractElement:
      return retDesc->updateState(pc->inst, outState);

    // Error states -- also don't mutate
    case Instruction::ShuffleVector:
    case Instruction::AtomicRMW:
    case Instruction::AtomicCmpXchg:
    case Instruction::VAArg:
    case Instruction::Unreachable:
    // Unimplemented
    default: {
      klee_error("%s is unimplemented for opcode %u!\n", 
          __PRETTY_FUNCTION__, pc->inst->getOpcode());
      return std::shared_ptr<NvmValueDesc>();
    } 
  }

  klee_error("Reached end of function!\n");
  return std::shared_ptr<NvmValueDesc>();
}

std::shared_ptr<NvmValueDesc> NvmValueDesc::staticState(llvm::Module *m) {
  NvmValueDesc desc;
  Function* mmaps[2];
  mmaps[0] = m->getFunction("mmap");
  mmaps[1] = m->getFunction("mmap64");
  for (Function *mmap : mmaps) {
    assert(mmap && "Function is null!");

    for (User *u : mmap->users()) {
      desc.nvmMmaps_.insert(u);
    }
  }

  assert(desc.nvmMmaps_.size() && "No mmap calls?");

  klee_warning("begin");
  for (Value *v : desc.nvmMmaps_) {
    errs() << *v << "\n";
  }
  klee_error("check");
}

std::string NvmValueDesc::str(void) const {
  std::stringstream s;
  s << "Number of values: " << state_.size();
  return s.str();
}

bool klee::operator==(const NvmValueDesc &lhs, const NvmValueDesc &rhs) {
  return lhs.state_ == rhs.state_;
}

/**
 * NvmInstructionDesc
 */

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
      std::shared_ptr<NvmValueDesc> new_values = values_->speculateOnNext(stackframe_, curr_);
      std::shared_ptr<NvmStackFrameDesc> ns = stackframe_->doReturn();

      NvmInstructionDesc desc(apa_, mod_, mod_->getKInstruction(ni), new_values, ns);
      ret.push_back(desc);
      return ret;

    } else if (BranchInst *bi = dyn_cast<BranchInst>(ip)) {
      for (auto *sbb : bi->successors()) {
        Instruction *ni = &(sbb->front());
        NvmInstructionDesc desc(apa_, mod_, mod_->getKInstruction(ni), values_, stackframe_);
        ret.push_back(desc);
      }
      return ret;

    } else if (SwitchInst *si = dyn_cast<SwitchInst>(ip)) {
      for (auto &c : si->cases()) {
        Instruction *ni = &(c.getCaseSuccessor()->front());
        NvmInstructionDesc desc(apa_, mod_, mod_->getKInstruction(ni), values_, stackframe_);
        ret.push_back(desc);
      }
      return ret;

    } else if (isa<UnreachableInst>(ip)) {
      return ret;
    } else {
      errs() << *ip << "\n";
      assert(false && "Assumption violated -- terminator instruction is not a return or branch!");
    }

  } else if (CallInst *ci = utils::getNestedFunctionCallInst(ip)) {
    Function *nf = ci->getCalledFunction();
    if (!nf) {
      errs() << *ci << "\n";
      klee_error("Could not get the called function!");
    } else {
      errs() << "call\n";
      errs() << *nf << "\n";
      errs() << "end call\n";
    }

    if (mod_->functionMap.find(nf) != mod_->functionMap.end()) {
      // Successor is the entry to the next function.
      Instruction *nextInst = &(nf->getEntryBlock().front());
      // Instruction *retLoc = ci->getNextNonDebugInstruction();
      Instruction *retLoc = ip->getNextNode();
      // errs() << "ret loc is " << *retLoc << "\n";
      assert(retLoc && "Assumption violated -- call instruction is the last instruction in a basic block!");

      std::shared_ptr<NvmStackFrameDesc> newStack = stackframe_->doCall(ci, retLoc);

      NvmInstructionDesc desc(apa_, mod_, mod_->getKInstruction(nextInst), values_, newStack);
      ret.push_back(desc);
    } else {
      // Instruction *ni = ip->getNextNonDebugInstruction();
      Instruction *ni = ip->getNextNode();
      std::shared_ptr<NvmValueDesc> new_values = values_->speculateOnNext(stackframe_, curr_);
      NvmInstructionDesc desc(apa_, mod_, mod_->getKInstruction(ni), new_values, stackframe_);
      ret.push_back(desc);
    }
    return ret;

  } else if (CallInst *ci = dyn_cast<CallInst>(ip)) {
    // We can't immediately find the function. So, let's see if we can find it indirectly.
    // -- Inline assembly is skipped over.
    // -- Debug calls and other intrinsics
    if (!ci->isInlineAsm() && !isa<IntrinsicInst>(ci)){
      errs() << "dooop\n";
      errs() << *ci << "\n";
      assert(ci->isIndirectCall() && "call assumption is violated!");
      // go up the use-def chain to find the function.
      // std::list<Value*> chain;
      // chain.push_back(ci->getCalledOperand());
      // Function *indirect = nullptr;

      // while(chain.size()) {
      //   Value *v = chain.front();
      //   chain.pop_front();
      //   errs() << "\t inspecting: " << *v << "\n";
      //   if ((indirect = dyn_cast<Function>(v))) {
      //     break;
      //   } else if (User *usr = dyn_cast<User>(v)) {
      //     for (auto it = usr->op_begin(); it != usr->op_end(); it++) {
      //       chain.push_back(it->get());
      //     }
      //   }
      // }

      // if (!indirect) {
      //   // Further tracking would be quite tedious
      // }
      
      /**
       * Further analysis would be tedious and potentially unwanted. Since this
       * is potentially highly runtime dependent, we will defer and increase the weight
       * of the underlying instruction. We will then resolve this at runtime.
       * 
       * TODO: At some point, we may want to check a static-only heuristic. We
       * will require some sort of alias analysis for this.
       */
      this->weight_ += 1;
      NvmInstructionDesc desc(apa_, mod_, nullptr, values_, stackframe_);
      ret.push_back(desc);
    } else {
      // Fallthrough to the default case.
    }
  }

  // Default case

  // Instruction *ni = ip->getNextNonDebugInstruction();
  Instruction *ni = ip->getNextNode();
  std::shared_ptr<NvmValueDesc> new_values = values_->speculateOnNext(stackframe_, curr_);
  NvmInstructionDesc desc(apa_, mod_, mod_->getKInstruction(ni), new_values, stackframe_);
  ret.push_back(desc);

  return ret;
}

void NvmInstructionDesc::setSuccessors() {
  // errs() << "setting successors for" << *curr_->inst << "\n";
  for (const NvmInstructionDesc &succ : constructSuccessors()) {
    std::shared_ptr<NvmInstructionDesc> sptr = getShared(succ);
    // errs() << "\t" << *sptr->curr_->inst << "\n";
    successors_.push_back(sptr);
    sptr->addPredecessor(*this);
  }

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
  {
    std::string tmp;
    llvm::raw_string_ostream rs(tmp);
    curr_->inst->print(rs);
    s << tmp;
  }
  s << "\n----****----\n";
  return s.str();
}

std::shared_ptr<NvmInstructionDesc> NvmInstructionDesc::createEntry(KModule *m, KFunction *mainFn) {
  std::shared_ptr<Andersen> anders = std::make_shared<Andersen>(*m->module);
  
  llvm::Instruction *inst = &(mainFn->function->getEntryBlock().front());
  KInstruction *start = mainFn->getKInstruction(inst);

  NvmInstructionDesc entryInst = NvmInstructionDesc(anders, m, start);
  return getShared(entryInst);
}

// NvmInstructionDesc::attach(std::shared_ptr<NvmInstructionDesc> current, 
//                            std::shared_ptr<NvmInstructionDesc> successor) {
  
//   current_->successors_.push_back(successor);
//   current_->isTerminal = false;

//   successor_->predecessors_->push_back(current);
//   successor_->isEntry = false;
// }

bool klee::operator==(const NvmInstructionDesc &lhs, const NvmInstructionDesc &rhs) {
  bool instEq = lhs.curr_ == rhs.curr_;
  bool valEq  = lhs.values_ == rhs.values_;
  bool stkEq  = lhs.stackframe_ == rhs.stackframe_;
  // bool succEq = lhs.successors_ == rhs.successors_;
  // bool predEq = lhs.predecessors_ == rhs.predecessors_;
  bool wEq    = lhs.weight_ == rhs.weight_;
  // if (lhs.curr_->inst == rhs.curr_->inst) {
    // errs() << *lhs.curr_->inst << " == " << *rhs.curr_->inst <<
    //     ": " << instEq << " " << valEq << " " << stkEq << " " << succEq
    //     << " " << predEq << " " << wEq << "\n";
    // errs() << *lhs.curr_->inst << " == " << *rhs.curr_->inst <<
    //     ": " << instEq << " " << valEq << " " << stkEq << " " << wEq << "\n";
  // }
  return instEq &&
         valEq &&
         stkEq &&
        //  succEq &&
        //  predEq &&
         wEq;
}

/**
 * NvmHeuristicInfo
 */

std::unordered_map<std::shared_ptr<NvmInstructionDesc>, uint64_t> NvmHeuristicInfo::priority;

NvmHeuristicInfo::NvmHeuristicInfo(KModule *m, KFunction *mainFn) {
  current_state = NvmInstructionDesc::createEntry(m, mainFn);
  errs() << "init: " << current_state->str() << "\n";
  computeCurrentPriority();
}

void NvmHeuristicInfo::computeCurrentPriority(void) {
  // errs() << "Looking for " << current_state->str();
  if (priority.count(current_state)) {
    // errs() << "Found!\n";
    return;
  }
  // errs() << "Not Found!\n";

  // fprintf(stderr, "pointer of current state: %p\n", current_state.get());
  // priority[current_state] = 0;
  // assert(priority.count(current_state) && "what now");

  // errs() << "loop start\n";
  // for (const auto &p : priority) {
  //   errs() << p.first->str() << "\n";
  //   errs() << "\t" << (*p.first == *current_state) << "\n";
  //   errs() << "\t- " << (p.first == current_state) << "\n";
  // }
  // errs() << "loop end\n";

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

    // Instructions can be invalid if we don't have enough information to 
    // figure out their successors statically. In this case, we just need to
    // skip them.
    if (!instDesc->isValid()) continue;

    // errs() << instDesc->str();
    // errs() << toTraverse.size() << " " << traversed.size() << "\n";

    // assert(traversed.find(instDesc) == traversed.end() && "Repeat!");
    // for (auto sptr : traversed) {
    //   assert(!(*instDesc == *sptr) && "I guess shared_ptr== doesn't do it");
    // }
    traversed.insert(instDesc);
    
    const auto &succ = instDesc->getSuccessors(traversed);
    if (succ.size()) {
      for (auto sptr : succ) toTraverse.push_back(sptr);
    } else if (instDesc->isTerminator()) {
      terminators.insert(instDesc);
    }
  }
  
  // Base state:
  for (auto sptr : terminators) {
    priority[sptr] = sptr->getWeight();
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
        if (propagated.find(pred) != propagated.end()) continue;

        uint64_t current_priority = priority[pred];
        priority[pred] = max(current_priority, pred->getWeight() + priority[sptr]);

        propagated.insert(pred);
        nextLevel.insert(pred);
      }
    }

    level = nextLevel;
  }

  // for (const auto &p : priority) {
  //   errs() << p.first->str();
  //   if (*p.first == *current_state) errs() << "+++HEY+++ " << (p.first.get() == current_state.get())  <<"\n";
  // }
  // errs() << "computeCurrentPriority done!\n";
  // assert(priority.count(current_state) && "can't find our work!");
}

void NvmHeuristicInfo::updateCurrentState(KInstruction *pc, NvmValueState state) {
  current_state = current_state->update(pc, state);
  computeCurrentPriority();
}

void NvmHeuristicInfo::stepState(KInstruction *nextPC) {
  if (current_state->isTerminator()) {
    assert(nextPC == current_state->kinst() && "assumption violated");
    return;
  }

  auto candidates = current_state->getMatchingSuccessors(nextPC);
  if (candidates.size() > 1) {
    klee_error("too many candidates!");
    return;
  } else if (!candidates.size()) {
    errs() << nextPC->inst->getFunction()->getName() << "@" << *nextPC->inst << "\n";
    errs() << "Does not match any of: \n";
    for (const auto &s : current_state->getSuccessors()) {
      errs() << s->str() << "\n";
    }

    // This can happen with function pointers, if they aren't handled carefully.
    klee_error("no candidates! we need to recalculate");
    return;
  }

  std::shared_ptr<NvmInstructionDesc> next = candidates.front();

  if (!next->isValid()) {
    // We now know what the next instruction is, thanks to the symbolic execution
    // state.
    next->setPC(nextPC);
    computeCurrentPriority();
  }

  current_state = next;
  // klee_warning("tick.\n%s", current_state->str().c_str());
}


/* vim: set tabstop=2 softtabstop=2 shiftwidth=2: */
