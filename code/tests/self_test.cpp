#include "gpu/self_test.hpp"
#include "gpu/contracts.hpp"
#include "gpu/ds/binary_heap.hpp"
#include "gpu/ds/directed_graph.hpp"
#include "gpu/ds/id_hash_table.hpp"
#include "gpu/ds/ring_deque.hpp"
#include "gpu/priority.hpp"
#include "gpu/workload_generators.hpp"
#include "../src/dag_algorithms.hpp"
#include <algorithm>
#include <functional>
#include <limits>
#include <ostream>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
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
ValidatedWorkload make_workload(const std::vector<Tick>& durations,
                                const std::vector<EdgePair>& edges) {
    ValidatedWorkload workload;
    for (std::size_t task = 0; task < durations.size(); ++task)
        workload.tasks.push_back({std::to_string(task), 0, durations[task]});
    workload.graph = DirectedGraph::build(workload.tasks.size(), edges);
    workload.topological_order = detail::topological_order(workload.graph);
    return workload;
}
ParsedInput make_parsed_input(const std::vector<Tick>& durations,
                              const std::vector<EdgePair>& edges) {
    ParsedInput input;
    for (std::size_t task = 0; task < durations.size(); ++task)
        input.tasks.push_back({std::to_string(task), 0, durations[task]});
    input.edges = edges;
    return input;
}
TaskRecord make_task_record(TaskId task, Tick ready, Tick start, Tick finish, UnitId unit) {
    return {task, ready, start, finish, unit, unit, false};
}
RunResult make_run_result(std::vector<TaskRecord> tasks, std::vector<Tick> busy_ticks,
                          Tick makespan) {
    RunResult result;
    result.tasks = std::move(tasks);
    result.busy_ticks = std::move(busy_ticks);
    result.makespan = makespan;
    return result;
}
void require_task_specs(const ParsedInput& input, Tick release, Tick duration) {
    for (std::size_t task = 0; task < input.tasks.size(); ++task) {
        require(input.tasks[task].external_id == "T" + std::to_string(task),
                "generated task name or dense ordering mismatch");
        require(input.tasks[task].release == release &&
                input.tasks[task].duration == duration,
                "generated task timing mismatch");
    }
}
void require_edges(const ParsedInput& input, const std::vector<EdgePair>& expected) {
    require(input.edges.size() == expected.size(), "generated edge count mismatch");
    for (std::size_t edge = 0; edge < expected.size(); ++edge) {
        require(input.edges[edge].from == expected[edge].from &&
                input.edges[edge].to == expected[edge].to,
                "generated edge set or order mismatch");
    }
}
void require_same_input(const ParsedInput& left, const ParsedInput& right) {
    require(left.tasks.size() == right.tasks.size() && left.edges.size() == right.edges.size(),
            "repeated generator call changed workload size");
    for (std::size_t task = 0; task < left.tasks.size(); ++task) {
        require(left.tasks[task].external_id == right.tasks[task].external_id &&
                left.tasks[task].release == right.tasks[task].release &&
                left.tasks[task].duration == right.tasks[task].duration,
                "repeated generator call changed task data");
    }
    for (std::size_t edge = 0; edge < left.edges.size(); ++edge) {
        require(left.edges[edge].from == right.edges[edge].from &&
                left.edges[edge].to == right.edges[edge].to,
                "repeated generator call changed edge data");
    }
}
ValidatedWorkload validate_generated_input(const ParsedInput& input) {
    const auto workload = validate_workload(input);
    require(workload.graph.vertex_count() == input.tasks.size(),
            "generated graph vertex count mismatch");
    require(workload.graph.edge_count() == input.edges.size(),
            "generated graph edge count mismatch");
    require(workload.critical_ranks.size() == input.tasks.size(),
            "generated rank count mismatch");
    require_topological_order(workload.graph, workload.topological_order);
    return workload;
}
} // namespace

