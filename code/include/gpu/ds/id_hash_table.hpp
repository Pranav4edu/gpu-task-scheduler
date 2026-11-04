#pragma once
#include "gpu/types.hpp"
#include <functional>

namespace gpu {
// Divyaa's implementation contract. Declarations only: NOT implemented yet.
// Default: own unsigned-byte hash. Injected hasher permits forced-collision tests.
// insert(false) preserves the existing mapping; find returns a value, not a
// pointer invalidated by rehash. Double bucket count before load exceeds 0.75.
class IdHashTable {
public:
    using HashFunction = std::function<std::size_t(const std::string&)>;
    explicit IdHashTable(std::size_t initial_buckets = 8, HashFunction hash = {});
    bool insert(const std::string& key, TaskId value);
    std::optional<TaskId> find(const std::string& key) const;
    std::size_t size() const noexcept;
    double load_factor() const noexcept;
    void reserve(std::size_t expected_keys);
private:
    struct Entry { std::string key; TaskId value; std::size_t next; };
    std::vector<std::size_t> buckets_;
    std::vector<Entry> entries_;
    HashFunction hash_;
    void rehash(std::size_t bucket_count);
};
} // namespace gpu
