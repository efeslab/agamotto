//===-- SpecialFunctionHandler.cpp ----------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "SpecialFunctionHandler.h"

#include "Executor.h"
#include "Memory.h"
#include "MemoryManager.h"
#include "Searcher.h"
#include "TimingSolver.h"

#include "klee/ExecutionState.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/Support/Debug.h"
#include "klee/Internal/Support/ErrorHandling.h"
#include "klee/MergeHandler.h"
#include "klee/OptionCategories.h"
#include "klee/Solver/SolverCmdLine.h"

#include "llvm/ADT/Twine.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Module.h"

#include <errno.h>
#include <sys/mman.h>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <fstream>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

using namespace llvm;
using namespace klee;

namespace {
cl::opt<bool>
    ReadablePosix("readable-posix-inputs", cl::init(false),
                  cl::desc("Prefer creation of POSIX inputs (command-line "
                           "arguments, files, etc.) with human readable bytes. "
                           "Note: option is expensive when creating lots of "
                           "tests (default=false)"),
                  cl::cat(TestGenCat));

cl::opt<bool>
    SilentKleeAssume("silent-klee-assume", cl::init(false),
                     cl::desc("Silently terminate paths with an infeasible "
                              "condition given to klee_assume() rather than "
                              "emitting an error (default=false)"),
                     cl::cat(TerminationCat));

cl::opt<unsigned>
    CallocMaxSize("calloc-max-unit-size", cl::init(0),
                  cl::desc("Max size for memory objects in calloc. If the "
                           "overall size is larger, will allocate multiple, "
                           "contiguous MemoryObjects instead (default=0, "
                           "with 0 disabling this feature)."),
                  cl::cat(SolvingCat));
} // namespace

/// \todo Almost all of the demands in this file should be replaced
/// with terminateState calls.

///

// FIXME: We are more or less committed to requiring an intrinsic
// library these days. We can move some of this stuff there,
// especially things like realloc which have complicated semantics
// w.r.t. forking. Among other things this makes delayed query
// dispatch easier to implement.
static SpecialFunctionHandler::HandlerInfo handlerInfo[] = {
#define add(name, handler, ret) { name, \
                                  &SpecialFunctionHandler::handler, \
                                  false, ret, false }
// addDNR is to add function handler with attribute: noreturn, which will lead
// to "unreachable" LLVM IR
#define addDNR(name, handler) { name, \
                                &SpecialFunctionHandler::handler, \
                                true, false, false }
  addDNR("__assert_rtn", handleAssertFail),
  addDNR("__assert_fail", handleAssertFail),
  addDNR("__assert", handleAssertFail),
  addDNR("_assert", handleAssert),
  addDNR("abort", handleAbort),
  addDNR("_exit", handleExit),
  { "exit", &SpecialFunctionHandler::handleExit, true, false, true },
  addDNR("klee_abort", handleAbort),
  addDNR("klee_silent_exit", handleSilentExit),
  addDNR("klee_report_error", handleReportError),
  add("calloc", handleCalloc, true),
  add("free", handleFree, false),
  add("klee_assume", handleAssume, false),
  add("klee_check_memory_access", handleCheckMemoryAccess, false),
  add("klee_get_valuef", handleGetValue, true),
  add("klee_get_valued", handleGetValue, true),
  add("klee_get_valuel", handleGetValue, true),
  add("klee_get_valuell", handleGetValue, true),
  add("klee_get_value_i32", handleGetValue, true),
  add("klee_get_value_i64", handleGetValue, true),
  add("klee_define_fixed_object", handleDefineFixedObject, false),
  add("klee_get_obj_size", handleGetObjSize, true),
  add("klee_get_errno", handleGetErrno, true),
#ifndef __APPLE__
  add("__errno_location", handleErrnoLocation, true),
#else
  add("__error", handleErrnoLocation, true),
#endif
  add("klee_is_symbolic", handleIsSymbolic, true),
  add("klee_make_symbolic", handleMakeSymbolic, false),
  add("klee_mark_global", handleMarkGlobal, false),
  add("klee_open_merge", handleOpenMerge, false),
  add("klee_close_merge", handleCloseMerge, false),
  add("klee_prefer_cex", handlePreferCex, false),
  add("klee_posix_prefer_cex", handlePosixPreferCex, false),
  add("klee_print_expr", handlePrintExpr, false),
  add("klee_print_range", handlePrintRange, false),
  add("klee_set_forking", handleSetForking, false),
  add("klee_stack_trace", handleStackTrace, false),
  add("klee_warning", handleWarning, false),
  add("klee_warning_once", handleWarningOnce, false),
  add("malloc", handleMalloc, true),
  add("memalign", handleMemalign, true),
  add("realloc", handleRealloc, true),

  // operator delete[](void*)
  add("_ZdaPv", handleDeleteArray, false),
  // operator delete(void*)
  add("_ZdlPv", handleDelete, false),

  // operator new[](unsigned int)
  add("_Znaj", handleNewArray, true),
  // operator new(unsigned int)
  add("_Znwj", handleNew, true),

  // FIXME-64: This is wrong for 64-bit long...

  // operator new[](unsigned long)
  add("_Znam", handleNewArray, true),
  // operator new(unsigned long)
  add("_Znwm", handleNew, true),

  // Run clang with -fsanitize=signed-integer-overflow and/or
  // -fsanitize=unsigned-integer-overflow
  add("__ubsan_handle_add_overflow", handleAddOverflow, false),
  add("__ubsan_handle_sub_overflow", handleSubOverflow, false),
  add("__ubsan_handle_mul_overflow", handleMulOverflow, false),
  add("__ubsan_handle_divrem_overflow", handleDivRemOverflow, false),

  /* Persistent Memory Management */
  add("klee_pmem_alloc_pmem", handleAllocPmem, true),
  add("klee_pmem_mark_persistent", handleMarkPersistent, true),
  add("klee_pmem_check_persisted", handleIsPersisted, false),
  add("klee_pmem_check_ordered_before", handleIsOrderedBefore, false),
  add("klee_pmem_is_pmem", handleIsPmem, true),

  /* For the good of the MMAP! */
  add("klee_define_fixed_object_from_existing", handleDefineFixedObjectFromExisting, false),
  add("klee_init_concrete_zero", handleInitConcreteZero, false),
  add("klee_undefine_fixed_object", handleUndefineFixedObject, false),

  /* Thread Scheduling Management */
  add("klee_thread_create", handleThreadCreate, false),
  addDNR("klee_thread_terminate", handleThreadTerminate),
  add("klee_get_context", handleGetContext, false),
  add("klee_get_wlist", handleGetWList, true),
  add("klee_thread_preempt", handleThreadPreempt, false),
  add("klee_thread_sleep", handleThreadSleep, false),
  add("klee_thread_notify", handleThreadNotify, false),

  /* Process Management Placeholder */
  add("klee_process_fork", handleProcessFork, true),
  addDNR("klee_process_terminate", handleProcessTerminate),

  /* Shared Memory Placeholder */
  add("klee_make_shared", handleMakeShared, false),

  /* Misc */
  add("klee_get_time", handleGetTime, true),
  add("klee_set_time", handleSetTime, false),

