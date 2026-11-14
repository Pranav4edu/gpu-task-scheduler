#include "gpu/self_test.hpp"
#include "gpu/contracts.hpp"
#include "gpu/ds/binary_heap.hpp"
#include "gpu/ds/directed_graph.hpp"
#include "gpu/ds/id_hash_table.hpp"
#include "gpu/ds/ring_deque.hpp"
#include "gpu/priority.hpp"
#include "../src/dag_algorithms.hpp"
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
std::vector<TaskId> successors(const DirectedGraph& graph, TaskId from) {
    std::vector<TaskId> result;
    graph.for_each_successor(from, [&](TaskId to) { result.push_back(to); });
    std::sort(result.begin(), result.end());
    return result;
}
void require_topological_order(const DirectedGraph& graph, const std::vector<TaskId>& order) {
    require(order.size() == graph.vertex_count(), "topological order has wrong size");
    std::vector<std::size_t> positions(graph.vertex_count(), no_edge);
    for (std::size_t index = 0; index < order.size(); ++index) {
        require(order[index] < graph.vertex_count(), "topological order has invalid vertex");
        require(positions[order[index]] == no_edge, "topological order repeats a vertex");
        positions[order[index]] = index;
    }
    for (TaskId from = 0; from < graph.vertex_count(); ++from) {
        graph.for_each_successor(from, [&](TaskId to) {
            require(positions[from] < positions[to], "edge is not ordered predecessor-first");
        });
    }
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
        }},
        {"directed graph handles empty and isolated vertices", [] {
            const auto empty = DirectedGraph::build(0, {});
            require(empty.vertex_count() == 0 && empty.edge_count() == 0 &&
                    empty.indegrees().empty(), "empty graph mismatch");
            const auto isolated = DirectedGraph::build(3, {});
            require(isolated.vertex_count() == 3 && isolated.edge_count() == 0,
                    "isolated graph counts mismatch");
            require(isolated.indegrees() == std::vector<std::size_t>({0, 0, 0}),
                    "isolated graph indegrees mismatch");
            for (TaskId id = 0; id < isolated.vertex_count(); ++id)
                require(successors(isolated, id).empty(), "isolated vertex has successors");
        }},
        {"directed graph stores single edges and original indegrees", [] {
            const auto graph = DirectedGraph::build(3, {{0, 2}});
            require(graph.vertex_count() == 3 && graph.edge_count() == 1,
                    "single-edge graph counts mismatch");
            require(graph.indegrees() == std::vector<std::size_t>({0, 0, 1}),
                    "single-edge indegrees mismatch");
            require(successors(graph, 0) == std::vector<TaskId>({2}),
                    "single-edge successor mismatch");
            require(successors(graph, 1).empty() && successors(graph, 2).empty(),
                    "unexpected successor in single-edge graph");
            require(graph.indegrees() == std::vector<std::size_t>({0, 0, 1}),
                    "successor traversal changed original indegrees");
        }},
        {"directed graph traverses multiple successors", [] {
            const auto graph = DirectedGraph::build(4, {{0, 2}, {0, 1}, {0, 3}, {2, 3}});
            require(graph.edge_count() == 4, "multiple-successor edge count mismatch");
            require(successors(graph, 0) == std::vector<TaskId>({1, 2, 3}),
                    "multiple successors were not traversed");
            require(successors(graph, 2) == std::vector<TaskId>({3}),
                    "successor chain mismatch");
            require(graph.indegrees() == std::vector<std::size_t>({0, 1, 1, 2}),
                    "multiple-successor indegrees mismatch");
        }},
        {"directed graph rejects duplicate edges", [] {
            throws<std::invalid_argument>([] {
                DirectedGraph::build(2, {{0, 1}, {0, 1}});
            });
        }},
        {"directed graph rejects self-edges and invalid endpoints", [] {
            throws<std::invalid_argument>([] { DirectedGraph::build(2, {{1, 1}}); });
            throws<std::invalid_argument>([] { DirectedGraph::build(2, {{2, 0}}); });
            throws<std::invalid_argument>([] { DirectedGraph::build(2, {{0, 2}}); });
            const auto graph = DirectedGraph::build(1, {});
            throws<std::out_of_range>([&] { graph.for_each_successor(1, [](TaskId) {}); });
        }},
        {"Kahn topological order supports an empty graph", [] {
            const auto graph = DirectedGraph::build(0, {});
            const auto order = detail::topological_order(graph);
            require(order.empty(), "empty graph produced a nonempty order");
            require_topological_order(graph, order);
        }},
        {"Kahn topological order supports a single vertex", [] {
            const auto graph = DirectedGraph::build(1, {});
            const auto order = detail::topological_order(graph);
            require(order == std::vector<TaskId>({0}), "single-vertex order mismatch");
            require_topological_order(graph, order);
        }},
        {"Kahn topological order handles a chain", [] {
            const auto graph = DirectedGraph::build(4, {{0, 1}, {1, 2}, {2, 3}});
            const auto order = detail::topological_order(graph);
            require(order == std::vector<TaskId>({0, 1, 2, 3}), "chain order mismatch");
            require_topological_order(graph, order);
        }},
        {"Kahn topological order starts independent vertices in ID order", [] {
            const auto graph = DirectedGraph::build(4, {});
            const auto order = detail::topological_order(graph);
            require(order == std::vector<TaskId>({0, 1, 2, 3}),
                    "independent vertices were not initially queued by ID");
            require_topological_order(graph, order);
        }},
        {"Kahn topological order handles a fork and join", [] {
            const auto graph = DirectedGraph::build(4, {{0, 1}, {0, 2}, {1, 3}, {2, 3}});
            const auto order = detail::topological_order(graph);
            require_topological_order(graph, order);
        }},
        {"Kahn topological order rejects a simple cycle", [] {
            const auto graph = DirectedGraph::build(2, {{0, 1}, {1, 0}});
            throws<std::invalid_argument>([&] { detail::topological_order(graph); });
        }},
        {"Kahn topological order rejects a disconnected cycle", [] {
            const auto graph = DirectedGraph::build(4, {{0, 1}, {2, 3}, {3, 2}});
            throws<std::invalid_argument>([&] { detail::topological_order(graph); });
        }},
        {"Kahn topological order preserves original indegrees", [] {
            const auto graph = DirectedGraph::build(4, {{0, 1}, {0, 2}, {1, 3}, {2, 3}});
            const auto original_indegrees = graph.indegrees();
            detail::topological_order(graph);
            require(graph.indegrees() == original_indegrees,
                    "topological sort modified original indegrees");
        }},
        {"Kahn output puts every predecessor before its successor", [] {
            const auto graph = DirectedGraph::build(6, {
                {0, 2}, {1, 2}, {2, 4}, {3, 4}, {4, 5}
            });
            const auto order = detail::topological_order(graph);
            require_topological_order(graph, order);
        }}
    };
    std::size_t failed=0;
    for(const auto& test:tests){
        try {test.second();output<<"PASS "<<test.first<<'\n';}
        catch(const std::exception& e){++failed;output<<"FAIL "<<test.first<<": "<<e.what()<<'\n';}
    }
    output<<tests.size()-failed<<'/'<<tests.size()<<" foundation tests passed\n";
    output<<"Pending: deque, hash table, parser, critical-path ranks, simulation and full-result tests.\n";
    return failed==0?0:1;
}
} // namespace gpu
