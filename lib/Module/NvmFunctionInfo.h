#ifndef __NVM_FUNCTION_INFO_H__
#define __NVM_FUNCTION_INFO_H__

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

#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicInst.h"

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
 * ASSUMPTIONS: This all currently hinges on pointers to NVM being local variables
 * or function arguments. No pointers are checked in the global scope. This limitation
 * will need to be addressed at some point.
 */

namespace klee {

  class NvmFunctionInfo;

  /**
   * A desciption of a call to a function with NVM relevant information.
   * Encodes the function that is called and which arguments are pointers to
   * NVM.
   */
  class NvmFunctionCallDesc {
    private:
      const llvm::Function *fn_;
      std::unordered_set<unsigned> nvm_args_;
    public:
      NvmFunctionCallDesc();
      NvmFunctionCallDesc(const llvm::Function*);
      NvmFunctionCallDesc(const llvm::Function*, const std::unordered_set<unsigned>&);

      const llvm::Function* Fn() const { return fn_; }

      const std::unordered_set<unsigned>& NvmArgs() const { return nvm_args_; };

      struct HashFn : public std::unary_function<NvmFunctionCallDesc, size_t> {
        size_t operator()(const NvmFunctionCallDesc&) const;
      };

      friend bool operator==(const NvmFunctionCallDesc &rhs, const NvmFunctionCallDesc &lhs) {
        return rhs.Fn() == lhs.Fn() && rhs.NvmArgs() == lhs.NvmArgs();
      }

      void dumpInfo() const;
  };

  /**
   * (iangneal): Part of my refactoring efforts.
   *
   * This class encapsulates the information about a single call to NVM.
   * Based on the function and which of it's arguments are pointers to NVM, we
   * can query this object for information like the number of interesting
   * basic blocks, etc.
   */
  class NvmFunctionCallInfo {
    private:
      NvmFunctionInfo *parent_;
      // The defining characteristics of this Function call.
      const llvm::Function *fn_;
      const std::unordered_set<unsigned> nvm_args_;
      // -- We need a blacklist either for recursive calls or if, for some
      // reason, you had weird interdependent function calls.
      std::unordered_set<const llvm::Function*> blacklist_;

      // The locations at which pointers to NVM are stored for this call.
      std::unordered_set<const llvm::Value*> nvm_ptr_locs_;
      // The pointers defined in this function that point to NVM.
      std::unordered_set<const llvm::Value*> nvm_ptrs_;
      // The instructions that modify NVM. Also includes sfences.
      std::unordered_set<const llvm::Value*> nvm_mods_;
      // The instructions that are nested function calls.
      std::unordered_set<const llvm::CallInst*> nested_calls_;

      // Numbers for our factors.
      // -- "Importance factor": The number of modifiers in a single basic block.
      std::unordered_map<const llvm::BasicBlock*, size_t> imp_factor_;
      // -- "Nested factor": An intermediate calculation which adds in the
      // nested call's magnitude.
      std::unordered_map<const llvm::BasicBlock*, size_t> imp_nested_;
      // -- "Successor factor": The number of modifiers of this basic block and
      // the maximum across it's successors, including nested calls. This is
      // essentially the heuristic.
      std::unordered_map<const llvm::BasicBlock*, size_t> succ_factor_;
      // -- "Magnitude": The maximum number of modifiers in a single path of
      // this function (single traversal, one pass through loops). This
      // calculation is a way of caching for other functions which may use this
      // function as a nested call.
      size_t magnitude_;

      // The set of basic blocks which have a non-zero nested importance factor.
      std::unordered_set<const llvm::BasicBlock*> imp_bbs_;

      /**
       * 1. Get the NVM pointers and pointer locations that are explicitly
       * defined in the function body.
       * 2. Get the NVM pointers from the argument list.
       * 3. Find all derivative pointers.
       * 4. Now that we have all the derivative pointers, we can get all the
       * interesting basic blocks in this function.
       */
      void getNvmInfo();

      /**
       * Calculates the successor factor of a specific basic block. Recursive.
       * Tracks backedges to avoid repeated loop traversals.
       */
      void computeSuccessorFactor(const llvm::BasicBlock*,
                                  const std::unordered_set<const llvm::BasicBlock*>&);

      /**
       * Compute all of the heuristic values we're going to need.
       */
      void computeFactors();

      /**
       * The body of initialization. Useful for the constructor overloading.
       */
      void init();


    public:
      NvmFunctionCallInfo(NvmFunctionInfo *parent,
                          const NvmFunctionCallDesc &desc);

      NvmFunctionCallInfo(NvmFunctionInfo *parent,
                          const NvmFunctionCallDesc &desc,
                          const std::unordered_set<const llvm::Function*> &blacklist);

      size_t getMagnitude() const { return magnitude_; }

      size_t getSuccessorFactor(const llvm::BasicBlock *bb) const;

      size_t getImportanceFactor(const llvm::BasicBlock *bb) const;
      /**
       * If the given instruction is part of this function, determine which of
       * it's call arguments are NVM pointers.
       */
      std::unordered_set<unsigned> queryNvmArgs(const llvm::CallInst*) const;

      /**
       * Gives a reference to the basic blocks which are interesting.
       */
      const std::unordered_set<const llvm::BasicBlock*> getAllInterestingBB() const {
        return imp_bbs_;
      }

      void dumpInfo() const;
  };

  class NvmFunctionInfo {
    private:
      std::unordered_map<NvmFunctionCallDesc,
                         std::shared_ptr<NvmFunctionCallInfo>,
                         NvmFunctionCallDesc::HashFn> fn_info_;

      NvmFunctionCallDesc root_;

    public:
      NvmFunctionInfo() = default;

      void setRoot(const NvmFunctionCallDesc &desc);

      const NvmFunctionCallInfo* get(const NvmFunctionCallDesc&);
      // With a blacklist to prevent recursion.
      const NvmFunctionCallInfo* get(const NvmFunctionCallDesc&,
          const std::unordered_set<const llvm::Function*>&);

      const NvmFunctionCallInfo* findInfo(const NvmFunctionCallDesc&);

      // Compute the ratio of coverage of interesting basic blocks and perform
      // a basic sanity check
      double computeCoverageRatio(const std::unordered_set<const llvm::BasicBlock*>&);

      /**
       * Given the function call description of the function calling the "call"
       * instruction, determine the set of arguments for the nested call that
       * are pointers to NVM.
       */
      std::unordered_set<unsigned> getNvmArgs(const NvmFunctionCallDesc& caller,
          const llvm::CallInst* call) const;

      void dumpAllInfo() const;

  };
}
#endif //__NVM_FUNCTION_INFO_H__
/*
 * vim: set tabstop=2 softtabstop=2 shiftwidth=2:
 * vim: set filetype=cpp:
 */
