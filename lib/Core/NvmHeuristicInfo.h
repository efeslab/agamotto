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
#include "Memory.h"

#include "AndersenAA.h"

/**
 *
 */

namespace klee {
  class Executor;
  class ExecutionState;

  class NvmInstructionDesc;

  typedef std::shared_ptr<AndersenAAWrapperPass> andersen_sptr_t;

  class Hashable {
    private:
      mutable bool hash_initialized_ = false;
      mutable uint64_t hash_value_ = 0;
      virtual uint64_t hash(void) const = 0;
    public:
      virtual uint64_t getHash(void) const {
        if (!hash_initialized_) {
          hash_initialized_ = true;
          hash_value_ = hash();
        }

        return hash_value_;
      }

      template <class H>
      struct HashFn : public std::unary_function<H, uint64_t> {
        uint64_t operator()(const H &hashable) const {
          return hashable.getHash();
        } 
      };
  };

  template<class X>
  class StaticStorage {    
    private:
      // static_assert(std::is_base_of<Hashable, X>::value, "Stored class must be hashable!");

      typedef std::unordered_map<X, std::shared_ptr<X>, Hashable::HashFn<X>> object_map_t;
      // typedef std::shared_ptr<object_map_t> shared_map_t;

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

      static size_t getNumSharedObjs(void) { return objects_.size(); }
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
      typedef std::vector<llvm::Instruction*> stack_t;
      stack_t caller_stack;
      stack_t return_stack;
      std::shared_ptr<NvmStackFrameDesc> caller_desc;

      NvmStackFrameDesc() {}

    public:

      virtual ~NvmStackFrameDesc() = default;

      uint64_t hash(void) const override;

      size_t size(void) const { return caller_stack.size(); };

      llvm::Function *at(unsigned idx) const {
        return caller_stack[idx]->getFunction();
      }

      bool isEmpty(void) const {
        return !caller_stack.size();
      }

      std::shared_ptr<NvmStackFrameDesc> doReturn(void) const;

      std::shared_ptr<NvmStackFrameDesc> doCall(
          const std::shared_ptr<NvmStackFrameDesc> &caller_stack,
          llvm::Instruction *caller, llvm::Instruction *retLoc) const;

      llvm::Instruction *getCaller(void) const {
        return caller_stack.back();
      }

      llvm::Instruction *getReturnLocation(void) const {
        return return_stack.back();
      }
      
      static std::shared_ptr<NvmStackFrameDesc> empty() { 
        return getShared(NvmStackFrameDesc()); 
      }

      std::string str(void) const;

      friend bool operator==(const NvmStackFrameDesc &lhs, const NvmStackFrameDesc &rhs);
      friend bool operator!=(const NvmStackFrameDesc &lhs, const NvmStackFrameDesc &rhs);
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
   * 
   * ---
   * This is the runtime state.
   */
  class NvmValueDesc : public Hashable, public StaticStorage<NvmValueDesc> {
    private:
      friend class NvmInstructionDesc;
      // Here we track mmap locations to make weight calculation easier. Whether
      // or not 
      std::unordered_set<llvm::Value*> mmap_calls_;

      /**
       * We need three sets of values for a context-sensitive history:
       * 1) Global Variable State
       *  - We need this as they are accessable to all function contexts.
       * 2) Local Context State
       *  - The local context is likely to be the most influential. Also 
       *    includes the arguments to the function.
       */
      // std::unordered_map<llvm::Value*, NvmValueState> global_state_;
      // std::unordered_map<llvm::Value*, NvmValueState> arg_state_;
      // std::unordered_map<llvm::Value*, NvmValueState> local_state_;
      std::unordered_set<llvm::Value*> global_nvm_;
      std::unordered_set<llvm::Value*> local_nvm_;

      /**
       * 
       */
      bool needs_resolution_ = false;

      /**
       * Vararg functions break the current assumption about passing around
       * NVM pointers, as you can't directly track them. So, instead, we figure
       * out if an NVM value was passed as any of the var args. If so, we mark
       * calls to llvm.va_{start,copy,end} as interesting to resolve all values.
       */ 
      bool varargs_contain_nvm_;

      // Storing the caller values makes it easier to update when "executing"
      // a return instruction.
      std::shared_ptr<NvmValueDesc> caller_values_;
      llvm::CallInst *call_site_;

      NvmValueDesc() {}

    public:

      uint64_t hash(void) const override;

      /**
       * Set up the value state when performing a function call.
       * We just return an instance with the global variables and the 
       * propagated state due to the function call arguments.
       */
      std::shared_ptr<NvmValueDesc> doCall(andersen_sptr_t apa, llvm::CallInst *ci, llvm::Function *f) const;

