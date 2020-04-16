//===-- Memory.cpp --------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Memory.h"

#include "Context.h"
#include "MemoryManager.h"
#include "ObjectHolder.h"

#include "klee/Expr/ArrayCache.h" 
#include "klee/Expr/Expr.h"
#include "klee/Expr/ExprPPrinter.h"
#include "klee/Internal/Support/ErrorHandling.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/OptionCategories.h"
#include "klee/Solver/Solver.h"
#include "klee/util/BitArray.h"
#include "klee/ExecutionState.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include <cassert>
#include <sstream>

using namespace klee;

using llvm::GlobalValue;
using llvm::Instruction;

namespace {
  llvm::cl::opt<bool>
  UseConstantArrays("use-constant-arrays",
                    llvm::cl::desc("Use constant arrays instead of updates when possible (default=true)\n"),
                    llvm::cl::init(true),
                    llvm::cl::cat(SolvingCat));
}

/* #region ObjectHolder */

ObjectHolder::ObjectHolder(const ObjectHolder &b) : os(b.os) { 
  if (os) ++os->refCount; 
}

ObjectHolder::ObjectHolder(ObjectState *_os) : os(_os) { 
  if (os) ++os->refCount; 
}

ObjectHolder::~ObjectHolder() { 
  if (os && --os->refCount==0) delete os; 
}
  
ObjectHolder &ObjectHolder::operator=(const ObjectHolder &b) {
  if (b.os) ++b.os->refCount;
  if (os && --os->refCount==0) delete os;
  os = b.os;
  return *this;
}

/* #endregion */

/* #region MemoryObject */

int MemoryObject::counter = 0;

MemoryObject::~MemoryObject() {
  if (parent) {
    // klee_warning("Memory object %p pointing to %p is destructing!", this, (void*)this->address);
    parent->markFreed(this);
  }
}

void MemoryObject::getAllocInfo(std::string &result) const {
  llvm::raw_string_ostream info(result);

  info << "MO" << id << "[" << size << "]";

  if (allocSite) {
    info << " allocated at ";
    if (const Instruction *i = dyn_cast<Instruction>(allocSite)) {
      info << i->getParent()->getParent()->getName() << "():";
      info << *i;
    } else if (const GlobalValue *gv = dyn_cast<GlobalValue>(allocSite)) {
      info << "global:" << gv->getName();
    } else {
      info << "value:" << *allocSite;
    }
  } else {
    info << " (no allocation info)";
  }
  
  info.flush();
}

/* #endregion */

/* #region ObjectState */

ObjectState::ObjectState(const MemoryObject *mo)
  : copyOnWriteOwner(0),
    refCount(0),
    object(mo),
    concreteStore(new uint8_t[mo->size]),
    concreteMask(0),
    flushMask(0),
    knownSymbolics(0),
    updates(0, 0),
    size(mo->size),
    readOnly(false) {
  mo->refCount++;
  if (!UseConstantArrays) {
    static unsigned id = 0;
    const Array *array =
        getArrayCache()->CreateArray("tmp_arr" + llvm::utostr(++id), size);
    updates = UpdateList(array, 0);
  }
  memset(concreteStore, 0, size);
}


ObjectState::ObjectState(const MemoryObject *mo, const Array *array)
  : copyOnWriteOwner(0),
    refCount(0),
    object(mo),
    concreteStore(new uint8_t[mo->size]),
    concreteMask(0),
    flushMask(0),
    knownSymbolics(0),
    updates(array, 0),
    size(mo->size),
    readOnly(false) {
  mo->refCount++;
  makeSymbolic();
  memset(concreteStore, 0, size);
}

ObjectState::ObjectState(const ObjectState &os) 
  : copyOnWriteOwner(0),
    refCount(0),
    object(os.object),
    concreteStore(new uint8_t[os.size]),
    concreteMask(os.concreteMask ? new BitArray(*os.concreteMask, os.size) : 0),
    flushMask(os.flushMask ? new BitArray(*os.flushMask, os.size) : 0),
    knownSymbolics(0),
    updates(os.updates),
    size(os.size),
    readOnly(false) {
  assert(!os.readOnly && "no need to copy read only object?");
  if (object)
    object->refCount++;

  if (os.knownSymbolics) {
    knownSymbolics = new ref<Expr>[size];
    for (unsigned i=0; i<size; i++)
      knownSymbolics[i] = os.knownSymbolics[i];
  }

  memcpy(concreteStore, os.concreteStore, size*sizeof(*concreteStore));
}

ObjectState::~ObjectState() {
  delete concreteMask;
  delete flushMask;
  delete[] knownSymbolics;
  delete[] concreteStore;

  if (object)
  {
    assert(object->refCount > 0);
    object->refCount--;
    if (object->refCount == 0)
    {
      delete object;
    }
  }
}

