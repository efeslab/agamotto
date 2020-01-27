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

using namespace llvm;

namespace klee {
  char NvmAnalysisPass::ID = 0;

  bool NvmAnalysisPass::runOnModule(Module &M) {

    return false;
  }
}
/* vim: set tabstop=2 softtabstop=2 shiftwidth=2: */
