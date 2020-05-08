//===-- RaiseAsm.cpp ------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Passes.h"
#include "klee/Config/Version.h"
#include "klee/Internal/Support/ErrorHandling.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Instructions.h"

#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#if LLVM_VERSION_CODE >= LLVM_VERSION(6, 0)
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Target/TargetMachine.h"
#else
#include "llvm/Target/TargetLowering.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetSubtargetInfo.h"
#endif

using namespace llvm;
using namespace klee;

char RaiseAsmPass::ID = 0;

Function *RaiseAsmPass::getIntrinsic(llvm::Module &M, unsigned IID, Type **Tys,
                                     unsigned NumTys) {
  return Intrinsic::getDeclaration(&M, (llvm::Intrinsic::ID) IID,
                                   llvm::ArrayRef<llvm::Type*>(Tys, NumTys));
}

// Helper to match a string separated by whitespace.
static bool matchAsm(StringRef S, ArrayRef<const char *> Pieces) {
  S = S.substr(S.find_first_not_of(" \t")); // Skip leading whitespace.

  for (StringRef Piece : Pieces) {
    if (!S.startswith(Piece)) // Check if the piece matches.
      return false;

    S = S.substr(Piece.size());
    StringRef::size_type Pos = S.find_first_not_of(" \t,");
    if (Pos == 0) // We matched a prefix.
      return false;

    S = S.substr(Pos);
  }

  return S.empty();
}

static bool matchAsm(const SmallVectorImpl<StringRef> &AsmPieces,
                     ArrayRef<ArrayRef<const char *> > Pieces) {
  if (AsmPieces.size() != Pieces.size())
    return false;

  for (size_t i = 0; i < AsmPieces.size(); ++i) {
    if (!matchAsm(AsmPieces[i], Pieces[i]))
      return false;
  }

  return true;
}

// NOTE: if you you add to here, also modify IntrinsicCleaner and main.cpp.
static bool X86RaiseToIntrinsic(Module &M, CallInst *I, InlineAsm *IA,
                                const SmallVectorImpl<StringRef> &AsmPieces) {
  if (AsmPieces.size() == 0)
    return false;

  Function *intrinsicFunction = nullptr;

  // Persistent memory related asm intrinsics
  if (matchAsm(AsmPieces, {{"sfence"}})) {
    intrinsicFunction = Intrinsic::getDeclaration(&M, Intrinsic::x86_sse_sfence);
  } else if (matchAsm(AsmPieces, {{"mfence"}})) {
    intrinsicFunction = Intrinsic::getDeclaration(&M, Intrinsic::x86_sse2_mfence);
  } else if (matchAsm(AsmPieces, {{".byte", "0x66"}, {"clflush", "$0"}})) {
    intrinsicFunction = Intrinsic::getDeclaration(&M, Intrinsic::x86_clflushopt);
  } else if (matchAsm(AsmPieces, {{".byte", "0x66"}, {"xsaveopt", "$0"}})) {
    intrinsicFunction = Intrinsic::getDeclaration(&M, Intrinsic::x86_clwb);
  }

  // Safe to ignore. Raise them up to be ignored by IntrinsicCleaner.
  else if (matchAsm(AsmPieces, {{"pause"}})) {
    intrinsicFunction = Intrinsic::getDeclaration(&M, Intrinsic::x86_sse2_pause);
  } else if (AsmPieces.size() == 1 && AsmPieces[0].startswith("prefetch")) {
    intrinsicFunction = Intrinsic::getDeclaration(&M, Intrinsic::prefetch);
  }

  if (intrinsicFunction) {
    IRBuilder<> Builder(I);
    std::vector<llvm::Value*> Args;
    for (size_t i = 0; i < I->getNumArgOperands(); ++i)
      Args.push_back(I->getArgOperand(i));
    Builder.CreateCall(intrinsicFunction, Args);
    I->eraseFromParent();
    return true;
  }

  return false;
}

/**
 * Example:
 *
 * asm volatile ("xchgq %0,%1"
 *       :"=r" ((uint64_t)val)
 *       :"m" (*(volatile uint64_t *)target), "0" ((uint64_t) x)
 *       :"memory");
 *
 * %val = call i64 asm sideeffect "xchgq $0,$1",
 *          "=r,=*m,0,~{memory},~{dirflag},~{fpsr},~{flags}" (i64* %target, i64 %val)
 */