#undef addDNR
#undef add
};

SpecialFunctionHandler::const_iterator SpecialFunctionHandler::begin() {
  return SpecialFunctionHandler::const_iterator(handlerInfo);
}

SpecialFunctionHandler::const_iterator SpecialFunctionHandler::end() {
  // NULL pointer is sentinel
  return SpecialFunctionHandler::const_iterator(0);
}

SpecialFunctionHandler::const_iterator& SpecialFunctionHandler::const_iterator::operator++() {
  ++index;
  if ( index >= SpecialFunctionHandler::size())
  {
    // Out of range, return .end()
    base=0; // Sentinel
    index=0;
  }

  return *this;
}

int SpecialFunctionHandler::size() {
	return sizeof(handlerInfo)/sizeof(handlerInfo[0]);
}

SpecialFunctionHandler::SpecialFunctionHandler(Executor &_executor) 
  : executor(_executor) {}

void SpecialFunctionHandler::prepare(
    std::vector<const char *> &preservedFunctions) {
  unsigned N = size();

  for (unsigned i=0; i<N; ++i) {
    HandlerInfo &hi = handlerInfo[i];
    Function *f = executor.kmodule->module->getFunction(hi.name);

    // No need to create if the function doesn't exist, since it cannot
    // be called in that case.
    if (f && (!hi.doNotOverride || f->isDeclaration())) {
      preservedFunctions.push_back(hi.name);
      // Make sure NoReturn attribute is set, for optimization and
      // coverage counting.
      if (hi.doesNotReturn)
        f->addFnAttr(Attribute::NoReturn);

      // Change to a declaration since we handle internally (simplifies
      // module and allows deleting dead code).
      if (!f->isDeclaration())
        f->deleteBody();
    }
  }
}

void SpecialFunctionHandler::bind() {
  unsigned N = sizeof(handlerInfo)/sizeof(handlerInfo[0]);

  for (unsigned i=0; i<N; ++i) {
    HandlerInfo &hi = handlerInfo[i];
    Function *f = executor.kmodule->module->getFunction(hi.name);
    
    if (f && (!hi.doNotOverride || f->isDeclaration()))
      handlers[f] = std::make_pair(hi.handler, hi.hasReturnValue);
  }
}


bool SpecialFunctionHandler::handle(ExecutionState &state, 
                                    Function *f,
                                    KInstruction *target,
                                    std::vector< ref<Expr> > &arguments) {
  handlers_ty::iterator it = handlers.find(f);
  if (it != handlers.end()) {    
    Handler h = it->second.first;
    bool hasReturnValue = it->second.second;
     // FIXME: Check this... add test?
    if (!hasReturnValue && !target->inst->use_empty()) {
      executor.terminateStateOnExecError(state, 
                                         "expected return value from void special function");
    } else {
      (this->*h)(state, target, arguments);
    }
    return true;
  } else {
    return false;
  }
}

/****/

// reads a concrete string from memory
std::string 
SpecialFunctionHandler::readStringAtAddress(ExecutionState &state, 
                                            ref<Expr> addressExpr) {
  ObjectPair op;
  addressExpr = executor.toUnique(state, addressExpr);
  if (!isa<ConstantExpr>(addressExpr)) {
    executor.terminateStateOnError(
        state, "Symbolic string pointer passed to one of the klee_ functions",
        Executor::TerminateReason::User);
    return "";
  }
  ref<ConstantExpr> address = cast<ConstantExpr>(addressExpr);
  if (!state.addressSpace.resolveOne(address, op)) {
    executor.terminateStateOnError(
        state, "Invalid string pointer passed to one of the klee_ functions",
        Executor::TerminateReason::User);
    return "";
  }
  bool res __attribute__ ((unused));
  assert(executor.solver->mustBeTrue(state, 
                                     EqExpr::create(address, 
                                                    op.first->getBaseExpr()),
                                     res) &&
         res &&
         "XXX interior pointer unhandled");
  const MemoryObject *mo = op.first;
  const ObjectState *os = op.second;

  char *buf = new char[mo->size];

  unsigned i;
  for (i = 0; i < mo->size - 1; i++) {
    ref<Expr> cur = os->read8(i);
    cur = executor.toUnique(state, cur);
    assert(isa<ConstantExpr>(cur) && 
           "hit symbolic char while reading concrete string");
    buf[i] = cast<ConstantExpr>(cur)->getZExtValue(8);
  }
  buf[i] = 0;
  
  std::string result(buf);
  delete[] buf;
  return result;
}

/****/

void SpecialFunctionHandler::handleAbort(ExecutionState &state,
                           KInstruction *target,
                           std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==0 && "invalid number of arguments to abort");
  executor.terminateStateOnError(state, "abort failure", Executor::Abort);
}

void SpecialFunctionHandler::handleExit(ExecutionState &state,
                           KInstruction *target,
                           std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==1 && "invalid number of arguments to exit");
  executor.terminateStateOnExit(state);
}

void SpecialFunctionHandler::handleSilentExit(ExecutionState &state,
                                              KInstruction *target,
                                              std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==1 && "invalid number of arguments to exit");
  executor.terminateState(state);
}

void SpecialFunctionHandler::handleAssert(ExecutionState &state,
                                          KInstruction *target,
                                          std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==3 && "invalid number of arguments to _assert");  
  executor.terminateStateOnError(state,
				 "ASSERTION FAIL: " + readStringAtAddress(state, arguments[0]),
				 Executor::Assert);
}

void SpecialFunctionHandler::handleAssertFail(ExecutionState &state,
                                              KInstruction *target,
                                              std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==4 && "invalid number of arguments to __assert_fail");
  executor.terminateStateOnError(state,
				 "ASSERTION FAIL: " + readStringAtAddress(state, arguments[0]),
				 Executor::Assert);
}

void SpecialFunctionHandler::handleReportError(ExecutionState &state,
                                               KInstruction *target,
                                               std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==4 && "invalid number of arguments to klee_report_error");
  
  // arguments[0], arguments[1] are file, line
  executor.terminateStateOnError(state,
				 readStringAtAddress(state, arguments[2]),
				 Executor::ReportError,
				 readStringAtAddress(state, arguments[3]).c_str());
}

void SpecialFunctionHandler::handleOpenMerge(ExecutionState &state,
    KInstruction *target,
    std::vector<ref<Expr> > &arguments) {
  if (!UseMerge) {
    klee_warning_once(0, "klee_open_merge ignored, use '-use-merge'");
    return;
  }

  state.openMergeStack.push_back(
      ref<MergeHandler>(new MergeHandler(&executor, &state)));

  if (DebugLogMerge)
    llvm::errs() << "open merge: " << &state << "\n";
}

