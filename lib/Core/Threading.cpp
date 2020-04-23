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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE DEPENDABLE SYSTEMS LABORATORY, EPFL BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * All contributors are listed in CLOUD9-AUTHORS file.
 *
 */

#include "klee/Threading.h"
#include "klee/Internal/Module/Cell.h"
#include "klee/Internal/Module/KModule.h"
#include "ExecutorCmdLine.h"

#include <iomanip>

using llvm::Function;

namespace klee {
/***/

StackFrame::StackFrame(KInstIterator _caller, KFunction *_kf)
    : caller(_caller), kf(_kf), callPathNode(0), minDistToUncoveredOnReturn(0),
      varargs(0) {
  locals = new Cell[kf->numRegisters];
}

StackFrame::StackFrame(const StackFrame &s)
    : caller(s.caller), kf(s.kf), callPathNode(s.callPathNode),
      allocas(s.allocas),
      minDistToUncoveredOnReturn(s.minDistToUncoveredOnReturn),
      varargs(s.varargs) {
  locals = new Cell[s.kf->numRegisters];
  for (unsigned i = 0; i < s.kf->numRegisters; i++)
    locals[i] = s.locals[i];
}

StackFrame::~StackFrame() { delete[] locals; }

Thread::Thread(thread_id_t tid, process_id_t pid, KFunction *start_function)
    : enabled(true), waitingList(0), isInPOSIX(false),
      POSIXDepth(0), isInLIBC(false), LIBCDepth(0) {
  assert(start_function != nullptr &&
         "null start_function when creating a new thread");
  tuid = std::make_pair(tid, pid);
  if (start_function) {
    stack.push_back(StackFrame(nullptr, start_function));
    pc = start_function->instructions;
    prevPC = nullptr;
  }
}

/* Debugging helper */
void Thread::dumpStack(llvm::raw_ostream &out) const {
  unsigned idx = 0;
  // If a thread is just created and no instructions have been executed. Then
  // this helper will dump the information of the next instruction to be
  // executed.
  const KInstruction *target = prevPC? prevPC: pc;
  out << "Thread " << tuid.first << ", Process " << tuid.second << ", "
      << (enabled ? "enabled" : "disabled") << '\n';
  for (stack_ty::const_reverse_iterator
         it = stack.rbegin(), ie = stack.rend();
       it != ie; ++it) {
    const StackFrame &sf = *it;
    Function *f = sf.kf->function;
    const InstructionInfo &ii = *target->info;
    out << "\t#" << idx++;
    std::stringstream AssStream;
    AssStream << std::setw(8) << std::setfill('0') << ii.assemblyLine;
    out << AssStream.str();
    out << " in " << f->getName().str() << " (";
    // Yawn, we could go up and print varargs if we wanted to.
    unsigned index = 0;
    for (Function::arg_iterator ai = f->arg_begin(), ae = f->arg_end();
         ai != ae; ++ai) {
      if (ai!=f->arg_begin()) out << ", ";

      out << ai->getName().str();
      // XXX should go through function
      ref<Expr> value = sf.locals[sf.kf->getArgRegister(index++)].value;
      if (value.get() && isa<ConstantExpr>(value))
        out << "=" << value;
    }
    out << ")";
    if (ii.file != "")
      out << " at " << ii.file << ":" << ii.line;
    out << "\n";
    target = sf.caller;
  }
}

} // namespace klee