ObjectState *ObjectState::clone() const {
  return new ObjectState(*this);
}

ArrayCache *ObjectState::getArrayCache() const {
  assert(object && "object was NULL");
  return object->parent->getArrayCache();
}

/***/

const UpdateList &ObjectState::getUpdates() const {
  // Constant arrays are created lazily.
  if (!updates.root) {
    // Collect the list of writes, with the oldest writes first.
    
    // FIXME: We should be able to do this more efficiently, we just need to be
    // careful to get the interaction with the cache right. In particular we
    // should avoid creating UpdateNode instances we never use.
    unsigned NumWrites = updates.head ? updates.head->getSize() : 0;
    std::vector< std::pair< ref<Expr>, ref<Expr> > > Writes(NumWrites);
    const UpdateNode *un = updates.head;
    for (unsigned i = NumWrites; i != 0; un = un->next) {
      --i;
      Writes[i] = std::make_pair(un->index, un->value);
    }

    std::vector< ref<ConstantExpr> > Contents(size);

    // Initialize to zeros.
    for (unsigned i = 0, e = size; i != e; ++i)
      Contents[i] = ConstantExpr::create(0, Expr::Int8);

    // Pull off as many concrete writes as we can.
    unsigned Begin = 0, End = Writes.size();
    for (; Begin != End; ++Begin) {
      // Push concrete writes into the constant array.
      ConstantExpr *Index = dyn_cast<ConstantExpr>(Writes[Begin].first);
      if (!Index)
        break;

      ConstantExpr *Value = dyn_cast<ConstantExpr>(Writes[Begin].second);
      if (!Value)
        break;

      Contents[Index->getZExtValue()] = Value;
    }

    static unsigned id = 0;
    const Array *array = getArrayCache()->CreateArray(
        "const_arr" + llvm::utostr(++id), size, &Contents[0],
        &Contents[0] + Contents.size());
    updates = UpdateList(array, 0);

    // Apply the remaining (non-constant) writes.
    for (; Begin != End; ++Begin)
      updates.extend(Writes[Begin].first, Writes[Begin].second);
  }

  return updates;
}

void ObjectState::flushToConcreteStore(TimingSolver *solver,
                                       const ExecutionState &state) const {
  for (unsigned i = 0; i < size; i++) {
    if (isByteKnownSymbolic(i)) {
      ref<ConstantExpr> ce;
      bool success = solver->getValue(state, read8(i), ce);
      if (!success)
        klee_warning("Solver timed out when getting a value for external call, "
                     "byte %p+%u will have random value",
                     (void *)object->address, i);
      else
        ce->toMemory(concreteStore + i);
    }
  }
}

void ObjectState::makeConcrete() {
  delete concreteMask;
  delete flushMask;
  delete[] knownSymbolics;
  concreteMask = 0;
  flushMask = 0;
  knownSymbolics = 0;
}

void ObjectState::makeSymbolic() {
  assert(!updates.head &&
         "XXX makeSymbolic of objects with symbolic values is unsupported");

  // XXX simplify this, can just delete various arrays I guess
  for (unsigned i=0; i<size; i++) {
    markByteSymbolic(i);
    setKnownSymbolic(i, 0);
    markByteFlushed(i);
  }
}

void ObjectState::initializeToZero() {
  makeConcrete();
  memset(concreteStore, 0, size);
}

void ObjectState::initializeToRandom() {  
  makeConcrete();
  for (unsigned i=0; i<size; i++) {
    // randomly selected by 256 sided die
    concreteStore[i] = 0xAB;
  }
}

/*
Cache Invariants
--
isByteKnownSymbolic(i) => !isByteConcrete(i)
isByteConcrete(i) => !isByteKnownSymbolic(i)
!isByteFlushed(i) => (isByteConcrete(i) || isByteKnownSymbolic(i))
 */

void ObjectState::fastRangeCheckOffset(ref<Expr> offset,
                                       unsigned *base_r,
                                       unsigned *size_r) const {
  *base_r = 0;
  *size_r = size;
}

void ObjectState::flushRangeForRead(unsigned rangeBase, 
                                    unsigned rangeSize) const {
  if (!flushMask) flushMask = new BitArray(size, true);
 
  for (unsigned offset=rangeBase; offset<rangeBase+rangeSize; offset++) {
    if (!isByteFlushed(offset)) {
      if (isByteConcrete(offset)) {
        updates.extend(ConstantExpr::create(offset, Expr::Int32),
                       ConstantExpr::create(concreteStore[offset], Expr::Int8));
      } else {
        assert(isByteKnownSymbolic(offset) && "invalid bit set in flushMask");
        updates.extend(ConstantExpr::create(offset, Expr::Int32),
                       knownSymbolics[offset]);
      }

      flushMask->unset(offset);
    }
  } 
}

