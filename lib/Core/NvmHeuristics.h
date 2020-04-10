#ifndef __NVM_HEURISTICS_H__
#define __NVM_HEURISTICS_H__

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

  class NvmStackFrameDesc;
  class NvmValueDesc;
  class NvmInstructionDesc;

  typedef std::shared_ptr<AndersenAAWrapperPass> andersen_sptr_t;
  // typedef std::shared_ptr<NvmStackFrameDesc> nsf_sptr_t;
  // typedef std::shared_ptr<NvmValueDesc> nv_sptr_t;
  // typedef std::shared_ptr<NvmInstructionDesc> ni_sptr_t;



  /**
   * This describes the call stack information we need for the heuristic.
   * Different than the runtime stack information.
   * 
   * All we need to do is to store the return instruction, as we will inherit
   * the value state from the return instruction.
   */
  /* #region NvmStackFrameDesc definition */
  class NvmStackFrameDesc final {
    private:
      std::shared_ptr<NvmStackFrameDesc> caller_desc;
      llvm::Instruction *caller_inst;
      llvm::Instruction *return_inst;

      NvmStackFrameDesc() : caller_desc(nullptr),
                            caller_inst(nullptr),
                            return_inst(nullptr) {}

      NvmStackFrameDesc(const std::shared_ptr<NvmStackFrameDesc> &caller_stack,
                        llvm::Instruction *caller, 
                        llvm::Instruction *retLoc) : caller_desc(caller_stack),
                                                     caller_inst(caller),
                                                     return_inst(retLoc) {}

    public:

      bool isEmpty(void) const { return !caller_inst; }

      llvm::Instruction *getCaller(void) const { return caller_inst; }

      llvm::Instruction *getReturnLocation(void) const { return return_inst; }

      bool containsFunction(llvm::Function *f) const;

      std::shared_ptr<NvmStackFrameDesc> doReturn(void) const;

      std::shared_ptr<NvmStackFrameDesc> doCall(const std::shared_ptr<NvmStackFrameDesc> &caller_stack,
                                                llvm::Instruction *caller, 
                                                llvm::Instruction *retLoc) const;

      std::string str(void) const;

      void dump(void) const { llvm::errs() << str() << "\n"; }

      static std::shared_ptr<NvmStackFrameDesc> empty() { 
        return std::make_shared<NvmStackFrameDesc>(NvmStackFrameDesc()); 
      }

      friend bool operator==(const NvmStackFrameDesc &lhs, const NvmStackFrameDesc &rhs);
      friend bool operator!=(const NvmStackFrameDesc &lhs, const NvmStackFrameDesc &rhs);
  };
  /* #endregion */

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
  /* #region NvmValueDesc */
  class NvmValueDesc final {
    private:
      friend class NvmInstructionDesc;
      // Here we track mmap locations to make weight calculation easier. Whether
      // or not 
      std::unordered_set<llvm::Value*> mmap_calls_;

      /**
       * We conservatively assume that any modification site that points to one
       * of the "mmap" calls is an NVM-modifying instruction
       * 
       * We count do global and local to avoid propagating unnecessary 
       */
      std::unordered_set<llvm::Value*> not_local_nvm_, not_global_nvm_;

      bool mayPointTo(andersen_sptr_t apa, const llvm::Value *a, const llvm::Value *b) const;

      bool pointsToIsEq(andersen_sptr_t apa, const llvm::Value *a, const llvm::Value *b) const;

      bool matchesKnownVolatile(andersen_sptr_t apa, const llvm::Value *posNvm) const;

      /**
       * Vararg functions break the current assumption about passing around
       * NVM pointers, as you can't directly track them. So, instead, we figure
       * out if an NVM value was passed as any of the var args. If so, we mark
       * calls to llvm.va_{start,copy,end} as interesting to resolve all values.
       */ 
      // bool varargs_contain_nvm_;

      // Storing the caller values makes it easier to update when "executing"
      // a return instruction.
      std::shared_ptr<NvmValueDesc> caller_values_;
      llvm::CallInst *call_site_;

      NvmValueDesc() {}

    public:

      /**
       * Set up the value state when performing a function call.
       * We just return an instance with the global variables and the 
       * propagated state due to the function call arguments.
       */
      std::shared_ptr<NvmValueDesc> doCall(andersen_sptr_t apa, 
                                           llvm::CallInst *ci, 
                                           llvm::Function *f=nullptr) const;

      /** 
       * Set up the value state when doing a return.
       * This essentially just pops the "stack" and propagates the return val.
       */
      std::shared_ptr<NvmValueDesc> doReturn(andersen_sptr_t apa, 
                                             llvm::ReturnInst *i) const;

      /**
       * Directly create a new description. This is generally for when we actually
       * execute and want to update our assumptions.
       */
      std::shared_ptr<NvmValueDesc> updateState(llvm::Value *val, 
                                                bool nvm) const;

      /**
       * When we do an indirect function call, we can't propagate local nvm variables
       * cuz we don't know the arguments yet. This lets us do that.
       */
      std::shared_ptr<NvmValueDesc> resolveFunctionPointer(andersen_sptr_t apa, 
                                                           llvm::Function *f);

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
        // if (!varargs_contain_nvm_) return false;

        // if (llvm::isa<llvm::VAArgInst>(i) && 
        //     i->getType()->isPtrOrPtrVectorTy()) return true;
        // // TODO: The getelementptr case is more annoying
        // // if (llvm::LoadInstruction *li = llvm::dyn_cast<llvm::LoadInst>(i)) {
        // //   if (llvm::GetElementPtrInst *gi = llvm::dyn_cast<llvm::GetElementPtrInst>(li->getPointerOperand())) {
        // //     if (gi->getPointerOperandType())
        // //   }
        // // }

        // // Conservative answer:
        // // By catching vaend, we will get all va_arg calls.
        // if (llvm::CallInst *ci = llvm::dyn_cast<llvm::CallInst>(i)) {
        //   llvm::Function *f = ci->getCalledFunction();
        //   return (f && f->isDeclaration() &&
        //           (f->getIntrinsicID() == llvm::Intrinsic::vastart ||
        //            f->getIntrinsicID() == llvm::Intrinsic::vacopy ||
        //            f->getIntrinsicID() == llvm::Intrinsic::vaend));
        // }
        
        return false;
      }

      /**
       * The "points-to" set points to allocation sites.
       */
      bool isNvm(andersen_sptr_t apa, const llvm::Value *ptr) const;

      std::string str(void) const;

      // Populate with all the calls to mmap.
      static std::shared_ptr<NvmValueDesc> staticState(llvm::Module *m);

      friend bool operator==(const NvmValueDesc &lhs, const NvmValueDesc &rhs);
  };
  /* #endregion */

  /**
   * Sparse.
   * 
   * Each NvmInstructionDesc contains successors, which are the set of reachable
   * instructions that follow it which have some weight. If the successor set
   * is empty, that means this is the last instruction in a path that is worth
   * executing for our purposes.
   * 
   * A more precise tree can be kept (tracking every instruction), but this 
   * takes up much more space. Under this model, we store <1% of the data as 
   * tracking NvmInstructionDesc for each instruction in the system. In the old
   * system, tracking these states was responsible for 98% of klee's overhead.
   * 
   * Terminology: "Scion" is any descendant for any given instruction, 
   * regardless of whether or not it is interesting. A "successor" is an 
   * descendant which has a positive weight.
   */
  class NvmInstructionDesc {
    private:

      // The utility state.
      Executor *executor_;
      andersen_sptr_t apa_; // Andersen's whole program pointer analysis
      KModule *mod_;
      // The real state.
      KInstruction *curr_; // The 
      std::shared_ptr<NvmValueDesc> values_;
      std::shared_ptr<NvmStackFrameDesc> stackframe_;

      std::list<std::shared_ptr<NvmInstructionDesc>> successors_;
      std::list<std::shared_ptr<NvmInstructionDesc>> predecessors_;

      // This is the set of valid instructions between this state and it's
      // predecessors. We leave this as an unordered set to accomodate loops,
      // which may iterate many times.
      std::unordered_set<llvm::Instruction*> path_;

      llvm::Function *runtime_function_ = nullptr;
      bool need_resolution_ = false;

      /**
       * Recursion prevention.
       */
      bool ready_to_recurse_ = false;
      bool can_recurse_ = false;

      /** 
       * Laziness triggers.
       * - We need to be lazy in a lot of cases for speed.
       */
      bool force_special_handler_call_fall_over_ = false;

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
      bool weight_init_ = false;
      uint64_t weight_ = 0;
      uint64_t priority_ = 0;

      NvmInstructionDesc() = delete;

      NvmInstructionDesc(Executor *executor,
                         andersen_sptr_t apa,
                         KInstruction *location, 
                         std::shared_ptr<NvmValueDesc> values, 
                         std::shared_ptr<NvmStackFrameDesc> stackframe,
                         const std::unordered_set<llvm::Instruction*> &currpath);

      NvmInstructionDesc(Executor *executor,
                         andersen_sptr_t apa, 
                         KInstruction *entry);

      /* Methods for creating successor states. */

      /* Helper functions. */

      /**
       * Construct the default and immediate scion for
       */
      NvmInstructionDesc constructDefaultScion(void) const;

      /**
       * Calculates the weight without modifying anything.
       * 
       * This differs between static and dynamic as static has no concept of 
       * unresolved functions.
       */
      virtual uint64_t calculateWeight(ExecutionState *es) const;

    public:

      struct HashFn : public std::unary_function<NvmInstructionDesc, uint64_t> {
        uint64_t operator()(const NvmInstructionDesc &desc) const {
          return std::hash<void*>{}(desc.curr_) ^ 
                 std::hash<void*>{}(desc.values_.get()) ^
                 std::hash<void*>{}(desc.stackframe_.get());
        } 
      };

      struct FullEq : public std::equal_to<NvmInstructionDesc> {
        bool operator()(const NvmInstructionDesc &lhs, 
                        const NvmInstructionDesc &rhs) const {
          return lhs == rhs && lhs.path_ == rhs.path_;
        }
      };

      typedef std::shared_ptr<NvmInstructionDesc> Shared;
      typedef std::unordered_set<NvmInstructionDesc, HashFn> UnorderedSet;
      typedef std::unordered_set<NvmInstructionDesc, HashFn, FullEq> PathCheckSet;
      typedef std::unordered_set<std::shared_ptr<NvmInstructionDesc>> SharedUnorderedSet;
      typedef std::list<NvmInstructionDesc> List;
      typedef std::list<std::shared_ptr<NvmInstructionDesc>> SharedList;

      virtual ~NvmInstructionDesc() = default;

      // This is a convenient storage variable to make the calculation easier
      NvmInstructionDesc::Shared workingPredecessor;

      /* inline getters */
      bool isTerminator(void) const { return !successors_.size(); } 

      bool isEntry(void) const { return !predecessors_.size(); }

      bool isValid(void) const { return !!curr_; }

      bool isRecursable(void) const { return can_recurse_; }

      bool forceFallOverUnset(void) const { return !force_special_handler_call_fall_over_; }

      uint64_t getWeight(void) const { 
        assert(weight_init_);
        return weight_;
      }

      uint64_t getPriority(void) const { return priority_; }

      llvm::Instruction *inst(void) const { return curr_->inst; }

      KInstruction *kinst(void) const { return curr_; }

      bool needsResolution(void) const { return need_resolution_ && !runtime_function_; }

      /**
       * This allows the process to be forward and backward. After we find all
       * block weights, we go from the ends back up to bubble up the overall
       * priority.
       */
      std::list<std::shared_ptr<NvmInstructionDesc>> &getPredecessors() {
        // Predecessors are set externally. This call should only ever occur after
        // all successors have been calculated.
        return predecessors_;
      }

      std::list<std::shared_ptr<NvmInstructionDesc>> &getSuccessors() { 
        return successors_; 
      }

      void addToPath(llvm::Instruction* prior) {
        path_.insert(prior);
      }

      void addToPath(NvmInstructionDesc &other) {
        assert(curr_ == other.curr_);
        path_.insert(other.path_.begin(), other.path_.end());
      }

      bool pathContains(llvm::Instruction *i) {
        return path_.count(i);
      }


      void resetPath(void) {
        path_.clear();
        path_.insert(curr_->inst);
      }

      void dumpPath(llvm::Instruction *i) {
        for (auto *ip : path_) {
          llvm::errs() << "\tpath inst" << *ip;
          if (ip == i) {
            llvm::errs() << "equals";
          } else {
            llvm::errs() << "does not equal";
          }
          llvm::errs() << *i << "\n";
        }
      }

      /* out-line getters */

      bool isCachelineModifier(ExecutionState *es) const;

      /**
       * This creates the descriptions of the immediate scion states. Not all
       * of these will go on to become true successors, as many will have a 
       * weight of 0. However, we must construct these so we can iterate through
       * and construct the states we actually care about.
       * 
       * Does all the loop checking, recursion checking, etc.
       * 
       * Does some modifications
       */
      virtual NvmInstructionDesc::UnorderedSet constructScions(void);

      /**
       * Creates a version of the instruction description with recursable 
       * set true.
       */
      std::shared_ptr<NvmInstructionDesc> getRecursable() const;

      /**
       * Creates a version of the instruction description with special
       * function handler fall over enabled. 
       * 
       * Note, you can replace successors with this new one to safe effort of 
       * specialFunctionHandler resolution across execution states.
       */
      std::shared_ptr<NvmInstructionDesc> getForceFallOver() const;

      /**
       */

      std::shared_ptr<NvmInstructionDesc> update(KInstruction *pc, bool isNvm) {
        std::shared_ptr<NvmValueDesc> up = values_->updateState(pc->inst, isNvm);
        NvmInstructionDesc nd(executor_, apa_, curr_, up, stackframe_, path_);
        return std::make_shared<NvmInstructionDesc>(nd);
      }

      std::shared_ptr<NvmInstructionDesc> resolve(KInstruction *nextPC) const;

      /* modifiers */

      /**
       * Try to update the weight. Return true if the current weight was changed
       * during this process.
       * 
       * Also updates priority (gives us the base case).
       */
      bool updateWeight(ExecutionState *es) {
        // Check if this will need resolution
        assert(curr_ && curr_->inst);
        if (llvm::CallInst *ci = dyn_cast<llvm::CallInst>(curr_->inst)) {
          need_resolution_ = (!ci->getCalledFunction()) && ci->isIndirectCall();
        }

        uint64_t new_weight = calculateWeight(es);
        bool changed = weight_ == new_weight || !weight_init_;
        priority_ = (priority_ - weight_) + new_weight;
        weight_ = new_weight;
        weight_init_ = true;
        return changed;
      }

      /**
       * Sets the current priority based on the current successors. Returns 
       * true if the priority was changed, false if it is the same.
       */
      bool setPriority(uint64_t new_priority) {
        bool changed = priority_ == new_priority;
        priority_ = new_priority;
        return changed;
      }
      
      /* helpers */

      /**
       * Generate a string representation of this instruction description.
       * Used for debugging, so it is not well optimized.
       */
      std::string str(void) const;

      /* statics */

      /**
       * Create/remove the link between two instruction descriptions which are 
       * significant. Return true if changes are made.
       */
      static bool attach(std::shared_ptr<NvmInstructionDesc> &pptr,
                         std::shared_ptr<NvmInstructionDesc> &sptr);

      static bool detach(std::shared_ptr<NvmInstructionDesc> &pptr,
                         std::shared_ptr<NvmInstructionDesc> &sptr);

      static bool replace(std::shared_ptr<NvmInstructionDesc> &pptr,
                          std::shared_ptr<NvmInstructionDesc> &optr,
                          std::shared_ptr<NvmInstructionDesc> &nptr);
      
      /**
       * Create the first NvmInstructionDesc instance. This is the only entry
       * point to create an instruction from scratch.
       */
      static std::shared_ptr<NvmInstructionDesc> createEntry(Executor *executor, KFunction *mainFn);

      /**
       * These are likely unnecessary
       */
      friend bool operator==(const NvmInstructionDesc &lhs, const NvmInstructionDesc &rhs);
      friend bool operator!=(const NvmInstructionDesc &lhs, const NvmInstructionDesc &rhs);
  };

  // Two concrete initializations of NvmInstructionDesc:
  // -- Static version
  // -- Dynamic version


  /**
   * This is per state.
   */
  class NvmHeuristicInfo {
    private:

      /**
       * The priority of the ExecutionState is max(current_states).
       * 
       * We continue under the assumption that we could be in any of these
       * states until we execute an instruction that corresponds to one of these
       * states.
       * 
       * We occasionally want to update these states. We do these in-place, so
       * we don't affect other states.
       */
      std::shared_ptr<NvmInstructionDesc> last_state_ = nullptr; // For SFH
      // List to allow mutability
      std::list<std::shared_ptr<NvmInstructionDesc>> current_states_;
      uint64_t current_priority_ = 0;

      /**
       * Returns the new current state
       */
      NvmInstructionDesc::SharedList doComputation(ExecutionState *es, 
                                                   NvmInstructionDesc::SharedList initial);

      /**
       * We halt propagation at the current instructions, because we don't want
       * to taint the earlier tree, as other ExecutionStates may use those as
       * their blank slate of evaluation.
       */
      void computeCurrentPriority(ExecutionState *es);

    public:
      NvmHeuristicInfo(Executor *executor, KFunction *mainFn, ExecutionState *es);

      uint64_t getCurrentPriority(void) const { 
        return current_priority_; 
      }

      bool isCurrentTerminator(void) const {
        if (last_state_) return last_state_->isTerminator();
        return current_states_.size() == 0;
      }

      /**
       * Checks that the current PC is in the set of currently considered 
       * instructions. This can be used in PersistentState when a modification
       * to NVM occurs.
       */
      void assertIsImportantInstruction(KInstruction *pc) const;

      /**
       * May change one of the current states, or could not.
       */
      void updateCurrentState(ExecutionState *es, KInstruction *pc, bool isNvm);

      /**
       * Advance the current state, if we can.
       * 
       * It's fine if the current PC was a jump, branch, etc. We already computed
       * the possible successor states for ourself (without symbolic values of course).
       * If we did our job correctly, this should work fine. Otherwise, we error.
       * 
       * The only case we currently don't handle well is interprocedurally 
       * generated function pointers. We will resolve them at runtime.
       * 
       * In stepState, we also want check if we modified any persistent state.
       * 
       * We need the current PC to resolve when we execute one of our possible
       * states. We need the next PC to resolve function pointers.
       */
      void stepState(ExecutionState *es, KInstruction *pc, KInstruction *nextPC);
  };
}
#endif //__NVM_HEURISTICS_H__
/*
 * vim: set tabstop=2 softtabstop=2 shiftwidth=2:
 * vim: set filetype=cpp:
 */