void SpecialFunctionHandler::handleCloseMerge(ExecutionState &state,
    KInstruction *target,
    std::vector<ref<Expr> > &arguments) {
  if (!UseMerge) {
    klee_warning_once(0, "klee_close_merge ignored, use '-use-merge'");
    return;
  }
  Instruction *i = target->inst;

  if (DebugLogMerge)
    llvm::errs() << "close merge: " << &state << " at [" << *i << "]\n";

  if (state.openMergeStack.empty()) {
    std::ostringstream warning;
    warning << &state << " ran into a close at " << i << " without a preceding open";
    klee_warning("%s", warning.str().c_str());
  } else {
    assert(executor.mergingSearcher->inCloseMerge.find(&state) ==
               executor.mergingSearcher->inCloseMerge.end() &&
           "State cannot run into close_merge while being closed");
    executor.mergingSearcher->inCloseMerge.insert(&state);
    state.openMergeStack.back()->addClosedState(&state, i);
    state.openMergeStack.pop_back();
  }
}

void SpecialFunctionHandler::handleNew(ExecutionState &state,
                         KInstruction *target,
                         std::vector<ref<Expr> > &arguments) {
  // XXX should type check args
  assert(arguments.size()==1 && "invalid number of arguments to new");

  executor.executeAlloc(state, arguments[0], false, target);
}

void SpecialFunctionHandler::handleDelete(ExecutionState &state,
                            KInstruction *target,
                            std::vector<ref<Expr> > &arguments) {
  // FIXME: Should check proper pairing with allocation type (malloc/free,
  // new/delete, new[]/delete[]).

  // XXX should type check args
  assert(arguments.size()==1 && "invalid number of arguments to delete");
  executor.executeFree(state, arguments[0]);
}

void SpecialFunctionHandler::handleNewArray(ExecutionState &state,
                              KInstruction *target,
                              std::vector<ref<Expr> > &arguments) {
  // XXX should type check args
  assert(arguments.size()==1 && "invalid number of arguments to new[]");
  executor.executeAlloc(state, arguments[0], false, target);
}

void SpecialFunctionHandler::handleDeleteArray(ExecutionState &state,
                                 KInstruction *target,
                                 std::vector<ref<Expr> > &arguments) {
  // XXX should type check args
  assert(arguments.size()==1 && "invalid number of arguments to delete[]");
  executor.executeFree(state, arguments[0]);
}

void SpecialFunctionHandler::handleMalloc(ExecutionState &state,
                                  KInstruction *target,
                                  std::vector<ref<Expr> > &arguments) {
  // XXX should type check args
  assert(arguments.size()==1 && "invalid number of arguments to malloc");
  executor.executeAlloc(state, arguments[0], false, target);
}

void SpecialFunctionHandler::handleMemalign(ExecutionState &state,
                                            KInstruction *target,
                                            std::vector<ref<Expr>> &arguments) {
  if (arguments.size() != 2) {
    executor.terminateStateOnError(state,
      "Incorrect number of arguments to memalign(size_t alignment, size_t size)",
      Executor::User);
    return;
  }

  std::pair<ref<Expr>, ref<Expr>> alignmentRangeExpr =
      executor.solver->getRange(state, arguments[0]);
  ref<Expr> alignmentExpr = alignmentRangeExpr.first;
  auto alignmentConstExpr = dyn_cast<ConstantExpr>(alignmentExpr);

  if (!alignmentConstExpr) {
    executor.terminateStateOnError(state,
      "Could not determine size of symbolic alignment",
      Executor::User);
    return;
  }

  uint64_t alignment = alignmentConstExpr->getZExtValue();

  // Warn, if the expression has more than one solution
  if (alignmentRangeExpr.first != alignmentRangeExpr.second) {
    klee_warning_once(
        0, "Symbolic alignment for memalign. Choosing smallest alignment");
  }

  executor.executeAlloc(state, arguments[1], false, target, false, 0,
                        alignment);
}

void SpecialFunctionHandler::handleAssume(ExecutionState &state,
                            KInstruction *target,
                            std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==1 && "invalid number of arguments to klee_assume");
  
  ref<Expr> e = arguments[0];
  
  if (e->getWidth() != Expr::Bool)
    e = NeExpr::create(e, ConstantExpr::create(0, e->getWidth()));
  
  bool res;
  bool success __attribute__ ((unused)) = executor.solver->mustBeFalse(state, e, res);
  assert(success && "FIXME: Unhandled solver failure");
  if (res) {
    if (SilentKleeAssume) {
      executor.terminateState(state);
    } else {
      executor.terminateStateOnError(state,
                                     "invalid klee_assume call (provably false)",
                                     Executor::User);
    }
  } else {
    executor.addConstraint(state, e);
  }
}

void SpecialFunctionHandler::handleIsSymbolic(ExecutionState &state,
                                KInstruction *target,
                                std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==1 && "invalid number of arguments to klee_is_symbolic");

  executor.bindLocal(target, state, 
                     ConstantExpr::create(!isa<ConstantExpr>(arguments[0]),
                                          Expr::Int32));
}

void SpecialFunctionHandler::handlePreferCex(ExecutionState &state,
                                             KInstruction *target,
                                             std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==2 &&
         "invalid number of arguments to klee_prefex_cex");

  ref<Expr> cond = arguments[1];
  if (cond->getWidth() != Expr::Bool)
    cond = NeExpr::create(cond, ConstantExpr::alloc(0, cond->getWidth()));

  Executor::ExactResolutionList rl;
  executor.resolveExact(state, arguments[0], rl, "prefex_cex");
  
  assert(rl.size() == 1 &&
         "prefer_cex target must resolve to precisely one object");

  rl[0].first.first->cexPreferences.push_back(cond);
}

void SpecialFunctionHandler::handlePosixPreferCex(ExecutionState &state,
                                             KInstruction *target,
                                             std::vector<ref<Expr> > &arguments) {
  if (ReadablePosix)
    return handlePreferCex(state, target, arguments);
}

void SpecialFunctionHandler::handlePrintExpr(ExecutionState &state,
                                  KInstruction *target,
                                  std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==2 &&
         "invalid number of arguments to klee_print_expr");

  std::string msg_str = readStringAtAddress(state, arguments[0]);
  llvm::errs() << msg_str << ":" << arguments[1] << "\n";
}

void SpecialFunctionHandler::handleSetForking(ExecutionState &state,
                                              KInstruction *target,
                                              std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==1 &&
         "invalid number of arguments to klee_set_forking");
  ref<Expr> value = executor.toUnique(state, arguments[0]);
  
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(value)) {
    state.forkDisabled = CE->isZero();
  } else {
    executor.terminateStateOnError(state, 
                                   "klee_set_forking requires a constant arg",
                                   Executor::User);
  }
}

void SpecialFunctionHandler::handleStackTrace(ExecutionState &state,
                                              KInstruction *target,
                                              std::vector<ref<Expr> > &arguments) {
  state.dumpStack(outs());
}

void SpecialFunctionHandler::handleWarning(ExecutionState &state,
                                           KInstruction *target,
                                           std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==1 && "invalid number of arguments to klee_warning");

  std::string msg_str = readStringAtAddress(state, arguments[0]);
  klee_warning("%s: %s", state.stack().back().kf->function->getName().data(), 
               msg_str.c_str());
}

void SpecialFunctionHandler::handleWarningOnce(ExecutionState &state,
                                               KInstruction *target,
                                               std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==1 &&
         "invalid number of arguments to klee_warning_once");

  std::string msg_str = readStringAtAddress(state, arguments[0]);
  klee_warning_once(0, "%s: %s", state.stack().back().kf->function->getName().data(),
                    msg_str.c_str());
}

