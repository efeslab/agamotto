#ifndef KLEE_PERSISTENTMEMORYSTATE_H
#define KLEE_PERSISTENTMEMORYSTATE_H

#include <llvm/Support/raw_ostream.h>

#include <boost/icl/interval_map.hpp>
#include <boost/icl/split_interval_set.hpp>

#include <limits>

namespace klee {

/// Software that uses non-volatile main memory needs to properly ensure
/// that updates to memory get persisted.
///
/// Once a region of persistent memory has been modified, that write will be
/// flushed from cache to memory at some point in the future. The range of
/// time during which the write could persist is called the "persistence interval."
///
/// Persistence intervals for data are essentially indefinite unless the program
/// explicitly instructs the cache to be written back. After issuing a flush/clwb
/// instruction, the interval is _still_ indefinite, because processors may delay
/// execution of the flush instructions. After issuing a flush AND a memory fence,
/// the modification is guaranteed to be persisted.
///
/// The entire execution of a program can be broken up into "persistence epochs"
/// delimited by sfence instructions. Any memory operation (store, flush/clwb)
/// is issued and completes during one persistence epoch.
///
/// This class tracks modifications, flushes, and fences for persistent memory.
/// At any point in time, a region of memory can be queried to determine if it
/// is definitely persisted. You can also query pairs of regions to check if
/// modifications to one region are guaranteed to have persisted before
/// modifications to the other.
class PersistentMemoryState {
public:
  static const uint64_t DEFAULT_CACHE_ALIGN = 64;

  PersistentMemoryState(uint64_t cacheLineSize=DEFAULT_CACHE_ALIGN);

  /// Mark the persistent range between [base, base+size) as modified.
  void store(uint64_t base, uint64_t size);

  /// Notify the state of a flush instruction to the cache line containing addr.
  void flush(uint64_t addr);

  /// Notify the state of an sfence instruction.
  void fence();

  /// Return true if the most recent store(s) to the persistent range
  /// [base, base+size) is/are guaranteed to be persisted at this time.
  bool isPersisted(uint64_t base, uint64_t size) const;

  /// Return true if the most recent store(s) to range [baseA, baseA+sizeA)
  /// is/are guaranteed to be persisted before any of the most recent stores to
  /// range [baseB, baseB+sizeB).
  bool isOrderedBefore(uint64_t baseA, uint64_t sizeA,
                       uint64_t baseB, uint64_t sizeB) const;

  /// Truncate addr to be aligned to cache lines.
  uint64_t alignToCache(uint64_t addr) const;

  void print(llvm::raw_ostream &os) const;

private:
  /// Current persistence epoch.
  /// Epochs are delimited by memory fence instructions.
  unsigned currEpoch;

  /// The configured cache line size.
  const uint64_t cacheAlign;

public:
  typedef boost::icl::interval<uint64_t>::type addr_range;

  /// A persistence epoch "infinitely" in the future (just INT_MAX).
  static const unsigned EPOCH_INF = std::numeric_limits<unsigned>::max();

  struct PersistInterval {
    /// The persistence epoch during which the data was 
    /// most recently modified.
    unsigned mod_epoch;

    /// The persistence epoch where the data was fully persisted.
    unsigned persist_epoch;

    PersistInterval() : mod_epoch(EPOCH_INF), persist_epoch(EPOCH_INF) {}
    PersistInterval(unsigned begin, unsigned end)
      : mod_epoch(begin), persist_epoch(end) {}

    bool overlaps(const PersistInterval &other) const;
    bool operator==(const PersistInterval &other) const;
  };

private:
  /// For each persistent memory range [start_addr, end_addr), store
  /// a pair of ints representing that range's persistence interval.
  boost::icl::interval_map<uint64_t, PersistInterval> persistIntervals;

  /// For each cache line, keep track of when it was last flushed.
  /// Flushes are not committed into this map until the epoch ends.
  boost::icl::interval_map<uint64_t, unsigned> lastFlushedEpoch;

  /// Holds all memory ranges that have unresolved persist intervals
  /// (i.e., persist_epoch = EPOCH_INF).
  /// When an epoch ends, we try to remove ranges from this set
  /// if we can prove that they have been fully persisted.
  boost::icl::interval_map<uint64_t, unsigned> dirtyRanges;

  /// The set of cache lines flushed during the current epoch.
  /// Gets reset at the end of the epoch.
  /// Cache lines are removed from this set if another write is made
  /// to them during the current epoch.
  boost::icl::split_interval_set<uint64_t> flushedThisEpoch;

  /// Round the given addr_range to begin and end at cache boundaries.
  addr_range alignToCache(const addr_range &range) const;

  bool isFullyFlushed(const addr_range &range) const;
};

llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                              const PersistentMemoryState::PersistInterval &pi);

llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                              const PersistentMemoryState::addr_range &range);

} // End klee namespace

#endif /* KLEE_PERSISTENTMEMORYSTATE_H */
