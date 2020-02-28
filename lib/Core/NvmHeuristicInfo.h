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
#include <queue>
#include <string>
#include <deque>
#include <memory>
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

#include "NvmAnalysisUtils.h"

/**
 *
 * TODO: (iangneal)
 * - Need to propagate return values of pointers.
 * - Assumes that every nested function call is in a separate basic block.
 * I should create this pass at some point.
 */

namespace klee {
  
  enum NvmValueState {
    NotAValue = 0b1,
    NotAPointer = 0b11,
    DoesNotContain = 0b111,
    ContainsTypeCastAddress = 0b1111,
    ContainsPointer = 0b11111,
  };

  class Hashable {
    private:
      virtual uint64_t hash(void) const = 0;
  }

  template<class X>
  class StaticStorage {    
    private:
      static_assert(std::is_base_of<Hashable, X>::value, "Stored class must be hashable!");

      struct HashFn : public unary_function<X, uint64_t> {
        uint64_t operator(const X &x) {
          return x.hash();
        } 
      }

      /**
       * This way, we don't end up with a ton of duplicates.
       */
      static std::unordered_map<X, std::shared_ptr<X>, HashFn> objects_;    
      
    protected:
      static std::shared_ptr<X> getShared(const X &x);
  };

  /**
   * This class represents the state of all values at any point during  
   * symbolic execution. If this ever changes on a fork or on the resolution
   * of a mmap call, we must recompute the overall heuristic information for
   * a given execution state.
   * 
   * This also has global variables, why not?
   */
  class NvmValueDesc : public Hashable, public StaticStorage<NvmValueDesc> {
    private:
      std::unordered_map<llvm::Value*, NvmValueState> state_;

      NvmValueDesc() {}

      override uint64_t hash(void) const;

      /**
       * 
       */
      void mutateState(llvm::Value *val, NvmValueState vs);

    public:

      /**
       * We initialize all global variables to an appropriate non-NVM value,
       * as a call to mmap must be made to change that.
       */
      static std::shared_ptr<NvmValueDesc> create(llvm::Module*);

      /**
       * Directly create a new description
       */
      std::shared_ptr<NvmValueDesc> changeState(llvm::Value *val, NvmValueState vs) const;

      /**
       * Compute a state change if we need it, otherwise returns this same value.
       */
      std::shared_ptr<NvmValueDesc> changeState(KInstruction *pc) const;

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
      typedef std::vector<std::shared_ptr<llvm::Instruction*>> stack_t;
      stack_t return_stack;

      NvmStackFrameDesc() {}

      override uint64_t hash(void) const;

    public:
      std::shared_ptr<NvmStackFrameDesc> doReturn(void) const {
        NvmStackFrameDesc ns = *this;
        ns->return_stack.pop_back();
        return getShared(ns);
      }

      std::shared_ptr<NvmStackFrameDesc> doCall(llvm::Instruction *i) const {
        NvmStackFrameDesc ns = *this;
        ns->return_stack.push_back(i);
        return getShared(ns);
      }
      
      static std::shared_ptr<NvmStackFrameDesc> empty() { 
        return getShared(NvmStackFrameDesc()); 
      }
  }

  class NvmInstructionDesc : public Hashable, public StaticStorage<NvmInstructionDesc> {
    private:

      // The real state.
      std::shared_ptr<NvmValueDesc> values_;
      std::shared_ptr<NvmStackFrameDesc> stackframe_;
      KInstruction *curr_;

      // The utility state.
      KModule *mod_;

      std::list<std::shared_ptr<NvmInstructionDesc>> successors;
      bool isTerminal = false;
      std::list<std::shared_ptr<NvmInstructionDesc>> predecessors;
      bool isEntry = false;

      override uint64_t hash(void) {
        return ((uint64_t)curr_) ^ globals.hash() ^ ptrs.hash() ^ stack.hash();
      }

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

      /* Methods for creating successor states. */
      NvmValueDesc maybeModifyValues(void);
      NvmStackFrameDesc maybeModifyStack(void);

      /* Helper functions. */

      // This creates the descriptions of the successor states, but does not
      // retrieve the actual shared successors.
      std::list<NvmInstructionDesc> constructSuccessors(void);

      void setSuccessors() {
        for (const NvmInstructionDesc &succ : constructSuccessors()) {
          std::shared_ptr<NvmInstructionDesc> sptr = getShared(succ);
          successors.push_back(sptr);
          sptr->addPredecessor(*this);
        }
      }

      // When you get the successors, you want to add yourself to your successor's
      // predecessor list.
      void addPredecessor(const NvmInstructionDesc& pred) {
        predecessors.push_back(getShared(pred));
      }

    public:

      /**
       * Essentially, the successors are speculative. They assume that:
       * (1) Only the current set of values that point to NVM do so.
       */
      const std::list<std::shared_ptr<NvmInstructionDesc>> &getSuccessors() {
        if (successors.size() == 0 && !isTerminal) {
          setSuccessors();
        }

        assert((successors.size() > 0 || isTerminal) && "Error in succ calculation!");

        return successors;
      }

      /**
       * This allows the process to be forward and backward. After we find all
       * block weights, we go from the ends back up to bubble up the overall
       * priority.
       */
      const std::list<std::shared_ptr<NvmInstructionDesc>> &getPredecessors() {
        // Predecessors are set externally. This call should only ever occur after
        // all successors have been calculated.
        assert((predecessors.size() > 0 || isEntry) && "Error in pred calculation!");

        return predecessors;
      }

      uint64_t getWeight(void) const { return weight_; };

      /**
       */
      static std::shared_ptr<NvmInstructionDesc> createEntry(KModule *m, KFunction *mainFn) {
        llvm::Instruction *inst = &(mainFn->function->getEntryBlock().front());
        KInstuction *start = mainFn->getKInstruction(inst);
        NvmInstructionDesc entryInst = NvmInstructionDesc(m, start);

        return getShared(entryInst);
      }
  }


  class NvmHeuristicInfo {
    private:
      /**
       * This is static as it is shared among all execution states. 
       */

      // This is the final heuristic value
      static std::unordered_map<std::shared_ptr<NvmInstructionDesc>, uint64_t> priority;

      std::shared_ptr<NvmInstructionDesc> current_state;

      uint64_t computePriority(std::shared_ptr<NvmInstructionDesc>);

    public:
      NvmHeuristicInfo(const llvm::Module *theModule);
      
      /**
       * May change the current_state, or may not.
       */
      void updateCurrentState(KInstruction *pc, NvmValueState state=NotAValue);

      uint64_t getCurrentPriority(void) const { return priority.at(current_state); };
  }
}
#endif //__NVM_HEURISTIC_INFO_H__
/*
 * vim: set tabstop=2 softtabstop=2 shiftwidth=2:
 * vim: set filetype=cpp:
 */
