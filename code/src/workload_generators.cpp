#include "gpu/workload_generators.hpp"
#include <limits>
#include <stdexcept>
#include <string>

namespace gpu {
namespace {
void validate_task_times(Tick release, Tick duration) {
    if (release < 0) throw std::invalid_argument("task release time must be nonnegative");
    if (duration <= 0) throw std::invalid_argument("task duration must be positive");
}

std::size_t checked_size_add(std::size_t left, std::size_t right, const char* message) {
    if (right > std::numeric_limits<std::size_t>::max() - left)
        throw std::overflow_error(message);
    return left + right;
}

std::size_t checked_size_multiply(std::size_t left, std::size_t right,
                                  const char* message) {
    if (right != 0 && left > std::numeric_limits<std::size_t>::max() / right)
        throw std::overflow_error(message);
    return left * right;
}

void check_capacity(std::size_t task_count, std::size_t edge_count) {
    if (task_count > std::vector<TaskSpec>().max_size())
        throw std::length_error("task count exceeds vector capacity");
    if (edge_count > std::vector<EdgePair>().max_size())
        throw std::length_error("edge count exceeds vector capacity");
}

ParsedInput make_input(std::size_t task_count, std::size_t edge_count,
                       Tick release, Tick duration) {
    validate_task_times(release, duration);
    check_capacity(task_count, edge_count);

    ParsedInput input;
    input.tasks.reserve(task_count);
    input.edges.reserve(edge_count);
    for (TaskId task = 0; task < task_count; ++task)
        input.tasks.push_back({"T" + std::to_string(task), release, duration});
    return input;
}
} // namespace

ParsedInput make_chain_workload(std::size_t task_count, Tick release, Tick duration) {
    const std::size_t edge_count = task_count == 0 ? 0 : task_count - 1;
    ParsedInput input = make_input(task_count, edge_count, release, duration);
    for (TaskId task = 0; task < edge_count; ++task)
        input.edges.push_back({task, task + 1});
    return input;
}

ParsedInput make_fork_join_workload(std::size_t branch_count, Tick release, Tick duration) {
    if (branch_count == 0)
        throw std::invalid_argument("fork-join workload requires at least one branch");
    const std::size_t task_count =
        checked_size_add(branch_count, 2, "fork-join task count overflow");
    const std::size_t edge_count =
        checked_size_multiply(branch_count, 2, "fork-join edge count overflow");
    ParsedInput input = make_input(task_count, edge_count, release, duration);
    const TaskId sink = task_count - 1;
    for (TaskId branch = 1; branch < sink; ++branch)
        input.edges.push_back({0, branch});
    for (TaskId branch = 1; branch < sink; ++branch)
        input.edges.push_back({branch, sink});
    return input;
}

ParsedInput make_layered_workload(const std::vector<std::size_t>& layer_widths,
                                  Tick release, Tick duration) {
    validate_task_times(release, duration);
    std::size_t task_count = 0;
    for (std::size_t width : layer_widths) {
        if (width == 0) throw std::invalid_argument("layer widths must be positive");
        task_count = checked_size_add(task_count, width, "layered task count overflow");
    }

    std::size_t edge_count = 0;
    const std::size_t adjacent_layer_count =
        layer_widths.empty() ? 0 : layer_widths.size() - 1;
    for (std::size_t layer = 0; layer < adjacent_layer_count; ++layer) {
        const std::size_t between_layers =
            checked_size_multiply(layer_widths[layer], layer_widths[layer + 1],
                                  "layered edge count overflow");
        edge_count = checked_size_add(edge_count, between_layers,
                                      "layered edge count overflow");
    }
    check_capacity(task_count, edge_count);
    ParsedInput input = make_input(task_count, edge_count, release, duration);

    TaskId layer_start = 0;
    for (std::size_t layer = 0; layer < adjacent_layer_count; ++layer) {
        const TaskId next_layer_start = layer_start + layer_widths[layer];
        for (TaskId from_offset = 0; from_offset < layer_widths[layer]; ++from_offset) {
            for (TaskId to_offset = 0; to_offset < layer_widths[layer + 1]; ++to_offset) {
                input.edges.push_back({layer_start + from_offset, next_layer_start + to_offset});
            }
        }
        layer_start = next_layer_start;
    }
    return input;
}
} // namespace gpu
