//===-- RootCause.h ---------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_ROOT_CAUSE_H
#define KLEE_ROOT_CAUSE_H

#include <list>
#include <stdint.h>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/GlobalVariable.h"

#include "klee/Internal/Module/KInstIterator.h"
#include "klee/Internal/Module/KModule.h"

namespace klee {

  class ExecutionState;

  enum RootCauseReason {
    PM_Unpersisted,
    PM_UnnecessaryFlush,
    PM_FlushOnUnmodified
  };

  class RootCauseLocation {
    private:

      struct RootCauseStackFrame {
        KInstIterator caller;
        KFunction *kf;

        RootCauseStackFrame(const KInstIterator &c, KFunction *f)
          : caller(c), kf(f) {}
        
        bool operator==(const RootCauseStackFrame &other) const {
          return caller == other.caller && kf == other.kf;
        }
      };
  
      const llvm::Value *allocSite;
      const KInstruction *inst;
      std::list<RootCauseStackFrame> stack;
      RootCauseReason reason;

      /**
       * We maintain our own stack to check the absolute location,
       * but the stack description from ExecutionState contains argument values
       * which can be helpful for debugging.
       */
      std::string stackStr;

    public:

      struct Hash {
        uint64_t operator()(const RootCauseLocation &) const;
      };

      // We use this just to compute whether or not these locations are at the
      // same place in the assembly.ll file.
      struct LocationEq {
        bool operator()(const RootCauseLocation*, const RootCauseLocation*) const;
      };

      RootCauseLocation(const ExecutionState &state, 
                        const llvm::Value *allocationSite, 
                        const KInstruction *pc,
                        RootCauseReason r);

      std::string str() const;

      friend bool operator==(const RootCauseLocation &lhs, 
                             const RootCauseLocation &rhs);
  };

  class RootCauseManager {
    private:
      uint64_t nextId = 1lu;
      std::unordered_map<RootCauseLocation, 
                         uint64_t, 
                         RootCauseLocation::Hash> rootToId;
      std::unordered_map<uint64_t, const RootCauseLocation*> idToRoot;

    public:
      RootCauseManager() {}

      uint64_t getRootCauseLocationID(const ExecutionState &state, 
                                      const llvm::Value *allocationSite, 
                                      const KInstruction *pc,
                                      RootCauseReason);
      
      std::string getRootCauseString(uint64_t id) const;

      std::unordered_set<std::string> 
      getUniqueRootCauseStrings(const std::unordered_set<uint64_t> &ids) const;

      void clear();
  };

}

#endif // KLEE_ROOT_CAUSE_H