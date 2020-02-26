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
      virtual uint64_t hash(void) = 0;
  }

  template<class X>
  class StaticStorage {
    static_assert(std::is_base_of<Hashable, X>::value, "Stored class must be hashable!");
    private:

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

      std::shared_ptr<X> getShared(const X &x) {
        if (objects_[x]) return objects_[x];
        
        objects_[x] = std::shared_ptr<X>::make_shared(x);
        return objects_[x];
      } 

    public:

  };

  class NvmValueDesc

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

      NvmValueDesc();

      override uint64_t hash(void) {
        uint64_t hash_value = 0;
        uint64_t count = 0;
        for (const auto &p : state_) {
          uint64_t val = (uint64_t)p.second;
          hash_value ^= (val << count) | (val >> (64llu - count));
          count++;
        }

        return hash_value;
      }

      void mutateState(llvm::Value *val, NvmValueState vs) {
        state_[val] = vs;
      }

    public:

      /**
       * We initialize all global variables to an appropriate non-NVM value,
       * as a call to mmap must be made to change that.
       */
      static std::shared_ptr<NvmValueDesc> create(llvm::Module*);

      /**
       * Directly create a new description
       */
      std::shared_ptr<NvmValueDesc> changeState(llvm::Value *val, NvmValueState vs) {
        NvmValueDesc vd = *this;
        gvd.mutateState(val, vs);
        return getShared(vd);
      }

      /**
       * Compute a state change if we need it, otherwise returns this same value.
       */
      std::shared_ptr<NvmValueDesc> changeState(KInstruction *pc) {

      }

  };

  class NvmHeuristicBasicBlockWrapper;


  /**
   * This describes the call stack information we need for the heuristic.
   * Different than the runtime stack information.
   */
  class NvmStackFrameDesc : public Hashable, public StaticStorage<NvmStackFrameDesc> {
    private:
      // A vector of return basic blocks
      typedef std::vector<std::shared_ptr<NvmHeuristicBasicBlockWrapper>> stack_t;
      stack_t return_stack;

      NvmStackFrameDesc() {}

      override uint64_t hash(void) {
        uint64_t hash_value = 0;
        for (uint64_t i = 0; i < return_stack.size(); ++i) {
          ptr_val = ((uint64_t)return_stack[i].get())
          hash_value ^= (ptr_val << i) | (ptr_val >> (64llu - i)); // Rotational shift.
        }

        return hash_value;
      }

    public:
      NvmStackFrameDesc doReturn(void) const {
        NvmStackFrameDesc ns = *this;
        ns->return_stack.pop_back();
        return getShared(ns);
      }

      NvmStackFrameDesc doCall(std::shared_ptr<NvmHeuristicBasicBlockWrapper> ptr) const {
        NvmStackFrameDesc ns = *this;
        ns->return_stack.push_back(ptr);
        return getShared(ns);
      }

      /**
       * We could either do this or allow the default constructor. Personally,
       * I like this better as it's more semantically meaningful. Also, it 
       * will give us erros if we don't construct the wrapper class correctly,
       * which is nice.
       */
      static NvmStackFrameDesc empty() { return NvmStackFrameDesc(); }
  }

  class NvmHeuristicBasicBlockWrapper : public Hashable, public StaticStorage<NvmStackFrameDesc> {
    private:

      NvmValueDesc values_;
      NvmStackFrameDesc stackframe_;
      const llvm::BasicBlock *block_;

      std::list<std::shared_ptr<NvmHeuristicBasicBlockWrapper>> successors;
      std::list<std::shared_ptr<NvmHeuristicBasicBlockWrapper>> predecessors;

      struct HashFn : public std::unary_function<NvmHeuristicBasicBlockWrapper, uint64_t> {
        uint64_t operator()(const NvmHeuristicBasicBlockWrapper &blkw) {
          return ((uint64_t)block_) ^ globals.hash() ^ ptrs.hash() ^ stack.hash();
        }
      }

      /**
       * Part of the heuristic calculation. This weight is the "interesting-ness"
       * of the underlying basic block given the current state of values in the system.
       * 
       * Weight is given to basic blocks which:
       * (1) Contain a call to mmap.
       * (2) Perform a write to a memory location know to be NVM.
       * (3) Perform cacheline writebacks on known NVM addresses.
       * (4) Contain memory fences.
       * 
       * We will not speculate about the return values of mmap.
       */
      uint64_t weight_;

      NvmHeuristicBasicBlockWrapper();

      /* Methods for creating successor states. */
      NvmValueDesc maybeModifyValues(void);
      NvmStackFrameDesc maybeModifyStack(void);

    public:

      /**
       * Essentially, the successors are speculative. They assume that:
       * (1) Only the current set of values that point to NVM do so.
       */
      std::list<std::shared_ptr<NvmHeuristicBasicBlockWrapper>> getSuccessors();

      /**
       * This allows the process to be forward and backward. After we find all
       * block weights, we go from the ends back up to bubble up the overall
       * priority.
       */
      std::list<std::shared_ptr<NvmHeuristicBasicBlockWrapper>> getPredecessors();

      static std::shared_ptr<NvmHeuristicBasicBlockWrapper> createEntry(llvm::Module*);

      uint64_t getWeight(void) const { return weight_; };
  }


  class NvmHeuristicInfo {
    private:
      /**
       * This is static as it is shared among all execution states. 
       */

      // This is the final heuristic value
      static std::unordered_map<std::shared_ptr<NvmHeuristicBasicBlockWrapper>, uint64_t> priority;

      std::shared_ptr<NvmHeuristicBasicBlockWrapper> current_state;

      uint64_t computePriority(std::shared_ptr<NvmHeuristicBasicBlockWrapper>);

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
