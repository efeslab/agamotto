//===-- Memory.h ------------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_MEMORY_H
#define KLEE_MEMORY_H

#include "Context.h"
#include "TimingSolver.h"

#include "klee/Expr/Expr.h"

#include "llvm/ADT/StringExtras.h"

#include <string>
#include <vector>

namespace llvm {
  class Value;
}

namespace klee {

class BitArray;
class MemoryManager;
class Solver;
class ArrayCache;

class MemoryObject {
  friend class STPBuilder;
  friend class ObjectState;
  friend class ExecutionState;

private:
  static int counter;
  mutable unsigned refCount;

public:
  unsigned id;
  uint64_t address;

  /// size in bytes
  unsigned size;
  mutable std::string name;

  bool isLocal;
  mutable bool isGlobal;
  bool isFixed;

  bool isUserSpecified;

  MemoryManager *parent;

  /// "Location" for which this memory object was allocated. This
  /// should be either the allocating instruction or the global object
  /// it was allocated for (or whatever else makes sense).
  const llvm::Value *allocSite;
  
  /// A list of boolean expressions the user has requested be true of
  /// a counterexample. Mutable since we play a little fast and loose
  /// with allowing it to be added to during execution (although
  /// should sensibly be only at creation time).
  mutable std::vector< ref<Expr> > cexPreferences;

  // DO NOT IMPLEMENT
  MemoryObject(const MemoryObject &b);
  MemoryObject &operator=(const MemoryObject &b);

public:
  // XXX this is just a temp hack, should be removed
  explicit
  MemoryObject(uint64_t _address) 
    : refCount(0),
      id(counter++), 
      address(_address),
      size(0),
      isFixed(true),
      parent(NULL),
      allocSite(0) {
  }

  MemoryObject(uint64_t _address, unsigned _size, 
               bool _isLocal, bool _isGlobal, bool _isFixed,
               const llvm::Value *_allocSite,
               MemoryManager *_parent)
    : refCount(0), 
      id(counter++),
      address(_address),
      size(_size),
      name("unnamed"),
      isLocal(_isLocal),
      isGlobal(_isGlobal),
      isFixed(_isFixed),
      isUserSpecified(false),
      parent(_parent), 
      allocSite(_allocSite) {
  }

  ~MemoryObject();

  /// Get an identifying string for this allocation.
  void getAllocInfo(std::string &result) const;

  void setName(std::string name) const {
    this->name = name;
  }

  ref<ConstantExpr> getBaseExpr() const { 
    return ConstantExpr::create(address, Context::get().getPointerWidth());
  }
  ref<ConstantExpr> getSizeExpr() const { 
    return ConstantExpr::create(size, Context::get().getPointerWidth());
  }
  ref<Expr> getOffsetExpr(ref<Expr> pointer) const {
    return SubExpr::create(pointer, getBaseExpr());
  }
  ref<Expr> getBoundsCheckPointer(ref<Expr> pointer) const {
    return getBoundsCheckOffset(getOffsetExpr(pointer));
  }
  ref<Expr> getBoundsCheckPointer(ref<Expr> pointer, unsigned bytes) const {
    return getBoundsCheckOffset(getOffsetExpr(pointer), bytes);
  }

  ref<Expr> getBoundsCheckOffset(ref<Expr> offset) const {
    if (size==0) {
      return EqExpr::create(offset, 
                            ConstantExpr::alloc(0, Context::get().getPointerWidth()));
    } else {
      return UltExpr::create(offset, getSizeExpr());
    }
  }
  ref<Expr> getBoundsCheckOffset(ref<Expr> offset, unsigned bytes) const {
    if (bytes<=size) {
      return UltExpr::create(offset, 
                             ConstantExpr::alloc(size - bytes + 1, 
                                                 Context::get().getPointerWidth()));
    } else {
      return ConstantExpr::alloc(0, Expr::Bool);
    }
  }
};

class ObjectState {
public:
  enum Kind {
    Volatile,
    Persistent
  };

private:
  friend class AddressSpace;
  unsigned copyOnWriteOwner; // exclusively for AddressSpace

  friend class ObjectHolder;
  unsigned refCount;

  const MemoryObject *object;

  uint8_t *concreteStore;

  // XXX cleanup name of flushMask (its backwards or something)
  BitArray *concreteMask;

  // mutable because may need flushed during read of const
  mutable BitArray *flushMask;

  ref<Expr> *knownSymbolics;

  // mutable because we may need flush during read of const
  mutable UpdateList updates;

public:
  unsigned size;

  bool readOnly;

public:
  /// Create a new object state for the given memory object with concrete
  /// contents. The initial contents are undefined, it is the callers
  /// responsibility to initialize the object contents appropriately.
  ObjectState(const MemoryObject *mo);

  /// Create a new object state for the given memory object with symbolic
  /// contents.
  ObjectState(const MemoryObject *mo, const Array *array);

  ObjectState(const ObjectState &os);
  virtual ~ObjectState();

  virtual Kind getKind() const { return Volatile; }
  static bool classof(const ObjectState *os) {
    return os->getKind() == Volatile;
  }

  const MemoryObject *getObject() const { return object; }

  void setReadOnly(bool ro) { readOnly = ro; }

  // make contents all concrete and zero
  void initializeToZero();
  // make contents all concrete and random
  void initializeToRandom();

  virtual ref<Expr> read(ref<Expr> offset, Expr::Width width) const;
  virtual ref<Expr> read(unsigned offset, Expr::Width width) const;
  virtual ref<Expr> read8(unsigned offset) const;

