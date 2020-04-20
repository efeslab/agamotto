//===-- NvmHeuristics.h -----------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
// Author: Ian Neal (iangneal@umich.edu)
//
//===----------------------------------------------------------------------===//

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
 * This file 
 */

namespace klee {
  /**
   * Forward declarations cuz that's KLEE's whole thing
   */
  class Executor;
  class ExecutionState;
  class NvmContextDynamicHeuristic;
  class NvmHeuristicBuilder; // Defined at the end

  typedef std::shared_ptr<AndersenAAWrapperPass> SharedAndersen;

  /* #region NvmValueDesc */
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
  class NvmValueDesc : public std::enable_shared_from_this<NvmValueDesc> {
    public:
      typedef std::shared_ptr<NvmValueDesc> Shared;
    private:
      SharedAndersen andersen_;
      // Here we track mmap locations to make weight calculation easier. Whether
      // or not 
      std::unordered_set<const llvm::Value*> mmap_calls_;

      /**
       * We conservatively assume that any modification site that points to one
       * of the "mmap" calls is an NVM-modifying instruction
       * 
       * We count do global and local to avoid propagating unnecessary local 
       * variables when we go to the next context.
       */
      std::unordered_set<llvm::Value*> not_local_nvm_, not_global_nvm_;

      bool mayPointTo(const llvm::Value *a, const llvm::Value *b) const;

      bool pointsToIsEq(const llvm::Value *a, const llvm::Value *b) const;

      bool matchesKnownVolatile(const llvm::Value *posNvm) const;

      /**
       * Vararg functions break the current assumption about passing around
       * NVM pointers, as you can't directly track them. So, instead, we figure
       * out if an NVM value was passed as any of the var args. If so, we mark
       * calls to llvm.va_{start,copy,end} as interesting to resolve all values.
       */ 
      // bool varargs_contain_nvm_;

      NvmValueDesc() {}
      NvmValueDesc(SharedAndersen apa, 
                   std::unordered_set<const llvm::Value*> mmap,
                   std::unordered_set<llvm::Value*> globals) 
                   : andersen_(apa), mmap_calls_(mmap), not_global_nvm_(globals) {}

    public:

      uint64_t hash(void) const {
        return std::hash<uint64_t>{}((not_local_nvm_.size() << 16) | 
                                     (not_global_nvm_.size() << 8) |
                                      mmap_calls_.size());
      }

      /**
       * Set up the value state when performing a function call.
       * We just return an instance with the global variables and the 
       * propagated state due to the function call arguments.
       */
      NvmValueDesc::Shared doCall(llvm::CallInst *ci, llvm::Function *f) const;

      /** 
       * Set up the value state when doing a return.
       * This essentially just pops the "stack" and propagates the return val.
       */
      NvmValueDesc::Shared doReturn(NvmValueDesc::Shared callerVals,
                                    llvm::ReturnInst *ret, 
                                    llvm::Instruction *dest) const;

      /**
       * Directly create a new description. This is generally for when we actually
       * execute and want to update our assumptions.
       */
      NvmValueDesc::Shared updateState(llvm::Value *val, bool nvm) const;

      /**
       * When we do an indirect function call, we can't propagate local nvm variables
       * cuz we don't know the arguments yet. This lets us do that.
       */
      NvmValueDesc::Shared resolveFunctionPointer(llvm::Function *f);

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
      bool isNvm(const llvm::Value *ptr) const;

      std::string str(void) const;

      // Populate with all the calls to mmap.
      static NvmValueDesc::Shared staticState(SharedAndersen andersen, 
                                              llvm::Module *m);

      friend bool operator==(const NvmValueDesc &lhs, const NvmValueDesc &rhs);
  };
  /* #endregion */

  /* #region NvmContextDesc */


  class NvmContextDesc : public std::enable_shared_from_this<NvmContextDesc> {
    public: 
      typedef std::shared_ptr<NvmContextDesc> Shared;
    private:
      friend class NvmContextDynamicHeuristic;
      SharedAndersen andersen;

