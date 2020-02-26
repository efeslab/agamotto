//===-- IsolateCallInsts.cpp - Give calls unique basic blocks -----------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Derived from LowerSwitch.cpp in LLVM, heavily modified by piotrek
// to get rid of the binary search transform, as it was creating
// multiple paths through the program (i.e., extra paths that didn't
// exist in the original program).
//
//===----------------------------------------------------------------------===//

#include "Passes.h"
#include "klee/Config/Version.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include <algorithm>
#include <assert.h>

using namespace llvm;

namespace klee {

char IsolateCallInstsPass::ID = 0;

bool IsolateCallInstsPass::runOnFunction(Function &F) {
  bool changed = false;

  for (Function::iterator I = F.begin(), E = F.end(); I != E; ) {
    BasicBlock *cur = &*I;
    I++; // Advance over block so we don't traverse new blocks

    do {
      cur = splitOnCall(cur);
    } while (cur);
  }

  return changed;
}

BasicBlock *IsolateCallInstsPass::splitOnCall(BasicBlock *BB) {
  if (BB->size() <= 1) return nullptr;

  for (BasicBlock::iterator I = BB->begin(), E = BB->end(); I != E; I++) {
    if (!isa<CallInst>(&*I) || isa<IntrinsicInst>(&*I)) continue;

    BasicBlock *callBlock = BB->splitBasicBlock(I, "callBlock");
    if (callBlock->size() <= 1) return callBlock;

    BasicBlock::iterator NI = callBlock->begin();
    NI++; // To advance beyond the call instruction.
    assert(NI != callBlock->end() && "Error in bounds check logic!");
    
    BasicBlock *postCall = callBlock->splitBasicBlock(NI, "postCall");
    return postCall;
  }

  return nullptr;
}
}
