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
  class NvmFunctionCallInfo;

  class NvmBasicBlockInfo;

  /**
   * A desciption of a call to a function with NVM relevant information.
   * Encodes the function that is called and which arguments are pointers to
   * NVM.
   */
  class NvmFunctionCallDesc {
    private:
      const llvm::Function *fn_;
      const std::unordered_set<unsigned> nvm_args_;
    public:
      NvmFunctionCallDesc(const llvm::Function*, const std::unordered_set<unsigned>&);

      const llvm::Function* Fn() const { return fn_; }

      const std::unordered_set<unsigned>& NvmArgs() const { return nvm_args_ };

      struct HashFn : public std::unary_function<NvmFunctionCallDesc, size_t> {
        size_t operator()(const NvmFunctionCallDesc&) const;
      };

      friend bool operator==(const NvmFunctionCallDesc &rhs, const NvmFunctionCallDesc &lhs);
  }

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
      const NvmFunctionInfo &parent_;
      // The defining characteristics of this Function call.
      const llvm::Function *fn_;
      const std::unordered_set<unsigned> nvm_args_;
      // -- We need a blacklist either for recursive calls or if, for some
      // reason, you had weird interdependent function calls.
      const std::unordered_set<const Function*> blacklist_;

      // The locations at which pointers to NVM are stored for this call.
      std::unordered_set<const llvm::Value*> nvm_ptr_locs_;
      // The pointers defined in this function that point to NVM.
      std::unordered_set<const llvm::Value*> nvm_ptrs_;
      // The instructions that modify NVM. Also includes sfences.
      std::unordered_set<const llvm::Value*> nvm_mods_;
      // The instructions that are nested function calls.
      std::unordered_set<const CallInst*> nested_calls_;

      // Numbers for our factors.
      // -- "Importance factor": The number of modifiers in a single basic block.
      std::unordered_map<const BasicBlock*, size_t> imp_factor_;
      // -- "Nested factor": An intermediate calculation which adds in the
      // nested call's magnitude.
      std::unordered_map<const BasicBlock*, size_t> imp_nested_;
      // -- "Successor factor": The number of modifiers of this basic block and
      // the maximum across it's successors, including nested calls. This is
      // essentially the heuristic.
      std::unordered_set<const BasicBlock*, size_t> succ_factor_;
      // -- "Magnitude": The maximum number of modifiers in a single path of
      // this function (single traversal, one pass through loops). This
      // calculation is a way of caching for other functions which may use this
      // function as a nested call.
      size_t magnitude_;

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
       * Get the magnitude of the specified function.
       */
      size_t getFnMag(const llvm::Function*, std::unordered_set<const llvm::Function*>&);

      /**
       * Compute all of the heuristic values we're going to need.
       */
      void computeFactors();

      /**
       * The body of initialization. Useful for the constructor overloading.
       */
      void init();

    public:
      NvmFunctionCallInfo(const NvmFunctionInfo &parent,
                          const NvmFunctionCallDesc& desc);
                          const llvm::Function *fn,
                          const std::unordered_set<unsigned> &nvm_args);

      NvmFunctionCallInfo(const NvmFunctionInfo &parent,
                          const llvm::Function *fn,
                          const std::unordered_set<unsigned> &nvm_args,
                          const std::unordered_set<const Function*> &blacklist);

      size_t getMagnitude() const { return magnitude_; }
  };

  class NvmFunctionInfo {
    private:
      Module &m_;
      std::unordered_map<NvmFunctionCallDesc,
                         NvmFunctionCallDesc::HashFn,
                         std::shared_ptr<NVMFunctionCallInfo>> fn_info_;
    public:
      NvmFunctionInfo(Module&);

      const NVMFunctionCallInfo* get(const NvmFunctionCallDesc&);
      // With a blacklist to prevent recursion.
      const NVMFunctionCallInfo* get(const NvmFunctionCallDesc&,
          std::unordered_set<const llvm::Function*>&);

  }