      /**
       * These are the core pieces that define a context.
       */
      llvm::Function *function;
      NvmValueDesc::Shared valueState;
      bool returnHasWeight; 

      /**
       * Many function contexts will be the same.
       */
      struct ContextCacheKey {
        llvm::Function *function;
        NvmValueDesc::Shared initialState;

        ContextCacheKey(llvm::Function *f, NvmValueDesc::Shared init) 
          : function(f), initialState(init) {}

        bool operator==(const ContextCacheKey &rhs) const {
          return function == rhs.function && *initialState == *rhs.initialState;
        }

        struct Hash {
          uint64_t operator()(const ContextCacheKey &cck) const {
            return std::hash<void*>{}(cck.function) ^ cck.initialState->hash();
          }
        };
      };

      static std::unordered_map<ContextCacheKey, 
                                NvmContextDesc::Shared,
                                ContextCacheKey::Hash> contextCache;

      /**
       * This function has a bunch of instructions. They have weights based
       * on the current context.
       */
      std::unordered_map<llvm::Instruction*, uint64_t> weights;

      bool hasCoreWeight = false;

      /**
       * This functions's instructions also have a bunch of priorities.
       */
      std::unordered_map<llvm::Instruction*, uint64_t> priorities;

      /**
       * CallInsts have succeeding ContextDesc, which is nice to pre-compute
       */
      std::unordered_map<llvm::CallInst*, NvmContextDesc::Shared> contexts;

      /* METHODS */

      /**
       * Generally used for generating contexts for calls.
       */
      NvmContextDesc(SharedAndersen anders,
                     llvm::Function *fn,
                     NvmValueDesc::Shared initialArgs,
                     bool parentHasWeight);

      /**
       * Returns the priority of the subcontext.
       */
      uint64_t constructCalledContext(llvm::CallInst *ci, llvm::Function *f);
      uint64_t constructCalledContext(llvm::CallInst *ci);

      /**
       * Core instructions are instructions that impact NVM
       */
      bool isaCoreInst(llvm::Instruction *i) const;

      /**
       * Auxiliary instructions are instructions that have weight as a 
       * consequency of control flow.
       * 
       * For this version of the heuristic, this will just be call and return
       * instructions.
       */
      bool isaAuxInst(llvm::Instruction *i) const;
      uint64_t computeAuxInstWeight(llvm::Instruction *i);

      /**
       * After this, the context should be fully valid.
       */
      std::list<llvm::Instruction*> setCoreWeights(void);
      void setAuxWeights(std::list<llvm::Instruction*> auxInsts);
      void setPriorities(void);

    public:

      /**
       * Constructs the first context, generally for whatever function KLEE is 
       * using for a main function.
       */
      NvmContextDesc(SharedAndersen anders, llvm::Module *m, llvm::Function *main);

      /**
       * Gets the next context if the given PC is a call or return instruction.
       * Otherwise, returns this.
       */
      NvmContextDesc::Shared tryGetNextContext(KInstruction *pc, 
                                               KInstruction *nextPC);

      /**
       * Gets the resulting context of updating the state. If updating the state
       * does not cause any change in priority, returns this.
       */
      NvmContextDesc::Shared tryUpdateContext(llvm::Value *v, bool isNvm);

      NvmContextDesc::Shared tryResolveFnPtr(llvm::CallInst *ci, 
                                             llvm::Function *f);

      /**
       * Gets the priority at the root of the function, i.e. at the first 
       * instruction.
       */
      uint64_t getRootPriority(void) const {
        llvm::Instruction *i = function->getEntryBlock().getFirstNonPHIOrDbg();
        if (priorities.count(i)) return priorities.at(i);
        
        return hasCoreWeight ? 1lu : 0lu;
      }

      uint64_t getPriority(KInstruction *pc) const {
        return priorities.at(pc->inst);
      }

      NvmContextDesc::Shared dup(void) const {
        return std::make_shared<NvmContextDesc>(*this);
      }

      std::string str() const;
  };