int run_self_tests(std::ostream& output) {
    std::vector<std::pair<const char*, std::function<void()>>> tests = {
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
        {"directed graph rejects non-adjacent duplicate edges after sorting", [] {
            throws<std::invalid_argument>([] {
                DirectedGraph::build(4, {{0, 1}, {1, 2}, {2, 3}, {0, 2}, {0, 1}});
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
        }},
        {"critical-path ranks support an empty workload", [] {
            const auto workload = make_workload({}, {});
            require(detail::critical_path_ranks(workload).empty(),
                    "empty workload produced ranks");
        }},
        {"critical-path rank for a single task is its duration", [] {
            const auto workload = make_workload({5}, {});
            require(detail::critical_path_ranks(workload) == std::vector<Tick>({5}),
                    "single-task rank mismatch");
        }},
        {"critical-path ranks follow a chain", [] {
            const auto workload = make_workload({2, 3, 4}, {{0, 1}, {1, 2}});
            require(detail::critical_path_ranks(workload) ==
                    std::vector<Tick>({9, 7, 4}), "chain ranks mismatch");
        }},
        {"critical-path ranks handle a fork and join", [] {
            const auto workload = make_workload({2, 3, 5, 7},
                {{0, 1}, {0, 2}, {1, 3}, {2, 3}});
            require(detail::critical_path_ranks(workload) ==
                    std::vector<Tick>({14, 10, 12, 7}), "fork-join ranks mismatch");
        }},
        {"critical-path ranks for independent tasks equal their durations", [] {
            const auto workload = make_workload({4, 9, 1}, {});
            require(detail::critical_path_ranks(workload) ==
                    std::vector<Tick>({4, 9, 1}), "independent-task ranks mismatch");
        }},
        {"critical-path ranks select the longest-duration successor path", [] {
            const auto workload = make_workload({1, 2, 9, 3},
                {{0, 1}, {1, 3}, {0, 2}});
            require(detail::critical_path_ranks(workload) ==
                    std::vector<Tick>({10, 5, 9, 3}), "longest successor path mismatch");
        }},
        {"critical-path rank addition detects overflow", [] {
            const Tick maximum = std::numeric_limits<Tick>::max();
            const auto workload = make_workload({maximum, 1}, {{0, 1}});
            throws<std::overflow_error>([&] { detail::critical_path_ranks(workload); });
        }},
        {"critical-path ranks match the six-task hand oracle", [] {
            const auto workload = make_workload({2, 3, 6, 2, 4, 1},
                {{0, 3}, {1, 3}, {2, 4}, {3, 5}, {4, 5}});
            require(detail::critical_path_ranks(workload) ==
                    std::vector<Tick>({5, 6, 11, 3, 5, 1}),
                    "six-task critical-path ranks mismatch");
        }},
        {"critical-path ranking preserves indegrees and task records", [] {
            const auto workload = make_workload({2, 3, 6, 2, 4, 1},
                {{0, 3}, {1, 3}, {2, 4}, {3, 5}, {4, 5}});
            const auto original_indegrees = workload.graph.indegrees();
            const auto original_tasks = workload.tasks;
            detail::critical_path_ranks(workload);
            require(workload.graph.indegrees() == original_indegrees,
                    "critical-path ranking changed original indegrees");
            require(workload.tasks.size() == original_tasks.size(),
                    "critical-path ranking changed task count");
            for (std::size_t task = 0; task < workload.tasks.size(); ++task) {
                require(workload.tasks[task].external_id == original_tasks[task].external_id &&
                        workload.tasks[task].release == original_tasks[task].release &&
                        workload.tasks[task].duration == original_tasks[task].duration,
                        "critical-path ranking changed a task record");
            }
        }},
        {"workload validation supports empty input", [] {
            const auto workload = validate_workload({});
            require(workload.tasks.empty() && workload.graph.vertex_count() == 0 &&
                    workload.graph.edge_count() == 0 &&
                    workload.topological_order.empty() && workload.critical_ranks.empty(),
                    "empty workload result mismatch");
        }},
        {"workload validation handles a single task", [] {
            ParsedInput input;
            input.tasks.push_back({"only", 3, 7});
            const auto workload = validate_workload(std::move(input));
            require(workload.tasks.size() == 1 &&
                    workload.tasks[0].external_id == "only" &&
                    workload.tasks[0].release == 3 && workload.tasks[0].duration == 7,
                    "single task was not preserved");
            require(workload.topological_order == std::vector<TaskId>({0}) &&
                    workload.critical_ranks == std::vector<Tick>({7}),
                    "single-task DAG metadata mismatch");
        }},
        {"workload validation handles independent tasks", [] {
            const auto workload = validate_workload(make_parsed_input({3, 5, 1}, {}));
            require(workload.tasks[0].external_id == "0" &&
                    workload.tasks[1].external_id == "1" &&
                    workload.tasks[2].external_id == "2",
                    "workload validation changed task order");
            require(workload.topological_order == std::vector<TaskId>({0, 1, 2}) &&
                    workload.critical_ranks == std::vector<Tick>({3, 5, 1}),
                    "independent-task DAG metadata mismatch");
        }},
        {"workload validation handles a chain", [] {
            const auto workload = validate_workload(
                make_parsed_input({2, 3, 4}, {{0, 1}, {1, 2}}));
            require(workload.topological_order == std::vector<TaskId>({0, 1, 2}) &&
                    workload.critical_ranks == std::vector<Tick>({9, 7, 4}),
                    "chain DAG metadata mismatch");
        }},
        {"workload validation handles a fork and join", [] {
            const auto workload = validate_workload(make_parsed_input(
                {2, 3, 5, 7}, {{0, 1}, {0, 2}, {1, 3}, {2, 3}}));
            require_topological_order(workload.graph, workload.topological_order);
            require(workload.critical_ranks == std::vector<Tick>({14, 10, 12, 7}),
                    "fork-join ranks mismatch");
        }},
        {"workload validation matches the six-task rank oracle", [] {
            const auto workload = validate_workload(make_parsed_input(
                {2, 3, 6, 2, 4, 1},
                {{0, 3}, {1, 3}, {2, 4}, {3, 5}, {4, 5}}));
            require(workload.critical_ranks == std::vector<Tick>({5, 6, 11, 3, 5, 1}),
                    "six-task ranks mismatch");
            require_topological_order(workload.graph, workload.topological_order);
        }},
        {"workload validation rejects a negative release", [] {
            ParsedInput input;
            input.tasks.push_back({"negative-release", -1, 1});
            throws<std::invalid_argument>([&] { validate_workload(std::move(input)); });
        }},
        {"workload validation rejects zero and negative durations", [] {
            ParsedInput zero_duration;
            zero_duration.tasks.push_back({"zero-duration", 0, 0});
            throws<std::invalid_argument>([&] { validate_workload(std::move(zero_duration)); });
            ParsedInput negative_duration;
            negative_duration.tasks.push_back({"negative-duration", 0, -1});
            throws<std::invalid_argument>([&] { validate_workload(std::move(negative_duration)); });
        }},
        {"workload validation rejects invalid edge endpoints", [] {
            throws<std::invalid_argument>([] {
                validate_workload(make_parsed_input({1, 1}, {{2, 0}}));
            });
            throws<std::invalid_argument>([] {
                validate_workload(make_parsed_input({1, 1}, {{0, 2}}));
            });
        }},
        {"workload validation rejects duplicate edges", [] {
            throws<std::invalid_argument>([] {
                validate_workload(make_parsed_input({1, 1}, {{0, 1}, {0, 1}}));
            });
        }},
        {"workload validation rejects self-edges", [] {
            throws<std::invalid_argument>([] {
                validate_workload(make_parsed_input({1}, {{0, 0}}));
            });
        }},
        {"workload validation rejects simple and disconnected cycles", [] {
            throws<std::invalid_argument>([] {
                validate_workload(make_parsed_input({1, 1}, {{0, 1}, {1, 0}}));
            });
            throws<std::invalid_argument>([] {
                validate_workload(make_parsed_input(
                    {1, 1, 1, 1}, {{0, 1}, {2, 3}, {3, 2}}));
            });
        }},
        {"workload validation propagates critical-rank overflow", [] {
            const Tick maximum = std::numeric_limits<Tick>::max();
            throws<std::overflow_error>([&] {
                validate_workload(make_parsed_input({maximum, 1}, {{0, 1}}));
            });
        }},
        {"workload validation preserves original graph indegrees", [] {
            const auto workload = validate_workload(make_parsed_input(
                {2, 3, 4, 5}, {{0, 2}, {1, 2}, {2, 3}}));
            require(workload.graph.indegrees() ==
                    std::vector<std::size_t>({0, 0, 2, 1}),
                    "validation changed original graph indegrees");
        }},
        {"result verifier accepts an empty workload", [] {
            const auto workload = validate_workload({});
            const RunConfig config{2, Policy::fifo, Distribution::central};
            const auto result = make_run_result({}, {0, 0}, 0);
            verify_result(workload, config, result);
        }},
        {"result verifier accepts one valid task", [] {
            const auto workload = make_workload({2}, {});
            const RunConfig config{2, Policy::fifo, Distribution::central};
            const auto result = make_run_result({make_task_record(0, 0, 0, 2, 1)}, {0, 2}, 2);
            verify_result(workload, config, result);
        }},
        {"result verifier accepts a valid chain", [] {
            const auto workload = make_workload({2, 3}, {{0, 1}});
            const RunConfig config{2, Policy::fifo, Distribution::central};
            const auto result = make_run_result({
                make_task_record(0, 0, 0, 2, 0),
                make_task_record(1, 2, 2, 5, 1)
            }, {2, 3}, 5);
            verify_result(workload, config, result);
        }},
        {"result verifier accepts a valid fork and join", [] {
            const auto workload = make_workload({2, 1, 1, 2},
                {{0, 1}, {0, 2}, {1, 3}, {2, 3}});
            const RunConfig config{2, Policy::fifo, Distribution::central};
            const auto result = make_run_result({
                make_task_record(0, 0, 0, 2, 0),
                make_task_record(1, 2, 2, 3, 0),
                make_task_record(2, 2, 2, 3, 1),
                make_task_record(3, 3, 3, 5, 0)
            }, {5, 1}, 5);
            verify_result(workload, config, result);
        }},
        {"result verifier accepts independent tasks on different units", [] {
            const auto workload = make_workload({2, 3}, {});
            const RunConfig config{2, Policy::fifo, Distribution::central};
            const auto result = make_run_result({
                make_task_record(0, 0, 0, 2, 0),
                make_task_record(1, 0, 0, 3, 1)
            }, {2, 3}, 3);
            verify_result(workload, config, result);
        }},
        {"result verifier accepts adjacent intervals on one unit", [] {
            const auto workload = make_workload({2, 3}, {});
            const RunConfig config{1, Policy::fifo, Distribution::central};
            const auto result = make_run_result({
                make_task_record(0, 0, 0, 2, 0),
                make_task_record(1, 0, 2, 5, 0)
            }, {5}, 5);
            verify_result(workload, config, result);
        }},
        {"result verifier accepts simultaneous completion and release", [] {
            ParsedInput input;
            input.tasks = {{"first", 0, 2}, {"released-at-completion", 2, 1}};
            const auto workload = validate_workload(std::move(input));
            const RunConfig config{1, Policy::fifo, Distribution::central};
            const auto result = make_run_result({
                make_task_record(0, 0, 0, 2, 0),
                make_task_record(1, 2, 2, 3, 0)
            }, {3}, 3);
            verify_result(workload, config, result);
        }},
        {"result verifier uses the latest predecessor finish for ready time", [] {
            const auto workload = make_workload({2, 5, 1}, {{0, 2}, {1, 2}});
            const RunConfig config{2, Policy::fifo, Distribution::central};
            const auto result = make_run_result({
                make_task_record(0, 0, 0, 2, 0),
                make_task_record(1, 0, 0, 5, 1),
                make_task_record(2, 5, 5, 6, 1)
            }, {2, 6}, 6);
            verify_result(workload, config, result);

            auto incorrect = result;
            incorrect.tasks[2].ready = 2;
            throws<std::invalid_argument>([&] { verify_result(workload, config, incorrect); });
        }},
        {"result verifier uses release time when later than predecessor finishes", [] {
            ParsedInput input;
            input.tasks = {{"predecessor", 0, 2}, {"later-release", 5, 1}};
            input.edges = {{0, 1}};
            const auto workload = validate_workload(std::move(input));
            const RunConfig config{1, Policy::fifo, Distribution::central};
            const auto result = make_run_result({
                make_task_record(0, 0, 0, 2, 0),
                make_task_record(1, 5, 5, 6, 0)
            }, {3}, 6);
            verify_result(workload, config, result);

            auto incorrect = result;
            incorrect.tasks[1].ready = 2;
            throws<std::invalid_argument>([&] { verify_result(workload, config, incorrect); });
        }},
        {"result verifier rejects a missing task record", [] {
            const auto workload = make_workload({1, 1}, {});
            const RunConfig config{2, Policy::fifo, Distribution::central};
            const auto result = make_run_result({make_task_record(0, 0, 0, 1, 0)}, {1, 0}, 1);
            throws<std::invalid_argument>([&] { verify_result(workload, config, result); });
        }},
        {"result verifier rejects an extra task record", [] {
            const auto workload = make_workload({1}, {});
            const RunConfig config{2, Policy::fifo, Distribution::central};
            const auto result = make_run_result({
                make_task_record(0, 0, 0, 1, 0),
                make_task_record(1, 0, 0, 1, 1)
            }, {1, 1}, 1);
            throws<std::invalid_argument>([&] { verify_result(workload, config, result); });
        }},
        {"result verifier rejects duplicate and incorrect task IDs", [] {
            const auto workload = make_workload({1, 1}, {});
            const RunConfig config{2, Policy::fifo, Distribution::central};
            const auto duplicate = make_run_result({
                make_task_record(0, 0, 0, 1, 0),
                make_task_record(0, 0, 0, 1, 1)
            }, {1, 1}, 1);
            throws<std::invalid_argument>([&] { verify_result(workload, config, duplicate); });
            const auto incorrect = make_run_result({
                make_task_record(1, 0, 0, 1, 0),
                make_task_record(0, 0, 0, 1, 1)
            }, {1, 1}, 1);
            throws<std::invalid_argument>([&] { verify_result(workload, config, incorrect); });
        }},
        {"result verifier rejects an invalid unit ID", [] {
            const auto workload = make_workload({1}, {});
            const RunConfig config{2, Policy::fifo, Distribution::central};
            const auto result = make_run_result({make_task_record(0, 0, 0, 1, 2)}, {1, 0}, 1);
            throws<std::invalid_argument>([&] { verify_result(workload, config, result); });
        }},
        {"result verifier rejects negative timestamps", [] {
            const auto workload = make_workload({2}, {});
            const RunConfig config{1, Policy::fifo, Distribution::central};
            auto result = make_run_result({make_task_record(0, 0, 0, 2, 0)}, {2}, 2);
            result.tasks[0].ready = -1;
            throws<std::invalid_argument>([&] { verify_result(workload, config, result); });
            result.tasks[0] = make_task_record(0, 0, 0, 2, 0);
            result.tasks[0].start = -1;
            throws<std::invalid_argument>([&] { verify_result(workload, config, result); });
            result.tasks[0] = make_task_record(0, 0, 0, 2, 0);
            result.tasks[0].finish = -1;
            throws<std::invalid_argument>([&] { verify_result(workload, config, result); });
        }},
        {"result verifier rejects an incorrect ready time", [] {
            ParsedInput input;
            input.tasks.push_back({"released-later", 2, 1});
            const auto workload = validate_workload(std::move(input));
            const RunConfig config{1, Policy::fifo, Distribution::central};
            const auto result = make_run_result({make_task_record(0, 1, 2, 3, 0)}, {1}, 3);
            throws<std::invalid_argument>([&] { verify_result(workload, config, result); });
        }},
        {"result verifier rejects a start before release", [] {
            ParsedInput input;
            input.tasks.push_back({"released-at-three", 3, 2});
            const auto workload = validate_workload(std::move(input));
            const RunConfig config{1, Policy::fifo, Distribution::central};
            const auto result = make_run_result({make_task_record(0, 3, 2, 4, 0)}, {2}, 4);
            throws<std::invalid_argument>([&] { verify_result(workload, config, result); });
        }},
        {"result verifier rejects a start before predecessor completion", [] {
            const auto workload = make_workload({2, 1}, {{0, 1}});
            const RunConfig config{2, Policy::fifo, Distribution::central};
            const auto result = make_run_result({
                make_task_record(0, 0, 0, 2, 0),
                make_task_record(1, 2, 1, 2, 1)
            }, {2, 1}, 2);
            throws<std::invalid_argument>([&] { verify_result(workload, config, result); });
        }},
        {"result verifier rejects an incorrect task duration", [] {
            const auto workload = make_workload({2}, {});
            const RunConfig config{2, Policy::fifo, Distribution::central};
            const auto result = make_run_result({make_task_record(0, 0, 0, 3, 0)}, {2, 0}, 3);
            throws<std::invalid_argument>([&] { verify_result(workload, config, result); });
        }},
        {"result verifier rejects overlapping intervals", [] {
            const auto workload = make_workload({3, 2}, {});
            const RunConfig config{1, Policy::fifo, Distribution::central};
            const auto result = make_run_result({
                make_task_record(0, 0, 0, 3, 0),
                make_task_record(1, 0, 2, 4, 0)
            }, {5}, 4);
            throws<std::invalid_argument>([&] { verify_result(workload, config, result); });
        }},
        {"result verifier rejects incorrect busy ticks", [] {
            const auto workload = make_workload({2}, {});
            const RunConfig config{2, Policy::fifo, Distribution::central};
            const auto result = make_run_result({make_task_record(0, 0, 0, 2, 0)}, {1, 0}, 2);
            throws<std::invalid_argument>([&] { verify_result(workload, config, result); });
        }},
        {"result verifier rejects negative busy ticks", [] {
            const auto workload = make_workload({2}, {});
            const RunConfig config{1, Policy::fifo, Distribution::central};
            const auto result = make_run_result({make_task_record(0, 0, 0, 2, 0)}, {-2}, 2);
            throws<std::invalid_argument>([&] { verify_result(workload, config, result); });
        }},
        {"result verifier rejects an incorrect busy-tick vector size", [] {
            const auto workload = make_workload({2}, {});
            const RunConfig config{2, Policy::fifo, Distribution::central};
            const auto result = make_run_result({make_task_record(0, 0, 0, 2, 0)}, {2}, 2);
            throws<std::invalid_argument>([&] { verify_result(workload, config, result); });
        }},
        {"result verifier rejects an incorrect makespan", [] {
            const auto workload = make_workload({2}, {});
            const RunConfig config{1, Policy::fifo, Distribution::central};
            const auto result = make_run_result({make_task_record(0, 0, 0, 2, 0)}, {2}, 3);
            throws<std::invalid_argument>([&] { verify_result(workload, config, result); });
        }},
        {"result verifier rejects negative makespan", [] {
            const auto workload = validate_workload({});
            const RunConfig config{1, Policy::fifo, Distribution::central};
            const auto result = make_run_result({}, {0}, -1);
            throws<std::invalid_argument>([&] { verify_result(workload, config, result); });
        }},
        {"result verifier detects timestamp overflow", [] {
            const Tick maximum = std::numeric_limits<Tick>::max();
            ParsedInput input;
            input.tasks.push_back({"overflow", 2, maximum - 1});
            const auto workload = validate_workload(std::move(input));
            const RunConfig config{1, Policy::fifo, Distribution::central};
            const auto result = make_run_result({
                make_task_record(0, 2, 2, maximum, 0)
            }, {maximum - 1}, maximum);
            throws<std::overflow_error>([&] { verify_result(workload, config, result); });
        }},
        {"chain workload generator handles empty, singleton, and multiple tasks", [] {
            const auto empty = make_chain_workload(0);
            require(empty.tasks.empty() && empty.edges.empty(), "empty chain is not empty");
            require_task_specs(empty, 0, 1);
            require(validate_generated_input(empty).critical_ranks.empty(),
                    "empty chain validation mismatch");

            const auto singleton = make_chain_workload(1, 2, 5);
            require(singleton.tasks.size() == 1, "singleton chain task count mismatch");
            require_edges(singleton, {});
            require_task_specs(singleton, 2, 5);
            require(validate_generated_input(singleton).critical_ranks ==
                    std::vector<Tick>({5}), "singleton chain rank mismatch");

            const auto chain = make_chain_workload(4, 2, 3);
            require(chain.tasks.size() == 4, "chain task count mismatch");
            require_edges(chain, {{0, 1}, {1, 2}, {2, 3}});
            require_task_specs(chain, 2, 3);
            require_same_input(chain, make_chain_workload(4, 2, 3));
            require(validate_generated_input(chain).critical_ranks ==
                    std::vector<Tick>({12, 9, 6, 3}), "chain rank mismatch");
        }},
        {"fork-join workload generator handles one and multiple branches", [] {
            const auto single_branch = make_fork_join_workload(1, 1, 2);
            require(single_branch.tasks.size() == 3, "single-branch task count mismatch");
            require_edges(single_branch, {{0, 1}, {1, 2}});
            require_task_specs(single_branch, 1, 2);
            require(validate_generated_input(single_branch).critical_ranks ==
                    std::vector<Tick>({6, 4, 2}), "single-branch ranks mismatch");

            const auto fork_join = make_fork_join_workload(3);
            require(fork_join.tasks.size() == 5, "fork-join task count mismatch");
            require_edges(fork_join, {{0, 1}, {0, 2}, {0, 3}, {1, 4}, {2, 4}, {3, 4}});
            require_task_specs(fork_join, 0, 1);
            require_same_input(fork_join, make_fork_join_workload(3));
            require(validate_generated_input(fork_join).critical_ranks ==
                    std::vector<Tick>({3, 2, 2, 2, 1}), "fork-join ranks mismatch");
        }},
        {"layered workload generator handles empty, single, and multiple layers", [] {
            const auto empty = make_layered_workload({});
            require(empty.tasks.empty() && empty.edges.empty(), "empty layered graph mismatch");
            require(validate_generated_input(empty).critical_ranks.empty(),
                    "empty layered validation mismatch");

            const auto single_layer = make_layered_workload({3}, 2, 4);
            require(single_layer.tasks.size() == 3, "single-layer task count mismatch");
            require_edges(single_layer, {});
            require_task_specs(single_layer, 2, 4);
            require(validate_generated_input(single_layer).critical_ranks ==
                    std::vector<Tick>({4, 4, 4}), "single-layer ranks mismatch");

            const auto layered = make_layered_workload({2, 3, 1}, 1, 2);
            require(layered.tasks.size() == 6, "layered task count mismatch");
            require_edges(layered, {
                {0, 2}, {0, 3}, {0, 4}, {1, 2}, {1, 3}, {1, 4},
                {2, 5}, {3, 5}, {4, 5}
            });
            require_task_specs(layered, 1, 2);
            require_same_input(layered, make_layered_workload({2, 3, 1}, 1, 2));
            require(validate_generated_input(layered).critical_ranks ==
                    std::vector<Tick>({6, 6, 4, 4, 4, 2}), "layered ranks mismatch");
        }},
        {"workload generators reject invalid timing parameters", [] {
            throws<std::invalid_argument>([] { make_chain_workload(1, -1, 1); });
            throws<std::invalid_argument>([] { make_fork_join_workload(1, 0, 0); });
            throws<std::invalid_argument>([] { make_layered_workload({}, 0, -1); });
        }},
        {"fork-join generator rejects zero branches", [] {
            throws<std::invalid_argument>([] { make_fork_join_workload(0); });
        }},
        {"layered generator rejects zero-width layers", [] {
            throws<std::invalid_argument>([] { make_layered_workload({2, 0, 1}); });
        }},
        {"workload generators detect count and edge arithmetic overflow", [] {
            const std::size_t maximum = std::numeric_limits<std::size_t>::max();
            throws<std::overflow_error>([&] { make_fork_join_workload(maximum); });
            throws<std::overflow_error>([&] {
                make_fork_join_workload(maximum / 2 + 1);
            });
            throws<std::overflow_error>([&] { make_layered_workload({maximum, 1}); });
            throws<std::overflow_error>([&] {
                make_layered_workload({maximum / 2 + 1, 2});
            });
        }},
        {"chain generator rejects task counts beyond vector capacity", [] {
            throws<std::length_error>([] {
                make_chain_workload(std::numeric_limits<std::size_t>::max());
            });
        }}
    };
    for (auto& t : ring_deque_tests()) tests.push_back(std::move(t));
    for (auto& t : scheduler_tests()) tests.push_back(std::move(t));
    for (auto& t : policy_tests()) tests.push_back(std::move(t));
    for (auto& t : static_scheduler_tests()) tests.push_back(std::move(t));
    for (auto& t : export_tests()) tests.push_back(std::move(t));
    for (auto& t : workload_tests()) tests.push_back(std::move(t));
    for (auto& t : stealing_tests()) tests.push_back(std::move(t));
    for (auto& t : ds_metrics_tests()) tests.push_back(std::move(t));
    for (auto& t : scheduler_engine_tests()) tests.push_back(std::move(t));
    std::size_t failed=0;
    for(const auto& test:tests){
        try {test.second();output<<"PASS "<<test.first<<'\n';}
        catch(const std::exception& e){++failed;output<<"FAIL "<<test.first<<": "<<e.what()<<'\n';}
    }
    output<<tests.size()-failed<<'/'<<tests.size()<<" foundation and graph tests passed\n";
    const int engine_failed = run_engine_tests(output);
    output<<"Pending integration: hash table, parser, metrics and full-result tests.\n";
    return failed==0 && engine_failed==0 ? 0 : 1;
}
} // namespace gpu
