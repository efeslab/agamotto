//===-- Searcher.h ----------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_SEARCHER_H
#define KLEE_SEARCHER_H

#include "klee/Internal/System/Time.h"
#include "../Module/Passes.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include <map>
#include <queue>
#include <set>
#include <vector>
#include <functional>
#include <utility>
#include <tuple>

namespace llvm {
  class BasicBlock;
  class Function;
  class Instruction;
  class raw_ostream;
}

namespace klee {
  template<class T> class DiscretePDF;
  class ExecutionState;
  class Executor;

  class Searcher {
    friend class BatchingSearcher;
    friend class MergingSearcher;
    friend class InterleavedSearcher;
    friend class IterativeDeepeningTimeSearcher;

  protected:
    Executor &executor;

    virtual ExecutionState &selectState() = 0;

  public:
    Searcher(Executor &_executor);

    virtual ~Searcher();

    // (iangneal): Gives us a common method for updating the NVM coverage 
    // information.
    virtual ExecutionState &selectStateAndUpdateInfo();

    virtual void update(ExecutionState *current,
                        const std::vector<ExecutionState *> &addedStates,
                        const std::vector<ExecutionState *> &removedStates) = 0;

    virtual bool empty() = 0;

    // prints name of searcher as a klee_message()
    // TODO: could probably make prettier or more flexible
    virtual void printName(llvm::raw_ostream &os) {
      os << "<unnamed searcher>\n";
    }

    // pgbovine - to be called when a searcher gets activated and
    // deactivated, say, by a higher-level searcher; most searchers
    // don't need this functionality, so don't have to override.
    virtual void activate() {}
    virtual void deactivate() {}

    // utility functions

    void addState(ExecutionState *es, ExecutionState *current = 0) {
      std::vector<ExecutionState *> tmp;
      tmp.push_back(es);
      update(current, tmp, std::vector<ExecutionState *>());
    }

    void removeState(ExecutionState *es, ExecutionState *current = 0) {
      std::vector<ExecutionState *> tmp;
      tmp.push_back(es);
      update(current, std::vector<ExecutionState *>(), tmp);
    }

    enum CoreSearchType {
      DFS,
      BFS,
      RandomState,
      RandomPath,
      NURS_CovNew,
      NURS_MD2U,
      NURS_Depth,
      NURS_RP,
      NURS_ICnt,
      NURS_CPICnt,
      NURS_QC,
      // (iangneal): Option for driving search towards NVM-modifying instructions.
      NvmMod
    };
  };

  class DFSSearcher : public Searcher {
    std::vector<ExecutionState*> states;
  protected:
    ExecutionState &selectState();
  public:
    DFSSearcher(Executor &executor) : Searcher(executor) {}
    void update(ExecutionState *current,
                const std::vector<ExecutionState *> &addedStates,
                const std::vector<ExecutionState *> &removedStates);
    bool empty() { return states.empty(); }
    void printName(llvm::raw_ostream &os) {
      os << "DFSSearcher\n";
    }
  };

  class BFSSearcher : public Searcher {
    std::deque<ExecutionState*> states;
  protected:
    ExecutionState &selectState();
  public:
    BFSSearcher(Executor &executor) : Searcher(executor) {}
    void update(ExecutionState *current,
                const std::vector<ExecutionState *> &addedStates,
                const std::vector<ExecutionState *> &removedStates);
    bool empty() { return states.empty(); }
    void printName(llvm::raw_ostream &os) {
      os << "BFSSearcher\n";
    }
  };

  class RandomSearcher : public Searcher {
    std::vector<ExecutionState*> states;
  protected:
    ExecutionState &selectState();
  public:
    RandomSearcher(Executor &executor) : Searcher(executor) {}
    void update(ExecutionState *current,
                const std::vector<ExecutionState *> &addedStates,
                const std::vector<ExecutionState *> &removedStates);
    bool empty() { return states.empty(); }
    void printName(llvm::raw_ostream &os) {
      os << "RandomSearcher\n";
    }
  };

  class WeightedRandomSearcher : public Searcher {
  public:
    enum WeightType {
      Depth,
      RP,
      QueryCost,
      InstCount,
      CPInstCount,
      MinDistToUncovered,
      CoveringNew
    };

  private:
    DiscretePDF<ExecutionState*> *states;
    WeightType type;
    bool updateWeights;

    double getWeight(ExecutionState*);

  protected:
    ExecutionState &selectState();