void SpecialFunctionHandler::handlePrintRange(ExecutionState &state,
                                  KInstruction *target,
                                  std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==2 &&
         "invalid number of arguments to klee_print_range");

  std::string msg_str = readStringAtAddress(state, arguments[0]);
  llvm::errs() << msg_str << ":" << arguments[1];
  if (!isa<ConstantExpr>(arguments[1])) {
    // FIXME: Pull into a unique value method?
    ref<ConstantExpr> value;
    bool success __attribute__ ((unused)) = executor.solver->getValue(state, arguments[1], value);
    assert(success && "FIXME: Unhandled solver failure");
    bool res;
    success = executor.solver->mustBeTrue(state, 
                                          EqExpr::create(arguments[1], value), 
                                          res);
    assert(success && "FIXME: Unhandled solver failure");
    if (res) {
      llvm::errs() << " == " << value;
    } else { 
      llvm::errs() << " ~= " << value;
      std::pair< ref<Expr>, ref<Expr> > res =
        executor.solver->getRange(state, arguments[1]);
      llvm::errs() << " (in [" << res.first << ", " << res.second <<"])";
    }
  }
  llvm::errs() << "\n";
}

void SpecialFunctionHandler::handleGetObjSize(ExecutionState &state,
                                  KInstruction *target,
                                  std::vector<ref<Expr> > &arguments) {
  // XXX should type check args
  assert(arguments.size()==1 &&
         "invalid number of arguments to klee_get_obj_size");
  Executor::ExactResolutionList rl;
  executor.resolveExact(state, arguments[0], rl, "klee_get_obj_size");
  for (Executor::ExactResolutionList::iterator it = rl.begin(), 
         ie = rl.end(); it != ie; ++it) {
    executor.bindLocal(
        target, *it->second,
        ConstantExpr::create(it->first.first->size,
                             executor.kmodule->targetData->getTypeSizeInBits(
                                 target->inst->getType())));
  }
}

void SpecialFunctionHandler::handleGetErrno(ExecutionState &state,
                                            KInstruction *target,
                                            std::vector<ref<Expr> > &arguments) {
  // XXX should type check args
  assert(arguments.size()==0 &&
         "invalid number of arguments to klee_get_errno");
#ifndef WINDOWS
  int *errno_addr = executor.getErrnoLocation(state);
#else
  int *errno_addr = nullptr;
#endif

  // Retrieve the memory object of the errno variable
  ObjectPair result;
  bool resolved = state.addressSpace.resolveOne(
      ConstantExpr::create((uint64_t)errno_addr, Expr::Int64), result);
  if (!resolved)
    executor.terminateStateOnError(state, "Could not resolve address for errno",
                                   Executor::User);
  executor.bindLocal(target, state, result.second->read(0, Expr::Int32));
}

void SpecialFunctionHandler::handleErrnoLocation(
    ExecutionState &state, KInstruction *target,
    std::vector<ref<Expr> > &arguments) {
  // Returns the address of the errno variable
  assert(arguments.size() == 0 &&
         "invalid number of arguments to __errno_location/__error");

#ifndef WINDOWS
  int *errno_addr = executor.getErrnoLocation(state);
#else
  int *errno_addr = nullptr;
#endif

  executor.bindLocal(
      target, state,
      ConstantExpr::create((uint64_t)errno_addr,
                           executor.kmodule->targetData->getTypeSizeInBits(
                               target->inst->getType())));
}
void SpecialFunctionHandler::handleCalloc(ExecutionState &state,
                            KInstruction *target,
                            std::vector<ref<Expr> > &arguments) {
  // XXX should type check args
  assert(arguments.size()==2 &&
         "invalid number of arguments to calloc");
  
  ref<Expr> nObjExpr = arguments[0];
  ref<Expr> objSzExpr = arguments[1];

  ref<Expr> size = MulExpr::create(nObjExpr, objSzExpr);
  if (CallocMaxSize == 0) {
    executor.executeAlloc(state, size, false, target, true);
    return;
  }

  size_t realSize;
  ref<ConstantExpr> ce = dyn_cast<ConstantExpr>(size);
  if (!ce.isNull()) {
    realSize = ce->getZExtValue();
  } else {
    size = executor.optimizer.optimizeExpr(size, true);

    std::pair<ref<Expr>, ref<Expr> > 
        range = executor.solver->getRange(state, size);

    ref<ConstantExpr> lo = dyn_cast<ConstantExpr>(range.first);
    ref<ConstantExpr> hi = dyn_cast<ConstantExpr>(range.second);  
    assert(!lo.isNull() && !hi.isNull() && "FIXME: unhandled solver failure");

    uint64_t loVal = lo->getZExtValue();
    uint64_t hiVal = hi->getZExtValue();

    if (loVal <= CallocMaxSize) {
      realSize = std::min((size_t)CallocMaxSize, hiVal);
    } else {
      realSize = loVal;
    }
  }

  if (realSize <= CallocMaxSize) {
    executor.executeAlloc(state, size, false, target, true);
    return;
  }
  
  // Here's the part where we actually do the allocateContiguous thing
  size_t nObjs = (size_t)std::ceil(realSize / (double)CallocMaxSize);
  size_t objSz = CallocMaxSize;

  assert(nObjs * objSz >= realSize && "can't do math!");

  klee_warning("Rather than allocating %lu bytes, allocating %lu objects "
               "of size %lu", realSize, nObjs, objSz);

  doAllocContiguous(state, target, nObjs, objSz, "calloc");
}

void SpecialFunctionHandler::handleRealloc(ExecutionState &state,
                            KInstruction *target,
                            std::vector<ref<Expr> > &arguments) {
  // XXX should type check args
  assert(arguments.size()==2 &&
         "invalid number of arguments to realloc");
  ref<Expr> address = arguments[0];
  ref<Expr> size = arguments[1];

  Executor::StatePair zeroSize = executor.fork(state, 
                                               Expr::createIsZero(size), 
                                               true);
  
  if (zeroSize.first) { // size == 0
    executor.executeFree(*zeroSize.first, address, target);   
  }
  if (zeroSize.second) { // size != 0
    Executor::StatePair zeroPointer = executor.fork(*zeroSize.second, 
                                                    Expr::createIsZero(address), 
                                                    true);
    
    if (zeroPointer.first) { // address == 0
      executor.executeAlloc(*zeroPointer.first, size, false, target);
    } 
    if (zeroPointer.second) { // address != 0
      Executor::ExactResolutionList rl;
      executor.resolveExact(*zeroPointer.second, address, rl, "realloc");
      
      for (Executor::ExactResolutionList::iterator it = rl.begin(), 
             ie = rl.end(); it != ie; ++it) {
        executor.executeAlloc(*it->second, size, false, target, false, 
                              it->first.second);
      }
    }
  }
}

void SpecialFunctionHandler::handleFree(ExecutionState &state,
                          KInstruction *target,
                          std::vector<ref<Expr> > &arguments) {
  // XXX should type check args
  assert(arguments.size()==1 &&
         "invalid number of arguments to free");
  executor.executeFree(state, arguments[0]);
}