void ObjectState::flushRangeForWrite(unsigned rangeBase, 
                                     unsigned rangeSize) {
  if (!flushMask) flushMask = new BitArray(size, true);

  for (unsigned offset=rangeBase; offset<rangeBase+rangeSize; offset++) {
    if (!isByteFlushed(offset)) {
      if (isByteConcrete(offset)) {
        updates.extend(ConstantExpr::create(offset, Expr::Int32),
                       ConstantExpr::create(concreteStore[offset], Expr::Int8));
        markByteSymbolic(offset);
      } else {
        assert(isByteKnownSymbolic(offset) && "invalid bit set in flushMask");
        updates.extend(ConstantExpr::create(offset, Expr::Int32),
                       knownSymbolics[offset]);
        setKnownSymbolic(offset, 0);
      }

      flushMask->unset(offset);
    } else {
      // flushed bytes that are written over still need
      // to be marked out
      if (isByteConcrete(offset)) {
        markByteSymbolic(offset);
      } else if (isByteKnownSymbolic(offset)) {
        setKnownSymbolic(offset, 0);
      }
    }
  } 
}

bool ObjectState::isByteConcrete(unsigned offset) const {
  return !concreteMask || concreteMask->get(offset);
}

bool ObjectState::isByteFlushed(unsigned offset) const {
  return flushMask && !flushMask->get(offset);
}

bool ObjectState::isByteKnownSymbolic(unsigned offset) const {
  return knownSymbolics && knownSymbolics[offset].get();
}

void ObjectState::markByteConcrete(unsigned offset) {
  if (concreteMask)
    concreteMask->set(offset);
}

void ObjectState::markByteSymbolic(unsigned offset) {
  if (!concreteMask)
    concreteMask = new BitArray(size, true);
  concreteMask->unset(offset);
}

void ObjectState::markByteUnflushed(unsigned offset) {
  if (flushMask)
    flushMask->set(offset);
}

void ObjectState::markByteFlushed(unsigned offset) {
  if (!flushMask) {
    flushMask = new BitArray(size, false);
  } else {
    flushMask->unset(offset);
  }
}

void ObjectState::setKnownSymbolic(unsigned offset, 
                                   Expr *value /* can be null */) {
  if (knownSymbolics) {
    knownSymbolics[offset] = value;
  } else {
    if (value) {
      knownSymbolics = new ref<Expr>[size];
      knownSymbolics[offset] = value;
    }
  }
}

/***/

ref<Expr> ObjectState::read8(unsigned offset) const {
  if (isByteConcrete(offset)) {
    return ConstantExpr::create(concreteStore[offset], Expr::Int8);
  } else if (isByteKnownSymbolic(offset)) {
    return knownSymbolics[offset];
  } else {
    assert(isByteFlushed(offset) && "unflushed byte without cache value");
    
    return ReadExpr::create(getUpdates(), 
                            ConstantExpr::create(offset, Expr::Int32));
  }    
}

ref<Expr> ObjectState::read8(ref<Expr> offset) const {
  assert(!isa<ConstantExpr>(offset) && "constant offset passed to symbolic read8");
  unsigned base, size;
  fastRangeCheckOffset(offset, &base, &size);
  flushRangeForRead(base, size);

  if (size>4096) {
    std::string allocInfo;
    object->getAllocInfo(allocInfo);
    klee_warning_once(0, "flushing %d bytes on read, may be slow and/or crash: %s", 
                      size,
                      allocInfo.c_str());
  }
  
  return ReadExpr::create(getUpdates(), ZExtExpr::create(offset, Expr::Int32));
}

void ObjectState::write8(const ExecutionState &_unused, unsigned offset, uint8_t value) {
  //assert(read_only == false && "writing to read-only object!");
  concreteStore[offset] = value;
  setKnownSymbolic(offset, 0);

  markByteConcrete(offset);
  markByteUnflushed(offset);
}

void ObjectState::write8(const ExecutionState &state, unsigned offset, ref<Expr> value) {
  // can happen when ExtractExpr special cases
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(value)) {
    write8(state, offset, (uint8_t) CE->getZExtValue(8));
  } else {
    setKnownSymbolic(offset, value.get());
      
    markByteSymbolic(offset);
    markByteUnflushed(offset);
  }
}

void ObjectState::write8(const ExecutionState &_unused, ref<Expr> offset, ref<Expr> value) {
  assert(!isa<ConstantExpr>(offset) && "constant offset passed to symbolic write8");
  unsigned base, size;
  fastRangeCheckOffset(offset, &base, &size);
  flushRangeForWrite(base, size);

  if (size>4096) {
    std::string allocInfo;
    object->getAllocInfo(allocInfo);
    klee_warning_once(0, "flushing %d bytes on read, may be slow and/or crash: %s", 
                      size,
                      allocInfo.c_str());
  }
  
  updates.extend(ZExtExpr::create(offset, Expr::Int32), value);
}

