//===-- ExecutionState.cpp ------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Memory.h"

#include "klee/ExecutionState.h"

#include "klee/Expr/Expr.h"
#include "klee/Internal/Module/Cell.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/Support/ErrorHandling.h"
#include "klee/OptionCategories.h"
#include "klee/Interpreter.h"

#include "llvm/IR/Function.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include "../Core/UserSearcher.h"
#include "Executor.h"
#include "RootCause.h"

#include <cassert>
#include <ctime>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdarg.h>

using namespace llvm;
using namespace klee;

namespace {
cl::opt<bool> DebugLogStateMerge(
    "debug-log-state-merge", cl::init(false),
    cl::desc("Debug information for underlying state merging (default=false)"),
    cl::cat(MergeCat));
}

/***/

void ExecutionState::setupMain(KFunction *kf) {
  // single process, make its id always be 0
  // the first thread, set its id to be 1
  Thread mainThread = Thread(1, 0, executor_, kf);
  threads.insert(std::make_pair(mainThread.tuid, mainThread));
  crtThreadIt = threads.begin();
}

void ExecutionState::setupTime() {
  stateTime = (long long)std::time(0) * 1000000L;
}

ExecutionState::ExecutionState(Executor *executor, KFunction *kf) :
    wlistCounter(1),
    rootCauseMgr(executor->rootCauseMgr),
    depth(0),
    instsSinceCovNew(0),
    coveredNew(false),
    forkDisabled(false),
    ptreeNode(0),
    steppedInstructions(0),
    executor_(executor) {
  setupMain(kf);
  setupTime();
}

ExecutionState::ExecutionState(const std::vector<ref<Expr> > &assumptions)
  : wlistCounter(1), 
    constraints(assumptions),
    ptreeNode(0) {}

ExecutionState::~ExecutionState() {
  for (threads_ty::value_type &tit: threads) {
    Thread &t = tit.second;
    while (!t.stack.empty()) popFrame(t);
  }

  for (unsigned int i=0; i<symbolics.size(); i++)
  {
    const MemoryObject *mo = symbolics[i].first;
    assert(mo->refCount > 0);
    mo->refCount--;
    if (mo->refCount == 0)
      delete mo;
  }

  for (auto cur_mergehandler: openMergeStack){
    cur_mergehandler->removeOpenState(this);
  }
}

ExecutionState::ExecutionState(const ExecutionState& state):
    threads(state.threads),
    waitingLists(state.waitingLists),
    wlistCounter(state.wlistCounter),
    stateTime(state.stateTime),

    rootCauseMgr(state.rootCauseMgr),

    addressSpace(state.addressSpace),
    constraints(state.constraints),

    depth(state.depth),

    pathOS(state.pathOS),
    symPathOS(state.symPathOS),

    instsSinceCovNew(state.instsSinceCovNew),
    coveredNew(state.coveredNew),
    forkDisabled(state.forkDisabled),
    coveredLines(state.coveredLines),
    ptreeNode(state.ptreeNode),
    symbolics(state.symbolics),
    persistentObjects(state.persistentObjects),
    arrayNames(state.arrayNames),
    openMergeStack(state.openMergeStack),
    steppedInstructions(state.steppedInstructions),
    executor_(state.executor_)
{
  for (unsigned int i=0; i<symbolics.size(); i++)
    symbolics[i].first->refCount++;

  for (auto cur_mergehandler: openMergeStack)
    cur_mergehandler->addOpenState(this);
  crtThreadIt = threads.find(state.crtThreadIt->first);
}

ExecutionState *ExecutionState::branch() {
  depth++;

  ExecutionState *falseState = new ExecutionState(*this);
  falseState->coveredNew = false;
  falseState->coveredLines.clear();

  return falseState;
}

void ExecutionState::addSymbolic(const MemoryObject *mo, const Array *array) {
  mo->refCount++;
  symbolics.push_back(std::make_pair(mo, array));
}

/**/

llvm::raw_ostream &klee::operator<<(llvm::raw_ostream &os, const MemoryMap &mm) {
  os << "{";
  MemoryMap::iterator it = mm.begin();
  MemoryMap::iterator ie = mm.end();
  if (it!=ie) {
    os << "MO" << it->first->id << ":" << it->second;
    for (++it; it!=ie; ++it)
      os << ", MO" << it->first->id << ":" << it->second;
  }
  os << "}";
  return os;
}