void SpecialFunctionHandler::handleCheckMemoryAccess(ExecutionState &state,
                                                     KInstruction *target,
                                                     std::vector<ref<Expr> > 
                                                       &arguments) {
  assert(arguments.size()==2 &&
         "invalid number of arguments to klee_check_memory_access");

  ref<Expr> address = executor.toUnique(state, arguments[0]);
  ref<Expr> size = executor.toUnique(state, arguments[1]);
  if (!isa<ConstantExpr>(address) || !isa<ConstantExpr>(size)) {
    executor.terminateStateOnError(state, 
                                   "check_memory_access requires constant args",
				   Executor::User);
  } else {
    ObjectPair op;

    if (!state.addressSpace.resolveOne(cast<ConstantExpr>(address), op)) {
      executor.terminateStateOnError(state,
                                     "check_memory_access: memory error",
				     Executor::Ptr, NULL,
                                     executor.getAddressInfo(state, address));
    } else {
      ref<Expr> chk = 
        op.first->getBoundsCheckPointer(address, 
                                        cast<ConstantExpr>(size)->getZExtValue());
      if (!chk->isTrue()) {
        executor.terminateStateOnError(state,
                                       "check_memory_access: memory error",
				       Executor::Ptr, NULL,
                                       executor.getAddressInfo(state, address));
      }
    }
  }
}

void SpecialFunctionHandler::handleGetValue(ExecutionState &state,
                                            KInstruction *target,
                                            std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==1 &&
         "invalid number of arguments to klee_get_value");

  executor.executeGetValue(state, arguments[0], target);
}

void SpecialFunctionHandler::handleDefineFixedObject(ExecutionState &state,
                                                     KInstruction *target,
                                                     std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==2 &&
         "invalid number of arguments to klee_define_fixed_object");
  assert(isa<ConstantExpr>(arguments[0]) &&
         "expect constant address argument to klee_define_fixed_object");
  assert(isa<ConstantExpr>(arguments[1]) &&
         "expect constant size argument to klee_define_fixed_object");
  
  uint64_t address = cast<ConstantExpr>(arguments[0])->getZExtValue();
  uint64_t size = cast<ConstantExpr>(arguments[1])->getZExtValue();
  MemoryObject *mo = executor.memory->allocateFixed(address, size, state.prevPC()->inst);
  executor.bindObjectInState(state, mo, false);
  mo->isUserSpecified = true; // XXX hack;
}

void SpecialFunctionHandler::handleDefineFixedObjectFromExisting(
  ExecutionState &state, KInstruction *target, std::vector<ref<Expr> > &arguments) 
{
  assert(arguments.size()==2 &&
         "invalid number of arguments to klee_define_fixed_object");
  assert(isa<ConstantExpr>(arguments[0]) &&
         "expect constant address argument to klee_define_fixed_object");
  assert(isa<ConstantExpr>(arguments[1]) &&
         "expect constant size argument to klee_define_fixed_object");
  
  uint64_t address = cast<ConstantExpr>(arguments[0])->getZExtValue();
  uint64_t size = cast<ConstantExpr>(arguments[1])->getZExtValue();
  MemoryObject *mo = executor.memory->allocateFixed(address, size, state.prevPC()->inst);
  executor.bindObjectInState(state, mo, false);
  mo->isUserSpecified = true; // XXX hack;

  const ObjectState *cos = state.addressSpace.findObject(mo);
  ObjectState *os = state.addressSpace.getWriteable(mo, cos);
  assert(os);
  char *data = (char*)address;
  for (uint64_t i = 0; i < size; ++i) {
    os->write8(state, i, data[i]);
  }
}

void SpecialFunctionHandler::handleInitConcreteZero(ExecutionState &state,
                                                    KInstruction *target,
                                                    std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==2 &&
         "invalid number of arguments to klee_init_concrete_zero");
  assert(isa<ConstantExpr>(arguments[0]) &&
         "expect constant address argument to klee_init_concrete_zero");

  // Executor::ExactResolutionList rl;
  // executor.resolveExact(state, arguments[0], rl, "klee_init_concrete_zero");

  // for (Executor::ExactResolutionList::iterator it = rl.begin(), ie = rl.end(); 
  //         it != ie; ++it) {
  //   const MemoryObject *mo = it->first.first;
  //   const ObjectState *cos = it->first.second;
  //   ObjectState *os = state.addressSpace.getWriteable(mo, cos);
    
  //   os->initializeToZero();
  // }

  ref<Expr> addr = arguments[0];
  ref<Expr> size = arguments[1];
  uint64_t realSize = 0;
  if (ConstantExpr *ce = dyn_cast<ConstantExpr>(size.get())) {
    realSize = ce->getZExtValue();
  } else {
    size->dump();
    klee_error("Not sure how to handle symbolic size argument yet!");
  }

  std::list<ObjectPair> pmemObjs;
  uint64_t offset = 0;
  for (; offset < realSize; offset += PersistentState::MaxSize) {
    ref<Expr> offsetExpr = ConstantExpr::create(offset, Expr::Int64);
    ref<Expr> ptrExpr = AddExpr::create(addr, offsetExpr);
    
    ObjectPair res;
    bool success;
    assert(state.addressSpace.resolveOne(state, executor.solver, 
                                         ptrExpr, res, success));
    assert(success && "could not resolve one! (isPmem)");
    assert(isa<PersistentState>(res.second) && "should not use this on others");
    pmemObjs.push_back(res);
  }
  
  for (ObjectPair &op : pmemObjs) {
    const ObjectState *cos = state.addressSpace.findObject(op.first);
    assert(cos);
    ObjectState *wos = state.addressSpace.getWriteable(op.first, cos);
    assert(wos);
    wos->initializeToZero();
  }
}

void SpecialFunctionHandler::handleUndefineFixedObject(ExecutionState &state,
                                                       KInstruction *target,
                                                       std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==1 &&
         "invalid number of arguments to klee_undefine_fixed_object");
  assert(isa<ConstantExpr>(arguments[0]) &&
         "expect constant address argument to klee_undefine_fixed_object");

  Executor::ExactResolutionList rl;
  executor.resolveExact(state, arguments[0], rl, "klee_undefine_fixed_object");

  for (Executor::ExactResolutionList::iterator it = rl.begin(), ie = rl.end(); 
          it != ie; ++it) {
    const MemoryObject *mo = it->first.first;
    if (state.persistentObjects.count(mo)) {
      state.persistentObjects.erase(mo);
    }

    it->second->addressSpace.unbindObject(it->first.first);
  }
}


