#include "NvmFunctionInfo.h"

using namespace llvm;
using namespace std;
using namespace klee;

/**
 * StaticStorage
 */

template<class X>
static std::shared_ptr<X> StaticStorage::getShared(const X &x) {
  if (objects_[x]) return objects_[x];

  objects_[x] = std::shared_ptr<X>::make_shared(x);
  return objects_[x];
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

std::shared_ptr<NvmValueDesc> NvmValueDesc::changeState(Value *val, NvmValueState vs) const {
  NvmValueDesc vd = *this;
  gvd.mutateState(val, vs);
  return getShared(vd);
}

std::shared_ptr<NvmValueDesc> NvmValueDesc::changeState(KInstruction *pc) const {
  klee_error("%s is unimplemented!\n", __PRETTY_FUNCTION__);
  return std::shared_ptr<NvmValueDesc>();
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
      std::shared_ptr<>
    } else if ((BranchInst *bi = dyn_cast<BranchInst>(ip))) {

    } else {
      assert(retLoc && "Assumption violated -- terminator instruction is not a return or branch!");
    }

  } else if ((llvm::CallInst *ci = utils::getNestedFunctionCallInst(ip))) {
    // Successor is the entry to the next function.
    llvm::Function *nf = ci->getCalledFunction();
    llvm::Instruction *nextInst = &(nf->getEntryBlock().front());
    llvm::Instruction *retLoc = ci->getNextNonDebugInstruction();
    assert(retLoc && "Assumption violated -- call instruction is the last instruction in a basic block!");

    std::shared_ptr<NvmStackFrameDesc> newStack = stackframe_->doCall(retLoc);

    NvmStackFrameDesc desc(mod_, mod_->getKInstruction(nextInst), values_, newStack);
    ret.push_back(desc);
  } else {
    llvm::Instruction *ni = ip->getNextNonDebugInstruction();
    NvmInstructionDesc desc(mod_, mod_->getKInstruction(ni), values_, stackframe_);
    ret.push_back(desc);
  }

  return ret;
}

/**
 * NvmHeuristicInfo
 */


/* vim: set tabstop=2 softtabstop=2 shiftwidth=2: */