  // return bytes written.
  virtual void write(unsigned offset, ref<Expr> value);
  virtual void write(ref<Expr> offset, ref<Expr> value);

  virtual void write8(unsigned offset, uint8_t value);
  virtual void write16(unsigned offset, uint16_t value);
  virtual void write32(unsigned offset, uint32_t value);
  virtual void write64(unsigned offset, uint64_t value);
  virtual void print() const;

  /*
    Looks at all the symbolic bytes of this object, gets a value for them
    from the solver and puts them in the concreteStore.
  */
  void flushToConcreteStore(TimingSolver *solver,
                            const ExecutionState &state) const;

protected:
  const UpdateList &getUpdates() const;

  void makeConcrete();

  void makeSymbolic();

  virtual ref<Expr> read8(ref<Expr> offset) const;
  virtual void write8(unsigned offset, ref<Expr> value);
  virtual void write8(ref<Expr> offset, ref<Expr> value);

  void fastRangeCheckOffset(ref<Expr> offset, unsigned *base_r, 
                            unsigned *size_r) const;
  void flushRangeForRead(unsigned rangeBase, unsigned rangeSize) const;
  void flushRangeForWrite(unsigned rangeBase, unsigned rangeSize);

  bool isByteConcrete(unsigned offset) const;
  bool isByteFlushed(unsigned offset) const;
  bool isByteKnownSymbolic(unsigned offset) const;

  void markByteConcrete(unsigned offset);
  void markByteSymbolic(unsigned offset);
  void markByteFlushed(unsigned offset);
  void markByteUnflushed(unsigned offset);
  void setKnownSymbolic(unsigned offset, Expr *value);

  ArrayCache *getArrayCache() const;
};

/// \brief A subclass of ObjectState used for tracking the state of a
/// persistent MemoryObject.
///
/// Tracks updates to a symbolic Array of bools that represents the
/// dirtiness/persistedness of each cache line that the MemoryObject spans.
class PersistentState : public ObjectState {
  private:
    /// Both of these UpdateLists share one underlying symbolic bool Array.
    /// The Array has one entry per cache line: 1 if persisted, 0 if not.
    /// Each write and each cache line flush are tracked as updates to this array.
    ///
    /// These two update lists diverge during a persistence epoch (epochs are
    /// delimited by memory fences).
    /// Until the next sfence, cacheLineUpdates contains only the writes made
    /// during the current epoch, while pendingCacheLineUpdates contains both
    /// the writes and flushes.
    ///
    /// Queries about the persistence of cache lines are formed using
    /// cacheLineUpdates rather than pendingCacheLineUpdates.
    /// Thus, any persistence queries made during the middle of an epoch will
    /// not falsely report that writes have been persisted.
    ///
    /// When the epoch ends, the pending flushes are added to cacheLineUpdates,
    /// preserving their original ordering WRT the writes.
    ///
    /// Example program:
    ///   WRITE cache line 0
    ///   FLUSH cache line 0
    ///   SFENCE              // end of epoch 0
    ///   WRITE cache line 2
    ///   FLUSH cache line 2
    ///   WRITE cache line 2
    ///   SFENCE              // end of epoch 1
    ///
    /// Contents of update lists:
    ///
    ///   At start of epoch 1:
    ///     v pendingCacheLineUpdates
    ///     [0]=1 -> [0]=0 -> nullptr
    ///     ^ cacheLineUpdates
    ///
    ///   Before the second SFENCE:
    ///     v pendingCacheLineUpdates
    ///     [2]=0 -> [2]=1 -> [2]=0 ->
    ///                                [0]=1 -> [0]=0 -> nullptr
    ///              [2]=0 -> [2]=0 ->
    ///              ^ cacheLineUpdates
    ///
    ///   After the second SFENCE:
    ///     v pendingCacheLineUpdates
    ///     [2]=0 -> [2]=1 -> [2]=0 -> [0]=1 -> [0]=0 -> nullptr
    ///     ^ cacheLineUpdates
    ///
    UpdateList cacheLineUpdates;
    UpdateList pendingCacheLineUpdates;

  public:
    /// Create a new persistent object state from the given non-persistent
    /// object state and symbolic bool array of cache lines.
    PersistentState(const ObjectState *os, const Array *cacheLines);

    PersistentState(const PersistentState &ps);

    virtual Kind getKind() const { return Persistent; }
    static bool classof(const ObjectState *os) {
      return os->getKind() == Persistent;
    }

    void write8(unsigned offset, uint8_t value) override;
    void write8(unsigned offset, ref<Expr> value) override;
    void write8(ref<Expr> offset, ref<Expr> value) override;

    void dirtyCacheLineAtOffset(unsigned offset);
    void dirtyCacheLineAtOffset(ref<Expr> offset);

    // Make a *pending* persist of the cache line containing offset.
    void persistCacheLineAtOffset(unsigned offset);
    void persistCacheLineAtOffset(ref<Expr> offset);

    void commitPendingPersists();

    // Returns true if all modified cache lines
    // are guaranteed to be persisted.
    ref<Expr> isPersisted() const;

    static ref<ConstantExpr> getPersistedExpr();
    static ref<ConstantExpr> getDirtyExpr();

  private:
    ref<Expr> isCacheLinePersisted(unsigned offset) const;
    ref<Expr> isCacheLinePersisted(ref<Expr> offset) const;

    ref<Expr> getCacheLine(ref<Expr> offset) const;
    unsigned numCacheLines() const;
    unsigned cacheLineSize() const;
};
  
} // End klee namespace

#endif /* KLEE_MEMORY_H */