void SpecialFunctionHandler::handleMakeSymbolic(ExecutionState &state,
                                                KInstruction *target,
                                                std::vector<ref<Expr> > &arguments) {
  std::string name;

  if (arguments.size() != 3) {
    executor.terminateStateOnError(state, "Incorrect number of arguments to klee_make_symbolic(void*, size_t, char*)", Executor::User);
    return;
  }

  name = arguments[2]->isZero() ? "" : readStringAtAddress(state, arguments[2]);

  if (name.length() == 0) {
    name = "unnamed";
    klee_warning("klee_make_symbolic: renamed empty name to \"unnamed\"");
  }

  Executor::ExactResolutionList rl;
  executor.resolveExact(state, arguments[0], rl, "make_symbolic");
  
  for (Executor::ExactResolutionList::iterator it = rl.begin(), 
         ie = rl.end(); it != ie; ++it) {
    const MemoryObject *mo = it->first.first;
    mo->setName(name);
    
    const ObjectState *old = it->first.second;
    ExecutionState *s = it->second;
    
    if (old->readOnly) {
      executor.terminateStateOnError(*s, "cannot make readonly object symbolic",
                                     Executor::User);
      return;
    } 

    // FIXME: Type coercion should be done consistently somewhere.
    bool res;
    bool success __attribute__ ((unused)) =
      executor.solver->mustBeTrue(*s, 
                                  EqExpr::create(ZExtExpr::create(arguments[1],
                                                                  Context::get().getPointerWidth()),
                                                 mo->getSizeExpr()),
                                  res);
    assert(success && "FIXME: Unhandled solver failure");
    
    if (res) {
      executor.executeMakeSymbolic(*s, mo, name);
    } else {      
      executor.terminateStateOnError(*s, 
                                     "wrong size given to klee_make_symbolic[_name]", 
                                     Executor::User);
    }
  }
}

void SpecialFunctionHandler::handleMarkGlobal(ExecutionState &state,
                                              KInstruction *target,
                                              std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==1 &&
         "invalid number of arguments to klee_mark_global");  

  Executor::ExactResolutionList rl;
  executor.resolveExact(state, arguments[0], rl, "mark_global");
  
  for (Executor::ExactResolutionList::iterator it = rl.begin(), 
         ie = rl.end(); it != ie; ++it) {
    const MemoryObject *mo = it->first.first;
    assert(!mo->isLocal);
    mo->isGlobal = true;
  }
}

void SpecialFunctionHandler::handleAddOverflow(ExecutionState &state,
                                               KInstruction *target,
                                               std::vector<ref<Expr> > &arguments) {
  executor.terminateStateOnError(state, "overflow on addition",
                                 Executor::Overflow);
}

void SpecialFunctionHandler::handleSubOverflow(ExecutionState &state,
                                               KInstruction *target,
                                               std::vector<ref<Expr> > &arguments) {
  executor.terminateStateOnError(state, "overflow on subtraction",
                                 Executor::Overflow);
}

void SpecialFunctionHandler::handleMulOverflow(ExecutionState &state,
                                               KInstruction *target,
                                               std::vector<ref<Expr> > &arguments) {
  executor.terminateStateOnError(state, "overflow on multiplication",
                                 Executor::Overflow);
}

void SpecialFunctionHandler::handleDivRemOverflow(ExecutionState &state,
                                               KInstruction *target,
                                               std::vector<ref<Expr> > &arguments) {
  executor.terminateStateOnError(state, "overflow on division or remainder",
                                 Executor::Overflow);
}

void SpecialFunctionHandler::doAllocContiguous(ExecutionState &state, 
                                               KInstruction *target,
                                               size_t nObj, 
                                               size_t unitSz,
                                               std::string baseName,
                                               bool make_persistent,
                                               bool init_zero,
                                               std::string file_name) {
  auto mos = executor.memory->allocateContiguous(
                            unitSz, nObj, false, true, state.prevPC()->inst);
  int objNum = 0;

  int fd = -1;
  if (!file_name.empty()) {
    fd = open(file_name.c_str(), O_RDONLY);
    if (fd < 0) {
      klee_error("could not open %s, %s", file_name.c_str(), strerror(errno));
    }
  }

  for (auto *mo : mos) {
    assert(mo);
    std::string moName;
    llvm::raw_string_ostream ss(moName);
    ss << baseName << "_" << objNum;
    objNum++;
    mo->setName(ss.str());
    
    if (make_persistent) {
      ObjectState *os = nullptr;
      if (init_zero && !file_name.empty()) {
        klee_error("can only set init_zero or file-based init!");
      }

      if (init_zero) {
        os = executor.bindObjectInState(state, mo, false);
        os->initializeToZero();
        assert(os && "could not bind object!");
      } else if (!file_name.empty()) {
        os = executor.bindObjectInState(state, mo, false);
        os->initializeToZero();

        assert(os && "could not bind object!");

        assert(fd >= 0);

        char *buffer = (char*)malloc(mo->size);
        assert(buffer);
        ssize_t r = read(fd, buffer, mo->size);
        assert(r == mo->size && "did not read all!");

        for (unsigned i = 0; i < mo->size; ++i) {
          os->write8(state, i, (uint8_t)buffer[i]); 
        }

        free(buffer);
      } else {
        os = executor.bindObjectInState(state, mo, false);
        assert(os && "could not bind object!");
        executor.executeMakeSymbolic(state, mo, mo->name);
      }
      
      executor.executeMarkPersistent(state, mo);
    } else {
      auto *os = executor.bindObjectInState(state, mo, false);
      assert(os && "could not bind object!!");
    }
  }

  if (!mos.size()) {
    static ref<Expr> nullptrExpr = ConstantExpr::alloc(0, Context::get().getPointerWidth());
    executor.bindLocal(target, state, nullptrExpr);
  } else {
    executor.bindLocal(target, state, mos.front()->getBaseExpr());
  }
}

/* Persistent Memory */

std::list<ObjectPair> 
SpecialFunctionHandler::getPmemObjsInRange(ExecutionState &state,
                                           ref<Expr> addr,
                                           uint64_t realSize) {
  std::list<ObjectPair> pmemObjs;
  for (uint64_t offset = 0; offset < realSize; offset += PersistentState::MaxSize) {
    ref<Expr> offsetExpr = ConstantExpr::create(offset, Expr::Int64);
    ref<Expr> ptrExpr = AddExpr::create(addr, offsetExpr);
    
    ObjectPair res;
    bool success;
    assert(state.addressSpace.resolveOne(state, executor.solver, ptrExpr, res, success));
    assert(success && "could not resolve one! (isPmem)");
    assert(isa<PersistentState>(res.second) && "volatile object in range!");
    pmemObjs.push_back(res);
  }

  return pmemObjs;
}