      /** 
       * Set up the value state when doing a return.
       * This essentially just pops the "stack" and propagates the return val.
       */
      std::shared_ptr<NvmValueDesc> doReturn(andersen_sptr_t apa, llvm::ReturnInst *i) const;

      /**
       * Directly create a new description. This is generally for when we actually
       * execute and want to update our assumptions.
       */
      std::shared_ptr<NvmValueDesc> updateState(llvm::Value *val, bool nvm) const;

      /**
       * When we do an indirect function call, we can't propagate local nvm variables
       * cuz we don't know the arguments yet. This lets us do that.
       */
      std::shared_ptr<NvmValueDesc> resolveFunctionPointer(andersen_sptr_t apa, llvm::Function *f);

      bool isMmapCall(llvm::CallInst *ci) const {
        return !!mmap_calls_.count(ci);
      }

      /**
       * It is possible for a function to have var args, with one of these 
       * arguments being a pointer which points to NVM. Example: snprintf to
       * NVM.
       * 
       * We need to mark certain va_arg instructions as important to resolve.
       * These instructions will be the ones that convert a var arg into a 
       * pointer value---scalars do not matter to us.
       * 
       * Note that va_arg (http://llvm.org/docs/LangRef.html#i-va-arg) is not 
       * supported on many targets, in that case we will look for a getelementptr
       * and subsequent load.
       */
      bool isImportantVAArg(llvm::Instruction *i) const {
        if (!varargs_contain_nvm_) return false;

        if (llvm::isa<llvm::VAArgInst>(i) && 
            i->getType()->isPtrOrPtrVectorTy()) return true;
        // TODO: The getelementptr case is more annoying
        // if (llvm::LoadInstruction *li = llvm::dyn_cast<llvm::LoadInst>(i)) {
        //   if (llvm::GetElementPtrInst *gi = llvm::dyn_cast<llvm::GetElementPtrInst>(li->getPointerOperand())) {
        //     if (gi->getPointerOperandType())
        //   }
        // }

        // Conservative answer:
        if (llvm::CallInst *ci = llvm::dyn_cast<llvm::CallInst>(i)) {
          llvm::Function *f = ci->getCalledFunction();
          return (f && f->isDeclaration() &&
                  (f->getIntrinsicID() == llvm::Intrinsic::vastart ||
                   f->getIntrinsicID() == llvm::Intrinsic::vacopy ||
                   f->getIntrinsicID() == llvm::Intrinsic::vaend));
        }
        
        return false;
      }

      /**
       * The "points-to" set points to allocation sites.
       */
      bool isNvm(andersen_sptr_t apa, const llvm::Value *ptr) const {
        for (const llvm::Value *v : local_nvm_) {
          if (llvm::AliasResult::MustAlias == apa->getResult().andersenAlias(v, ptr)) {
            return true;
          }
        }

        for (const llvm::Value *v : global_nvm_) {
          if (llvm::AliasResult::MustAlias == apa->getResult().andersenAlias(v, ptr)) {
            return true;
          }
        }

        return false;
      }

      // Populate with all the calls to mmap.
      static std::shared_ptr<NvmValueDesc> staticState(llvm::Module *m);

      std::string str(void) const;

      friend bool operator==(const NvmValueDesc &lhs, const NvmValueDesc &rhs);
  };

  class NvmInstructionDesc : public Hashable, public StaticStorage<NvmInstructionDesc> {
    private:

      // The utility state.
      Executor *executor_;
      andersen_sptr_t apa_; // Andersen's whole program pointer analysis
      KModule *mod_;
      // The real state.
      KInstruction *curr_;
      std::shared_ptr<NvmValueDesc> values_;
      std::shared_ptr<NvmStackFrameDesc> stackframe_;

      std::list<std::shared_ptr<NvmInstructionDesc>> successors_;
      bool isTerminal = false;

      std::list<std::shared_ptr<NvmInstructionDesc>> predecessors_;
      bool isEntry = true;

      llvm::Function *runtime_function = nullptr;
      bool need_resolution = false;

      /**
       * Recursion prevention.
       */
      bool ready_to_recurse_ = false;
      bool can_recurse_ = false;

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
      uint64_t weight_ = 0;
      
      NvmInstructionDesc() = delete;

      NvmInstructionDesc(Executor *executor,
                         andersen_sptr_t apa,
                         KInstruction *location, 
                         std::shared_ptr<NvmValueDesc> values, 
                         std::shared_ptr<NvmStackFrameDesc> stackframe);

      NvmInstructionDesc(Executor *executor,
                         andersen_sptr_t apa, 
                         KInstruction *location);

      /* Methods for creating successor states. */

      /* Helper functions. */

