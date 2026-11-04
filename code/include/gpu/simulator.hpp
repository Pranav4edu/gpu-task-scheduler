#pragma once

#include "gpu/contracts.hpp"
#include "gpu/ds/binary_heap.hpp"
#include <functional>

namespace gpu {
using SuccessorVisitor = std::function<void(TaskId)>;

// Non-owning, synchronous access to an immutable, validated graph. The indegree
// vector and callback captures must outlive simulate_events. The callback visits
// each successor exactly once and must not retain the visitor. This adds no
// alternate production graph storage and leaves DirectedGraph's API unchanged.
struct DependencyView {
    const std::vector<std::size_t>& indegrees;
    std::function<void(TaskId, const SuccessorVisitor&)> for_each_successor;
};

struct EngineStats {
    HeapStats events;
    std::size_t timestamp_batches = 0;
    std::size_t successor_visits = 0;
    std::size_t ready_tasks = 0;
    std::size_t dispatch_calls = 0;
};

// Inject a fresh, empty scheduler configured for config. It must dispatch all
// available work allowed by that distribution. Dependencies are validated by the
// graph owner; defensive checks here do not replace DAG validation. Ranks may be
// empty for FIFO/SJF, but CP requires one nonnegative rank per task.
//
// Runtime state and heap counters are fresh per call. On failure, discard the
// scheduler (it may have consumed queue entries). stats is zeroed on entry and
// populated only on success. The two-argument simulate wrapper remains an
// integration task until the real graph and scheduler implementations are ready.
RunResult simulate_events(const std::vector<TaskSpec>& tasks,
                          const DependencyView& dependencies,
                          const std::vector<Tick>& critical_ranks,
                          const RunConfig& config, Scheduler& scheduler,
                          EngineStats* stats = nullptr);
} // namespace gpu
