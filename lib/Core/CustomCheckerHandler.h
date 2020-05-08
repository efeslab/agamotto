//===-- CustomCheckerHandler.h ------  --------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
// (iangneal):
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_CUSTOMCHECKERHANDLER_H
#define KLEE_CUSTOMCHECKERHANDLER_H

#include <iterator>
#include <list>
#include <map>
#include <vector>
#include <string>
#include "AddressSpace.h"

namespace llvm {
  class Function;
}

namespace klee {
  class Executor;
  class Expr;
  class ExecutionState;
  struct KInstruction;
  template<typename T> class ref;

  class CustomChecker {
  protected:
    Executor __attribute__((unused)) &executor;
  public:
    CustomChecker(Executor &_executor);
    virtual ~CustomChecker() = 0;
    virtual void operator()(ExecutionState &state) = 0;
  };

  class CustomCheckerHandler {
  private:

    std::list<CustomChecker*> checkers;

    Executor &executor;

  public:
    /**
     * If you want to add more checkers, add it to the constructor.
     */
    CustomCheckerHandler(Executor &_executor);
    ~CustomCheckerHandler();

    void handle(ExecutionState &state);
  };
} // End klee namespace

#endif /* KLEE_CUSTOMCHECKERHANDLER_H */
