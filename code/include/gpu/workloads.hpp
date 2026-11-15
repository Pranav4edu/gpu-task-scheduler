#pragma once
#include "gpu/types.hpp"
#include <cstddef>
#include <string>

namespace gpu {

// Owner-skew workload family (Aayush's fixtures).
//
// Purpose: expose the cost of STATIC placement. Under static round robin a task
// is owned by unit (row index mod P). In this family every task owned by unit 0 is
// HEAVY and every other task is LIGHT, so unit 0 gets most of the work while the
// other units finish early and then sit idle even though unit 0 still has queued
// tasks. A central queue lets the idle units help, so the two distribution
// strategies differ sharply on the same input.
//
// Shape: N independent tasks (no dependencies), all released at time 0, named
// T0, T1, ... in row order. Task i (0-based row) has duration `heavy_duration`
// when i % units == 0, otherwise `light_duration`. No random numbers are used, so
// the files are identical on every platform and compiler.
//
// The pattern depends on P, so these workloads are valid for comparing central
// versus static placement at the SAME P, but not for cross-P speedup. With P = 1
// every task is owned by unit 0, so central and static behave identically.
struct OwnerSkewSettings {
    std::size_t tasks = 100;
    std::size_t units = 2;       // P used to decide which tasks are heavy
    Tick heavy_duration = 10;
    Tick light_duration = 1;
};

// The workload CSV in the project input format
// (task_id,release_time,duration,dependencies; LF line endings; empty dependency
// field). Throws std::invalid_argument if tasks == 0, units == 0 or a duration <= 0.
std::string owner_skew_csv(const OwnerSkewSettings& settings);

// File stem for a default-duration workload, e.g. "owner_skew_N100_P2".
std::string owner_skew_name(std::size_t tasks, std::size_t units);

// Human-readable record of the generator settings that is saved next to the CSVs.
std::string owner_skew_settings_text();

// Writes the 12 saved fixtures (N in {100, 1000, 10000} x P in {1, 2, 4, 8}) and
// owner_skew_settings.txt into `directory`, which must already exist.
void write_owner_skew_fixtures(const std::string& directory);

} // namespace gpu
