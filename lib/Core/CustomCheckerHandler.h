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
  class CustomCheckerHandler;
  class Executor;
  class Expr;
  class ExecutionState;
  struct KInstruction;
  typedef std::pair<const MemoryObject*, const ObjectState*> ObjectPair;
  typedef std::vector<ObjectPair> ResolutionList;  
  template<typename T> class ref;

  class CustomChecker {
  protected:
    CustomCheckerHandler *handler;
    Executor __attribute__((unused)) &executor;

    bool isMemoryOperation(KInstruction *ki);
    ResolutionList &getResolutionList(void);
    ref<Expr> &getAddress(void);
    llvm::Module *getModule(void);
    ref<Expr> getOpValue(ExecutionState &state, int opNum);

    ObjectPair &&resolveAddress(ExecutionState &state, ref<Expr> addr);

  public:
    CustomChecker(CustomCheckerHandler *_handler, Executor &_executor);
    virtual ~CustomChecker() = 0;
    /**
     * How the checker is invoked after every instruction.
     */
    virtual void operator()(ExecutionState &state) = 0;
  };

  class CustomCheckerHandler {
  private:
    friend class CustomChecker;

    std::list<CustomChecker*> checkers;

    Executor &executor;

    ref<Expr> addr;
    ResolutionList currentResList;

  public:
    /**
     * If you want to add more checkers, add it to the constructor.
     */
    CustomCheckerHandler(Executor &_executor);
    ~CustomCheckerHandler();

    /**
     * Runs the handling for all of the checker handlers in the order that they
     * were installed into the custom checker handler.
     */
    void handle(ExecutionState &state);

    /**
     * Give the custom checkers information about the most recent memory 
     * operation. This is an efficiency mechanism --- there's nothing wrong
     * with the custom checkers making there own memory resolution calls, but
     * it's expensive. Also, reduces the amount of code needed.
     */
    void setMemoryResolution(ref<Expr> address, ResolutionList &&rl);
  };
} // End klee namespace

#endif /* KLEE_CUSTOMCHECKERHANDLER_H */