/***/

ref<Expr> ObjectState::read(ref<Expr> offset, Expr::Width width) const {
  // Truncate offset to 32-bits.
  offset = ZExtExpr::create(offset, Expr::Int32);

  // Check for reads at constant offsets.
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(offset))
    return read(CE->getZExtValue(32), width);

  // Treat bool specially, it is the only non-byte sized write we allow.
  if (width == Expr::Bool)
    return ExtractExpr::create(read8(offset), 0, Expr::Bool);

  // Otherwise, follow the slow general case.
  unsigned NumBytes = width / 8;
  assert(width == NumBytes * 8 && "Invalid read size!");
  ref<Expr> Res(0);
  for (unsigned i = 0; i != NumBytes; ++i) {
    unsigned idx = Context::get().isLittleEndian() ? i : (NumBytes - i - 1);
    ref<Expr> Byte = read8(AddExpr::create(offset, 
                                           ConstantExpr::create(idx, 
                                                                Expr::Int32)));
    Res = i ? ConcatExpr::create(Byte, Res) : Byte;
  }

  return Res;
}

ref<Expr> ObjectState::read(unsigned offset, Expr::Width width) const {
  // Treat bool specially, it is the only non-byte sized write we allow.
  if (width == Expr::Bool)
    return ExtractExpr::create(read8(offset), 0, Expr::Bool);

  // Otherwise, follow the slow general case.
  unsigned NumBytes = width / 8;
  assert(width == NumBytes * 8 && "Invalid width for read size!");
  ref<Expr> Res(0);
  for (unsigned i = 0; i != NumBytes; ++i) {
    unsigned idx = Context::get().isLittleEndian() ? i : (NumBytes - i - 1);
    ref<Expr> Byte = read8(offset + idx);
    Res = i ? ConcatExpr::create(Byte, Res) : Byte;
  }

  return Res;
}

void ObjectState::write(const ExecutionState &state, ref<Expr> offset, ref<Expr> value) {
  // Truncate offset to 32-bits.
  offset = ZExtExpr::create(offset, Expr::Int32);

  // Check for writes at constant offsets.
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(offset)) {
    write(state, CE->getZExtValue(32), value);
    return;
  }

  // Treat bool specially, it is the only non-byte sized write we allow.
  Expr::Width w = value->getWidth();
  if (w == Expr::Bool) {
    write8(state, offset, ZExtExpr::create(value, Expr::Int8));
    return;
  }

  // Otherwise, follow the slow general case.
  unsigned NumBytes = w / 8;
  assert(w == NumBytes * 8 && "Invalid write size!");
  for (unsigned i = 0; i != NumBytes; ++i) {
    unsigned idx = Context::get().isLittleEndian() ? i : (NumBytes - i - 1);
    write8(state, AddExpr::create(offset, ConstantExpr::create(idx, Expr::Int32)),
           ExtractExpr::create(value, 8 * i, Expr::Int8));
  }
}

void ObjectState::write(const ExecutionState &state, unsigned offset, ref<Expr> value) {
  // Check for writes of constant values.
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(value)) {
    Expr::Width w = CE->getWidth();
    if (w <= 64 && bits64::isPowerOfTwo(w)) {
      uint64_t val = CE->getZExtValue();
      switch (w) {
      default: assert(0 && "Invalid write size!");
      case  Expr::Bool:
      case  Expr::Int8:  write8(state, offset, val); return;
      case Expr::Int16: write16(state, offset, val); return;
      case Expr::Int32: write32(state, offset, val); return;
      case Expr::Int64: write64(state, offset, val); return;
      }
    }
  }

  // Treat bool specially, it is the only non-byte sized write we allow.
  Expr::Width w = value->getWidth();
  if (w == Expr::Bool) {
    write8(state, offset, ZExtExpr::create(value, Expr::Int8));
    return;
  }

  // Otherwise, follow the slow general case.
  unsigned NumBytes = w / 8;
  assert(w == NumBytes * 8 && "Invalid write size!");
  for (unsigned i = 0; i != NumBytes; ++i) {
    unsigned idx = Context::get().isLittleEndian() ? i : (NumBytes - i - 1);
    write8(state, offset + idx, ExtractExpr::create(value, 8 * i, Expr::Int8));
  }
} 

void ObjectState::write16(const ExecutionState &state, unsigned offset, uint16_t value) {
  unsigned NumBytes = 2;
  for (unsigned i = 0; i != NumBytes; ++i) {
    unsigned idx = Context::get().isLittleEndian() ? i : (NumBytes - i - 1);
    write8(state, offset + idx, (uint8_t) (value >> (8 * i)));
  }
}

