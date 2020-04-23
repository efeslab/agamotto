/*
 * Cloud9 Parallel Symbolic Execution Engine
 *
 * Copyright (c) 2011, Dependable Systems Laboratory, EPFL
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Dependable Systems Laboratory, EPFL nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE DEPENDABLE SYSTEMS LABORATORY, EPFL BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * All contributors are listed in CLOUD9-AUTHORS file.
 *
*/
/*
 * This is an incomplete port of cloud9's multi-threading support
 */
#ifndef _KLEE_THREADING_H_
#define _KLEE_THREADING_H_
#include "klee/Internal/Module/KInstIterator.h"
#include "klee/Internal/Module/KModule.h"

#include "llvm/Support/raw_ostream.h"
namespace klee {

class CallPathNode;
struct Cell;
class MemoryObject;
struct StackFrame {
  KInstIterator caller;
  KFunction *kf;
  CallPathNode *callPathNode;

  std::vector<const MemoryObject *> allocas;
  Cell *locals;

  /// Minimum distance to an uncovered instruction once the function
  /// returns. This is not a good place for this but is used to
  /// quickly compute the context sensitive minimum distance to an
  /// uncovered instruction. This value is updated by the StatsTracker
  /// periodically.
  unsigned minDistToUncoveredOnReturn;

  // For vararg functions: arguments not passed via parameter are
  // stored (packed tightly) in a local (alloca) memory object. This
  // is set up to match the way the front-end generates vaarg code (it
  // does not pass vaarg through as expected). VACopy is lowered inside
  // of intrinsic lowering.
  MemoryObject *varargs;

  StackFrame(KInstIterator caller, KFunction *kf);
  StackFrame(const StackFrame &s);
  ~StackFrame();
};

// note that I have not ported multi-processes support, process_id_t is just a
// place holder.
typedef uint64_t thread_id_t;
typedef uint64_t process_id_t;
typedef uint64_t wlist_id_t;
typedef std::pair<thread_id_t, process_id_t> thread_uid_t;

class ExecutionState;
class Thread {
  friend class ExecutionState;
  friend class Executor;

public:
  typedef std::vector<StackFrame> stack_ty;

private:
  /// @brief Pointer to instruction to be executed after the current
  /// instruction
  KInstIterator pc;

  /// @brief Pointer to instruction which is currently executed
  KInstIterator prevPC;

  /// @brief Stack representing the current instruction stream
  stack_ty stack;

  /// @brief Remember from which Basic Block control flow arrived
  /// (i.e. to select the right phi values)
  unsigned incomingBBIndex;

  /// @brief If this thread is active (should be scheduled). Disabled threads
  /// are either sleeping or terminated.
  bool enabled;
  /// @brief If this thread is disabled (sleeping), which waiting list it is
  /// waiting for. valid wlist id > 0
  wlist_id_t waitingList;
  /// @brief the tuple (tid, pid). Note that we currently do not support multi
  /// processes, so the pid is always be 0 for now.
  thread_uid_t tuid;

  /// When IgnorePOSIXPath is set, isInPOSIX will be true if the latest frame
  ///   in current call stack is a function from POSIX runtime and UserMain
  //    func is already on the stack (we are not initializing).
  /// isInPOSIX will be clear to false when frames of POSIX runtime function
  ///   are poped out. It will also be false if we haven't called UserMain yet.
  /// When IgnorePOSIXPath is not set, isInPOSIX will be always false even if
  ///   POSIX runtime function is on the stack.
  /// It influences whether we should record/replay path trace
  /// It is designed to filter out all recording happened inside POSIX runtime
  bool isInPOSIX;
  unsigned POSIXDepth;
  bool isInLIBC;
  unsigned LIBCDepth;

public:
  Thread(thread_id_t tid, process_id_t pid, KFunction *start_function);
  thread_id_t getTid() const { return tuid.first; }
  process_id_t getPid() const { return tuid.second; }

  /* Debugging helper */
  void dumpStack(llvm::raw_ostream &out) const;
};
} // namespace klee
#endif // _KLEE_THREADING_H_
