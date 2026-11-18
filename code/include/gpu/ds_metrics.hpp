#pragma once
#include "gpu/contracts.hpp"
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace gpu {

// Operation counters of the scheduler-side data structures, for ds_metrics.csv
// (Aayush's evidence that the custom deque and the shared heap are really used).
// These are COUNTS of container operations: they are not simulated time and not
// host runtime, which are different quantities with their own columns elsewhere.
// A counter that does not apply to a structure is std::nullopt and prints as N/A.
struct DsMetricsRow {
    std::string workload;
    std::string config;              // e.g. "central_fifo", "static_fifo"
    std::size_t compute_units = 1;
    std::string structure;           // "RingDeque", "BinaryHeap" or "WorkStealing"
    std::string instance;            // "central_ready_queue", "unit_0_queue", "steal_search"
    std::optional<std::size_t> pushes, pops, peak_size;          // queues and heaps
    std::optional<std::size_t> resizes, elements_copied;         // RingDeque only
    std::optional<std::size_t> comparisons, swaps;               // BinaryHeap only
    std::optional<std::size_t> searches, victim_probes, steals;  // WorkStealing only
};

// Reads the counters from a scheduler built by make_scheduler / the concrete scheduler
// classes. One row per queue: central FIFO -> 1 RingDeque row; central SJF/CP -> 1
// BinaryHeap row; static FIFO -> P RingDeque rows (unit_0_queue ...); stealing -> P
// RingDeque rows plus 1 WorkStealing row. Counters live for the scheduler's lifetime,
// so use a fresh scheduler per run. Throws std::invalid_argument for a Scheduler of an
// unknown concrete type.
std::vector<DsMetricsRow> collect_ds_metrics(const Scheduler& scheduler, const std::string& workload,
                                             const RunConfig& config);

// CSV text. Header:
// workload,config,compute_units,structure,instance,pushes,pops,peak_size,resizes,
// elements_copied,comparisons,swaps,searches,victim_probes,steals
std::string ds_metrics_csv(const std::vector<DsMetricsRow>& rows);

} // namespace gpu
