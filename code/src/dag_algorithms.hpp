#pragma once

#include "gpu/ds/directed_graph.hpp"
#include <vector>

namespace gpu {
namespace detail {
std::vector<TaskId> topological_order(const DirectedGraph& graph);
} // namespace detail
} // namespace gpu
