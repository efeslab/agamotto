#include "../../lib/Core/PersistentMemoryState.h"
#include "gtest/gtest.h"
#include "gtest/gtest-death-test.h"

#include <cerrno>
#include <sstream>

using namespace klee;

using PersistInterval = PersistentMemoryState::PersistInterval;
using PersistIntervalsResult = PersistentMemoryState::PersistIntervalsResult;
#define INF PersistentMemoryState::EPOCH_INF

void store(PersistentMemoryState &state,
           uint64_t begin, uint64_t end) {
  state.store(begin, end-begin);
}

void flush(PersistentMemoryState &state, uint64_t addr) {
  state.flush(addr);
}

void fence(PersistentMemoryState &state) {
  state.fence();
}

auto isPersisted(PersistentMemoryState &state,
                 uint64_t begin, uint64_t end) {
  return state.isPersisted(begin, end-begin);
}

auto isOrderedBefore(PersistentMemoryState &state,
                     uint64_t beginA, uint64_t endA,
                     uint64_t beginB, uint64_t endB) {
  return state.isOrderedBefore(beginA, endA-beginA,
                               beginB, endB-beginB);
}

auto getPersistInterval(PersistentMemoryState &state,
                        uint64_t begin, uint64_t end) {
  return state.getPersistInterval(begin, end-begin);
}

TEST(PMemTest, Epoch) {
  PersistentMemoryState state;

  ASSERT_EQ(state.getEpoch(), 0u);
  fence(state);
  ASSERT_EQ(state.getEpoch(), 1u);
  store(state, 0, 1);
  flush(state, 0);
  ASSERT_EQ(state.getEpoch(), 1u);
  fence(state);
  ASSERT_EQ(state.getEpoch(), 2u);
}

TEST(PMemTest, DefaultCacheAlign) {
  PersistentMemoryState state;
  ASSERT_EQ(state.getCacheLineSize(), 64u);
}

TEST(PMemTest, CacheAlign) {
  PersistentMemoryState state(10);
  ASSERT_EQ(state.getCacheLineSize(), 10u);
}

TEST(PMemTest, IsPersist) {
  PersistentMemoryState state;

  // Try on never-before modified.
  ASSERT_FALSE(isPersisted(state, 10, 20));
  ASSERT_EQ(getPersistInterval(state, 10, 20),
            PersistInterval(INF, INF));

  // **MODIFY**
  store(state, 10, 20);
  ASSERT_FALSE(isPersisted(state, 10, 20));
  ASSERT_FALSE(isPersisted(state, 1, 3));
  ASSERT_FALSE(isPersisted(state, 12, 14));
  ASSERT_FALSE(isPersisted(state, 21, 30));
  ASSERT_EQ(getPersistInterval(state, 10, 20),
            PersistInterval(0, INF));

  // **FENCE**
  fence(state);
  ASSERT_FALSE(isPersisted(state, 10, 20));
  ASSERT_EQ(getPersistInterval(state, 10, 20),
            PersistInterval(0, INF));

  // **FLUSH**
  flush(state, 0);
  ASSERT_FALSE(isPersisted(state, 0, 10));
  ASSERT_EQ(getPersistInterval(state, 10, 20),
            PersistInterval(0, INF));

  // **FENCE**
  fence(state);
  ASSERT_TRUE(isPersisted(state, 10, 20));
  ASSERT_TRUE(isPersisted(state, 15, 17));
  ASSERT_EQ(getPersistInterval(state, 10, 20),
            PersistInterval(0, 1));

  // Try with a range that goes before/past.
  ASSERT_TRUE(isPersisted(state, 0, 20));
  ASSERT_TRUE(isPersisted(state, 10, 30));
  ASSERT_TRUE(isPersisted(state, 0, 30));

  // Try on zero-width range.
  ASSERT_FALSE(isPersisted(state, 12, 12));

  ASSERT_EQ(getPersistInterval(state, 12, 12),
            PersistInterval(INF, INF));
}

TEST(PMemTest, IsOrdered) {
  auto A1 = 0, A2 = 100;
  auto B1 = 100, B2 = 150;
  auto p = 0;
  auto q = 64;
  auto r = 128;

  {
    PersistentMemoryState state(64);

    // epoch 0
    store(state, A1, A2);
    flush(state, p);
    fence(state);
    // epoch 1
    store(state, B1, B2);
    flush(state, q);
    flush(state, r);
    fence(state);
    ASSERT_FALSE(isOrderedBefore(state, A1, A2, B1, B2));
  }

  {
    PersistentMemoryState state(64);
    // epoch 0
    store(state, A1, A2);
    flush(state, p);
    flush(state, q);
    fence(state);
    // epoch 1
    store(state, B1, B2);
    flush(state, r);
    fence(state);
    ASSERT_TRUE(isOrderedBefore(state, A1, A2, B1, B2));
  }
}

TEST(PMemTest, RewriteAfterFlush) {
  PersistentMemoryState state;

  store(state, 10, 20);
  flush(state, 10);
  store(state, 20, 30);
  fence(state);
  ASSERT_FALSE(isPersisted(state, 10, 20));
  ASSERT_FALSE(isPersisted(state, 20, 30));

  store(state, 10, 20);
  flush(state, 10);
  store(state, 20, 30);
  flush(state, 10);
  fence(state);
  ASSERT_TRUE(isPersisted(state, 10, 20));
  ASSERT_TRUE(isPersisted(state, 20, 30));
}

TEST(PMemTest, PersistInterval) {
  // Make [10, 20)->[0,0]
  //      [20, 30)->[1,2]
  //      [30, 40)->[1,1]
  //      [40, 50)->[2,INF]
  //      [50, 60)->[0,1]
  PersistentMemoryState state(10);
  // epoch 0
  store(state, 10, 20);
  store(state, 50, 60);
  flush(state, 10);
  fence(state);
  // epoch 1
  store(state, 20, 30);
  store(state, 30, 40);
  flush(state, 30);
  flush(state, 50);
  fence(state);
  // epoch 2
  store(state, 40, 50);
  flush(state, 20);
  fence(state);

  // Make [10, 20)->[0,0]
  //      [20, 30)->[1,2]
  //      [30, 40)->[1,1]
  //      [40, 50)->[2,INF]
  //      [50, 60)->[0,1]
  ASSERT_EQ(getPersistInterval(state, 10, 20),
            PersistInterval(0, 0));
  ASSERT_EQ(getPersistInterval(state, 20, 30),
            PersistInterval(1, 2));
  ASSERT_EQ(getPersistInterval(state, 30, 40),
            PersistInterval(1, 1));
  ASSERT_EQ(getPersistInterval(state, 40, 50),
            PersistInterval(2, INF));
  ASSERT_EQ(getPersistInterval(state, 50, 60),
            PersistInterval(0, 1));

  // Now some combos
  ASSERT_EQ(getPersistInterval(state, 10, 60),
            PersistInterval(0, INF));
  ASSERT_EQ(getPersistInterval(state, 10, 30),
            PersistInterval(0, 2));
  ASSERT_EQ(getPersistInterval(state, 20, 40),
            PersistInterval(1, 2));
  ASSERT_EQ(getPersistInterval(state, 30, 50),
            PersistInterval(1, INF));

  // Now some that include unknowns
  /* ASSERT_EQ(getPersistInterval(state, 0, 20), */
  /*           PersistInterval(INF, INF)); */
  /* ASSERT_EQ(getPersistInterval(state, 50, 70), */
  /*           PersistInterval(INF, INF)); */
}

