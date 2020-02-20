//===-- MemoryManager.h -----------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_MEMORYMANAGER_H
#define KLEE_MEMORYMANAGER_H

#include <cstddef>
#include <set>
#include <cstdint>

namespace llvm {
class Value;
}

namespace klee {
class MemoryObject;
class ArrayCache;

class MemoryManager {
private:
  typedef std::set<MemoryObject *> objects_ty;
  objects_ty objects;
  ArrayCache *const arrayCache;

  char *deterministicSpace;
  char *nextFreeSlot;
  size_t spaceSize;

  // Assumed size of processor cache lines, in bytes.
  static const size_t DEFAULT_CACHE_ALIGNMENT = 64;
  size_t cacheAlignment;

public:
  MemoryManager(ArrayCache *arrayCache,
                size_t cacheAlignment = DEFAULT_CACHE_ALIGNMENT);
  ~MemoryManager();

  /**
   * Returns memory object which contains a handle to real virtual process
   * memory.
   */
  MemoryObject *allocate(uint64_t size, bool isLocal, bool isGlobal,
                         const llvm::Value *allocSite, size_t alignment);
  MemoryObject *allocateFixed(uint64_t address, uint64_t size,
                              const llvm::Value *allocSite);
  void deallocate(const MemoryObject *mo);
  void markFreed(MemoryObject *mo);
  ArrayCache *getArrayCache() const { return arrayCache; }

  /*
   * Returns the size used by deterministic allocation in bytes
   */
  size_t getUsedDeterministicSize();

  size_t getCacheAlignment() const { return cacheAlignment; }
  uint64_t alignToCache(uint64_t addr) const;
  size_t getSizeInCacheLines(size_t sizeInBytes) const;
};

} // End klee namespace

#endif /* KLEE_MEMORYMANAGER_H */
