#include "gpu/contracts.hpp"
#include "dag_algorithms.hpp"
#include <stdexcept>
#include <utility>

namespace gpu {
ValidatedWorkload validate_workload(ParsedInput input) {
    for (const TaskSpec& task : input.tasks) {
        if (task.release < 0)
            throw std::invalid_argument("task release time must be nonnegative");
        if (task.duration <= 0)
            throw std::invalid_argument("task duration must be positive");
    }

    ValidatedWorkload workload;
    workload.tasks = std::move(input.tasks);
    workload.graph = DirectedGraph::build(workload.tasks.size(), std::move(input.edges));
    workload.topological_order = detail::topological_order(workload.graph);
    workload.critical_ranks = detail::critical_path_ranks(workload);
    return workload;
}
} // namespace gpu
