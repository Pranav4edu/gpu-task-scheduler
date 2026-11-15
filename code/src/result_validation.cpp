#include "gpu/contracts.hpp"
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace gpu {
void verify_result(const ValidatedWorkload& workload, const RunConfig& config,
                   const RunResult& result) {
    validate_config(config);

    const std::size_t task_count = workload.tasks.size();
    if (workload.graph.vertex_count() != task_count)
        throw std::invalid_argument("workload graph vertex count does not match task count");
    if (result.tasks.size() != task_count)
        throw std::invalid_argument("result must contain exactly one record per task");
    if (result.busy_ticks.size() != config.compute_units)
        throw std::invalid_argument("busy_ticks must contain one entry per compute unit");
    if (result.makespan < 0)
        throw std::invalid_argument("makespan must be nonnegative");

    struct Interval {
        UnitId unit;
        Tick start;
        Tick finish;
    };
    std::vector<Interval> intervals;
    intervals.reserve(task_count);
    std::vector<Tick> expected_busy(config.compute_units, 0);
    std::vector<Tick> earliest_ready(task_count, 0);
    Tick expected_makespan = 0;

    for (TaskId task = 0; task < task_count; ++task) {
        const TaskSpec& spec = workload.tasks[task];
        const TaskRecord& record = result.tasks[task];
        if (spec.release < 0)
            throw std::invalid_argument("task release time must be nonnegative");
        if (spec.duration <= 0)
            throw std::invalid_argument("task duration must be positive");
        if (record.task != task)
            throw std::invalid_argument("task records must be indexed by their TaskId");
        if (record.unit >= config.compute_units)
            throw std::invalid_argument("task record has an invalid compute-unit ID");
        if (record.ready < 0 || record.start < 0 || record.finish < 0)
            throw std::invalid_argument("task timestamps must be nonnegative");
        if (record.start < record.ready)
            throw std::invalid_argument("task starts before its recorded ready time");
        if (record.finish != checked_add(record.start, spec.duration))
            throw std::invalid_argument("task finish does not match start plus duration");

        earliest_ready[task] = spec.release;
        expected_busy[record.unit] = checked_add(expected_busy[record.unit], spec.duration);
        intervals.push_back({record.unit, record.start, record.finish});
        if (record.finish > expected_makespan) expected_makespan = record.finish;
    }

    for (TaskId from = 0; from < task_count; ++from) {
        workload.graph.for_each_successor(from, [&](TaskId to) {
            if (to >= task_count)
                throw std::invalid_argument("workload graph contains an invalid successor");
            const Tick predecessor_finish = result.tasks[from].finish;
            if (predecessor_finish > earliest_ready[to])
                earliest_ready[to] = predecessor_finish;
        });
    }
    for (TaskId task = 0; task < task_count; ++task) {
        if (result.tasks[task].ready != earliest_ready[task])
            throw std::invalid_argument("task ready time does not match release and predecessor finishes");
        if (result.tasks[task].start < earliest_ready[task])
            throw std::invalid_argument("task starts before release or a predecessor finishes");
    }

    std::sort(intervals.begin(), intervals.end(), [](const Interval& left, const Interval& right) {
        if (left.unit != right.unit) return left.unit < right.unit;
        if (left.start != right.start) return left.start < right.start;
        return left.finish < right.finish;
    });
    bool have_unit = false;
    UnitId current_unit = 0;
    Tick latest_finish = 0;
    for (const Interval& interval : intervals) {
        if (!have_unit || interval.unit != current_unit) {
            current_unit = interval.unit;
            latest_finish = interval.finish;
            have_unit = true;
        } else {
            if (interval.start < latest_finish)
                throw std::invalid_argument("task execution intervals overlap on a compute unit");
            if (interval.finish > latest_finish) latest_finish = interval.finish;
        }
    }

    for (UnitId unit = 0; unit < config.compute_units; ++unit) {
        if (result.busy_ticks[unit] < 0)
            throw std::invalid_argument("busy_ticks values must be nonnegative");
        if (result.busy_ticks[unit] != expected_busy[unit])
            throw std::invalid_argument("busy_ticks does not match assigned task durations");
    }
    if (result.makespan != expected_makespan)
        throw std::invalid_argument("makespan does not match the maximum task finish time");
}
} // namespace gpu
