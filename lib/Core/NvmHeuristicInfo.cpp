#include "NvmHeuristicInfo.h"

using namespace llvm;
using namespace std;
using namespace klee;

/**
 * StaticStorage
 */

template<class X>
static std::shared_ptr<X> StaticStorage<X>::getShared(const X &x) {
  if (objects_[x]) return objects_[x];

  objects_[x] = std::shared_ptr<X>::make_shared(x);
  return objects_[x];
}

/**
 * NvmStackFrameDesc
 */

uint64_t NvmStackFrameDesc::hash(void) const {
  uint64_t hash_value = 0;
  for (uint64_t i = 0; i < return_stack.size(); ++i) {
    ptr_val = ((uint64_t)return_stack[i].get())
    hash_value ^= (ptr_val << i) | (ptr_val >> (64llu - i)); // Rotational shift.
  }

  return hash_value;
}

std::shared_ptr<NvmStackFrameDesc> NvmStackFrameDesc::doReturn(void) const {
  NvmStackFrameDesc ns = *this;
  ns->caller_stack.pop_back();
  ns->return_stack.pop_back();
  return getShared(ns);
}

std::shared_ptr<NvmStackFrameDesc> NvmStackFrameDesc::doCall(
    Instruction *caller, Instruction *retLoc) const {
  NvmStackFrameDesc ns = *this;
  ns->caller_stack.push_back(caller);
  ns->return_stack.push_back(retLoc);
  return getShared(ns);
}

/**
 * NvmValueDesc
 */