  /* #endregion */


  /* #region NvmHeuristicInfo (super class) */
  /**
   * This is per state.
   */

  class NvmHeuristicInfo {
    friend class NvmHeuristicBuilder;
    protected:

      typedef std::unordered_set<const llvm::Value*> ValueSet;
      typedef std::vector<const llvm::Value*> ValueVector;
      
      virtual void computePriority() = 0;

      virtual bool needsRecomputation() const = 0;

    public:
    
      virtual ~NvmHeuristicInfo() = 0;

      virtual uint64_t getCurrentPriority(void) const = 0;

      /**
       * May change one of the current states, or could not.
       */
      virtual void updateCurrentState(ExecutionState *es, KInstruction *pc, bool isNvm) = 0;

      /**
       * Resolve a function call. Useful for function pointer shenanigans.
       */
      virtual void resolveFunctionCall(KInstruction *pc, llvm::Function *f) {}

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
        return isa<llvm::StoreInst>(i) || utils::isFlush(i) || utils::isFence(i) || isNvmAllocSite(i);
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

  /**
   * TODO: resolve function pointers
   */
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

  /* #region NvmContextDynamicHeuristic */

  /**
   * We keep call stack information with our values, but not flow information.
   * We forgo a flow-sensitive analysis as it is extremely costly.
   */
  class NvmContextDynamicHeuristic : public NvmHeuristicInfo {
    friend class NvmHeuristicBuilder;
    protected:
      
      std::list<NvmContextDesc::Shared> contextStack;
      NvmContextDesc::Shared contextDesc;
      KInstruction *curr;

      /**
       * We can map a function and it's context to a priority value. This makes
       * re-computing priority fairly efficient.
       */

      NvmContextDynamicHeuristic(Executor *executor, KFunction *mainFn);

      virtual void computePriority() override {
        contextDesc->setAuxWeights(contextDesc->setCoreWeights());
        contextDesc->setPriorities();
      }

      virtual bool needsRecomputation() const override { return false; }

    public:

      NvmContextDynamicHeuristic(const NvmContextDynamicHeuristic &other) = default;

      virtual ~NvmContextDynamicHeuristic() {}

      virtual uint64_t getCurrentPriority(void) const override {
        return contextDesc->getPriority(curr);
      }

      /**
       * May change one of the current states, or could not.
       */
      virtual void updateCurrentState(ExecutionState *es, 
                                      KInstruction *pc, 
                                      bool isNvm) override;

      virtual void stepState(ExecutionState *es, 
                             KInstruction *pc, 
                             KInstruction *nextPC) override;

      virtual void dump(void) const override {
        uint64_t nonZeroWeights = 0, nonZeroPriorities = 0;

        for (const auto &p : contextDesc->weights) 
          nonZeroWeights += (p.second > 0);
        for (const auto &p : contextDesc->priorities) 
          nonZeroPriorities += (p.second > 0);

        double pWeights = 100.0 * ((double)nonZeroWeights / (double)contextDesc->weights.size());
        double pPriorities = 100.0 * ((double)nonZeroPriorities / (double)contextDesc->priorities.size());

        llvm::errs() << "NvmContext: \n" << contextDesc->str() << "\n";
        llvm::errs() << "\tCurrent instruction: " << *curr->inst << "\n"; 
        fprintf(stderr, "\t%% insts with weight: %f%%\n", pWeights);
        fprintf(stderr, "\t%% insts with priority: %f%%\n", pPriorities);
      }
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
            ptr = new NvmContextDynamicHeuristic(executor, main);
            break;
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
          // Since it's never updated, we don't have to copy it. Just share.
          return info;
        }

        if (auto iptr = dynamic_cast<const NvmInsensitiveDynamicHeuristic*>(info.get())) {
          ptr = new NvmInsensitiveDynamicHeuristic(*iptr);
        }

        if (auto cptr = dynamic_cast<const NvmContextDynamicHeuristic*>(info.get())) {
          ptr = new NvmContextDynamicHeuristic(*cptr);
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
