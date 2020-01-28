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

namespace klee {
  class Executor;
  class Searcher;

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