uint64_t NvmValueDesc::hash(void) {
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

NvmValueState NvmValueDesc::getOutput(Instruction *i) {
  bool contains = false;
  for (Use *u : i->op_range()) {
    NvmValueState nvs = state_.at(u->get());
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
  gvd.mutateState(val, vs);
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
      return retDesc->mutateState(sf.getCaller(), outState);
    // Control flow -- doesn't mutate values.
    case Instruction::Br:
    case Instruction::IndirectBr:
    case Instruction::Switch:
    case Instruction::Invoke:
    case Instruction::Call:
    // Compare -- not modifying 
    case Instruction::ICmp:
      return retDesc->mutateState(pc->inst, DoesNotContain);

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
      return retDesc->mutateState(pc->inst, outState);

    // Memory Instructions -- potentially modifying
    case Instruction::Alloca:
      return retDesc->mutateState(pc->inst, DoesNotContain);
    case Instruction::Load:
    case Instruction::Store:
    case Instruction::GetElementPtr:
    case Instruction::Trunc:
    case Instruction::ZExt:
    case Instruction::SExt:
    case Instruction::IntToPtr:
    case Instruction::PtrToInt:
    case Instruction::BitCast:
      return retDesc->mutateState(pc->inst, outState);

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
      return retDesc->mutateState(pc->inst, outState);

    // ???
    case Instruction::InsertValue:
    case Instruction::ExtractValue:
    case Instruction::Fence:
    case Instruction::InsertElement:
    case Instruction::ExtractElement:
      return retDesc->mutateState(pc->inst, outState);

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

  klee_error("Reached end of function!\n";
  return std::shared_ptr<NvmValueDesc>();
}

/**
 * NvmInstructionDesc
 */

std::list<NvmInstructionDesc> NvmInstructionDesc::constructSuccessors(void) {
  std::list<NvmInstructionDesc> ret;
  // I essentially need to figure out the next instruction.
  llvm::Instruction *ip = curr_->inst;
  llvm::BasicBlock *bb = ip->getParent();

  if (ip == bb->getTerminator()) {
    // Figure out the transition to the next basic block.
    // -- Could be a return or a branch.
    if ((ReturnInst *ri = dyn_cast<ReturnInst>(ip))) {
      llvm::Instruction *ni = stackframe_->getCaller();
      std::shared_ptr<NvmValueDesc> new_values = values_->speculateOnNext(stackframe_, curr_);
      std::shared_ptr<NvmStackFrameDesc> ns = stackframe_->doReturn();

      NvmInstructionDesc desc(mod_, mod_->getKInstruction(ni), new_values, stackframe_);
      ret.push_back(desc);
    } else if ((BranchInst *bi = dyn_cast<BranchInst>(ip))) {
      for (auto &s : bi->successors()) {
        Instruction *ni = &(s->front());
        NvmStackFrameDesc desc(mod_, mod_->getKInstruction(ni), values_, stackframe_);
        ret.push_back(desc);
      }
    } else if ((SwitchInst *si = dyn_cast<SwitchInst>(ip))) {
      for (auto &c : si->cases()) {
        Instruction *ni = &(s->getCaseSuccessor()->front());
        NvmStackFrameDesc desc(mod_, mod_->getKInstruction(ni), values_, stackframe_);
        ret.push_back(desc);
      }
    } else {
      assert(retLoc && "Assumption violated -- terminator instruction is not a return or branch!");
    }

  } else if ((llvm::CallInst *ci = utils::getNestedFunctionCallInst(ip))) {
    // Successor is the entry to the next function.
    llvm::Function *nf = ci->getCalledFunction();
    llvm::Instruction *nextInst = &(nf->getEntryBlock().front());
    llvm::Instruction *retLoc = ci->getNextNonDebugInstruction();
    assert(retLoc && "Assumption violated -- call instruction is the last instruction in a basic block!");

    std::shared_ptr<NvmStackFrameDesc> newStack = stackframe_->doCall(ci, retLoc);

    NvmStackFrameDesc desc(mod_, mod_->getKInstruction(nextInst), values_, newStack);
    ret.push_back(desc);
  } else {
    llvm::Instruction *ni = ip->getNextNonDebugInstruction();
    std::shared_ptr<NvmValueDesc> new_values = values_->speculateOnNext(stackframe_, curr_);
    NvmInstructionDesc desc(mod_, mod_->getKInstruction(ni), new_values, stackframe_);
    ret.push_back(desc);
  }

  return ret;
}

void NvmInstructionDesc::setSuccessors(const std::unordered_set<std::shared_ptr<NvmInstructionDesc>> &traversed) {
  for (const NvmInstructionDesc &succ : constructSuccessors()) {
    std::shared_ptr<NvmInstructionDesc> sptr = getShared(succ);
    successors.push_back(sptr);
    sptr->addPredecessor(*this);
  }

  std::list<std::shared_ptr<NvmInstructionDesc>> trueSuccessors;
  for (auto sptr : successors) {
    if (traversed.find(sptr) == traversed.end()) {
      trueSuccessors.push_back(sptr);
    }
  }

  successors = std::move(trueSuccessors);

  isTerminal = successors.size() == 0;
}

const std::list<std::shared_ptr<NvmInstructionDesc>> 
&NvmInstructionDesc::getSuccessors(
    const std::unordered_set<std::shared_ptr<NvmInstructionDesc>> &traversed) {
  if (successors.size() == 0 && !isTerminal) {
    setSuccessors(traversed);
  }

  assert((successors.size() > 0 || isTerminal) 
          && "Error in successor calculation!");

  return successors;
}

std::list<std::shared_ptr<NvmInstructionDesc>> getMatchingSuccessors(KInstruction *nextPC) {
  std::list<std::shared_ptr<NvmInstructionDesc>> ret;

  for (std::shared_ptr<NvmInstructionDesc> p : successors) {
    if (p->curr_ == nextPC) ret.push_back(p);
  }

  return ret;
}

/**
 * NvmHeuristicInfo
 */

NvmHeuristicInfo::NvmHeuristicInfo(KModule *m, KFunction *mainFn) {
  current_state = NvmInstructionDesc::createEntry(m, mainFn);
  computePriority();
}

void NvmHeuristicInfo::computePriority(void) {
  if (priority.find(current_state) != priority.end()) return;

  std::list<std::shared_ptr<NvmInstructionDesc>> toTraverse;
  toTraverse.push_back(current_state);

  std::unordered_set<std::shared_ptr<NvmInstructionDesc>> traversed;
  std::unordered_set<std::shared_ptr<NvmInstructionDesc>> terminators;

  // Downward traversal.
  while (toTraverse.size()) {
    std::shared_ptr<NvmInstructionDesc> instDesc = toTraverse.front();
    traversed.push_back(instDesc);
    toTraverse.pop_front();

    const auto &succ = instDesc->getSuccesors(traversed);
    if (succ.size()) {
      for (auto sptr : succ) toTraverse.push_back(sptr);
    } else {
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
