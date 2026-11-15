#include "dag_algorithms.hpp"
#include <stdexcept>

namespace gpu {
namespace detail {
std::vector<TaskId> topological_order(const DirectedGraph& graph) {
    const std::size_t vertex_count = graph.vertex_count();
    std::vector<std::size_t> remaining_indegrees = graph.indegrees();
    if (remaining_indegrees.size() != vertex_count)
        throw std::logic_error("graph indegree count does not match its vertices");

    std::vector<TaskId> ready;
    ready.reserve(vertex_count);
    for (TaskId task = 0; task < vertex_count; ++task) {
        if (remaining_indegrees[task] == 0) ready.push_back(task);
    }

    std::vector<TaskId> order;
    order.reserve(vertex_count);
    for (std::size_t read = 0; read < ready.size(); ++read) {
        const TaskId from = ready[read];
        order.push_back(from);
        graph.for_each_successor(from, [&](TaskId to) {
            if (to >= vertex_count || remaining_indegrees[to] == 0)
                throw std::logic_error("graph successor and indegree data are inconsistent");
            --remaining_indegrees[to];
            if (remaining_indegrees[to] == 0) ready.push_back(to);
        });
    }

    if (order.size() != vertex_count)
        throw std::invalid_argument("dependency graph contains a cycle");

    std::vector<std::size_t> positions(vertex_count, no_edge);
    for (std::size_t index = 0; index < order.size(); ++index) {
        const TaskId task = order[index];
        if (task >= vertex_count || positions[task] != no_edge)
            throw std::logic_error("topological order contains an invalid vertex");
        positions[task] = index;
    }
    for (TaskId from = 0; from < vertex_count; ++from) {
        graph.for_each_successor(from, [&](TaskId to) {
            if (positions[from] >= positions[to])
                throw std::logic_error("topological order violates an edge dependency");
        });
    }
    return order;
}

std::vector<Tick> critical_path_ranks(const ValidatedWorkload& workload) {
    const std::size_t vertex_count = workload.graph.vertex_count();
    if (workload.tasks.size() != vertex_count ||
        workload.topological_order.size() != vertex_count)
        throw std::logic_error("validated workload dimensions are inconsistent");

    std::vector<std::size_t> positions(vertex_count, no_edge);
    for (std::size_t index = 0; index < workload.topological_order.size(); ++index) {
        const TaskId task = workload.topological_order[index];
        if (task >= vertex_count || positions[task] != no_edge)
            throw std::logic_error("validated workload has an invalid topological order");
        positions[task] = index;
    }
    for (TaskId from = 0; from < vertex_count; ++from) {
        workload.graph.for_each_successor(from, [&](TaskId to) {
            if (positions[from] >= positions[to])
                throw std::logic_error("topological order violates an edge dependency");
        });
    }

    std::vector<Tick> ranks(vertex_count, 0);
    for (std::size_t index = workload.topological_order.size(); index > 0; --index) {
        const TaskId task = workload.topological_order[index - 1];
        const Tick duration = workload.tasks[task].duration;
        if (duration <= 0) throw std::invalid_argument("task durations must be positive");

        Tick longest_successor_rank = 0;
        workload.graph.for_each_successor(task, [&](TaskId successor) {
            if (ranks[successor] > longest_successor_rank)
                longest_successor_rank = ranks[successor];
        });
        ranks[task] = checked_add(duration, longest_successor_rank);
    }
    return ranks;
}
} // namespace detail
} // namespace gpu
