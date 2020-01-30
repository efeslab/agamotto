#ifndef KLEE_PERSISTENTMEMORYSTATE_H
#define KLEE_PERSISTENTMEMORYSTATE_H

#include <llvm/Support/raw_ostream.h>

#include <boost/icl/interval_map.hpp>
#include <boost/icl/split_interval_set.hpp>

#include <limits>
#include <vector>

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
  typedef boost::icl::interval<uint64_t>::type addr_range;

  /// A persistence epoch "infinitely" in the future (just INT_MAX).
  static const unsigned EPOCH_INF = std::numeric_limits<unsigned>::max();

  static const uint64_t DEFAULT_CACHE_ALIGN = 64;

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

    // boost::icl::interval_map's add() and set() members work differently.
    // If you set() a value for a range, it overwrites any existing mappings
    // in that range. If you add() a value for a range, it combines existing
    // mappings with the new value using operator++().
    // We use add() when epochs end to update _only_ the persist_epoch of
    // existing PersistIntervals in the map.
    PersistInterval &operator+=(const PersistInterval &other) {
      if (persist_epoch == EPOCH_INF) {
        persist_epoch = other.persist_epoch;
      }
      return *this;
    }
  };

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

  unsigned getEpoch() const;
  uint64_t getCacheLineSize() const;

  /// Return the possible persist interval for the address or range.
  /// Returns default PersistInterval (INF, INF) if no known persist interval.
  PersistInterval getPersistInterval(uint64_t addr) const;
  PersistInterval getPersistInterval(uint64_t base, uint64_t size) const;

  /// Return all persist intervals contained within the range.
  /// Outputs a sorted list of ((uint64_t, uint64_t), PersistInterval) pairs.
  typedef std::vector< std::pair<std::pair<uint64_t, uint64_t>,
                                 PersistInterval> > PersistIntervalsResult;
  void getPersistIntervals(uint64_t base,
                           uint64_t size,
                           PersistIntervalsResult &results) const;

  void print(llvm::raw_ostream &os) const;

private:
  /// Current persistence epoch.
  /// Epochs are delimited by memory fence instructions.
  unsigned currEpoch;

  /// The configured cache line size.
  const uint64_t cacheAlign;


private:
  /// For each persistent memory range [start_addr, end_addr), store
  /// a pair of ints representing that range's persistence interval.
  boost::icl::interval_map<uint64_t, PersistInterval> persistIntervals;

  /// The set of cache lines flushed during the current epoch.
  /// Gets reset at the end of the epoch.
  /// Cache lines are removed from this set if another write is made
  /// to them during the current epoch.
  boost::icl::split_interval_set<uint64_t> flushedThisEpoch;

  /// Round the given addr_range to begin and end at cache boundaries.
  addr_range alignToCache(const addr_range &range) const;

  /// Aggregate potentially multiple PersistIntervals into one
  /// (i.e., takes the min mod_epoch and the max mod_epoch).
  PersistInterval getPersistIntervalOfRange(const addr_range &range) const;
};

llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                              const PersistentMemoryState::PersistInterval &pi);

llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                              const PersistentMemoryState::addr_range &range);

} // End klee namespace

#endif /* KLEE_PERSISTENTMEMORYSTATE_H */