// FIXME: incomplete multithreading support
bool ExecutionState::merge(const ExecutionState &b) {
  if (DebugLogStateMerge)
    llvm::errs() << "-- attempting merge of A:" << this << " with B:" << &b
                 << "--\n";
  if (pc() != b.pc())
    return false;

  // XXX is it even possible for these to differ? does it matter? probably
  // implies difference in object states?
  if (symbolics!=b.symbolics)
    return false;

  {
    std::vector<StackFrame>::const_iterator itA = stack().begin();
    std::vector<StackFrame>::const_iterator itB = b.stack().begin();
    while (itA!=stack().end() && itB!=b.stack().end()) {
      // XXX vaargs?
      if (itA->caller!=itB->caller || itA->kf!=itB->kf)
        return false;
      ++itA;
      ++itB;
    }
    if (itA!=stack().end() || itB!=b.stack().end())
      return false;
  }

  std::set< ref<Expr> > aConstraints(constraints.begin(), constraints.end());
  std::set< ref<Expr> > bConstraints(b.constraints.begin(), 
                                     b.constraints.end());
  std::set< ref<Expr> > commonConstraints, aSuffix, bSuffix;
  std::set_intersection(aConstraints.begin(), aConstraints.end(),
                        bConstraints.begin(), bConstraints.end(),
                        std::inserter(commonConstraints, commonConstraints.begin()));
  std::set_difference(aConstraints.begin(), aConstraints.end(),
                      commonConstraints.begin(), commonConstraints.end(),
                      std::inserter(aSuffix, aSuffix.end()));
  std::set_difference(bConstraints.begin(), bConstraints.end(),
                      commonConstraints.begin(), commonConstraints.end(),
                      std::inserter(bSuffix, bSuffix.end()));
  if (DebugLogStateMerge) {
    llvm::errs() << "\tconstraint prefix: [";
    for (std::set<ref<Expr> >::iterator it = commonConstraints.begin(),
                                        ie = commonConstraints.end();
         it != ie; ++it)
      llvm::errs() << *it << ", ";
    llvm::errs() << "]\n";
    llvm::errs() << "\tA suffix: [";
    for (std::set<ref<Expr> >::iterator it = aSuffix.begin(),
                                        ie = aSuffix.end();
         it != ie; ++it)
      llvm::errs() << *it << ", ";
    llvm::errs() << "]\n";
    llvm::errs() << "\tB suffix: [";
    for (std::set<ref<Expr> >::iterator it = bSuffix.begin(),
                                        ie = bSuffix.end();
         it != ie; ++it)
      llvm::errs() << *it << ", ";
    llvm::errs() << "]\n";
  }

  // We cannot merge if addresses would resolve differently in the
  // states. This means:
  // 
  // 1. Any objects created since the branch in either object must
  // have been free'd.
  //
  // 2. We cannot have free'd any pre-existing object in one state
  // and not the other

  if (DebugLogStateMerge) {
    llvm::errs() << "\tchecking object states\n";
    llvm::errs() << "A: " << addressSpace.objects << "\n";
    llvm::errs() << "B: " << b.addressSpace.objects << "\n";
  }
    
  std::set<const MemoryObject*> mutated;
  MemoryMap::iterator ai = addressSpace.objects.begin();
  MemoryMap::iterator bi = b.addressSpace.objects.begin();
  MemoryMap::iterator ae = addressSpace.objects.end();
  MemoryMap::iterator be = b.addressSpace.objects.end();
  for (; ai!=ae && bi!=be; ++ai, ++bi) {
    if (ai->first != bi->first) {
      if (DebugLogStateMerge) {
        if (ai->first < bi->first) {
          llvm::errs() << "\t\tB misses binding for: " << ai->first->id << "\n";
        } else {
          llvm::errs() << "\t\tA misses binding for: " << bi->first->id << "\n";
        }
      }
      return false;
    }
    if (ai->second != bi->second) {
      if (DebugLogStateMerge)
        llvm::errs() << "\t\tmutated: " << ai->first->id << "\n";
      mutated.insert(ai->first);
    }
  }
  if (ai!=ae || bi!=be) {
    if (DebugLogStateMerge)
      llvm::errs() << "\t\tmappings differ\n";
    return false;
  }
  
  // merge stack

  ref<Expr> inA = ConstantExpr::alloc(1, Expr::Bool);
  ref<Expr> inB = ConstantExpr::alloc(1, Expr::Bool);
  for (std::set< ref<Expr> >::iterator it = aSuffix.begin(), 
         ie = aSuffix.end(); it != ie; ++it)
    inA = AndExpr::create(inA, *it);
  for (std::set< ref<Expr> >::iterator it = bSuffix.begin(), 
         ie = bSuffix.end(); it != ie; ++it)
    inB = AndExpr::create(inB, *it);

  // XXX should we have a preference as to which predicate to use?
  // it seems like it can make a difference, even though logically
  // they must contradict each other and so inA => !inB

  std::vector<StackFrame>::iterator itA = stack().begin();
  std::vector<StackFrame>::const_iterator itB = b.stack().begin();
  for (; itA!=stack().end(); ++itA, ++itB) {
    StackFrame &af = *itA;
    const StackFrame &bf = *itB;
    for (unsigned i=0; i<af.kf->numRegisters; i++) {
      ref<Expr> &av = af.locals[i].value;
      const ref<Expr> &bv = bf.locals[i].value;
      if (av.isNull() || bv.isNull()) {
        // if one is null then by implication (we are at same pc)
        // we cannot reuse this local, so just ignore
      } else {
        av = SelectExpr::create(inA, av, bv);
      }
    }
  }

  for (std::set<const MemoryObject*>::iterator it = mutated.begin(), 
         ie = mutated.end(); it != ie; ++it) {
    const MemoryObject *mo = *it;
    const ObjectState *os = addressSpace.findObject(mo);
    const ObjectState *otherOS = b.addressSpace.findObject(mo);
    assert(os && !os->readOnly && 
           "objects mutated but not writable in merging state");
    assert(otherOS);

    ObjectState *wos = addressSpace.getWriteable(mo, os);
    for (unsigned i=0; i<mo->size; i++) {
      ref<Expr> av = wos->read8(i);
      ref<Expr> bv = otherOS->read8(i);
      wos->write(*this, i, SelectExpr::create(inA, av, bv));
    }
  }

  constraints = ConstraintManager();
  for (std::set< ref<Expr> >::iterator it = commonConstraints.begin(), 
         ie = commonConstraints.end(); it != ie; ++it)
    constraints.addConstraint(*it);
  constraints.addConstraint(OrExpr::create(inA, inB));

  return true;
}
void ExecutionState::pushFrame(Thread &t, KInstIterator caller, KFunction *kf) {
  t.stack.push_back(StackFrame(caller,kf));
  ++kf->frequency;
}

