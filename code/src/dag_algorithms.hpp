#pragma once

#include "gpu/contracts.hpp"
#include <vector>

namespace gpu {
namespace detail {
std::vector<TaskId> topological_order(const DirectedGraph& graph);
std::vector<Tick> critical_path_ranks(const ValidatedWorkload& workload);
} // namespace detail
} // namespace gpu