#if 0
  /**
   * ASSUMPTIONS:
   * - An NVM pointer must be a direct pointer.
   * - There are no global variables used.
   */
  class NvmFunctionInfo {
      private:
          unordered_map<string, size_t> paths_total;
          unordered_map<string, size_t> paths_total_rec;
          unordered_map<string, size_t> paths_imp;
          unordered_map<string, size_t> paths_ret;

          unordered_map<string, unordered_set<const Value*>> nvm_locs;
          unordered_map<string, unordered_set<const Value*>> nvm_ptrs;
          unordered_map<string, unordered_set<const Value*>> nvm_usrs;
          unordered_map<string, vector<unordered_set<const Value*>>> nvm_arg_manip;

          typedef tuple<string, unordered_set<int>> key_t;
          struct key_hash : public std::unary_function<key_t, std::size_t>
          {
              std::size_t operator()(const key_t& k) const {
                  size_t hash_val = hash<string>{}(get<0>(k));
                  // This is okay to do unordered, as XOR is communative.
                  for (const int &i : get<1>(k)) {
                      hash_val ^= hash<int>{}(i);
                  }

                  return hash_val;
              }
          };

          unordered_map<key_t, bool, key_hash> manip;

          typedef tuple<const BasicBlock*, unordered_set<int>> bkey_t;
          struct bkey_hash : public std::unary_function<bkey_t, std::size_t>
          {
              std::size_t operator()(const bkey_t& k) const {
                  size_t hash_val = hash<const void*>{}((const void*)get<0>(k));
                  // This is okay to do unordered, as XOR is communative.
                  for (const int &i : get<1>(k)) {
                      hash_val ^= hash<int>{}(i);
                  }

                  return hash_val;
              }
          };
          unordered_map<bkey_t, size_t, bkey_hash> imp_factor;

          ModulePass &mp_;
          const Module &mod_;

          void findImportantOps(const Function *fn, const unordered_set<int> &args);

          /**
           * Given the NVM pointer locations, compute the set of all pointers
           * that point to NVM.
           */
          void getNvmPtrsFromLocs(const Function &f, unordered_set<const Value*> &s);

          /**
           * Given all NVM pointers, find all instructions which modify
           * the state of NVM, either by stores or by flushes.
           */
          void getNvmModifiers(const Function &f, unordered_set<const Value*> &s);

          /**
           * Find all declarations of NVM pointers, and all users of the NVM
           * declarations in these functions.
           */
          void initNvmDeclarations();

          /**
           * For the given function, return a set of values that modify the
           * locations pointed to by the Argument, assuming the argument is
           * a single-indirect pointer (T*). If the argument is not a single
           * indirect pointer, the set will be empty.
           */
          vector<unordered_set<const Value*>> getArgumentManip(const Function &fn);

          /**
           * Find all manipulations of NVM pointers, both those declared in
           * this scope and from pointer arguments.
           */
          void initManip();


      public:
          NvmFunctionInfo(ModulePass &mp, const Module &mod);

          /**
           * A function which manipulates NVM does a combination of at least
           * one of the following operations:
           *
           * 1) Has an sfence (creates epochs)
           * 2) Manipulates an NVM pointer, either one declared in the function
           * itself or from a function argument.
           * 3) Calls a function which manipulates NVM.
           */
          bool manipulatesNvm(const Function *fn, unordered_set<int> nvmArgs);

          bool manipulatesNvm(const Function *fn);

          void dumpManip(const Function *fn);

          /**
           * Returns the number of total paths within a function. Does not
           * include paths which terminate without a return, such as exit or
           * abort calls.
           *
           * This definition is NOT recursive.
           */
          size_t totalPathsInFunction(const Function *fn);

          /**
           * Returns the number of total paths through the function, as in the
           * number of paths through the function which properly return from
           * the function.
           *
           * This IS recursive
           *
           */
          size_t totalPathsThroughFunction(const Function *fn);

          /**
           * Returns the number of total paths through the function, as in the
           * number of paths through the function which properly return from
           * the function.
           *
           * This IS recursive, checks unique interesting paths
           *
           */
          size_t totalImportantPaths(const Function *fn);
          size_t totalImportantPaths(const Function*, const unordered_set<int>&);

          struct set_hash : public std::unary_function<unordered_set<const BasicBlock*>, std::size_t>
          {
              std::size_t operator()(const unordered_set<const BasicBlock*>& k) const {
                  size_t hash_val = 0;
                  // This is okay to do unordered, as XOR is communative.
                  for (const BasicBlock* ptr : k) {
                      hash_val ^= hash<const BasicBlock*>{}(ptr);
                  }
                  return hash_val;
              }
          };

          template<class T>
          struct list_hash : public std::unary_function<list<const T*>, std::size_t>
          {
              std::size_t operator()(const list<const T*>& path) const {
                  size_t hash_val = 0;
                  for (const T* ptr : path) {
                      hash_val ^= hash<const T*>{}(ptr);
                  }
                  return hash_val;
              }
          };

          typedef list_hash<BasicBlock> bb_list_hash;

          template<class A, class B>
          struct tuple_list_hash :
              public std::unary_function<list<tuple<const A*, deque<const B*>>>, std::size_t>
          {
              std::size_t operator()(const list<tuple<const A*, deque<const B*>>>& path) const {
                  size_t hash_val = 0;
                  for (const auto &t : path) {
                      hash_val ^= hash<const A*>{}(get<0>(t));
                      for (const B* b: get<1>(t)) {
                          hash_val ^= hash<const B*>{}(b);
                      }
                  }
                  return hash_val;
              }
          };

          typedef tuple_list_hash<BasicBlock, Instruction> bi_list_hash;
          typedef tuple<const BasicBlock*, deque<const Instruction*>> bbid_t;

          unordered_map<
              key_t,
              unordered_set<
                  list<bbid_t>,
                  bi_list_hash>,
              key_hash> paths_imp_total;
          unordered_set<list<bbid_t>, bi_list_hash>
              getImportantPaths(const Function*, const unordered_set<int>&);

          /**
           * Find the number of interesting basic blocks given a specific
           * root function.
           *
           * By specifying a root function, we disregard that function's
           * input arguments (i.e., main's argv).
           */
          size_t computeNumInterestingBB(const Function *fn);


          /**
           * Given the root function, compute the total number of basic blocks
           * in the function.
           */
          size_t computeNumBB(const Function *fn);

          /**
           * Compute the number of important successors a basic block has,
           * given the root function.
           *
           */
          void computeImportantSuccessors(const Function *root);

          unordered_map<string, size_t> acc_factor;

          unordered_map<bkey_t, size_t, bkey_hash> imp_total;

          void propagateToCallsites(const Function *fn,
                  const unordered_set<int> &args);

          void accumulateImportanceFactor(const Function *fn,
                  const unordered_set<int> &args);

          void doSuccessorCalculation(const Function *fn,
                  const unordered_set<int> &args);

          unordered_map<bkey_t, size_t, bkey_hash> imp_succ;

          void calcImportance(const BasicBlock* bb, const unordered_set<int> &arg,
                  const unordered_set<const BasicBlock*> &be);

          void dumpImportantSuccessors();

          void dumpPathsThrough();
          void dumpUnique();
  };
#endif
}
#endif //__NVM_FUNCTION_INFO_H__
/*
 * vim: set tabstop=2 softtabstop=2 shiftwidth=2:
 * vim: set filetype=cpp:
 */
