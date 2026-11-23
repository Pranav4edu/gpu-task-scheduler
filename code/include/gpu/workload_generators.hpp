#pragma once

#include "gpu/contracts.hpp"
#include <cstddef>
#include <vector>

namespace gpu {
ParsedInput make_chain_workload(std::size_t task_count, Tick release = 0, Tick duration = 1);
ParsedInput make_fork_join_workload(std::size_t branch_count, Tick release = 0,
                                   Tick duration = 1);
ParsedInput make_layered_workload(const std::vector<std::size_t>& layer_widths,
                                  Tick release = 0, Tick duration = 1);
} // namespace gpu
