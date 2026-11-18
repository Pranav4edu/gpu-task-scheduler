#pragma once
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace gpu {

// Operation counters for the report. Counts are per deque lifetime.
struct RingDequeStats {
    std::size_t pushes = 0;           // successful push_front + push_back
    std::size_t pops = 0;             // successful pop_front + pop_back
    std::size_t resizes = 0;          // number of times capacity doubled
    std::size_t elements_copied = 0;  // elements moved during all resizes
    std::size_t peak_size = 0;        // largest count_ ever reached
};

// Resizable circular-array deque (Aayush's data structure).
//
// Layout: storage_ is a fixed-size array of `capacity` slots. Logical element k
// (0 = front) lives at slot (head_ + k) % capacity. count_ is how many slots are
// in use; tracking count_ (instead of a tail index) removes the classic
// "empty vs full look identical" ambiguity.
//
// Invariants (checked by invariant_holds()):
//   1. capacity >= 1
//   2. 0 <= count_ <= capacity
//   3. head_ < capacity
//
// Requirements on T: default constructible and copyable (storage_ is pre-sized).
// This holds for TaskId and ReadyKey, the types the scheduler stores.
// std::vector is only the raw backing array; all ring indexing is done here.
template<class T>
class RingDeque {
public:
    explicit RingDeque(std::size_t initial_capacity = 8)
        : storage_(initial_capacity == 0 ? 1 : initial_capacity) {}

    // ---- insertion: O(1) amortized, O(Q) on the push that triggers a resize ----
    void push_front(const T& value) {
        T copy(value);                 // value may alias an element; grow() would invalidate it
        if (count_ == capacity()) grow();
        head_ = (head_ + capacity() - 1) % capacity();  // step head back one slot (wraps)
        storage_[head_] = std::move(copy);
        ++count_;
        note_push();
    }

    void push_back(const T& value) {
        T copy(value);
        if (count_ == capacity()) grow();
        storage_[(head_ + count_) % capacity()] = std::move(copy);  // first free slot after last
        ++count_;
        note_push();
    }

    // ---- removal: O(1) ----
    T pop_front() {
        if (empty()) throw std::underflow_error("pop_front on empty RingDeque");
        T result = std::move(storage_[head_]);
        head_ = (head_ + 1) % capacity();
        --count_;
        ++stats_.pops;
        return result;
    }

    T pop_back() {
        if (empty()) throw std::underflow_error("pop_back on empty RingDeque");
        T result = std::move(storage_[(head_ + count_ - 1) % capacity()]);
        --count_;
        ++stats_.pops;
        return result;
    }

    // ---- access ----
    const T& front() const {
        if (empty()) throw std::underflow_error("front on empty RingDeque");
        return storage_[head_];
    }
    const T& back() const {
        if (empty()) throw std::underflow_error("back on empty RingDeque");
        return storage_[(head_ + count_ - 1) % capacity()];
    }
    // Logical k-th element (0 = front). Used by tests/debug output to inspect order.
    const T& at(std::size_t k) const {
        if (k >= count_) throw std::out_of_range("RingDeque index out of range");
        return storage_[(head_ + k) % capacity()];
    }

    bool empty() const noexcept { return count_ == 0; }
    std::size_t size() const noexcept { return count_; }
    std::size_t capacity() const noexcept { return storage_.size(); }
    const RingDequeStats& stats() const noexcept { return stats_; }

    bool invariant_holds() const noexcept {
        return capacity() >= 1 && count_ <= capacity() && head_ < capacity();
    }

private:
    // Double capacity, copying elements in LOGICAL order so head_ becomes 0.
    // Called only when full. Unwraps a wrapped layout as a side effect.
    void grow() {
        const std::size_t old_capacity = capacity();
        if (old_capacity > std::numeric_limits<std::size_t>::max() / 2)
            throw std::length_error("RingDeque capacity overflow");
        std::vector<T> bigger(old_capacity * 2);
        for (std::size_t k = 0; k < count_; ++k)
            bigger[k] = std::move(storage_[(head_ + k) % old_capacity]);
        storage_ = std::move(bigger);
        head_ = 0;
        ++stats_.resizes;
        stats_.elements_copied += count_;
    }
    void note_push() {
        ++stats_.pushes;
        if (count_ > stats_.peak_size) stats_.peak_size = count_;
    }

    std::vector<T> storage_;
    std::size_t head_ = 0, count_ = 0;
    RingDequeStats stats_;
};

} // namespace gpu
