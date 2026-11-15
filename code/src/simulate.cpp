#include "gpu/scheduler.hpp"
#include "gpu/simulator.hpp"

namespace gpu {

// Two-argument entry point declared in contracts.hpp: builds a fresh production
// scheduler for the configuration (central FIFO/SJF/CP, static FIFO or stealing FIFO)
// and runs the event engine on the validated workload. Callers that also want the
// scheduler's data-structure counters (ds_metrics) or the engine's counters should call
// make_scheduler + simulate_with_scheduler themselves and keep the scheduler object.
RunResult simulate(const ValidatedWorkload& workload, const RunConfig& config) {
    auto scheduler = make_scheduler(config);  // validates the configuration
    return simulate_with_scheduler(workload, config, *scheduler);
}

} // namespace gpu
