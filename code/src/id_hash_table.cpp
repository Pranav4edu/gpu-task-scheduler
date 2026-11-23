#include "gpu/ds/id_hash_table.hpp"

#include <algorithm>
#include <utility>

namespace gpu {
namespace {
// FNV-1a over unsigned bytes, written out here rather than using std::hash.
std::size_t default_hash(const std::string& key) {
    std::uint64_t h = 14695981039346656037ULL;
    for (char c : key) {
        h ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        h *= 1099511628211ULL;
    }
    return static_cast<std::size_t>(h);
}

// True when `count` entries fit in `buckets` without exceeding load factor 0.75,
// i.e. count <= floor(3 * buckets / 4). Integer-only and overflow-safe:
// floor(3b/4) = b - ceil(b/4).
bool within_load_limit(std::size_t count, std::size_t buckets) {
    return count <= buckets - buckets / 4 - (buckets % 4 != 0 ? 1 : 0);
}
} // namespace

IdHashTable::IdHashTable(std::size_t initial_buckets, HashFunction hash)
    : buckets_(std::max<std::size_t>(initial_buckets, 1), no_edge),
      hash_(hash ? std::move(hash) : HashFunction(default_hash)) {}

bool IdHashTable::insert(const std::string& key, TaskId value) {
    std::size_t bucket = hash_(key) % buckets_.size();
    for (std::size_t i = buckets_[bucket]; i != no_edge; i = entries_[i].next)
        if (entries_[i].key == key) return false; // keep the existing mapping

    if (!within_load_limit(entries_.size() + 1, buckets_.size())) {
        const std::size_t limit = std::numeric_limits<std::size_t>::max() / 2;
        if (buckets_.size() > limit) throw std::length_error("hash table too large");
        rehash(buckets_.size() * 2);
        bucket = hash_(key) % buckets_.size();
    }
    entries_.push_back(Entry{key, value, buckets_[bucket]});
    buckets_[bucket] = entries_.size() - 1;
    return true;
}

std::optional<TaskId> IdHashTable::find(const std::string& key) const {
    const std::size_t bucket = hash_(key) % buckets_.size();
    for (std::size_t i = buckets_[bucket]; i != no_edge; i = entries_[i].next)
        if (entries_[i].key == key) return entries_[i].value;
    return std::nullopt;
}

std::size_t IdHashTable::size() const noexcept { return entries_.size(); }

double IdHashTable::load_factor() const noexcept {
    return static_cast<double>(entries_.size()) / static_cast<double>(buckets_.size());
}

void IdHashTable::reserve(std::size_t expected_keys) {
    std::size_t buckets = buckets_.size();
    const std::size_t limit = std::numeric_limits<std::size_t>::max() / 2;
    while (!within_load_limit(expected_keys, buckets)) {
        if (buckets > limit) throw std::length_error("hash table too large");
        buckets *= 2;
    }
    if (buckets != buckets_.size()) rehash(buckets);
    entries_.reserve(expected_keys);
}

void IdHashTable::rehash(std::size_t bucket_count) {
    bucket_count = std::max<std::size_t>(bucket_count, 1);
    std::vector<std::size_t> fresh(bucket_count, no_edge);
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const std::size_t bucket = hash_(entries_[i].key) % bucket_count;
        entries_[i].next = fresh[bucket];
        fresh[bucket] = i;
    }
    buckets_ = std::move(fresh);
}
} // namespace gpu
