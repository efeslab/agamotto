//===-- NvmAnalysisPass.cpp -------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

/**
 * (iangneal): Authored by Ian Neal
 *
 */

#include "Passes.h"
#include "klee/Config/Version.h"
#include "klee/Internal/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

#include <unordered_set>

using namespace llvm;
using namespace std;

namespace klee {
  char NvmAnalysisPass::ID = 0;

  NvmAnalysisPass::NvmAnalysisPass() : llvm::ModulePass(ID), nfi_(this) {}

  bool NvmAnalysisPass::runOnModule(Module &M) {
    // Create the function call description for the entry.
    const Function *main = M.getFunction("main");
    unordered_set<unsigned> empty_args;
    NvmFunctionCallDesc desc(main, empty_args);

    // Initialize all functions called from main.
    (void)nfi_.get(desc);

    return false;
  }
}
/* vim: set tabstop=2 softtabstop=2 shiftwidth=2: */
