// Unit tests for gpu::RingDeque. std::deque is used ONLY here, as an independent
// reference model; production code never touches it.
#include "gpu/ds/ring_deque.hpp"
#include "gpu/self_test.hpp"
#include <deque>
#include <random>
#include <stdexcept>
#include <vector>

namespace gpu {
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Exception, class Action>
void throws(Action action) {
    try { action(); } catch (const Exception&) { return; }
    throw std::runtime_error("expected exception was not thrown");
}
// Compare size, front/back, and every logical element against the reference.
void same_as(const RingDeque<int>& ring, const std::deque<int>& ref) {
    require(ring.invariant_holds(), "ring invariant broken");
    require(ring.size() == ref.size() && ring.empty() == ref.empty(), "size mismatch");
    for (std::size_t k = 0; k < ref.size(); ++k)
        require(ring.at(k) == ref[k], "logical order mismatch");
    if (!ref.empty()) {
        require(ring.front() == ref.front(), "front mismatch");
        require(ring.back() == ref.back(), "back mismatch");
    }
}
void capacity_one_and_zero() {
    RingDeque<int> zero(0);  // clamped up to 1
    require(zero.capacity() == 1, "capacity must be at least one");
    RingDeque<int> d(1);
    d.push_back(1); d.push_back(2); d.push_front(0);  // forces two resizes from capacity 1
    require(d.at(0) == 0 && d.at(1) == 1 && d.at(2) == 2, "order after growth from capacity 1");
    require(d.capacity() == 4, "capacity should double 1 -> 2 -> 4");
}
void wraparound_both_ends() {
    RingDeque<int> d(4);
    std::deque<int> ref;
    for (int i = 0; i < 4; ++i) { d.push_back(i); ref.push_back(i); }
    for (int i = 0; i < 3; ++i) { require(d.pop_front() == ref.front(), "pop_front"); ref.pop_front(); }
    for (int i = 10; i < 13; ++i) { d.push_back(i); ref.push_back(i); }  // tail wraps past slot 3
    same_as(d, ref);
    require(d.capacity() == 4 && d.stats().resizes == 0, "wrap must not need a resize");
    for (int i = 20; i < 22; ++i) { d.pop_back(); ref.pop_back(); }
    for (int i = 20; i < 22; ++i) { d.push_front(i); ref.push_front(i); }  // head wraps below slot 0
    same_as(d, ref);
}
void growth_while_wrapped() {
    RingDeque<int> d(4);
    std::deque<int> ref;
    for (int i = 0; i < 4; ++i) { d.push_back(i); ref.push_back(i); }
    d.pop_front(); ref.pop_front();
    d.pop_front(); ref.pop_front();
    d.push_back(7); ref.push_back(7);
    d.push_back(8); ref.push_back(8);   // full AND wrapped (head_ == 2)
    d.push_back(9); ref.push_back(9);   // triggers resize while wrapped
    same_as(d, ref);
    require(d.capacity() == 8 && d.stats().resizes == 1, "expected exactly one doubling");
    require(d.stats().elements_copied == 4, "resize should copy the 4 live elements");
    d.push_front(-1); ref.push_front(-1);
    same_as(d, ref);
}
void aliasing_survives_resize() {
    RingDeque<int> d(2);
    d.push_back(5); d.push_back(6);           // full
    d.push_back(d.front());                   // value aliases storage while resizing
    d.push_front(d.back());                   // ditto, on the other end
    require(d.at(0) == 5 && d.at(1) == 5 && d.at(2) == 6 && d.at(3) == 5, "aliased value corrupted");
}
void empty_access_throws() {
    RingDeque<int> d;
    throws<std::underflow_error>([&] { d.front(); });
    throws<std::underflow_error>([&] { d.back(); });
    throws<std::underflow_error>([&] { d.pop_front(); });
    throws<std::underflow_error>([&] { d.pop_back(); });
    throws<std::out_of_range>([&] { d.at(0); });
    require(d.stats().pops == 0, "failed pop must not be counted");
}
void drain_refill_cycles() {
    RingDeque<int> d(2);
    std::deque<int> ref;
    for (int round = 0; round < 50; ++round) {
        for (int i = 0; i < 20; ++i) { d.push_back(round * 100 + i); ref.push_back(round * 100 + i); }
        while (!ref.empty()) {
            require(d.pop_front() == ref.front(), "drain mismatch");
            ref.pop_front();
        }
        require(d.empty(), "deque should be empty after drain");
    }
    require(d.capacity() == 32, "capacity should stay at its peak (no shrinking)");
}
void random_mixed_against_reference() {
    std::mt19937 rng(101);
    RingDeque<int> d(1);
    std::deque<int> ref;
    for (int step = 0; step < 20000; ++step) {
        switch (rng() % 4) {
            case 0: { int v = static_cast<int>(rng() % 1000); d.push_front(v); ref.push_front(v); break; }
            case 1: { int v = static_cast<int>(rng() % 1000); d.push_back(v);  ref.push_back(v);  break; }
            case 2: if (!ref.empty()) { require(d.pop_front() == ref.front(), "pop_front"); ref.pop_front(); } break;
            case 3: if (!ref.empty()) { require(d.pop_back() == ref.back(), "pop_back");   ref.pop_back();  } break;
        }
        same_as(d, ref);
    }
}
void growth_is_geometric() {
    RingDeque<int> d(1);
    const std::size_t n = 1000;
    for (std::size_t i = 0; i < n; ++i) d.push_back(static_cast<int>(i));
    // 1 -> 2 -> ... -> 1024 is 10 doublings, total copies 1+2+...+512 = 1023 < 2n.
    require(d.stats().resizes == 10, "expected log2 resizes");
    require(d.stats().elements_copied == 1023, "copy total should be geometric, not quadratic");
    require(d.stats().pushes == n && d.stats().peak_size == n, "push/peak counters wrong");
}
} // namespace

std::vector<std::pair<const char*, std::function<void()>>> ring_deque_tests() {
    return {
        {"ring deque capacity zero and one", capacity_one_and_zero},
        {"ring deque wraparound at both ends", wraparound_both_ends},
        {"ring deque growth while wrapped", growth_while_wrapped},
        {"ring deque aliased value survives resize", aliasing_survives_resize},
        {"ring deque empty access throws", empty_access_throws},
        {"ring deque drain and refill cycles", drain_refill_cycles},
        {"ring deque mixed operations versus std::deque", random_mixed_against_reference},
        {"ring deque growth is geometric", growth_is_geometric},
    };
}
} // namespace gpu
