#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>

// Fixed-storage LRU cache for small embedded caches. The cache owns no heap
// memory and never moves a value after insertion; callers can keep a pointer
// until the entry is evicted or the cache is cleared.
template <typename Key, typename Value, size_t Capacity, typename Hash = std::hash<Key>,
          typename Equal = std::equal_to<Key>>
class FixedLruCache {
  static_assert(Capacity > 0, "LRU capacity must be positive");
  static_assert(Capacity < std::numeric_limits<uint16_t>::max(), "LRU capacity exceeds index type");

  static constexpr uint16_t INVALID_INDEX = std::numeric_limits<uint16_t>::max();
  static constexpr int16_t EMPTY_BUCKET = -1;
  static constexpr int16_t TOMBSTONE_BUCKET = -2;

  static constexpr size_t nextPowerOfTwo(size_t value) {
    size_t result = 1;
    while (result < value) result <<= 1;
    return result;
  }

  static constexpr size_t BUCKET_COUNT = nextPowerOfTwo(Capacity * 2);

  struct Entry {
    Key key{};
    Value value{};
    uint16_t previous = INVALID_INDEX;
    uint16_t next = INVALID_INDEX;
    bool used = false;
  };

 public:
  struct PutResult {
    size_t index = 0;
    bool inserted = false;
    bool evicted = false;
    Key evictedKey{};
  };

  FixedLruCache() { clear(); }

  FixedLruCache(const FixedLruCache&) = delete;
  FixedLruCache& operator=(const FixedLruCache&) = delete;

  void clear() {
    std::fill(buckets_, buckets_ + BUCKET_COUNT, EMPTY_BUCKET);
    std::fill(entries_, entries_ + Capacity, Entry{});
    size_ = 0;
    head_ = INVALID_INDEX;
    tail_ = INVALID_INDEX;
  }

  size_t size() const { return size_; }
  constexpr size_t capacity() const { return Capacity; }
  bool empty() const { return size_ == 0; }

  Value* find(const Key& key) {
    const int index = findIndex(key);
    if (index < 0) return nullptr;
    touch(static_cast<uint16_t>(index));
    return &entries_[index].value;
  }

  const Value* find(const Key& key) const {
    const int index = findIndex(key);
    return index < 0 ? nullptr : &entries_[index].value;
  }

  bool contains(const Key& key) const { return findIndex(key) >= 0; }

  PutResult put(const Key& key, const Value& value) {
    const int existing = findIndex(key);
    if (existing >= 0) {
      entries_[existing].value = value;
      touch(static_cast<uint16_t>(existing));
      return {static_cast<size_t>(existing), false, false, {}};
    }

    PutResult result;
    uint16_t index = INVALID_INDEX;
    if (size_ < Capacity) {
      for (uint16_t i = 0; i < Capacity; i++) {
        if (!entries_[i].used) {
          index = i;
          break;
        }
      }
    } else {
      index = tail_;
      result.evicted = true;
      result.evictedKey = entries_[index].key;
      removeBucket(entries_[index].key);
      unlink(index);
    }

    entries_[index].key = key;
    entries_[index].value = value;
    entries_[index].used = true;
    linkFront(index);
    insertBucket(key, index);
    if (size_ < Capacity) size_++;

    result.index = index;
    result.inserted = true;
    return result;
  }

  // The slot index is stable until that entry is evicted or the cache is cleared.
  bool usedAt(size_t index) const { return index < Capacity && entries_[index].used; }
  bool touchAt(size_t index) {
    if (!usedAt(index)) return false;
    touch(static_cast<uint16_t>(index));
    return true;
  }
  const Key& keyAt(size_t index) const { return entries_[index].key; }
  Value& valueAt(size_t index) { return entries_[index].value; }
  const Value& valueAt(size_t index) const { return entries_[index].value; }

 private:
  size_t bucketIndex(const Key& key) const { return Hash{}(key) & (BUCKET_COUNT - 1); }

  int findIndex(const Key& key) const {
    size_t bucket = bucketIndex(key);
    for (size_t probe = 0; probe < BUCKET_COUNT; probe++, bucket = (bucket + 1) & (BUCKET_COUNT - 1)) {
      const int16_t index = buckets_[bucket];
      if (index == EMPTY_BUCKET) return -1;
      if (index >= 0 && Equal{}(entries_[index].key, key)) return index;
    }
    return -1;
  }

  void insertBucket(const Key& key, uint16_t entryIndex) {
    size_t bucket = bucketIndex(key);
    size_t tombstone = BUCKET_COUNT;
    for (size_t probe = 0; probe < BUCKET_COUNT; probe++, bucket = (bucket + 1) & (BUCKET_COUNT - 1)) {
      if (buckets_[bucket] == TOMBSTONE_BUCKET && tombstone == BUCKET_COUNT) tombstone = bucket;
      if (buckets_[bucket] == EMPTY_BUCKET) {
        buckets_[tombstone == BUCKET_COUNT ? bucket : tombstone] = static_cast<int16_t>(entryIndex);
        return;
      }
    }
    // Active entries use at most half the buckets, so this is unreachable unless
    // the cache invariants are broken.
    if (tombstone != BUCKET_COUNT) buckets_[tombstone] = static_cast<int16_t>(entryIndex);
  }

  void removeBucket(const Key& key) {
    size_t bucket = bucketIndex(key);
    for (size_t probe = 0; probe < BUCKET_COUNT; probe++, bucket = (bucket + 1) & (BUCKET_COUNT - 1)) {
      const int16_t index = buckets_[bucket];
      if (index == EMPTY_BUCKET) return;
      if (index >= 0 && Equal{}(entries_[index].key, key)) {
        buckets_[bucket] = TOMBSTONE_BUCKET;
        return;
      }
    }
  }

  void unlink(uint16_t index) {
    Entry& entry = entries_[index];
    if (entry.previous != INVALID_INDEX) {
      entries_[entry.previous].next = entry.next;
    } else {
      head_ = entry.next;
    }
    if (entry.next != INVALID_INDEX) {
      entries_[entry.next].previous = entry.previous;
    } else {
      tail_ = entry.previous;
    }
    entry.previous = INVALID_INDEX;
    entry.next = INVALID_INDEX;
    entry.used = false;
  }

  void linkFront(uint16_t index) {
    Entry& entry = entries_[index];
    entry.previous = INVALID_INDEX;
    entry.next = head_;
    if (head_ != INVALID_INDEX) entries_[head_].previous = index;
    head_ = index;
    if (tail_ == INVALID_INDEX) tail_ = index;
  }

  void touch(uint16_t index) {
    if (index == head_) return;
    unlink(index);
    entries_[index].used = true;
    linkFront(index);
  }

  Entry entries_[Capacity]{};
  int16_t buckets_[BUCKET_COUNT]{};
  size_t size_ = 0;
  uint16_t head_ = INVALID_INDEX;
  uint16_t tail_ = INVALID_INDEX;
};
