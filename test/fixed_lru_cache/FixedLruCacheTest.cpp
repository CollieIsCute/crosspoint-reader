#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>

#include "lib/EpdFont/FixedLruCache.h"

namespace {

struct GlyphKey {
  uint32_t codepoint = 0;
  uint8_t style = 0;

  bool operator==(const GlyphKey& other) const {
    return codepoint == other.codepoint && style == other.style;
  }
};

struct GlyphKeyHash {
  size_t operator()(const GlyphKey& key) const {
    return static_cast<size_t>(key.codepoint * 2654435761u ^ key.style);
  }
};

struct ConstantHash {
  size_t operator()(int) const { return 0; }
};

}  // namespace

TEST(FixedLruCache, FindsEntriesAndEvictsLeastRecentlyUsed) {
  FixedLruCache<int, int, 3> cache;

  EXPECT_TRUE(cache.put(1, 10).inserted);
  EXPECT_TRUE(cache.put(2, 20).inserted);
  EXPECT_TRUE(cache.put(3, 30).inserted);
  ASSERT_EQ(cache.size(), 3u);

  ASSERT_NE(cache.find(1), nullptr);  // Refresh key 1 before eviction.
  const auto result = cache.put(4, 40);

  EXPECT_TRUE(result.inserted);
  EXPECT_TRUE(result.evicted);
  EXPECT_EQ(result.evictedKey, 2);
  EXPECT_EQ(*cache.find(1), 10);
  EXPECT_EQ(cache.find(2), nullptr);
  ASSERT_NE(cache.find(3), nullptr);
  EXPECT_EQ(*cache.find(4), 40);
}

TEST(FixedLruCache, UpdatingAnEntryRefreshesItsRecency) {
  FixedLruCache<int, int, 2> cache;

  cache.put(1, 10);
  cache.put(2, 20);
  const auto result = cache.put(1, 100);
  cache.put(3, 30);

  EXPECT_FALSE(result.inserted);
  EXPECT_FALSE(result.evicted);
  EXPECT_EQ(*cache.find(1), 100);
  EXPECT_EQ(cache.find(2), nullptr);
  EXPECT_EQ(*cache.find(3), 30);
}

TEST(FixedLruCache, ClearDropsEntriesAndReusesStorage) {
  FixedLruCache<int, int, 2> cache;

  cache.put(1, 10);
  cache.put(2, 20);
  cache.clear();

  EXPECT_TRUE(cache.empty());
  EXPECT_EQ(cache.find(1), nullptr);
  EXPECT_TRUE(cache.put(3, 30).inserted);
  EXPECT_EQ(*cache.find(3), 30);
}

TEST(FixedLruCache, SupportsCompositeGlyphKeys) {
  FixedLruCache<GlyphKey, uint16_t, 2, GlyphKeyHash> cache;
  const GlyphKey regular{0x4E00, 0};
  const GlyphKey bold{0x4E00, 1};

  cache.put(regular, 7);
  cache.put(bold, 8);

  ASSERT_NE(cache.find(regular), nullptr);
  ASSERT_NE(cache.find(bold), nullptr);
  EXPECT_EQ(*cache.find(regular), 7);
  EXPECT_EQ(*cache.find(bold), 8);
}

TEST(FixedLruCache, BatchLoaderReadsOnlyMisses) {
  FixedLruCache<int, int, 3> cache;
  int reads = 0;

  const auto batch = [&](std::initializer_list<int> keys) {
    for (const int key : keys) {
      if (cache.find(key)) continue;
      reads++;
      cache.put(key, key * 10);
    }
  };

  batch({1, 2, 3});
  EXPECT_EQ(reads, 3);

  batch({2, 3, 4});
  EXPECT_EQ(reads, 4);

  batch({2, 3, 4});
  EXPECT_EQ(reads, 4);
}

TEST(FixedLruCache, KeepsCollisionChainsValidAfterEviction) {
  FixedLruCache<int, int, 3, ConstantHash> cache;

  cache.put(1, 10);
  cache.put(2, 20);
  cache.put(3, 30);
  ASSERT_NE(cache.find(1), nullptr);
  cache.put(4, 40);  // Evicts key 2 while all keys share one bucket.

  EXPECT_EQ(cache.find(2), nullptr);
  EXPECT_EQ(*cache.find(1), 10);
  EXPECT_EQ(*cache.find(3), 30);
  EXPECT_EQ(*cache.find(4), 40);
}
