#include "PersistentMemoryState.h"

#include <limits>

using namespace klee;

using PersistInterval = PersistentMemoryState::PersistInterval;
using addr_range = PersistentMemoryState::addr_range;

static inline addr_range make_addr_range(uint64_t base, uint64_t size) {
  return boost::icl::interval<uint64_t>::right_open(base, base+size);
}

bool PersistInterval::overlaps(const PersistInterval &other) const {
  return this->mod_epoch <= other.persist_epoch &&
    this->persist_epoch >= other.mod_epoch;
}

bool PersistInterval::operator==(const PersistInterval &other) const {
        return this->mod_epoch == other.mod_epoch &&
          this->persist_epoch == other.persist_epoch;
}

PersistentMemoryState::PersistentMemoryState(uint64_t cacheLineSize)
  : currEpoch(1),
    cacheAlign(cacheLineSize) {
  llvm::errs() << "init()" << '\n';
  print(llvm::errs());
}

void PersistentMemoryState::store(uint64_t base, uint64_t size) {
  auto range = make_addr_range(base, size);

  // A new persist interval begins for this memory range.
  // Its epoch of persistence is not resolved until the range is fully flushed.
  PersistInterval newInterval(currEpoch, EPOCH_INF);
  persistIntervals.set(std::make_pair(range, newInterval));

  // Remove the cache lines spanned by the range from the
  // running set of flushed cache lines.
  addr_range cacheLines = alignToCache(range);
  flushedThisEpoch.erase(cacheLines);

  llvm::errs() << "modify(): " << range << '\n';
  print(llvm::errs());
}

void PersistentMemoryState::flush(uint64_t addr) {
  auto range = make_addr_range(addr, 1);

  addr_range cacheLines = alignToCache(range);
  llvm::errs() << "cacheLines = " << cacheLines << '\n';
  flushedThisEpoch.insert(cacheLines);

  llvm::errs() << "flush(): " << range << '\n';
  print(llvm::errs());
}

void PersistentMemoryState::fence() {
  // Commit all flushes performed during this epoch, updating the persist_epoch
  // of any covered address ranges with unresolved persist intervals.
  // Use add() instead of set() so we invoke PersistInterval's custom
  // operator++(), which will leave mod_epoch untouched (actually, use
  // add_intersection() so it only affects existing ranges).
  PersistInterval updatedPersistEpoch(EPOCH_INF, currEpoch);
  for (auto it = flushedThisEpoch.begin(); it != flushedThisEpoch.end(); ++it) {
    auto range = *it;
    add_intersection(persistIntervals, persistIntervals,
                     std::make_pair(range, updatedPersistEpoch));
  }

  flushedThisEpoch.clear();
  ++currEpoch;

  llvm::errs() << "fence()" << '\n';
  print(llvm::errs());
}

bool PersistentMemoryState::isPersisted(uint64_t base, uint64_t size) const {
  auto range = make_addr_range(base, size);

  PersistInterval pi = getPersistIntervalOfRange(range);
  bool result = pi.persist_epoch < currEpoch;

  llvm::errs() << "isPersist(): " << range << " --> " << result << '\n';
  return result;
}

bool PersistentMemoryState::isOrderedBefore(uint64_t baseA,
                                            uint64_t sizeA,
                                            uint64_t baseB,
                                            uint64_t sizeB) const {
  auto rangeA = make_addr_range(baseA, sizeA);
  auto rangeB = make_addr_range(baseB, sizeB);

  const PersistInterval &piA = getPersistIntervalOfRange(rangeA);
  const PersistInterval &piB = getPersistIntervalOfRange(rangeB);
  bool result = piA.mod_epoch < piB.mod_epoch && !piA.overlaps(piB);

  llvm::errs() << "isOrdered(): ";
  llvm::errs() << rangeA << ", " << rangeB << " --> " << result << '\n';
  return result;
}

uint64_t PersistentMemoryState::alignToCache(uint64_t addr) const {
  return addr - (addr % this->cacheAlign);
}

addr_range PersistentMemoryState::alignToCache(const addr_range &range) const {
  uint64_t begin = alignToCache(range.lower());
  uint64_t end = alignToCache(range.upper()) + this->cacheAlign;
  return make_addr_range(begin, end-begin);
}

PersistInterval PersistentMemoryState::getPersistIntervalOfRange(
    const addr_range &range) const {
  PersistInterval combined(EPOCH_INF, 0);

  auto itersInRange = persistIntervals.equal_range(range);
  for (auto it = itersInRange.first; it != itersInRange.second; ++it) {
    const PersistInterval &foundInterval = it->second;
    if (foundInterval.mod_epoch < combined.mod_epoch) {
      combined.mod_epoch = foundInterval.mod_epoch;
    }
    if (foundInterval.persist_epoch > combined.persist_epoch) {
      combined.persist_epoch = foundInterval.persist_epoch;
    }
  }

  return combined;
}

template<typename SetT>
static void printSet(const SetT &set, llvm::raw_ostream &os)
{
  for (auto it = set.begin(); it != set.end(); ++it) {
    os << *it << " ";
  }
  os << '\n';
}

template<typename MapT>
static void printMap(const MapT &map, llvm::raw_ostream &os)
{
  for (auto it = map.begin(); it != map.end(); ++it) {
    os << it->first << " : " << it->second << '\n';
  }
}

void PersistentMemoryState::print(llvm::raw_ostream &os) const {
  os << "======== PERSISTENT MEMORY STATE ========" << '\n';
  os << "currEpoch = " << currEpoch << '\n';
  if (!persistIntervals.empty()) {
    os << "----- Persist Intervals: -----" << '\n';
    printMap(persistIntervals, os);
  }
  if (!flushedThisEpoch.empty()) {
    os << "----- Flushed this epoch: -----" << '\n';
    printSet(flushedThisEpoch, os);
  }
  os << '\n';
}

namespace klee {

llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const PersistInterval &pi) {
  os << '[';
  if (pi.mod_epoch == PersistentMemoryState::EPOCH_INF)
    os << "INF";
  else
    os << pi.mod_epoch;
  os << ',';
  if (pi.persist_epoch == PersistentMemoryState::EPOCH_INF)
    os << "INF";
  else
    os << pi.persist_epoch;
  os << ']';
  return os;
}

llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const addr_range &range) {
  return os << '[' << range.lower() << ',' << range.upper() << ']';
}

} // end namespace klee
