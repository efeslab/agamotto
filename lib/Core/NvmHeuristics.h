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
  typedef std::shared_ptr<AndersenAAWrapperPass> SharedAndersen;
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

  // Two concrete initializations of NvmInstructionDesc:
  // -- Static version
  // -- Dynamic version


  /* #region NvmHeuristicInfo (super class) */
  /**
   * This is per state.
   */
  class NvmHeuristicBuilder;

  class NvmHeuristicInfo {
    friend class NvmHeuristicBuilder;
    protected:

      typedef std::unordered_set<const llvm::Value*> ValueSet;
      typedef std::vector<const llvm::Value*> ValueVector;

      static SharedAndersen createAndersen(llvm::Module &m);
      static ValueSet getNvmAllocationSites(llvm::Module *m, const SharedAndersen &ander);
      
      virtual void computePriority() = 0;

      virtual bool needsRecomputation() const = 0;

    public:

      static bool isNvmAllocationSite(llvm::Module *m, const llvm::Value *v);
      static bool isStore(llvm::Instruction *i);
      static bool isFlush(llvm::Instruction *i);
      static bool isFence(llvm::Instruction *i);
    
      virtual ~NvmHeuristicInfo() = 0;

      virtual uint64_t getCurrentPriority(void) const = 0;

      /**
       * May change one of the current states, or could not.
       */
      virtual void updateCurrentState(ExecutionState *es, KInstruction *pc, bool isNvm) = 0;

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
      virtual void stepState(ExecutionState *es, KInstruction *pc, KInstruction *nextPC) = 0;

      virtual void dump(void) const = 0;
  };
  /* #endregion */

  /* #region NvmStaticHeuristic */
  class NvmStaticHeuristic : public NvmHeuristicInfo {
    friend class NvmHeuristicBuilder;
    protected:
      typedef std::unordered_map<llvm::Instruction*, uint64_t> WeightMap;
      typedef std::shared_ptr<WeightMap> SharedWeightMap;

      Executor *executor_;
      SharedAndersen analysis_; // Andersen's whole program pointer analysis

      SharedWeightMap weights_;
      SharedWeightMap priorities_;

      void resetWeights(void) {
        weights_ = std::make_shared<WeightMap>();
        priorities_ = std::make_shared<WeightMap>();
      }

      llvm::Instruction *curr_;

      llvm::Module *module_;
      ValueSet nvmSites_;

      NvmStaticHeuristic(Executor *executor, KFunction *mainFn);

      bool isNvmAllocSite(llvm::Instruction *i) const {
        return nvmSites_.count(i);
      }

      virtual bool mayHaveWeight(llvm::Instruction *i) const {
        return isStore(i) || isFlush(i) || isFence(i) || isNvmAllocSite(i);
      }

      virtual const ValueSet &getCurrentNvmSites() const { return nvmSites_; }

      virtual bool modifiesNvm(llvm::Instruction *i) const;

      /**
       * Calculate what the weight of this instruction would be.
       */
      virtual uint64_t computeInstWeight(llvm::Instruction *i) const;

      virtual void computePriority() override;

      virtual bool needsRecomputation() const override { return false; }

    public:

      NvmStaticHeuristic(const NvmStaticHeuristic &other) = default;

      virtual ~NvmStaticHeuristic() {}

      virtual uint64_t getCurrentPriority(void) const override {
        uint64_t priority = priorities_->count(curr_) ? priorities_->at(curr_) : 0lu;
        if (!priority) {
          // llvm::errs() << curr_->getFunction()->getName() << '\n';
          // llvm::errs() << *curr_ << '\n';
        }
        return priority;
      };

      /**
       * May change one of the current states, or could not.
       */
      virtual void updateCurrentState(ExecutionState *es, 
                                      KInstruction *pc, 
                                      bool isNvm) override {}

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
      virtual void stepState(ExecutionState *es, 
                             KInstruction *pc, 
                             KInstruction *nextPC) override {
        curr_ = nextPC->inst;
      }

      virtual void dump(void) const override;
  };

  /* #endregion */

  /* #region NvmInsensitiveDynamicHeuristic */
  class NvmInsensitiveDynamicHeuristic : public NvmStaticHeuristic {
    friend class NvmHeuristicBuilder;
    protected:
      
      ValueSet activeNvmSites_;
      ValueSet knownVolatiles_;

      NvmInsensitiveDynamicHeuristic(Executor *executor, KFunction *mainFn) 
        : NvmStaticHeuristic(executor, mainFn) {}

      virtual const ValueSet &getCurrentNvmSites() const override { 
        return activeNvmSites_;
      }

      /**
       * Since we track volatiles, we know something modifies NVM if:
       * - It can point to an active NVM allocation.
       * - There is no known volatile that has the same points-to set.
       */
      virtual bool modifiesNvm(llvm::Instruction *i) const override;

      virtual bool needsRecomputation() const override;

    public:

      NvmInsensitiveDynamicHeuristic(const NvmInsensitiveDynamicHeuristic &other) = default;

      virtual ~NvmInsensitiveDynamicHeuristic() {}

      /**
       * May change one of the current states, or could not.
       */
      virtual void updateCurrentState(ExecutionState *es, 
                                      KInstruction *pc, 
                                      bool isNvm) override;

      virtual void dump(void) const override;
  };

  /* #endregion */

  /* #region NvmHeuristicBuilder */
  class NvmHeuristicBuilder {
    private:
      static const char *typeNames[];

      static const char *typeDesc[];

    public:
      NvmHeuristicBuilder() = delete;

      enum Type {
        None = 0,
        Static,
        InsensitiveDynamic,
        ContextDynamic,
        Invalid
      };

      static const char *stringify(Type t) {
        return typeNames[t];
      }

      static const char *explanation(Type t) {
        return typeDesc[t];
      }

      static Type toType(const char *tStr) {
        for (int t = None; t < Invalid; ++t) {
          if (tStr == typeNames[t]) {
            return (Type)t;
          }
        }
        return Invalid;
      }

      static std::shared_ptr<NvmHeuristicInfo> create(Type t, Executor *executor, KFunction *main) {
        NvmHeuristicInfo *ptr = nullptr;
        switch(t) {
          case None:
            assert(false && "unsupported!");
            break;
          case Static:
            ptr = new NvmStaticHeuristic(executor, main);
            break;
          case InsensitiveDynamic:
            ptr = new NvmInsensitiveDynamicHeuristic(executor, main);
            break;
          case ContextDynamic:
          default:
            assert(false && "unsupported!");
            break;
        }

        assert(ptr);

        ptr->computePriority();
        ptr->dump();
        
        return std::shared_ptr<NvmHeuristicInfo>(ptr);
      }

      static std::shared_ptr<NvmHeuristicInfo> copy(const std::shared_ptr<NvmHeuristicInfo> &info) {
        // llvm::errs() << "ding\n";
        // return info;
        // return std::shared_ptr<NvmHeuristicInfo>(nullptr);
        NvmHeuristicInfo *ptr = info.get();
        if (!ptr) {
          return info;
        }

        if (auto sptr = dynamic_cast<const NvmStaticHeuristic*>(info.get())) {
          return info;
          // return std::shared_ptr<NvmHeuristicInfo>(nullptr);
          // return info;
          // return new NvmStaticHeuristic(*ptr);
        }

        if (auto iptr = dynamic_cast<const NvmInsensitiveDynamicHeuristic*>(info.get())) {
          ptr = new NvmInsensitiveDynamicHeuristic(*iptr);
        }

        assert(ptr && "null!");
        return std::shared_ptr<NvmHeuristicInfo>(ptr);
      }
  };
  /* #endregion */

}
#endif //__NVM_HEURISTICS_H__
/*
 * vim: set tabstop=2 softtabstop=2 shiftwidth=2:
 * vim: set filetype=cpp:
 */
