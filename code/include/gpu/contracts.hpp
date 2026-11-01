#pragma once
#include "gpu/ds/directed_graph.hpp"
#include "gpu/types.hpp"

namespace gpu {
struct ParsedInput { std::vector<TaskSpec> tasks; std::vector<EdgePair> edges; };
struct ValidatedWorkload {
    std::vector<TaskSpec> tasks;
    DirectedGraph graph;
    std::vector<TaskId> topological_order;
    std::vector<Tick> critical_ranks;
};
struct Metrics {
    Tick makespan = 0;
    std::optional<double> mean_ready_wait, p95_ready_wait, mean_turnaround;
    std::optional<double> utilization, busy_time_cv;
};

// Agreed module boundaries; these functions are not implemented in this checkpoint.
ParsedInput parse_csv(const std::string& path); // Divyaa
ValidatedWorkload validate_workload(ParsedInput input); // Shaswitha
ValidatedWorkload load_and_validate(const std::string& path); // parser + DAG
RunResult simulate(const ValidatedWorkload& workload, const RunConfig& config); // Pranav
void verify_result(const ValidatedWorkload&, const RunConfig&, const RunResult&); // Shaswitha
Metrics compute_metrics(const ValidatedWorkload&, const RunResult&); // Divyaa

// Aayush supplies a concrete implementation. Engine owns runtime states/time.
// Scheduler owns immutable priority keys and ready-queue membership.
class Scheduler {
public:
    virtual ~Scheduler() = default;
    virtual void enqueue_ready(const std::vector<ReadyKey>& batch) = 0;
    virtual std::vector<Assignment> dispatch(const std::vector<UnitId>& idle_units) = 0;
};
} // namespace gpu
