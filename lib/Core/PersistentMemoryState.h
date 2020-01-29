#ifndef KLEE_PERSISTENTMEMORYSTATE_H
#define KLEE_PERSISTENTMEMORYSTATE_H

#include <llvm/Support/raw_ostream.h>

#include <boost/icl/interval_map.hpp>
#include <boost/icl/split_interval_set.hpp>

#include <limits>

namespace klee {

class PersistentMemoryState {
public:
  typedef boost::icl::interval<uint64_t>::type addr_range;

  /// A persistence epoch "infinitely" in the future (just INT_MAX).
  static const unsigned EPOCH_INF = std::numeric_limits<unsigned>::max();

  /// The assumed cache line size.
  static const uint64_t CACHE_ALIGN = 64;

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
  /// Current persistence epoch.
  /// Epochs are delimited by memory fence instructions.
  /// The first epoch is 0.
  unsigned currEpoch;

  /// Maps persistent memory ranges [start_addr, end_addr) to a pair
  /// of ints representing that memory range's persist interval.
  /// The persist interval begins at the epoch of the most recent
  /// modification to the range and ends at the epoch where it
  /// is fully persisted.
  boost::icl::interval_map<uint64_t, PersistInterval> persistIntervals;

  /// For each cache line, keep track of when it was last flushed.
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

public:
  PersistentMemoryState();

  /// Mark the persistent range between [base, base+size) as modified.
  void store(uint64_t base, uint64_t size);

  /// Mark the cache line containing addr as flushed.
  void flush(uint64_t addr);

  /// Notify the state that the persistence epoch is finishing.
  void fence();

  /// Return true if the most recent store(s) to the persistent range
  /// [base, base+size) is/are guaranteed to be persisted at this time.
  bool isPersisted(uint64_t base, uint64_t size) const;

  /// Return true if the persistence interval for the most recent
  /// store to range [baseA, baseA+sizeA) has no overlap with the
  /// persistence interval for the most recent store to
  /// range [baseB, baseB+sizeB).
  bool isOrderedBefore(uint64_t baseA, uint64_t sizeA,
                       uint64_t baseB, uint64_t sizeB) const;

  void print(llvm::raw_ostream &os) const;

private:
  bool isFullyFlushed(const addr_range &range) const;
};

llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                              const PersistentMemoryState::PersistInterval &pi);

llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                              const PersistentMemoryState::addr_range &range);

} // End klee namespace

#endif /* KLEE_PERSISTENTMEMORYSTATE_H */
