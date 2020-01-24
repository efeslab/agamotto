#pragma once

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

#include "pass_utils.hpp"

using namespace llvm;
using namespace std;

namespace utils {

    /**
     *
     * ASSUMPTIONS:
     * - An NVM pointer must be a direct pointer.
     * - There are no global variables used.
     */
    class FunctionInfo {
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
             * If i is an annotation instruction, get the location of the NVM
             * pointer that it annotates, AKA the T** that contains a pointer
             * to the NVM range.
             */
            Value* getNvmPtrLoc(const Instruction &i);

            /**
             * Aggregate all NVM pointer annotations in a function.
             */
            void getNvmPtrLocs(const Function &f, unordered_set<const Value*> &s);

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
            FunctionInfo(ModulePass &mp, const Module &mod);

            /**
             * A function which manipulates NVM does a combination of at least
             * one of the following operations:
             *
             * 1) Has an sfence (creates epochs)
             * 2) Manipulates an NVM pointer, either one declared in the function
             * itself or from a function argument.
             * 3) Calls a function which manipulates NVM.
             */
            bool manipulatesNVM(const Function *fn, unordered_set<int> nvmArgs);

            bool manipulatesNVM(const Function *fn);

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
}