static bool X86RaiseToAtomicRMWInst(Module &M, CallInst *I, InlineAsm *IA,
                                    const SmallVectorImpl<StringRef> &AsmPieces) {
  if (AsmPieces.size() != 1)
    return false;

  SmallVector<StringRef, 3> Pieces;
  SplitString(AsmPieces[0], Pieces, " \t,");

  // Pieces = {"xchgq", "$0", "$1"}
  if (Pieces.size() == 3 &&
      Pieces[0].startswith("xchg") &&
      Pieces[1].startswith("$") &&
      Pieces[2].startswith("$")) {
    // Collect the inputs from the call instruction
    std::vector<Value*> inputs;
    for (size_t i = 0; i < I->getNumArgOperands(); ++i)
      inputs.push_back(I->getArgOperand(i));

    // Go through the inline asm constraints. The output(s) come first, then
    // the inputs, then the clobbers.
    InlineAsm::ConstraintInfoVector constraints = IA->ParseConstraints();

    // The first one should always be the return value.
    size_t i = 1;

    // There may be more outputs (they should all be indirect outputs)
    while (constraints[i].Type == InlineAsm::isOutput) {
      // If it's not indirect, then what the heck is it? We only expect one output.
      if (!constraints[i].isIndirect) {
        errs() << "More than one output constraint in inline xchg assembly\n";
        return false;
      }

      // Consume an input argument
      inputs.erase(inputs.begin());

      ++i;
    }

    // There should be two inputs, and then clobbers
    if (constraints[i].Type != InlineAsm::isInput ||
        constraints[i + 1].Type != InlineAsm::isInput ||
        constraints[i + 2].Type != InlineAsm::isClobber) {
      errs() << "Did not find exactly 2 inputs for inline xchg assembly\n";
      return false;
    }

    // Okay, we have two inputs. One should be a pointer, one should be a value.
    assert(inputs.size() == 2);
    Value *ptrInput, *valInput;
    if (inputs[0]->getType()->isPointerTy()) {
      ptrInput = inputs[0];
      valInput = inputs[1];
    } else {
      ptrInput = inputs[1];
      valInput = inputs[0];
    }

    if (!ptrInput->getType()->isPointerTy()) {
      errs() << "Two non-pointer types given to inline xchg assembly\n";
      return false;
    }
    if (valInput->getType()->isPointerTy()) {
      errs() << "Two pointer types given to inline xchg assembly\n";
      return false;
    }

    // Finally, create the atomicrmw instruction!
    AtomicRMWInst *rmw = new AtomicRMWInst(AtomicRMWInst::Xchg,
                                           ptrInput, valInput,
                                           AtomicOrdering::SequentiallyConsistent,
                                           SyncScope::System);
    ReplaceInstWithInst(I, rmw);
    return true;
  }

  return false;
}

// FIXME: This should just be implemented as a patch to
// X86TargetAsmInfo.cpp, then everyone will benefit.
bool RaiseAsmPass::runOnInstruction(Module &M, Instruction *I) {
  // We can just raise inline assembler using calls
  CallInst *ci = dyn_cast<CallInst>(I);
  if (!ci)
    return false;

  InlineAsm *ia = dyn_cast<InlineAsm>(ci->getCalledValue());
  if (!ia)
    return false;

  // Try to use existing infrastructure
  if (!TLI)
    return false;

  if (TLI->ExpandInlineAsm(ci))
    return true;

  if (triple.getArch() == llvm::Triple::x86_64) {
    // Some intrinsics get coded as inline assembly in case compilers
    // don't support them, or if programmers are lazy.

    const std::string &AsmStr = ia->getAsmString();

    SmallVector<StringRef, 4> AsmPieces;
    SplitString(AsmStr, AsmPieces, ";\n");

    // Try to raise it up as a known intrinsic.
    if (X86RaiseToIntrinsic(M, ci, ia, AsmPieces))
      return true;

    // Try to raise it up as an atomicrmw instruction
    if (X86RaiseToAtomicRMWInst(M, ci, ia, AsmPieces))
      return true;
  }

  if (triple.getArch() == llvm::Triple::x86_64 &&
      (triple.getOS() == llvm::Triple::Linux ||
       triple.getOS() == llvm::Triple::Darwin ||
       triple.getOS() == llvm::Triple::FreeBSD)) {

    if (ia->getAsmString() == "" && ia->hasSideEffects()) {
      IRBuilder<> Builder(I);
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 9)
      Builder.CreateFence(llvm::AtomicOrdering::SequentiallyConsistent);
#else
      Builder.CreateFence(llvm::SequentiallyConsistent);
#endif
      I->eraseFromParent();
      return true;
    }
  }

  return false;
}

bool RaiseAsmPass::runOnModule(Module &M) {
  bool changed = false;

  std::string Err;
  std::string HostTriple = llvm::sys::getDefaultTargetTriple();
  const Target *NativeTarget = TargetRegistry::lookupTarget(HostTriple, Err);

  TargetMachine * TM = 0;
  if (NativeTarget == 0) {
    klee_warning("Warning: unable to select native target: %s", Err.c_str());
    TLI = 0;
  } else {
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 9)
    TM = NativeTarget->createTargetMachine(HostTriple, "", "", TargetOptions(),
        None);
    TLI = TM->getSubtargetImpl(*(M.begin()))->getTargetLowering();
#else
    TM = NativeTarget->createTargetMachine(HostTriple, "", "", TargetOptions());
    TLI = TM->getSubtargetImpl(*(M.begin()))->getTargetLowering();
#endif

    triple = llvm::Triple(HostTriple);
  }
  
  for (Module::iterator fi = M.begin(), fe = M.end(); fi != fe; ++fi) {
    for (Function::iterator bi = fi->begin(), be = fi->end(); bi != be; ++bi) {
      for (BasicBlock::iterator ii = bi->begin(), ie = bi->end(); ii != ie;) {
        Instruction *i = &*ii;
        ++ii;  
        changed |= runOnInstruction(M, i);
      }
    }
  }

  delete TM;

  return changed;
}
