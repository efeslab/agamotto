//===-- UserSearcher.h ------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_USERSEARCHER_H
#define KLEE_USERSEARCHER_H

#include "Searcher.h"

namespace klee {
  class Executor;

  // (iangneal): Exposing this so that klee's main can do a command line arg
  // callback to set this depending on what kind of NVM checks the user wants.
  extern llvm::cl::list<Searcher::CoreSearchType> CoreSearch;

  // XXX gross, should be on demand?
  bool userSearcherRequiresMD2U();

  // XXX (iangneal): also potentially gross, but Ian doesn't care too much
  bool userSearcherRequiresNvmAnalysis();

  void initializeSearchOptions();

  Searcher *constructUserSearcher(Executor &executor);
}

#endif /* KLEE_USERSEARCHER_H */
/*
 * vim: set tabstop=2 softtabstop=2 shiftwidth=2:
 * vim: set filetype=cpp:
 */
