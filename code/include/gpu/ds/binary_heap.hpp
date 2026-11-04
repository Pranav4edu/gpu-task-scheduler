#pragma once

#include <cstddef>
#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace gpu {
struct HeapStats {
    std::size_t pushes = 0, pops = 0, comparisons = 0, swaps = 0, peak_size = 0;
};

// Before(a, b) means a must be returned before b. std::less gives a min-heap.
// Intended payloads are copyable value records with non-throwing comparisons.
// Template code belongs in this header so policies can reuse the same heap.
template<class T, class Before = std::less<T>>
class BinaryHeap {
public:
    explicit BinaryHeap(Before before = Before{}) : before_(std::move(before)) {}

    bool empty() const noexcept { return data_.empty(); }
    std::size_t size() const noexcept { return data_.size(); }
    void reserve(std::size_t capacity) { data_.reserve(capacity); }
    const HeapStats& stats() const noexcept { return stats_; }

    const T& top() const {
        if (empty()) throw std::underflow_error("top on empty BinaryHeap");
        return data_.front();
    }

    void push(const T& value) {
        // Copy before a possible reallocation: heap.push(heap.top()) is valid.
        T copy(value);
        data_.push_back(std::move(copy));
        ++stats_.pushes;
        if (size() > stats_.peak_size) stats_.peak_size = size();
        std::size_t child = size() - 1;
        while (child > 0) {
            const std::size_t parent = (child - 1) / 2;
            if (!precedes(data_[child], data_[parent])) break;
            swap_at(child, parent);
            child = parent;
        }
    }

    T pop() {
        if (empty()) throw std::underflow_error("pop on empty BinaryHeap");
        T result = data_.front();
        if (size() == 1) {
            data_.pop_back();
        } else {
            data_.front() = std::move(data_.back());
            data_.pop_back();
            std::size_t parent = 0;
            // parent < n/2 means a left child exists, and prevents index overflow.
            while (parent < size() / 2) {
                std::size_t child = 2 * parent + 1;
                const std::size_t right = child + 1;
                if (right < size() && precedes(data_[right], data_[child])) child = right;
                if (!precedes(data_[child], data_[parent])) break;
                swap_at(parent, child);
                parent = child;
            }
        }
        ++stats_.pops;
        return result;
    }

    // O(n) debug/test inspection; excludes comparisons from operation statistics.
    bool invariant_holds() const {
        for (std::size_t child = 1; child < size(); ++child)
            if (before_(data_[child], data_[(child - 1) / 2])) return false;
        return true;
    }

private:
    bool precedes(const T& a, const T& b) {
        ++stats_.comparisons;
        return before_(a, b);
    }
    void swap_at(std::size_t a, std::size_t b) {
        using std::swap;
        swap(data_[a], data_[b]);
        ++stats_.swaps;
    }
    std::vector<T> data_;
    Before before_;
    HeapStats stats_;
};
} // namespace gpu