void ObjectState::write32(const ExecutionState &state, unsigned offset, uint32_t value) {
  unsigned NumBytes = 4;
  for (unsigned i = 0; i != NumBytes; ++i) {
    unsigned idx = Context::get().isLittleEndian() ? i : (NumBytes - i - 1);
    write8(state, offset + idx, (uint8_t) (value >> (8 * i)));
  }
}

void ObjectState::write64(const ExecutionState &state, unsigned offset, uint64_t value) {
  unsigned NumBytes = 8;
  for (unsigned i = 0; i != NumBytes; ++i) {
    unsigned idx = Context::get().isLittleEndian() ? i : (NumBytes - i - 1);
    write8(state, offset + idx, (uint8_t) (value >> (8 * i)));
  }
}

void ObjectState::print() const {
  llvm::errs() << "-- ObjectState --\n";
  llvm::errs() << "\tMemoryObject ID: " << object->id << "\n";
  llvm::errs() << "\tRoot Object: " << updates.root << "\n";
  llvm::errs() << "\tSize: " << size << "\n";

  llvm::errs() << "\tBytes:\n";
  for (unsigned i=0; i<size; i++) {
    llvm::errs() << "\t\t["<<i<<"]"
               << " concrete? " << isByteConcrete(i)
               << " known-sym? " << isByteKnownSymbolic(i)
               << " flushed? " << isByteFlushed(i) << " = ";
    ref<Expr> e = read8(i);
    llvm::errs() << e << "\n";
  }

  llvm::errs() << "\tUpdates:\n";
  for (const UpdateNode *un=updates.head; un; un=un->next) {
    llvm::errs() << "\t\t[" << un->index << "] = " << un->value << "\n";
  }
}

/* #endregion */

/* #region PersistentState */

uint64_t PersistentState::MaxSize = 4lu * (4096lu);

PersistentState::PersistentState(const ObjectState *os)
  : ObjectState(*os),
    cacheLineUpdates(nullptr, nullptr),
    pendingCacheLineUpdates(nullptr, nullptr),
    rootCauseLocations(nullptr, nullptr),
    pendingRootCauseLocations(nullptr, nullptr) {

  // Set up all the symbolic Arrays we need.
  ArrayCache *arrayCache = getArrayCache();
  const MemoryObject *object = getObject();
  std::string baseName = object->name;
  uint64_t size = object->parent->getSizeInCacheLines(object->size);

  // For initializing values
  std::vector<ref<ConstantExpr> > Init(size);

  // First, the symbolic cache line tracking array (initialize to persisted).
  Init.assign(size, getPersistedExpr());
  const Array *cacheLines = arrayCache->CreateArray(baseName + "_cacheLines", size,
                                                    &Init[0], &Init[0] + size,
                                                    Expr::Int32 /* domain */,
                                                    Expr::Int8 /* range */);
  cacheLineUpdates = UpdateList(cacheLines, nullptr);
  pendingCacheLineUpdates = UpdateList(cacheLineUpdates);

  // Set up the root causes symbolic array (initialize to nullptr).
  Init.assign(size, getNullptr());
  const Array *rootCauses = arrayCache->CreateArray(baseName + "_rootCauses", size,
                                                    &Init[0], &Init[0] + size,
                                                    Expr::Int32 /* domain */,
                                                    Expr::Int64 /* range */);
  rootCauseLocations = UpdateList(rootCauses, nullptr);
  pendingRootCauseLocations = UpdateList(rootCauseLocations);

  // Set up a symbolic integer to act as an "arbitrary offset" into this.
  const Array *idxArray = arrayCache->CreateArray(baseName + "_idx", 1,
                                                       nullptr, nullptr,
                                                       Expr::Int32,
                                                       Expr::Int32);
  // ReadExpr just copies the UpdateList you give it;
  // no need to store one as a member variable.
  idxUnbounded = ReadExpr::create(UpdateList(idxArray, nullptr),
                                  ConstantExpr::create(0, idxArray->range));
}

PersistentState::PersistentState(const PersistentState &ps)
  : ObjectState(ps),
    cacheLineUpdates(ps.cacheLineUpdates),
    pendingCacheLineUpdates(ps.pendingCacheLineUpdates),
    idxUnbounded(ps.idxUnbounded),
    rootCauseLocations(ps.rootCauseLocations),
    pendingRootCauseLocations(ps.rootCauseLocations),
    nextLocId(ps.nextLocId),
    allRootLocations(ps.allRootLocations) {}

ObjectState *PersistentState::clone() const {
  return new PersistentState(*this);
}

