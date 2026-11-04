#include "gpu/self_test.hpp"
#include "gpu/contracts.hpp"
#include "gpu/ds/binary_heap.hpp"
#include "gpu/ds/id_hash_table.hpp"
#include "gpu/ds/ring_deque.hpp"
#include "gpu/priority.hpp"
#include <algorithm>
#include <functional>
#include <limits>
#include <ostream>
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
void ordered_inputs() {
    for (int direction : {-1, 1}) {
        BinaryHeap<int> heap;
        std::vector<int> expected;
        for (int i = 0; i < 500; ++i) { heap.push(direction * i); expected.push_back(direction * i); }
        std::sort(expected.begin(), expected.end());
        for (int value : expected) {
            require(heap.top() == value && heap.pop() == value, "wrong extraction order");
            require(heap.invariant_holds(), "broken heap invariant");
        }
        require(heap.empty(), "heap did not drain");
    }
}
void mixed_reference() {
    std::mt19937 rng(101);
    BinaryHeap<int> heap;
    std::vector<int> reference;
    for (int step = 0; step < 6000; ++step) {
        if (reference.empty() || rng() % 3 != 0) {
            int value = static_cast<int>(rng() % 101) - 50;
            heap.push(value); reference.push_back(value);
        } else {
            auto smallest = std::min_element(reference.begin(), reference.end());
            require(heap.pop() == *smallest, "mixed operations disagree with reference");
            reference.erase(smallest);
        }
        require(heap.size() == reference.size(), "size mismatch");
        require(heap.invariant_holds(), "mixed operations broke invariant");
    }
    std::sort(reference.begin(), reference.end());
    for (int value : reference) require(heap.pop() == value, "remaining extraction mismatch");
}
void event_ties() {
    BinaryHeap<Event, EventBefore> heap;
    const std::vector<Event> input = {
        {3, EventKind::arrival, 0, 0, 0}, {3, EventKind::completion, 2, 1, 4},
        {2, EventKind::arrival, 1, 0, 1}, {3, EventKind::completion, 2, 0, 3},
        {3, EventKind::completion, 2, 0, 2}
    };
    auto expected = input;
    std::sort(expected.begin(), expected.end(), EventBefore{});
    for (const auto& e : input) heap.push(e);
    for (const auto& e : expected) {
        auto actual = heap.pop();
        require(!EventBefore{}(e, actual) && !EventBefore{}(actual, e), "event tie ordering mismatch");
    }
}
void policy_keys() {
    BinaryHeap<ReadyKey, SjfBefore> sjf;
    BinaryHeap<ReadyKey, CriticalPathBefore> cp;
    // Hand-calculated order independent of either comparator implementation.
    std::vector<ReadyKey> entries={{3,2,7,1},{2,2,7,0},{1,2,7,0},{0,1,4,5}};
    for (auto entry : entries) { sjf.push(entry); cp.push(entry); }
    for (TaskId id : {0u,1u,2u,3u}) require(sjf.pop().task == id, "SJF tie mismatch");
    for (TaskId id : {1u,2u,3u,0u}) require(cp.pop().task == id, "CP tie mismatch");
    cp.push({0,1,std::numeric_limits<Tick>::max(),0});
    cp.push({1,1,1,0});
    require(cp.pop().task == 0, "CP must compare ranks without negation");
}
} // namespace

int run_self_tests(std::ostream& output) {
    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        {"heap rejects empty top and pop", [] {
            BinaryHeap<int> h; throws<std::underflow_error>([&]{h.top();});
            throws<std::underflow_error>([&]{h.pop();});
            require(h.stats().pops == 0, "failed pop counted");
        }},
        {"heap singleton drain refill and duplicates", [] {
            BinaryHeap<int> h;
            for(int i=0;i<5;++i){h.push(7);require(h.pop()==7 && h.empty(), "singleton failure");}
            for(int i=0;i<100;++i)h.push(4);
            for(int i=0;i<100;++i)require(h.pop()==4, "duplicate failure");
        }},
        {"heap ascending and descending input", ordered_inputs},
        {"heap mixed operations versus independent reference", mixed_reference},
        {"heap supports max ordering", [] {
            BinaryHeap<int,std::greater<int>> h;
            for(int v:{1,9,-2,4})h.push(v);
            for(int v:{9,4,1,-2})require(h.pop()==v, "max ordering failure");
        }},
        {"heap aliasing input survives growth", [] {
            BinaryHeap<int> h;h.push(6);h.push(h.top());
            require(h.pop()==6 && h.pop()==6, "aliased value lost");
        }},
        {"heap statistics exclude validation", [] {
            BinaryHeap<int> h; h.reserve(8);h.push(3);h.push(1);h.push(2);
            auto before=h.stats().comparisons;require(h.invariant_holds(), "invariant");
            require(h.stats().comparisons==before, "validation polluted counts");
            h.pop();require(h.stats().pushes==3 && h.stats().pops==1 && h.stats().peak_size==3,
                            "incorrect operation counters");
        }},
        {"event time type and tie ordering", event_ties},
        {"SJF and CP immutable priority keys", policy_keys},
        {"checked tick addition", [] {
            const Tick max=std::numeric_limits<Tick>::max();
            require(checked_add(max-1,1)==max, "boundary addition failed");
            throws<std::overflow_error>([&]{checked_add(max,1);});
            throws<std::invalid_argument>([]{checked_add(-1,1);});
        }},
        {"configuration rejects invalid combinations", [] {
            validate_config({2,Policy::critical_path,Distribution::central});
            throws<std::invalid_argument>([]{validate_config({0,Policy::fifo,Distribution::central});});
            throws<std::invalid_argument>([]{validate_config({2,Policy::shortest_job_first,Distribution::static_round_robin});});
            throws<std::invalid_argument>([]{validate_config({2,static_cast<Policy>(99),Distribution::central});});
        }}
    };
    std::size_t failed=0;
    for(const auto& test:tests){
        try {test.second();output<<"PASS "<<test.first<<'\n';}
        catch(const std::exception& e){++failed;output<<"FAIL "<<test.first<<": "<<e.what()<<'\n';}
    }
    output<<tests.size()-failed<<'/'<<tests.size()<<" foundation tests passed\n";
    output<<"Pending: deque, graph, hash table, parser, simulation and full-result tests.\n";
    return failed==0?0:1;
}
} // namespace gpu