void SpecialFunctionHandler::handleAllocPmem(ExecutionState &state,
                                            KInstruction *target,
                                            std::vector<ref<Expr>> &arguments) {
  if (arguments.size() != 4) {
    executor.terminateStateOnError(state,
      "Incorrect number of arguments to "
      "klee_pmem_alloc_pmem(size_t size, char *name, bool init_zero, const char *fname)",
      Executor::User);
    return;
  }

  bool init_zero = false;
  if (ConstantExpr *initZero = dyn_cast<ConstantExpr>(arguments[2])) {
    init_zero = (bool)initZero->getZExtValue(Expr::Bool);
  } else {
    executor.terminateStateOnError(state,
      "klee_pmem_alloc_pmem(size_t size, char *name, bool init_zero) :"
      "init_zero must be concrete!",
      Executor::User);
  }

  std::string fname;
  fname = arguments[3]->isZero() ? "" : readStringAtAddress(state, arguments[3]);

  std::string name;
  name = arguments[1]->isZero() ? "" : readStringAtAddress(state, arguments[1]);

  if (name.length() == 0) {
    name = "unnamed";
    klee_warning("klee_pmem_alloc_pmem: name parameter empty, using \"unnamed\"");
  }

  size_t realSize = 0;

  ref<ConstantExpr> constSizeExpr = dyn_cast<ConstantExpr>(arguments[0]);
  if (constSizeExpr.isNull()) {
    executor.terminateStateOnError(state,
      "(klee_pmem_alloc_pmem) Could not determine size of symbolic alignment",
      Executor::User);
    return;
  } else {
    realSize = constSizeExpr->getZExtValue();
  }

  if (realSize % 64) {
    executor.terminateStateOnError(state,
      "(klee_pmem_alloc_pmem) size must be a multiple of cache line size",
      Executor::User);
    return;
  }

  // Should be concrete for the case we care about--pmem file.
  uint64_t unitSz = PersistentState::MaxSize;

  uint64_t totalSize = (realSize % unitSz) ? ((realSize / unitSz) + 1) * unitSz : realSize;

  size_t nObj = totalSize / unitSz;
  
  doAllocContiguous(state, target, nObj, unitSz, name, true, init_zero, fname);
}

void SpecialFunctionHandler::handleMarkPersistent(ExecutionState &state,
                                                  KInstruction *target,
                                                  std::vector<ref<Expr> > &arguments) {
  std::string name;

  if (arguments.size() != 3) {
    executor.terminateStateOnError(
        state,
        "Incorrect number of arguments to "
          "klee_pmem_mark_persistent(void*, size_t, char*)",
        Executor::User);
    return;
  }

  name = arguments[2]->isZero() ? "" : readStringAtAddress(state, arguments[2]);

  if (name.length() == 0) {
    name = "unnamed";
    klee_warning("klee_pmem_mark_persistent: name parameter empty, using \"unnamed\"");
  }

  ref<Expr> addr = arguments[0];
  ref<Expr> size = arguments[1];
  uint64_t realSize = 0;
  if (ConstantExpr *ce = dyn_cast<ConstantExpr>(size.get())) {
    realSize = ce->getZExtValue();
  } else {
    size->dump();
    klee_error("Not sure how to handle symbolic size argument yet!");
  }

  for (uint64_t offset = 0; offset < realSize; offset += PersistentState::MaxSize) {
    bool succ;
    ObjectPair found;
    ref<Expr> offsetExpr = ConstantExpr::create(offset, addr->getWidth());
    ref<Expr> currAddr = AddExpr::create(addr, offsetExpr);
    assert(state.addressSpace.resolveOne(state, executor.solver, currAddr, found, succ));
    assert(succ);
    const MemoryObject *mo = found.first;
    mo->setName(name);

    // FIXME: Type coercion should be done consistently somewhere.
    // Check that the size is proper.
    uint64_t properSize = std::min(PersistentState::MaxSize, realSize - offset);
    ref<Expr> properSizeExpr = ConstantExpr::create(properSize, offsetExpr->getWidth());
    ref<Expr> sizeCheckExpr = EqExpr::create(properSizeExpr, mo->getSizeExpr());
    bool res;
    bool success = executor.solver->mustBeTrue(state, sizeCheckExpr, res);
    assert(success && "FIXME: Unhandled solver failure");
    
    if (res) {
      executor.executeMarkPersistent(state, mo);
    } else {
      executor.terminateStateOnError(state, 
                                     "wrong size given to klee_pmem_mark_persistent[_name]", 
                                     Executor::User);
    }
  }

  // Just return the same address as what we received.
  executor.bindLocal(target, state, addr);
}

void SpecialFunctionHandler::handleIsPmem(ExecutionState &state,
                                          KInstruction *target,
                                          std::vector<ref<Expr> > &arguments) {
  assert(arguments.size() == 2 &&
      "invalid number of arguments to klee_pmem_is_pmem");

  ref<Expr> addr = arguments[0];
  ref<Expr> size = arguments[1];
  uint64_t realSize = 0;
  if (ConstantExpr *ce = dyn_cast<ConstantExpr>(size.get())) {
    realSize = ce->getZExtValue();
  } else {
    size->dump();
    klee_error("Not sure how to handle symbolic size argument yet!");
  }

  std::list<ObjectPair> pmemObjs = getPmemObjsInRange(state, addr, realSize);
  
  ref<Expr> allIsPmem = ConstantExpr::create(1, Expr::Bool);
  for (ObjectPair &op : pmemObjs) {
    const ObjectState *os = op.second;

    ref<Expr> isPmem = ConstantExpr::create(isa<PersistentState>(os), Expr::Bool);
    assert(!isPmem.isNull() && "null boolean expr from klee_pmem_is_pmem!");
    allIsPmem = AndExpr::create(allIsPmem, isPmem);
  }

  executor.bindLocal(target, state, allIsPmem); 
}

void SpecialFunctionHandler::handleIsPersisted(ExecutionState &state,
                                               KInstruction *target,
                                               std::vector<ref<Expr>> &arguments) {
  assert(arguments.size()==2 &&
      "invalid number of arguments to klee_pmem_check_persisted");

  ref<Expr> addr = arguments[0];
  ref<Expr> size = arguments[1];
  uint64_t realSize = 0;
  if (ConstantExpr *ce = dyn_cast<ConstantExpr>(size.get())) {
    realSize = ce->getZExtValue();
  } else {
    size->dump();
    klee_error("Not sure how to handle symbolic size argument yet!");
  }

  bool emitErrs = false;
  std::unordered_set<std::string> errors;
  for (uint64_t offset = 0; offset < realSize; offset += PersistentState::MaxSize) {
    ref<Expr> offsetExpr = ConstantExpr::create(offset, Expr::Int64);
    ref<Expr> ptrExpr = AddExpr::create(addr, offsetExpr);
    
    ObjectPair res;
    bool success;
    assert(state.addressSpace.resolveOne(state, executor.solver, ptrExpr, res, success));
    assert(success && "could not resolve one! (isPersisted)");

    const MemoryObject *mo = res.first;
    PersistentState *ps = dyn_cast<PersistentState>(state.addressSpace.getWriteable(mo, res.second));
    assert(ps && "trying to check if non-pmem is persisted!");

    bool hasErrs = executor.getPersistenceErrors(state, mo, errors);
    emitErrs = emitErrs || hasErrs;
    ps->flushAll();
  }

  
  if (emitErrs) {
    assert(errors.size() && "no errors to emit!");
    executor.emitPmemError(state, errors);
  } else {
    assert(errors.empty() && "forgot to emit!");
  }
}

void SpecialFunctionHandler::handleIsOrderedBefore(ExecutionState &state,
                                                   KInstruction *target,
                                                   std::vector<ref<Expr> > &arguments) {
  /* assert(arguments.size()==4 && */
  /*     "invalid number of arguments to klee_pmem_check_ordered_before"); */
  klee_warning_once(0, "klee_pmem_check_ordered_before not supported");
}