      // This creates the descriptions of the successor states, but does not
      // retrieve the actual shared successors.
      std::list<NvmInstructionDesc> constructSuccessors(void);

      /**
       * All successors.
       */
      void setSuccessors();

      // When you get the successors, you want to add yourself to your successor's
      // predecessor list.
      void addPredecessor(const NvmInstructionDesc& pred) {
        predecessors_.push_back(getShared(pred));
        isEntry = false;
      }

    public:

      uint64_t hash(void) const override {
        return ((uint64_t)curr_) ^ values_->getHash() ^ stackframe_->getHash() ^ 
                std::hash<void*>{}(runtime_function);
      }

      bool isTerminator(void) const { return isTerminal; } 

      bool isValid(void) const { return !!curr_; }

      bool isCachelineModifier(ExecutionState *es);

      uint64_t calculateWeight(ExecutionState *es);

      /**
       * Essentially, the successors are speculative. They assume that:
       * (1) Only the current set of values that point to NVM do so.
       * 
       * By checking against the traversed list, we do some backedge detection.
       */
      // std::list<std::shared_ptr<NvmInstructionDesc>> getSuccessors(
      //   const std::unordered_set<std::shared_ptr<NvmInstructionDesc>> &traversed);
      std::list<std::shared_ptr<NvmInstructionDesc>> getSuccessors(
        const std::unordered_set<NvmInstructionDesc*> &traversed);

      std::list<std::shared_ptr<NvmInstructionDesc>> getSuccessors(void) {
        return successors_;
      }

      std::shared_ptr<NvmInstructionDesc> getRecursable() const {
        assert(can_recurse_ && "Trying to recurse a non-recursable function!");
        NvmInstructionDesc newDesc = *this;
        newDesc.ready_to_recurse_ = true;
        newDesc.setSuccessors();
        return getShared(newDesc);
      }

      bool isRecursable() const { return can_recurse_; }

      /**
       * 
       */
      std::list<std::shared_ptr<NvmInstructionDesc>> getUniqueSuccessors(void) {
        std::list<std::shared_ptr<NvmInstructionDesc>> uniq;

        if (!curr_) return uniq;

        llvm::Instruction *curr = curr_->inst;
        llvm::Function *cfn = curr->getFunction();
        llvm::DominatorTree dom(*cfn);

        for (std::shared_ptr<NvmInstructionDesc> &sp : successors_) {
          
          llvm::Instruction *succ = sp->curr_->inst;
          llvm::Function *sfn = succ->getFunction();

          if (cfn == sfn && dom.dominates(succ, curr)) {
            // Loop detection. Skip.
            continue;
          } else if (isa<llvm::CallInst>(curr) && 
                     cfn == sfn && 
                     values_->caller_values_ == sp->values_) {
            // Uninteresting recursion, as we have the same set of values.
            continue;
          }

          uniq.push_back(sp);
        }

        return uniq;
      }

      /**
       * This allows the process to be forward and backward. After we find all
       * block weights, we go from the ends back up to bubble up the overall
       * priority.
       */
      const std::list<std::shared_ptr<NvmInstructionDesc>> &getPredecessors() const {
        // Predecessors are set externally. This call should only ever occur after
        // all successors have been calculated.
        assert((predecessors_.size() > 0 || isEntry) && "Error in pred calculation!");
        return predecessors_;
      }

      /**
       * Loopless.
       * 
       * If the current node dominates one of it's predecessors, 
       */
      // std::list<NvmInstructionDesc> getUniquePredecessors() {
      //   std::list<std::shared_ptr<NvmInstructionDesc>> uniq;

      //   if (!curr_) return uniq;

      //   llvm::Instruction *curr = curr_->inst;
      //   llvm::Function *cfn = curr->getFunction();
      //   llvm::DominatorTree dom(*cfn);

      //   for (std::shared_ptr<NvmInstructionDesc> &pptr : predecessors_) {
      //     llvm::Instruction *pred = pptr->curr_->inst;
      //     llvm::Function *pfn = pred->getFunction();

      //     if (cfn == pfn && dom.dominates(curr, pred)) {
      //       // Loop detection. Skip.
      //       continue;
      //     } else if (isa<llvm::CallInst>(curr) && 
      //                cfn == pfn && 
      //                values_->caller_values_ == pptr->values_) {
      //       // Uninteresting recursion, as we have the same set of values.
      //       continue;
      //     }

      //     uniq.push_back(pptr);
      //   }

      //   return uniq;
      // }

      uint64_t getWeight(void) const { return weight_; };

      llvm::Instruction *inst(void) const { return curr_->inst; }

      KInstruction *kinst(void) const { return curr_; }