  public:
    WeightedRandomSearcher(Executor &executor, WeightType type);
    ~WeightedRandomSearcher();

    void update(ExecutionState *current,
                const std::vector<ExecutionState *> &addedStates,
                const std::vector<ExecutionState *> &removedStates);
    bool empty();
    void printName(llvm::raw_ostream &os) {
      os << "WeightedRandomSearcher::";
      switch(type) {
        case Depth              : os << "Depth\n"; return;
        case RP                 : os << "RandomPath\n"; return;
        case QueryCost          : os << "QueryCost\n"; return;
        case InstCount          : os << "InstCount\n"; return;
        case CPInstCount        : os << "CPInstCount\n"; return;
        case MinDistToUncovered : os << "MinDistToUncovered\n"; return;
        case CoveringNew        : os << "CoveringNew\n"; return;
        default                 : os << "<unknown type>\n"; return;
      }
    }
  };

  class RandomPathSearcher : public Searcher {
  protected:
    ExecutionState &selectState();

  public:
    RandomPathSearcher(Executor &_executor);
    ~RandomPathSearcher();

    void update(ExecutionState *current,
                const std::vector<ExecutionState *> &addedStates,
                const std::vector<ExecutionState *> &removedStates);
    bool empty();
    void printName(llvm::raw_ostream &os) {
      os << "RandomPathSearcher\n";
    }
  };

  /**
   * (iangneal): The heuristic driver for NVM-modifying states.
   *
   * Essentially, we want the most NVM successors.
   */
  class NvmPathSearcher : public Searcher {
    size_t currGen = 0;

    /**
     * The three fields are:
     * 1. The execution state. Naturally.
     * 2. The generation of the execution state.
     * 3. The priority of the execution state.
     *
     * The "generation" is a way to prioritize coverage. If we fork some state,
     * and it has a common post-dominator with the same successors, it is likely
     * redundant to run, so it will be suspended until the current generation
     * is over.
     */
    typedef std::tuple<ExecutionState*, size_t, size_t> priority_tuple;

    /**
     * If the generation of rhs is higher, it has lower priority. If it has
     * the same generation, the priority is lower by standard rules.
     */
    struct priority_less : public std::less<priority_tuple> {
      bool operator()(const priority_tuple &rhs, const priority_tuple &lhs) {
        return std::get<1>(rhs) > std::get<1>(lhs) ||
               (std::get<1>(rhs) == std::get<1>(lhs) && std::get<2>(rhs) < std::get<2>(lhs));
      }
    };

    // C++ doesn't let us override only part of the template defaults, or if
    // we do, must be right to left. Oh well.
    // https://stackoverflow.com/questions/31840974/c-templates-override-some-but-not-all-default-arguments
    std::priority_queue<priority_tuple,
                        std::vector<priority_tuple>,
                        priority_less> states;

    std::unordered_set<ExecutionState*> removed;
    ExecutionState *lastState;

    std::unordered_map<ExecutionState*, bool> generateTest;
    std::unordered_set<const llvm::BasicBlock*> covered;

    /**
     * Given the recently-run or newly created state, either run it or kill
     * it depending on whether or not it has any remaining interesting states.
     *
     */
    bool addOrKillState(ExecutionState *, ExecutionState *);
    ExecutionState* filterState(ExecutionState *);

    /**
     * Given the current state, see if a newly added state should go in a second
     * generation.
     */
    size_t calculateGeneration(ExecutionState *current, ExecutionState *added);

    void outputCoverage();
  protected:
    ExecutionState &selectState();

  public:
    NvmPathSearcher(Executor &_executor);
    ~NvmPathSearcher();

    void update(ExecutionState *current,
                const std::vector<ExecutionState *> &addedStates,
                const std::vector<ExecutionState *> &removedStates);
    bool empty();
    void printName(llvm::raw_ostream &os) {
      os << "NvmPathSearcher\n";
    }

  };

  extern llvm::cl::opt<bool> UseIncompleteMerge;
  class MergeHandler;
  class MergingSearcher : public Searcher {
    friend class MergeHandler;

    private:

    Searcher *baseSearcher;

    /// States that have been paused by the 'pauseState' function
    std::vector<ExecutionState*> pausedStates;

    protected:
    ExecutionState &selectState();

    public:
    MergingSearcher(Searcher *baseSearcher);
    ~MergingSearcher();