void PersistentState::write8(const ExecutionState &state,
                             unsigned offset, uint8_t value) {
  ObjectState::write8(state, offset, value);
  dirtyCacheLineAtOffset(state, offset);
}

void PersistentState::write8(const ExecutionState &state,
                             unsigned offset, ref<Expr> value) {
  ObjectState::write8(state, offset, value);
  dirtyCacheLineAtOffset(state, offset);
}

void PersistentState::write8(const ExecutionState &state,
                             ref<Expr> offset, ref<Expr> value) {
  ObjectState::write8(state, offset, value);
  dirtyCacheLineAtOffset(state, offset);
}

void PersistentState::dirtyCacheLineAtOffset(const ExecutionState &state,
                                             unsigned offset) {
  dirtyCacheLineAtOffset(state, ConstantExpr::create(offset, Expr::Int32));
}

void PersistentState::dirtyCacheLineAtOffset(const ExecutionState &state,
                                             ref<Expr> offset) {
  /* llvm::errs() << getObject()->name << ":\n"; */
  /* ExprPPrinter::printOne(llvm::errs(), "dirtyCacheLineAtOffset", offset); */

  // Apply the dirty to the authoritative update list as well as the pending one
  // (so that we can properly identify unpersisted lines in the middle of an epoch).
  ref<Expr> cacheLine = getCacheLine(offset);
  ref<Expr> falseExpr = getDirtyExpr();
  cacheLineUpdates.extend(cacheLine, falseExpr);
  pendingCacheLineUpdates.extend(cacheLine, falseExpr);

  // Now update root cause.
  std::string info = getLocationInfo(state);
  if (!allRootLocations[info]) {
    allRootLocations[info] = nextLocId;
    nextLocId++;
  }

  ref<Expr> rootCauseExpr = 
    ConstantExpr::create(allRootLocations[info], rootCauseLocations.root->range);
  rootCauseLocations.extend(cacheLine, rootCauseExpr);
  // pendingRootCauseLocations.extend(cacheLine, rootCauseExpr);
}

void PersistentState::persistCacheLineAtOffset(const ExecutionState &state, 
                                               unsigned offset) {
  persistCacheLineAtOffset(state, ConstantExpr::create(offset, Expr::Int32));
}

void PersistentState::persistCacheLineAtOffset(const ExecutionState &state,
                                               ref<Expr> offset) {
  /* llvm::errs() << getObject()->name << ":\n"; */
  /* ExprPPrinter::printOne(llvm::errs(), "persistCacheLineAtOffset", offset); */
  ref<Expr> cacheLine = getCacheLine(offset);
  pendingCacheLineUpdates.extend(cacheLine, getPersistedExpr());

  // Now update root cause.
  std::string info = getLocationInfo(state);
  if (!allRootLocations[info]) {
    allRootLocations[info] = nextLocId;
    nextLocId++;
  }

  ref<Expr> rootCauseExpr = 
    ConstantExpr::create(allRootLocations[info], rootCauseLocations.root->range);
  rootCauseLocations.extend(cacheLine, rootCauseExpr);

  // Root cause 
  // pendingRootCauseLocations.extend(cacheLine, getNullptr());
}

void PersistentState::commitPendingPersists(const ExecutionState &state) {
  /* llvm::errs() << getObject()->name << ": "; */
  /* llvm::errs() << "commitPendingPersists\n"; */

  // Apply the writes and flushes accumulated during this epoch.
  // The UpdateList will clean up orphaned UpdateNodes from cacheLineUpdates.
  cacheLineUpdates = pendingCacheLineUpdates;
  // rootCauseLocations = pendingRootCauseLocations;
  lastCommit = getLocationInfo(state); 
}

ref<Expr> PersistentState::getIsOffsetPersistedExpr(ref<Expr> offset,
                                                    bool pending) const {
  return isCacheLinePersisted(getCacheLine(offset), pending);
}

ref<Expr> PersistentState::getAnyOffsetExpr() const {
  return ZExtExpr::create(idxUnbounded, Context::get().getPointerWidth());
}

#if 0
ref<Expr> PersistentState::isPersisted(TimingSolver *solver, 
                                       ExecutionState &state) const {
  static ref<ConstantExpr> alwaysTrue = ConstantExpr::create(true, Expr::Bool);
  static ref<ConstantExpr> alwaysFalse = ConstantExpr::create(false, Expr::Bool);

  std::pair<ref<Expr>, ref<Expr>> range = solver->getRange(state, isCacheLinePersisted(idxUnbounded));
  assert(isa<ConstantExpr>(range.first) && isa<ConstantExpr>(range.second));
  ref<ConstantExpr> lo = dyn_cast<ConstantExpr>(range.first);
  ref<ConstantExpr> hi = dyn_cast<ConstantExpr>(range.second);
  if (lo->getZExtValue() == 0) {
    assert(cacheLineUpdates.getSize() > 1 && "doesn't make sense");
    return alwaysFalse;
  }
  assert(lo->getZExtValue() == 1 && hi->getZExtValue() == 1);
  return alwaysTrue;
}
#endif