/* Thread Scheduling Management */
// void klee_thread_create(uint64_t tid, void *(*start_routine)(void *),
//                         void *arg);
void SpecialFunctionHandler::handleThreadCreate (
    ExecutionState &state, KInstruction *target,
    std::vector<ref<Expr>> &arguments) {
  assert(arguments.size() == 3 &&
         "invalid number of arguments to klee_thread_create");
  ref<Expr> tid = executor.toUnique(state, arguments[0]);
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(tid)) {
    executor.executeThreadCreate(state, CE->getZExtValue(), arguments[1],
                                 arguments[2]);
  } else {
    executor.terminateStateOnError(state, "klee_thread_create symbolic tid",
                                   Executor::User);
  }
}
// void klee_thread_terminate() __attribute__ ((__noreturn__));
void SpecialFunctionHandler::handleThreadTerminate (
    ExecutionState &state, KInstruction *target,
    std::vector<ref<Expr>> &arguments) {
  assert(arguments.empty() &&
         "invalid number of arguments to klee_thread_terminate");
  executor.executeThreadExit(state);
}
// uint64_t klee_get_wlist(void);
void SpecialFunctionHandler::handleGetWList (
    ExecutionState &state, KInstruction *target,
    std::vector<ref<Expr>> &arguments) {
  assert(arguments.empty() && "invalid number of arguments to klee_get_wlist");
    wlist_id_t wid = state.getWaitingList();
    executor.bindLocal(target, state,
                       ConstantExpr::create(wid, executor.getWidthForLLVMType(
                                                     target->inst->getType())));
}
// void klee_thread_preempt(int yield);
void SpecialFunctionHandler::handleThreadPreempt (
    ExecutionState &state, KInstruction *target,
    std::vector<ref<Expr>> &arguments) {
  assert(arguments.size() == 1 &&
         "invalid number of arugments to klee_thread_preempt");
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(arguments[0])) {
    executor.schedule(state, CE->isTrue());
  } else {
    executor.terminateStateOnError(state, "symbolic klee_thread_preempt",
                                   Executor::User);
  }
}
// void klee_thread_sleep(uint64_t wlist);
void SpecialFunctionHandler::handleThreadSleep (
    ExecutionState &state, KInstruction *target,
    std::vector<ref<Expr>> &arguments) {
  assert(arguments.size() == 1 &&
         "invalid number of arguments to klee_thread_sleep");
  ref<Expr> wlistExpr = executor.toUnique(state, arguments[0]);
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(wlistExpr)) {
    state.sleepThread(CE->getZExtValue());
    executor.schedule(state, false);
  } else {
    executor.terminateStateOnError(state, "symbolic klee_thread_sleep",
                                   Executor::User);
  }
}
// void klee_thread_notify(uint64_t wlist, int all);
void SpecialFunctionHandler::handleThreadNotify (
    ExecutionState &state, KInstruction *target,
    std::vector<ref<Expr>> &arguments) {
  assert(arguments.size() == 2 &&
         "invalid number of arguments to klee_thread_notify");
  ref<Expr> wlist = executor.toUnique(state, arguments[0]);
  ref<Expr> all = executor.toUnique(state, arguments[1]);
  if (ConstantExpr *wlistCE = dyn_cast<ConstantExpr>(wlist)) {
    wlist_id_t wlid = wlistCE->getZExtValue();
    if (ConstantExpr *allCE = dyn_cast<ConstantExpr>(all)) {
      if (allCE->isZero()) {
        // When you only notify one thread in the given waiting list, which
        // thread to wake up is undeterministic. The original Cloud9 optionally
        // forks to enumerate all scheduling choice. I have not implemented
        // forking, so executor is not involved here. I will just notify the
        // head of the given waiting list.
        // Cloud9 handler:
        // executor.executeThreadNotifyOne(state, wlistCE->getZExtValue());
        std::set<thread_uid_t> &wl = state.waitingLists[wlid];
        if (wl.size() == 0) {
          state.waitingLists.erase(wlid);
        }
        else {
          state.notifyOne(wlid, *(wl.begin()));
        }
      } else {
        // It is simple enough to be handled by the state class itself
        state.notifyAll(wlid);
      }
    } else {
      executor.terminateStateOnError(
          state, "symbolic `all` in klee_thread_notify", Executor::User);
    }
  } else {
    executor.terminateStateOnError(
        state, "symbolic `wlist` in klee_thread_notify", Executor::User);
  }
}

// void klee_get_context(uint64_t *tid, int32_t *pid);
void SpecialFunctionHandler::handleGetContext (
    ExecutionState &state, KInstruction *target,
    std::vector<ref<Expr>> &arguments) {
  assert(arguments.size() == 2 &&
         "invalid number of arguments in klee_get_context");
  ref<Expr> tidAddr = executor.toUnique(state, arguments[0]);
  ref<Expr> pidAddr = executor.toUnique(state, arguments[1]);
  if (!isa<ConstantExpr>(tidAddr) || !isa<ConstantExpr>(pidAddr)) {
    // error path
    executor.terminateStateOnError(state, "symbolic args to klee_get_context",
                                   Executor::User);
    return;
  }

  if (!tidAddr->isZero()) {
    executor.executeMemoryOperation(
        state, true, tidAddr,
        ConstantExpr::create(state.crtThread().getTid(), Expr::Int64),
        nullptr /*target*/);
  }
  if (!pidAddr->isZero()) {
    executor.executeMemoryOperation(
        state, true, pidAddr,
        ConstantExpr::create(state.crtThread().getPid(), Expr::Int32),
        nullptr /*target*/);
  }
}
// int klee_process_fork(int32_t pid);
void SpecialFunctionHandler::handleProcessFork (
    ExecutionState &state, KInstruction *target,
    std::vector<ref<Expr>> &arguments) {
  klee_warning("Executing klee_process_fork, do nothing");
}
// void klee_process_terminate() __attribute__ ((__noreturn__));
void SpecialFunctionHandler::handleProcessTerminate (
    ExecutionState &state, KInstruction *target,
    std::vector<ref<Expr>> &arguments) {
  klee_warning("Executing klee_process_terminate, do nothing");
}
// void klee_make_shared(void *addr, size_t nbytes);
void SpecialFunctionHandler::handleMakeShared (
    ExecutionState &state, KInstruction *target,
    std::vector<ref<Expr>> &arguments) {
  klee_warning_once(0, "Executing klee_make_shared, do nothing");
}

// uint64_t klee_get_time(void);
void SpecialFunctionHandler::handleGetTime (
    ExecutionState &state, KInstruction *target,
    std::vector<ref<Expr>> &arguments) {
  assert(arguments.empty() && "invalid number of arguments to klee_get_time");
  executor.bindLocal(
      target, state,
      ConstantExpr::create(state.stateTime, executor.getWidthForLLVMType(
                                                target->inst->getType())));
}

// void klee_set_time(uint64_t time);
void SpecialFunctionHandler::handleSetTime (
    ExecutionState &state, KInstruction *target,
    std::vector<ref<Expr>> &arguments) {
  assert(arguments.size() == 1 &&
         "invalid number of arguments to klee_set_time");
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(arguments[0])) {
    state.stateTime = CE->getZExtValue();
  } else {
    executor.terminateStateOnError(
        state, "klee_set_time requries a constant argument", Executor::User);
  }
}
