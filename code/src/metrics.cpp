#include "gpu/contracts.hpp"

#include <algorithm>
#include <cmath>

// Metrics over one finished run (Divyaa's module). Definitions follow
// docs/CONTRACTS.md:
//   ready wait  = start - ready          (ready = max(release, last predecessor finish),
//                                         recorded by the engine in TaskRecord::ready)
//   turnaround  = finish - release
//   makespan    = RunResult::makespan, measured from tick zero
//   utilization = total duration / (units * makespan)
//   busy-time CV = population std-dev of per-unit busy ticks / their mean
//   p95 wait    = nearest rank: sorted position ceil(0.95 * N), 1-based
// Empty workloads and zero denominators give std::nullopt ("N/A").
namespace gpu {
Metrics compute_metrics(const ValidatedWorkload& workload, const RunResult& result) {
    const std::size_t count = workload.tasks.size();
    if (result.tasks.size() != count)
        throw std::invalid_argument("run result must contain one record per task");

    Metrics metrics;
    metrics.makespan = result.makespan;

    std::vector<Tick> waits;
    waits.reserve(count);
    long double wait_sum = 0, turnaround_sum = 0;
    Tick total_duration = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const TaskRecord& record = result.tasks[i];
        const TaskSpec& spec = workload.tasks[i];
        if (record.task != i) throw std::invalid_argument("task records must be indexed by TaskId");
        if (record.ready < 0 || spec.release < 0 || spec.duration <= 0 ||
            record.start < record.ready || record.finish < record.start || record.finish < spec.release)
            throw std::invalid_argument("task record times are inconsistent");
        const Tick wait = record.start - record.ready;
        waits.push_back(wait);
        wait_sum += static_cast<long double>(wait);
        turnaround_sum += static_cast<long double>(record.finish - spec.release);
        total_duration = checked_add(total_duration, spec.duration);
    }

    if (count > 0) {
        metrics.mean_ready_wait = static_cast<double>(wait_sum / static_cast<long double>(count));
        metrics.mean_turnaround = static_cast<double>(turnaround_sum / static_cast<long double>(count));
        std::sort(waits.begin(), waits.end());
        // ceil(95 * N / 100) in integers; the rank is at least 1 because N >= 1.
        const std::size_t rank = count - count / 20;
        metrics.p95_ready_wait = static_cast<double>(waits[rank - 1]);
    }

    const std::size_t units = result.busy_ticks.size();
    if (units > 0 && result.makespan > 0) {
        metrics.utilization = static_cast<double>(total_duration) /
                              (static_cast<double>(units) * static_cast<double>(result.makespan));
    }

    if (units > 0) {
        long double sum = 0;
        for (Tick busy : result.busy_ticks) sum += static_cast<long double>(busy);
        const long double mean = sum / static_cast<long double>(units);
        if (mean > 0) {
            long double squares = 0;
            for (Tick busy : result.busy_ticks) {
                const long double diff = static_cast<long double>(busy) - mean;
                squares += diff * diff;
            }
            const long double deviation = std::sqrt(squares / static_cast<long double>(units));
            metrics.busy_time_cv = static_cast<double>(deviation / mean);
        }
    }
    return metrics;
}
} // namespace gpu
