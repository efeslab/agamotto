#ifndef __NVM_HEURISTIC_INFO_H__
#define __NVM_HEURISTIC_INFO_H__

#include <stdint.h>
#include <cassert>
#include <cstdarg>
#include <functional>
#include <vector>
#include <iostream>
#include <iomanip>
#include <map>
#include <unordered_map>
#include <set>
#include <unordered_set>
#include <list>
#include <string>
#include <deque>
#include <memory>
#include <typeinfo>
#include <type_traits>

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

#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Format.h"

#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Analysis/PostDominators.h"

#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/Support/ErrorHandling.h"

#include "NvmAnalysisUtils.h"

/**
 *
 */

namespace klee {
  
  enum NvmValueState {
    DoesNotContain = 0b1,
    ContainsDerivative = 0b11,
    ContainsPointer = 0b111,
  };

  struct Hashable {
    virtual uint64_t hash(void) const = 0;

    template <class H>
    struct HashFn : public std::unary_function<H, uint64_t> {
      uint64_t operator()(const H &hashable) const {
        return hashable.hash();
      } 
    };
  };

  template<class X>
  class StaticStorage {    
    private:
      // static_assert(std::is_base_of<Hashable, X>::value, "Stored class must be hashable!");

      typedef std::unordered_map<X, std::shared_ptr<X>, Hashable::HashFn<X>> object_map_t;
      typedef std::shared_ptr<object_map_t> shared_map_t;

      /**
       * This way, we don't end up with a ton of duplicates.
       */
      static object_map_t objects_;

    protected:
      
      /**
       * Creates a shared pointer instance 
       */ 
      static std::shared_ptr<X> getShared(const X &x);

    public:
      virtual ~StaticStorage() {}
  };

  /**
   * This describes the call stack information we need for the heuristic.
   * Different than the runtime stack information.
   * 
   * All we need to do is to store the return instruction, as we will inherit
   * the value state from the return instruction.
   */
  class NvmStackFrameDesc : public Hashable, public StaticStorage<NvmStackFrameDesc> {
    private:
      typedef std::list<llvm::Instruction*> stack_t;
      stack_t caller_stack;
      stack_t return_stack;

      NvmStackFrameDesc() {}

    public:

      uint64_t hash(void) const override;

      std::shared_ptr<NvmStackFrameDesc> doReturn(void) const;

      std::shared_ptr<NvmStackFrameDesc> doCall(
          llvm::Instruction *caller, llvm::Instruction *retLoc) const;

      llvm::Instruction *getCaller(void) const {
        return caller_stack.back();
      }
      
      static std::shared_ptr<NvmStackFrameDesc> empty() { 
        return getShared(NvmStackFrameDesc()); 
      }

      friend bool operator==(const NvmStackFrameDesc &lhs, const NvmStackFrameDesc &rhs);
  };

  /**
   * This class represents the state of all values at any point during  
   * symbolic execution. If this ever changes on a fork or on the resolution
   * of a mmap call, we must recompute the overall heuristic information for
   * a given execution state.
   * 
   * It is fine for this to initialize to empty, as global variables will be 
   * added as they are modified.
   * 
   * This also has global variables, why not?
   */
  class NvmValueDesc : public Hashable, public StaticStorage<NvmValueDesc> {
    private:
      std::unordered_map<llvm::Value*, NvmValueState> state_;

      NvmValueDesc() {}

      /**
       * Do an in-place modification of the values. Used internally.
       */
      void mutateState(llvm::Value *val, NvmValueState vs);

      /**
       * Speculate on the type of value that will be returned by the instruction
       * based on the values we currently have.
       */
      NvmValueState getOutput(llvm::Instruction *i) const;

    public:

      uint64_t hash(void) const override;

      /**
       * Directly create a new description. This is generally for when we actually
       * execute and want to update our assumptions.
       */
      std::shared_ptr<NvmValueDesc> updateState(llvm::Value *val, NvmValueState vs) const;

      /**
       * Compute a state change if we need it, otherwise returns this same value.
       */
      std::shared_ptr<NvmValueDesc> speculateOnNext(
        std::shared_ptr<NvmStackFrameDesc> sf, KInstruction *pc) const;

      static std::shared_ptr<NvmValueDesc> empty(void) { return getShared(NvmValueDesc()); }

      friend bool operator==(const NvmValueDesc &lhs, const NvmValueDesc &rhs);
  };

  class NvmInstructionDesc : public Hashable, public StaticStorage<NvmInstructionDesc> {
    private:

      // The utility state.
      KModule *mod_;
      // The real state.
      KInstruction *curr_;
      std::shared_ptr<NvmValueDesc> values_;
      std::shared_ptr<NvmStackFrameDesc> stackframe_;

      std::list<std::shared_ptr<NvmInstructionDesc>> successors_;
      bool isTerminal = false;

