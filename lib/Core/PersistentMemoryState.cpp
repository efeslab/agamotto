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

  // Put it in the dirty set until its interval gets resolved.
  dirtyRanges.set(std::make_pair(range, currEpoch));

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
  // Commit the pending flushes for this epoch.
  auto cacheLineIt = flushedThisEpoch.begin();
  while (cacheLineIt != flushedThisEpoch.end()) {
    lastFlushedEpoch.set(make_pair(*cacheLineIt++, currEpoch));
  }
  flushedThisEpoch.clear();

  // Check to see if any dirty ranges are now known to be persisted fully.
  auto dirtyIt = dirtyRanges.begin();
  while (dirtyIt != dirtyRanges.end()) {
    addr_range dirtyRange = (dirtyIt++)->first;
    llvm::errs() << "dirtyRange: " << dirtyRange << '\n';
    if (isFullyFlushed(dirtyRange)) {
      dirtyRanges.erase(dirtyRange);
      PersistInterval pi = persistIntervals.find(dirtyRange)->second;
      pi.persist_epoch = currEpoch;
      persistIntervals.set(std::make_pair(dirtyRange, pi));
    }
  }

  ++currEpoch;

  llvm::errs() << "fence()" << '\n';
  print(llvm::errs());
}

bool PersistentMemoryState::isPersisted(uint64_t base, uint64_t size) const {
  auto range = make_addr_range(base, size);

  const PersistInterval &pi = persistIntervals.find(range)->second;
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

  const PersistInterval &piA = persistIntervals.find(rangeA)->second;
  const PersistInterval &piB = persistIntervals.find(rangeB)->second;
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

bool PersistentMemoryState::isFullyFlushed(const addr_range &range) const {
  // All cache lines spanned by this range must exist in
  // lastFlushedEpoch (i.e., must have been flushed at least once)
  // and must have been flushed in an epoch >= the range's modified epoch.
  const PersistInterval &persistInterval = persistIntervals.find(range)->second;

  // Iterate through all cache lines spanned by this range.
  auto cacheAligned = alignToCache(range);
  llvm::errs() << "cacheAligned = " << cacheAligned << '\n';
  for (uint64_t cl = cacheAligned.lower(); cl < range.upper();
       cl += this->cacheAlign) {
    addr_range cacheLine = make_addr_range(cl, this->cacheAlign);
    auto clIt = lastFlushedEpoch.find(cacheLine);
    if (clIt == lastFlushedEpoch.end())
      return false;
    unsigned flushedEpoch = clIt->second;
    if (flushedEpoch < persistInterval.mod_epoch)
      return false;
  }

  return true;
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
  if (!lastFlushedEpoch.empty()) {
    os << "----- Last Flushed Epochs: -----" << '\n';
    printMap(lastFlushedEpoch, os);
  }
  if (!dirtyRanges.empty()) {
    os << "----- Dirty Ranges: -----" << '\n';
    printMap(dirtyRanges, os);
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