void ExecutionState::popFrame(Thread &t) {
  StackFrame &sf = t.stack.back();
  for (std::vector<const MemoryObject*>::iterator it = sf.allocas.begin(), 
         ie = sf.allocas.end(); it != ie; ++it) {
    const MemoryObject *mo = *it;
    const ObjectState *os = addressSpace.findObject(mo);
    assert(os && "trying to unbind null!");
    const PersistentState *ps = dyn_cast<PersistentState>(os);
    if (ps) {
      auto rootCauses = executor_->markPersistenceErrors(*this, mo, ps);
      if (rootCauses.size()) {
        klee_warning("ERROR: alloca pmem error");
      }
      persistentObjects.erase(mo);
    }
    addressSpace.unbindObject(mo);
  }
  
  t.stack.pop_back();
}

/* Multithreading related function  */
Thread &ExecutionState::createThread(thread_id_t tid, KFunction *kf) {
  // we currently assume there is only one process and its id is 0
  Thread newThread = Thread(tid, 0, NvmHeuristicBuilder::copy(nvmInfo()), kf);
  
  std::pair<threads_ty::iterator, bool> res =
      threads.insert(std::make_pair(newThread.tuid, newThread));
  assert(res.second);
  assert(crtThread().getTid() != res.first->second.getTid());
  return res.first->second;
}

void ExecutionState::terminateThread(threads_ty::iterator thrIt) {
  klee_message("Terminating thread %lu", thrIt->first.first);
  // we assume the scheduler found a new thread first
  assert(thrIt != crtThreadIt);
  assert(!thrIt->second.enabled);
  assert(thrIt->second.waitingList == 0);
  threads.erase(thrIt);
}

void ExecutionState::sleepThread(wlist_id_t wlist) {
  assert(crtThread().enabled);
  assert(wlist > 0);
  crtThread().enabled = false;
  crtThread().waitingList = wlist;
  std::set<thread_uid_t> &wl = waitingLists[wlist];
  wl.insert(crtThread().tuid);
}

void ExecutionState::notifyOne(wlist_id_t wlist, thread_uid_t tuid) {
  assert(wlist > 0);
  std::set<thread_uid_t> &wl = waitingLists[wlist];
  if (wl.erase(tuid) != 1) {
    assert(0 && "thread was not waiting");
  }
  threads_ty::iterator find_it = threads.find(tuid);
  assert(find_it != threads.end());
  Thread &thread = find_it->second;
  assert(!thread.enabled);
  thread.enabled = true;
  thread.waitingList = 0;
  if (wl.size() == 0)
    waitingLists.erase(wlist);
}

void ExecutionState::notifyAll(wlist_id_t wlist) {
  assert(wlist > 0);
  std::set<thread_uid_t> &wl = waitingLists[wlist];
  if (wl.size() > 0) {
    for (const thread_uid_t &tuid: wl) {
      threads_ty::iterator find_it = threads.find(tuid);
      assert(find_it != threads.end());
      Thread &thread = find_it->second;
      thread.enabled = true;
      thread.waitingList = 0;
    }
    wl.clear();
  }
  waitingLists.erase(wlist);
}

/* Debugging helper */
void ExecutionState::dumpStack(llvm::raw_ostream &out) const {
  out << "Current Thread: " << crtThread().tuid.first << '\n';
  for (const threads_ty::value_type &tit : threads) {
    tit.second.dumpStack(out);
  }
}

void ExecutionState::dumpConstraints(llvm::raw_ostream &out) const {
  for (ConstraintManager::const_iterator i = constraints.begin();
      i != constraints.end(); i++) {
    out << '*';
    (*i)->print(out);
    out << '\n';
  }
}
void ExecutionState::dumpConstraints() const {
  dumpConstraints(llvm::errs());
}
