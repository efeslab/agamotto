//===-- ExecutionState.h ----------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_EXECUTIONSTATE_H
#define KLEE_EXECUTIONSTATE_H

#include "klee/ADT/TreeStream.h"
#include "klee/Core/Interpreter.h"
#include "klee/Expr/Constraints.h"
#include "klee/Expr/Expr.h"
#include "klee/Module/KInstIterator.h"
#include "klee/System/Time.h"

#include "AddressSpace.h"
#include "MergeHandler.h"
#include "Threading.h"

#include <map>
#include <memory>
#include <set>
#include <vector>
#include <string>

namespace klee {
class Array;
class CallPathNode;
struct Cell;
struct KFunction;
struct KInstruction;
class MemoryObject;
class PTreeNode;
struct InstructionInfo;
class Executor;

llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const MemoryMap &mm);

/// @brief ExecutionState representing a path under exploration
class ExecutionState {
#ifdef KLEE_UNITTEST
public:
#else
private:
#endif
  // copy ctor
  ExecutionState(const ExecutionState &state);

public:
  typedef std::map<thread_uid_t, Thread> threads_ty;
  typedef std::map<wlist_id_t, std::set<thread_uid_t>> wlists_ty;
  typedef Thread::stack_ty stack_ty;

private:
  // for initialization
  void setupMain(KFunction *kf);
  void setupTime();

public:
  // Execution - Control Flow specific (include multi-threading)
  // FIXME: should I make them private?
  threads_ty threads;
  wlists_ty waitingLists;
  // used to allocate new wlist_id
  wlist_id_t wlistCounter;
  // logical timestamp, each instruction takes one unit time
  uint64_t stateTime;
  threads_ty::iterator crtThreadIt;

  // Overall state of the state - Data specific

  /// @brief Address space used by this state (e.g. Global and Heap)
  AddressSpace addressSpace;

  /// @brief Constraints collected so far
  ConstraintSet constraints;

  /// Statistics and information

  /// @brief Metadata utilized and collected by solvers for this state
  mutable SolverQueryMetaData queryMetaData;

  /// @brief Costs for all queries issued for this state, in seconds
  mutable time::Span queryCost;

  /// @brief Exploration depth, i.e., number of times KLEE branched for this state
  unsigned depth;

  /// @brief History of complete path: represents branches taken to
  /// reach/create this state (both concrete and symbolic)
  TreeOStream pathOS;

  /// @brief History of symbolic path: represents symbolic branches
  /// taken to reach/create this state
  TreeOStream symPathOS;

  /// @brief Counts how many instructions were executed since the last new
  /// instruction was covered.
  unsigned instsSinceCovNew;

  /// @brief Whether a new instruction was covered in this state
  bool coveredNew;

  /// @brief Disables forking for this state. Set by user code
  bool forkDisabled;

  /// @brief Set containing which lines in which files are covered by this state
  std::map<const std::string *, std::set<unsigned> > coveredLines;

  /// @brief Pointer to the process tree of the current state
  PTreeNode *ptreeNode;

  /// @brief Ordered list of symbolics: used to generate test cases.
  //
  // FIXME: Move to a shared list structure (not critical).
  std::vector<std::pair<ref<const MemoryObject>, const Array *>> symbolics;

  /// @brief Set of used array names for this state.  Used to avoid collisions.
  std::set<std::string> arrayNames;

  // The objects handling the klee_open_merge calls this state ran through
  std::vector<ref<MergeHandler> > openMergeStack;

  // The numbers of times this state has run through Executor::stepInstruction
  std::uint64_t steppedInstructions;

  /// @brief Keep track of unwinding state while unwinding, otherwise empty
  std::unique_ptr<UnwindingInformation> unwindingInformation;

  /// @brief the global state counter
  static std::uint32_t nextID;

  /// @brief the state id
  std::uint32_t id {0};

  /// @brief Reference to executor makes it easier to create new threads
  Executor *executor_;

public:
  #ifdef KLEE_UNITTEST
  // provide this function only in the context of unittests
  ExecutionState(){}
  #endif
  // only to create the initial state
  explicit ExecutionState(Executor *executor, KFunction *kf);
  // no copy assignment, use copy constructor
  ExecutionState &operator=(const ExecutionState &) = delete;
  // no move ctor
  ExecutionState(ExecutionState &&) noexcept = delete;
  // no move assignment
  ExecutionState& operator=(ExecutionState &&) noexcept = delete;
  // dtor
  ~ExecutionState();

  ExecutionState *branch();

  void pushFrame(KInstIterator caller, KFunction *kf) { pushFrame(crtThread(), caller, kf); }
  void pushFrame(Thread &t, KInstIterator caller, KFunction *kf);
  void popFrame() { popFrame(crtThread()); }
  void popFrame(Thread &t);

  void addSymbolic(const MemoryObject *mo, const Array *array);

  void addConstraint(ref<Expr> e);

  bool merge(const ExecutionState &b);

  void dumpConstraints(llvm::raw_ostream &out) const;
  void dumpConstraints() const;
  void dumpStack(llvm::raw_ostream &out) const;
  void dumpStack() const { dumpStack(llvm::errs()); }

  std::uint32_t getID() const { return id; };
  void setID() { id = nextID++; };
  
  /* Multi-threading related function */

  Thread &createThread(thread_id_t tid, KFunction *kf);
  void terminateThread(threads_ty::iterator it);
  threads_ty::iterator nextThread(threads_ty::iterator it) {
    if (it != threads.end())
      ++it;
    if (it == threads.end())
      it = threads.begin();
    return it;
  }
  void scheduleNext(threads_ty::iterator it) {
    assert(it != threads.end());
    crtThreadIt = it;
  }
  wlist_id_t getWaitingList() { return wlistCounter++; }
  void sleepThread(wlist_id_t wlist);
  void notifyOne(wlist_id_t wlist, thread_uid_t tid);
  void notifyAll(wlist_id_t wlist);

  /* Shortcut methods */
  Thread &crtThread() { return crtThreadIt->second; }
  const Thread &crtThread() const { return crtThreadIt->second; }
  KInstIterator &pc() { return crtThread().pc; }
  const KInstIterator &pc() const { return crtThread().pc; }
  KInstIterator &prevPC() { return crtThread().prevPC; }
  const KInstIterator &prevPC() const { return crtThread().prevPC; }
  stack_ty &stack() { return crtThread().stack; }
  const stack_ty &stack() const { return crtThread().stack; }
  unsigned &incomingBBIndex() { return crtThread().incomingBBIndex; }
  unsigned incomingBBIndex() const { return crtThread().incomingBBIndex; }

};

struct ExecutionStateIDCompare {
  bool operator()(const ExecutionState *a, const ExecutionState *b) const {
    return a->getID() < b->getID();
  }
};
}

#endif /* KLEE_EXECUTIONSTATE_H */