void PersistentState::clearRootCauses() {
  allRootLocations.clear();
}

std::unordered_set<std::string>
PersistentState::getRootCauses(TimingSolver *solver,
                               ExecutionState &state) const {
  std::unordered_set<std::string> causes;

  auto idx = getAnyOffsetExpr();
  auto inBoundsConstraint = getObject()->getBoundsCheckOffset(idx);

  state.constraints.addConstraint(inBoundsConstraint);
  causes = getRootCause(solver, state, getCacheLine(idx));
  state.constraints.removeConstraint(inBoundsConstraint);
  
  return causes;
}

ref<Expr> PersistentState::isCacheLinePersisted(unsigned cacheLine,
                                                bool pending) const {
  return isCacheLinePersisted(ConstantExpr::create(cacheLine, Expr::Int32),
                              pending);
}

ref<Expr> PersistentState::isCacheLinePersisted(ref<Expr> cacheLine,
                                                bool pending) const {
  auto &updateList = pending ? pendingCacheLineUpdates : cacheLineUpdates;
  ref<Expr> result = ReadExpr::create(updateList,
                                      ZExtExpr::create(cacheLine, Expr::Int32));
  result = EqExpr::create(result, getPersistedExpr());
  return result;
}

std::unordered_set<std::string> PersistentState::getRootCause(TimingSolver *solver, 
                                                              ExecutionState &state, 
                                                              unsigned cacheLine) const {
  return getRootCause(solver, state, ConstantExpr::create(cacheLine, Expr::Int32));
}

std::unordered_set<std::string>
PersistentState::getRootCause(TimingSolver *solver,
                              ExecutionState &state,
                              ref<Expr> cacheLine) const {
  ref<Expr> result = ReadExpr::create(rootCauseLocations,
                                      ZExtExpr::create(cacheLine, Expr::Int32));
  
  std::unordered_set<std::string> possible;

  llvm::errs() << "getRootCause: " << *cacheLine << "\n";

  #if 1
  std::pair<ref<Expr>, ref<Expr>> range = solver->getRange(state, result);
  ref<ConstantExpr> lo = dyn_cast<ConstantExpr>(range.first);
  ref<ConstantExpr> hi = dyn_cast<ConstantExpr>(range.second); 
  assert(!lo.isNull() && !hi.isNull());
  if (lo->getZExtValue() == 0 && hi->getZExtValue() == 0) {
    llvm::errs() << "all 0\n";
    return possible;
  }

  for (uint64_t cl = 0; cl < numCacheLines(); ++cl) {
    ref<Expr> clExpr = ConstantExpr::create(cl, rootCauseLocations.root->range);
    ref<Expr> clVal = ReadExpr::create(rootCauseLocations, 
                                       ZExtExpr::create(clExpr, Expr::Int32));
    std::pair<ref<Expr>, ref<Expr>> range = solver->getRange(state, clVal);

    ref<ConstantExpr> lo = dyn_cast<ConstantExpr>(range.first);
    ref<ConstantExpr> hi = dyn_cast<ConstantExpr>(range.second);  
    assert(!lo.isNull() && !hi.isNull());
    llvm::errs() << "Range for CL #" << cl << ": [" << lo->getZExtValue() 
        << ", " << hi->getZExtValue() << "]\n";
    uint64_t loVal = lo->getZExtValue();
    uint64_t hiVal = hi->getZExtValue();
    if (loVal == 0 && hiVal == 0) continue;

    if (loVal == 0) loVal++;
    assert(loVal <= hiVal);

    if (loVal == hiVal) {
      uint64_t id = loVal;
      for (const auto &p : allRootLocations) {
        if (p.second == id) possible.insert(p.first); // copy
      } 
    } else {
      for (uint64_t id = loVal; id <= hiVal; ++id) {
        ref<Expr> eqId = EqExpr::create(ConstantExpr::create(id, rootCauseLocations.root->range),
                                        clVal);
        bool mayBeCause = false;
        bool success = solver->mayBeTrue(state, eqId, mayBeCause);
        assert(success);
        if (!mayBeCause) continue;
        for (const auto &p : allRootLocations) {
          if (p.second == id) possible.insert(p.first); // copy
        } 
      }
    }
  }
  #elif 0
  std::pair<ref<Expr>, ref<Expr>> range = solver->getRange(state, result);
  if (!isa<ConstantExpr>(range.first) || !isa<ConstantExpr>(range.second)) {
    llvm::errs() << *range.first << "\n" << *range.second << "\n";
    assert(false);
  }

  ref<ConstantExpr> lo = dyn_cast<ConstantExpr>(range.first);
  ref<ConstantExpr> hi = dyn_cast<ConstantExpr>(range.second);
  llvm::errs() << *range.first << "\n" << *range.second << "\n";
  for (uint64_t locId = lo->getZExtValue(); locId <= hi->getZExtValue(); ++locId) {
    ref<Expr> locIdExpr = ConstantExpr::create(locId, rootCauseLocations.root->range);
    ref<Expr> isCause = EqExpr::create(result, locIdExpr);
    bool mayBeCause = false;
    bool success = solver->mayBeTrue(state, isCause, mayBeCause);
    if (success && mayBeCause) {
      for (const auto &p : allRootLocations) {
        if (p.second == locId) possible.insert(p.first); // copy
      }
    } else if (!success) {
      klee_warning("Did not succeed at finding the root cause!");
    }
  }

  #else
  for (const auto &p : allRootLocations) {
    ref<Expr> locId = ConstantExpr::create(p.second, rootCauseLocations.root->range);
    ref<Expr> isCause = EqExpr::create(result, locId);
    bool mayBeCause = false;
    bool success = solver->mayBeTrue(state, isCause, mayBeCause);
    if (success && mayBeCause) {
      possible.insert(p.first); // copy
    } else if (!success) {
      klee_warning("Did not succeed at finding the root cause!");
    }
  }
  #endif

  #if 0
  // If there are no causes, then there must be no updates.
  if (!possible.size()) {
    ref<Expr> nullId = ConstantExpr::create(0, rootCauseLocations.root->range);
    ref<Expr> isNullptr = EqExpr::create(result, getNullptr());
    bool mustBeNull = false;
    bool success = solver->mustBeTrue(state, isNullptr, mustBeNull);

    assert(success && "Could not solve!");
    if (!mustBeNull) llvm::errs() << *isNullptr << "\n"; 
    assert(mustBeNull && "Could not prove null!");
  }
  #endif

  if (!possible.size()) possible.insert(lastCommit);

  return possible;
}

