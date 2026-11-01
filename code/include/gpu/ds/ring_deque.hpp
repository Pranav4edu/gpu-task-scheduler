#pragma once
#include <cstddef>
#include <vector>

namespace gpu {
// Aayush's implementation contract. Declarations only: NOT implemented yet.
// Empty access/pop throws std::underflow_error. Growth doubles capacity and
// copies logical order. vector is backing storage, never erase(begin()) FIFO.
template<class T>
class RingDeque {
public:
    explicit RingDeque(std::size_t initial_capacity = 8);
    void push_front(const T& value);
    void push_back(const T& value);
    T pop_front();
    T pop_back();
    const T& front() const;
    const T& back() const;
    bool empty() const noexcept;
    std::size_t size() const noexcept;
private:
    std::vector<T> storage_;
    std::size_t head_ = 0, count_ = 0;
    void grow();
};
} // namespace gpu
