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
  
  // static shared_map_t objects_;

  if (objects_[x]) return objects_[x];
  
  objects_[x] = std::make_shared<X>(x);
  return objects_[x];
  // return std::make_shared<X>(x);
  // if (!rootObject_) {
  //   rootObject_ = std::make_shared<X>(x);
  //   rootObject_->objects_ = std::make_shared<shared_map_t>();
  //   rootObject_->objects()[x] = rootObject_;
  //   return rootObject_;
  // }

  // if (rootObject_->objects()[x]) return rootObject_->objects()[x];
  
  // rootObject_->objects()[x] = std::make_shared<X>(x);
  // return rootObject_->objects()[x];
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
  if (i->getType()->isPtrOrPtrVectorTy()) return ContainsPointer;
  return ContainsDerivative;
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
  Instruction *ip = curr_->inst;
  BasicBlock *bb = ip->getParent();

  if (isa<ReturnInst>(ip)) {
    errs() << "return " << (ip == bb->getTerminator() ? "is terminator\n" : "is not terminator\n");
  }

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

      NvmInstructionDesc desc(mod_, mod_->getKInstruction(ni), new_values, ns);
      ret.push_back(desc);
    } else if (BranchInst *bi = dyn_cast<BranchInst>(ip)) {
      for (auto *sbb : bi->successors()) {
        Instruction *ni = &(sbb->front());
        NvmInstructionDesc desc(mod_, mod_->getKInstruction(ni), values_, stackframe_);
        ret.push_back(desc);
      }
    } else if (SwitchInst *si = dyn_cast<SwitchInst>(ip)) {
      for (auto &c : si->cases()) {
        Instruction *ni = &(c.getCaseSuccessor()->front());
        NvmInstructionDesc desc(mod_, mod_->getKInstruction(ni), values_, stackframe_);
        ret.push_back(desc);
      }
    } else if (isa<UnreachableInst>(ip)) {
      return ret;
    } else {
      errs() << *ip << "\n";
      assert(false && "Assumption violated -- terminator instruction is not a return or branch!");
    }

  } else if (CallInst *ci = utils::getNestedFunctionCallInst(ip)) {
    Function *nf = ci->getCalledFunction();
    if (mod_->functionMap.find(nf) != mod_->functionMap.end()) {
      // Successor is the entry to the next function.
      Instruction *nextInst = &(nf->getEntryBlock().front());
      // Instruction *retLoc = ci->getNextNonDebugInstruction();
      Instruction *retLoc = ip->getNextNode();
      errs() << "ret loc is " << *retLoc << "\n";
      assert(retLoc && "Assumption violated -- call instruction is the last instruction in a basic block!");

      std::shared_ptr<NvmStackFrameDesc> newStack = stackframe_->doCall(ci, retLoc);

      NvmInstructionDesc desc(mod_, mod_->getKInstruction(nextInst), values_, newStack);
      ret.push_back(desc);
    } else {
      // Instruction *ni = ip->getNextNonDebugInstruction();
      Instruction *ni = ip->getNextNode();
      std::shared_ptr<NvmValueDesc> new_values = values_->speculateOnNext(stackframe_, curr_);
      NvmInstructionDesc desc(mod_, mod_->getKInstruction(ni), new_values, stackframe_);
      ret.push_back(desc);
    }
    
  } else {
    // Instruction *ni = ip->getNextNonDebugInstruction();
    Instruction *ni = ip->getNextNode();
    std::shared_ptr<NvmValueDesc> new_values = values_->speculateOnNext(stackframe_, curr_);
    NvmInstructionDesc desc(mod_, mod_->getKInstruction(ni), new_values, stackframe_);
    ret.push_back(desc);
  }

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

  errs() << "Number of successors: " << successors_.size() << 
    (isTerminal ? " isTerminal" : " not terminal") <<"\n";

  for (std::shared_ptr<NvmInstructionDesc> p : successors_) {
    if (p->curr_ == nextPC) ret.push_back(p);
    else {
      errs() << *p->curr_->inst << " != " << *nextPC->inst << "\n";
    }
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

bool klee::operator==(const NvmInstructionDesc &lhs, const NvmInstructionDesc &rhs) {
  bool instEq = lhs.curr_ == rhs.curr_;
  bool valEq  = lhs.values_ == rhs.values_;
  bool stkEq  = lhs.stackframe_ == rhs.stackframe_;
  bool succEq = lhs.successors_ == rhs.successors_;
  bool predEq = lhs.predecessors_ == rhs.predecessors_;
  bool wEq    = lhs.weight_ == rhs.weight_;
  // if (lhs.curr_->inst == rhs.curr_->inst) {
  //   errs() << *lhs.curr_->inst << " == " << *rhs.curr_->inst <<
  //       ": " << instEq << " " << valEq << " " << stkEq << " " << succEq
  //       << " " << predEq << " " << wEq << "\n";
  // }
  return instEq &&
         valEq &&
         stkEq &&
         succEq &&
         predEq &&
         wEq;
}

/**
 * NvmHeuristicInfo
 */

std::unordered_map<std::shared_ptr<NvmInstructionDesc>, uint64_t> NvmHeuristicInfo::priority;

NvmHeuristicInfo::NvmHeuristicInfo(KModule *m, KFunction *mainFn) {
  current_state = NvmInstructionDesc::createEntry(m, mainFn);
  computeCurrentPriority();
}

void NvmHeuristicInfo::computeCurrentPriority(void) {
  if (priority.find(current_state) != priority.end()) return;

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

    errs() << instDesc->str();
    errs() << toTraverse.size() << " " << traversed.size() << "\n";

    assert(traversed.find(instDesc) == traversed.end() && "Repeat!");
    for (auto sptr : traversed) {
      assert(!(*instDesc == *sptr) && "I guess shared_ptr== doesn't do it");
    }
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

  while (level.size()) {
    std::unordered_set<std::shared_ptr<NvmInstructionDesc>> nextLevel;

    for (auto sptr : level) {
      for (auto pred : sptr->getPredecessors()) {
        uint64_t current_priority = priority[pred];
        priority[pred] = max(current_priority, pred->getWeight() + priority[sptr]);

        nextLevel.insert(pred);
      }
    }

    level = std::move(nextLevel);
  }
}

void NvmHeuristicInfo::updateCurrentState(KInstruction *pc, NvmValueState state) {
  current_state = current_state->update(pc, state);
  computeCurrentPriority();
}

void NvmHeuristicInfo::stepState(KInstruction *nextPC) {
  auto candidates = current_state->getMatchingSuccessors(nextPC);
  if (candidates.size() > 1) {
    klee_error("too many candidates!");
    return;
  }
  if (!candidates.size()) {
    klee_error("no candidates!");
    return;
  }

  current_state = candidates.front();
}


/* vim: set tabstop=2 softtabstop=2 shiftwidth=2: */