      std::list<std::shared_ptr<NvmInstructionDesc>> predecessors_;
      bool isEntry = true;

      /**
       * Part of the heuristic calculation. This weight is the "interesting-ness"
       * of an instreuction given the current state of values in the system.
       * 
       * Weight is given to instructions which:
       * (1) Contain a call to mmap.
       * (2) Perform a write to a memory location know to be NVM.
       * (3) Perform cacheline writebacks on known NVM addresses.
       * (4) Contain memory fences.
       * 
       * We will not speculate about the return values of mmap.
       */
      uint64_t weight_;

      NvmInstructionDesc() = delete;
      NvmInstructionDesc(KModule *mod, KInstruction *location, 
                         std::shared_ptr<NvmValueDesc> values, 
                         std::shared_ptr<NvmStackFrameDesc> stackframe) 
        : mod_(mod), curr_(location), values_(values), stackframe_(stackframe) {}

      NvmInstructionDesc(KModule *mod, KInstruction *location) 
        : mod_(mod), curr_(location), 
          values_(NvmValueDesc::empty()), 
          stackframe_(NvmStackFrameDesc::empty()) {}

      /* Methods for creating successor states. */

      /* Helper functions. */

      // This creates the descriptions of the successor states, but does not
      // retrieve the actual shared successors.
      std::list<NvmInstructionDesc> constructSuccessors(void);

      void setSuccessors(
        const std::unordered_set<std::shared_ptr<NvmInstructionDesc>> &traversed);

      // When you get the successors, you want to add yourself to your successor's
      // predecessor list.
      void addPredecessor(const NvmInstructionDesc& pred) {
        predecessors_.push_back(getShared(pred));
        isEntry = false;
      }

    public:

      uint64_t hash(void) const override {
        return ((uint64_t)curr_) ^ values_->hash() ^ stackframe_->hash();
      }

      /**
       * Essentially, the successors are speculative. They assume that:
       * (1) Only the current set of values that point to NVM do so.
       * 
       * By checking against the traversed list, we do some backedge detection.
       */
      const std::list<std::shared_ptr<NvmInstructionDesc>> &getSuccessors(
        const std::unordered_set<std::shared_ptr<NvmInstructionDesc>> &traversed);

      /**
       * This allows the process to be forward and backward. After we find all
       * block weights, we go from the ends back up to bubble up the overall
       * priority.
       */
      const std::list<std::shared_ptr<NvmInstructionDesc>> &getPredecessors() {
        // Predecessors are set externally. This call should only ever occur after
        // all successors have been calculated.
        assert((predecessors_.size() > 0 || isEntry) && "Error in pred calculation!");
        return predecessors_;
      }

      uint64_t getWeight(void) const { return weight_; };

      /**
       */
      static std::shared_ptr<NvmInstructionDesc> createEntry(KModule *m, KFunction *mainFn) {
        llvm::Instruction *inst = &(mainFn->function->getEntryBlock().front());
        KInstruction *start = mainFn->getKInstruction(inst);
        NvmInstructionDesc entryInst = NvmInstructionDesc(m, start);

        return getShared(entryInst);
      }

      std::shared_ptr<NvmInstructionDesc> update(KInstruction *pc, NvmValueState state) {
        std::shared_ptr<NvmValueDesc> updated = values_->updateState(pc->inst, state);
        NvmInstructionDesc desc(mod_, curr_, updated, stackframe_);
        return getShared(desc);
      }

      std::list<std::shared_ptr<NvmInstructionDesc>> 
          getMatchingSuccessors(KInstruction *nextPC);

      friend bool operator==(const NvmInstructionDesc &lhs, const NvmInstructionDesc &rhs);
  };


  class NvmHeuristicInfo {
    private:
      /**
       * This is static as it is shared among all execution states. 
       */

      // This is the final heuristic value
      static std::unordered_map<std::shared_ptr<NvmInstructionDesc>, uint64_t> priority;

      std::shared_ptr<NvmInstructionDesc> current_state;

      void computeCurrentPriority(void);

    public:
      NvmHeuristicInfo(KModule *m, KFunction *mainFn);
      NvmHeuristicInfo(const NvmHeuristicInfo&) = default;
      
      /**
       * May change the current_state, or may not.
       */
      void updateCurrentState(KInstruction *pc, NvmValueState state=DoesNotContain);

      /**
       * Advance the current state.
       * 
       * It's fine if the current PC was a jump, branch, etc. We already computed
       * the possible successor states for ourself (without symbolic values of course).
       * If we did our job correctly, this should work fine. Otherwise, we error.
       */
      void stepState(KInstruction *nextPC);

      uint64_t getCurrentPriority(void) const { return priority.at(current_state); };
  };
}
#endif //__NVM_HEURISTIC_INFO_H__
/*
 * vim: set tabstop=2 softtabstop=2 shiftwidth=2:
 * vim: set filetype=cpp:
 */