      void setPC(ExecutionState *es, KInstruction *pc) {
        assert(!isValid() && "Trying to modify a valid instruction!");
        curr_ = pc;
        weight_ = calculateWeight(es);
      }

      /**
       */

      std::shared_ptr<NvmInstructionDesc> update(KInstruction *pc, bool isNvm) {
        std::shared_ptr<NvmValueDesc> updated = values_->updateState(pc->inst, isNvm);
        NvmInstructionDesc desc(executor_, apa_, curr_, updated, stackframe_);
        return getShared(desc);
      }

      bool needsResolution(void) const {
        return need_resolution && !runtime_function;
      }

      std::shared_ptr<NvmInstructionDesc> resolve(KInstruction *nextPC) const {
        assert(need_resolution && !runtime_function && "Bad resolve call!");
        llvm::Function *f = nextPC->inst->getFunction();
        // llvm::errs() << "\tresolving to: " << f->getName() << "\n";

        NvmInstructionDesc newDesc = *this; // explicit copy
        newDesc.runtime_function = f;
        newDesc.need_resolution = false;

        newDesc.setSuccessors();
        auto sptr = getShared(newDesc);

        assert(sptr->runtime_function == newDesc.runtime_function);
        assert(sptr->need_resolution == newDesc.need_resolution);
        sptr->isTerminal = false;
        // assert(*sptr == newDesc);
        // assert(sptr.get() != this);
        assert(sptr->successors_.size());
        assert(sptr->isValid());
        assert(!sptr->isTerminal);
        assert(!sptr->isTerminator());
        return sptr;
      }

      std::list<std::shared_ptr<NvmInstructionDesc>> 
          getMatchingSuccessors(KInstruction *nextPC);

      std::string str(void) const;

      static std::shared_ptr<NvmInstructionDesc> createEntry(Executor *executor, KFunction *mainFn);

      /**
       * When we have runtime knowledge.
       */
      // static void attach(std::shared_ptr<NvmInstructionDesc> current, std::shared_ptr<NvmInstructionDesc> successor);

      friend bool operator==(const NvmInstructionDesc &lhs, const NvmInstructionDesc &rhs);
      friend bool operator!=(const NvmInstructionDesc &lhs, const NvmInstructionDesc &rhs);
  };


  class NvmHeuristicInfo {
    private:
      /**
       * This is static as it is shared among all execution states. 
       */

      // This is the final heuristic value
      std::unordered_map<NvmInstructionDesc*, uint64_t> priority;

      std::shared_ptr<NvmInstructionDesc> current_state;

      /**
       * The nice part about the down and up traversal is that, in theory, if 
       * we change the weights in a subtree, the overall change in priority 
       * will be appropriately propagated up the tree.
       */
      void computeCurrentPriority(ExecutionState *es);

    public:
      NvmHeuristicInfo(Executor *executor, KFunction *mainFn, ExecutionState *es);
      NvmHeuristicInfo(const NvmHeuristicInfo&) = default;
      
      /**
       * May change the current_state, or may not.
       */
      void updateCurrentState(ExecutionState *es, KInstruction *pc, bool isNvm);

      /**
       * Advance the current state.
       * 
       * It's fine if the current PC was a jump, branch, etc. We already computed
       * the possible successor states for ourself (without symbolic values of course).
       * If we did our job correctly, this should work fine. Otherwise, we error.
       * 
       * The only case we currently don't handle well is interprocedurally 
       * generated function pointers. We will resolve them at runtime.
       * 
       * In stepState, we also want check if we modified any persistent state.
       */
      void stepState(ExecutionState *es, KInstruction *pc, KInstruction *nextPC);

      llvm::Instruction *currentInst(void) const {
        return current_state->inst();
      }

      uint64_t getCurrentPriority(void) const {
        if (priority.find(current_state.get()) == priority.end()) {
          llvm::errs() << "Pre-trap\n";
          llvm::errs() << current_state->str() << "\n"; 
          dumpState();
        }
        return priority.at(current_state.get()); 
      };

      uint64_t getCurrentWeight(void) const {
        return current_state->getWeight();
      }

      bool isCurrentTerminator(void) const {
        return current_state->isTerminator();
      }

      void dumpState(void) const {
        llvm::errs() << "##################################################\n";
        for (const auto &pair : priority) {
          llvm::errs() << "Instruction:\n" << pair.first->str();
          llvm::errs() << "\nWeight: " << pair.first->getWeight();
          llvm::errs() << "\nPriority: " << pair.second << "\n";
        }
        llvm::errs() << "##################################################\n";
      }
  };
}
#endif //__NVM_HEURISTIC_INFO_H__
/*
 * vim: set tabstop=2 softtabstop=2 shiftwidth=2:
 * vim: set filetype=cpp:
 */
