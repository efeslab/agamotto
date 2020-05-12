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

  class RootCauseManager;

  enum RootCauseReason {
    PM_Unpersisted,
    PM_UnnecessaryFlush,
    PM_FlushOnUnmodified,
    PM_UnnecessaryFence
  };

  struct RootCauseLocation {
    struct Hash {
      uint64_t operator()(const RootCauseLocation &) const;
    };

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

    /**
     * Sometimes, one error may mask another. We want to record the chain
     * of root causes that may be the original error.
     */
    std::unordered_set<uint64_t> maskedRoots;
    std::unordered_set<uint64_t> maskingRoots;


    RootCauseLocation(const ExecutionState &state,
                      const llvm::Value *allocationSite, 
                      const KInstruction *pc,
                      RootCauseReason r);

    void addMaskedError(uint64_t id);
    void addMaskingError(uint64_t id);

    const std::unordered_set<uint64_t> &getMaskedSet() { return maskedRoots; }

    std::string str(void) const;

    void installExampleStackTrace(const ExecutionState &state);

    std::string fullString(const RootCauseManager &mgr) const;

    const char *reasonString(void) const;

    RootCauseReason getReason(void) const { return reason; }

    friend bool operator==(const RootCauseLocation &lhs, 
                            const RootCauseLocation &rhs);
  };

  /**
   * 
   */
  class RootCauseManager {
    private:
      uint64_t nextId = 1lu;

      struct RootCauseInfo {
        RootCauseLocation rootCause;
        uint64_t occurences;
        RootCauseInfo(const RootCauseLocation &r) : rootCause(r), occurences(0) {}
      };

      std::unordered_map<RootCauseLocation, 
                         uint64_t, 
                         RootCauseLocation::Hash> rootToId;
      std::unordered_map<uint64_t, std::unique_ptr<RootCauseInfo>> idToRoot;

      uint64_t totalOccurences = 0;
      std::unordered_set<uint64_t> buggyIds;

      size_t largestStack = 0;

    public:
      RootCauseManager() {}

      uint64_t getRootCauseLocationID(const ExecutionState &state, 
                                      const llvm::Value *allocationSite, 
                                      const KInstruction *pc,
                                      RootCauseReason r);
                             
      uint64_t getRootCauseLocationID(const ExecutionState &state, 
                                      const llvm::Value *allocationSite, 
                                      const KInstruction *pc,
                                      RootCauseReason r, 
                                      const std::unordered_set<uint64_t> &ids);
      
      void markAsBug(uint64_t id);

      std::string getRootCauseString(uint64_t id) const;

      const RootCauseLocation &get(uint64_t id) const {
        return idToRoot.at(id)->rootCause;
      }

      void dumpCSV(llvm::raw_ostream &out) const;
      void dumpText(llvm::raw_ostream &out) const;

      void clear();

      std::string getSummary(void) const;
  };

}

#endif // KLEE_ROOT_CAUSE_H