bool PersistentState::mustBePersisted(TimingSolver *solver, ExecutionState &state) const {
  bool ret = true;
  
  auto idx = getAnyOffsetExpr();
  auto inBoundsConstraint = getObject()->getBoundsCheckOffset(idx);

  state.constraints.addConstraint(inBoundsConstraint);
  bool mustBe;
  bool success = solver->mustBeTrue(state,
                                    getIsOffsetPersistedExpr(idx),
                                    mustBe);
  state.constraints.removeConstraint(inBoundsConstraint);

  assert(success && "Could not solve!");
  ret = ret && mustBe;
  if (!ret) return false;
  
  return true;
}

ref<Expr> PersistentState::getCacheLine(ref<Expr> offset) const {
  return ZExtExpr::create(UDivExpr::create(offset,
                                           ConstantExpr::create(cacheLineSize(),
                                                                offset->getWidth())),
                          Expr::Int32);
}

unsigned PersistentState::numCacheLines() const {
  return getObject()->parent->getSizeInCacheLines(size);
}

unsigned PersistentState::cacheLineSize() const {
  return getObject()->parent->getCacheAlignment();
}

ref<ConstantExpr> PersistentState::getPersistedExpr() {
  static ref<ConstantExpr> persisted = ConstantExpr::create(1, Expr::Int8);
  return persisted;
}

ref<ConstantExpr> PersistentState::getDirtyExpr() {
  static ref<ConstantExpr> dirty = ConstantExpr::create(0, Expr::Int8);
  return dirty;
}

ref<ConstantExpr> PersistentState::getNullptr() {
  static ref<ConstantExpr> none = ConstantExpr::create(0, Expr::Int64);
  return none;
}

ref<Expr> PersistentState::ptrAsExpr(void *ptr) {
  if (!ptr) return getNullptr();
  return ConstantExpr::create((uint64_t)ptr, Expr::Int64);
}

std::string PersistentState::getLocationInfo(const ExecutionState &state) {
  const InstructionInfo *iip = state.prevPC->info;

  std::string infoStr;
  llvm::raw_string_ostream msg(infoStr);
  msg << "Persistent Memory Info:\n";
  msg << "\tName: " << getObject()->name << "\n";
  msg << "\tAddress: " << getObject()->address << "\n";
  msg << "\tSize: " << getObject()->size << "\n";
  if (iip->file != "") {
    msg << "File: " << iip->file << "\n";
    msg << "Line: " << iip->line << "\n";
    msg << "assembly.ll line: " << iip->assemblyLine << "\n";
  }
  msg << "Stack: \n";
  state.dumpStack(msg);

  return infoStr;
}

/* #endregion */