    /// ExecutionStates currently paused from scheduling because they are
    /// waiting to be merged in a klee_close_merge instruction
    std::set<ExecutionState *> inCloseMerge;

    /// Keeps track of all currently ongoing merges.
    /// An ongoing merge is a set of states (stored in a MergeHandler object)
    /// which branched from a single state which ran into a klee_open_merge(),
    /// and not all states in the set have reached the corresponding
    /// klee_close_merge() yet.
    std::vector<MergeHandler *> mergeGroups;

    /// Remove state from the searcher chain, while keeping it in the executor.
    /// This is used here to 'freeze' a state while it is waiting for other
    /// states in its merge group to reach the same instruction.
    void pauseState(ExecutionState &state){
      assert(std::find(pausedStates.begin(), pausedStates.end(), &state) == pausedStates.end());
      pausedStates.push_back(&state);
      baseSearcher->removeState(&state);
    }

    /// Continue a paused state
    void continueState(ExecutionState &state){
      auto it = std::find(pausedStates.begin(), pausedStates.end(), &state);
      assert( it != pausedStates.end());
      pausedStates.erase(it);
      baseSearcher->addState(&state);
    }

    void update(ExecutionState *current,
                const std::vector<ExecutionState *> &addedStates,
                const std::vector<ExecutionState *> &removedStates) {
      // We have to check if the current execution state was just deleted, as to
      // not confuse the nurs searchers
      if (std::find(pausedStates.begin(), pausedStates.end(), current) ==
          pausedStates.end()) {
        baseSearcher->update(current, addedStates, removedStates);
      }
    }

    bool empty() { return baseSearcher->empty(); }
    void printName(llvm::raw_ostream &os) {
      os << "MergingSearcher\n";
    }
  };

  class BatchingSearcher : public Searcher {
    Searcher *baseSearcher;
    time::Span timeBudget;
    unsigned instructionBudget;

    ExecutionState *lastState;
    time::Point lastStartTime;
    unsigned lastStartInstructions;
  protected:
    ExecutionState &selectState();

  public:
    BatchingSearcher(Searcher *baseSearcher,
                     time::Span _timeBudget,
                     unsigned _instructionBudget);
    ~BatchingSearcher();

    void update(ExecutionState *current,
                const std::vector<ExecutionState *> &addedStates,
                const std::vector<ExecutionState *> &removedStates);
    bool empty() { return baseSearcher->empty(); }
    void printName(llvm::raw_ostream &os) {
      os << "<BatchingSearcher> timeBudget: " << timeBudget
         << ", instructionBudget: " << instructionBudget
         << ", baseSearcher:\n";
      baseSearcher->printName(os);
      os << "</BatchingSearcher>\n";
    }
  };

  class IterativeDeepeningTimeSearcher : public Searcher {
    Searcher *baseSearcher;
    time::Point startTime;
    time::Span time;
    std::set<ExecutionState*> pausedStates;

  protected:
    ExecutionState &selectState();

  public:
    IterativeDeepeningTimeSearcher(Searcher *baseSearcher);
    ~IterativeDeepeningTimeSearcher();

    void update(ExecutionState *current,
                const std::vector<ExecutionState *> &addedStates,
                const std::vector<ExecutionState *> &removedStates);
    bool empty() { return baseSearcher->empty() && pausedStates.empty(); }
    void printName(llvm::raw_ostream &os) {
      os << "IterativeDeepeningTimeSearcher\n";
    }
  };

  class InterleavedSearcher : public Searcher {
    typedef std::vector<Searcher*> searchers_ty;

    searchers_ty searchers;
    unsigned index;
  
  protected:
    ExecutionState &selectState();

  public:
    explicit InterleavedSearcher(const searchers_ty &_searchers);
    ~InterleavedSearcher();

    void update(ExecutionState *current,
                const std::vector<ExecutionState *> &addedStates,
                const std::vector<ExecutionState *> &removedStates);
    bool empty() { return searchers[0]->empty(); }
    void printName(llvm::raw_ostream &os) {
      os << "<InterleavedSearcher> containing "
         << searchers.size() << " searchers:\n";
      for (searchers_ty::iterator it = searchers.begin(), ie = searchers.end();
           it != ie; ++it)
        (*it)->printName(os);
      os << "</InterleavedSearcher>\n";
    }
  };

}

#endif /* KLEE_SEARCHER_H */
/*
 * vim: set tabstop=2 softtabstop=2 shiftwidth=2:
 * vim: set filetype=cpp:
 */
