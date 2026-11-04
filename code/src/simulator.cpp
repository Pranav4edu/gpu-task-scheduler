#include "gpu/simulator.hpp"
#include "gpu/priority.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace gpu {
RunResult simulate_events(const std::vector<TaskSpec>& tasks,
                          const DependencyView& dependencies,
                          const std::vector<Tick>& critical_ranks,
                          const RunConfig& config, Scheduler& scheduler,
                          EngineStats* stats) {
    if (stats) *stats = {};
    validate_config(config);
    const auto count = tasks.size();
    // Exactly one arrival and one completion per task; sequence never wraps.
    if (count > std::numeric_limits<std::size_t>::max() / 2)
        throw std::length_error("too many tasks for event sequence numbers");
    if (dependencies.indegrees.size() != count)
        throw std::invalid_argument("indegree count does not match tasks");
    if (count != 0 && !dependencies.for_each_successor)
        throw std::invalid_argument("missing successor traversal");
    if ((!critical_ranks.empty() && critical_ranks.size() != count) ||
        (config.policy == Policy::critical_path && critical_ranks.size() != count))
        throw std::invalid_argument("rank count does not match tasks");
    for (Tick rank : critical_ranks)
        if (rank < 0) throw std::invalid_argument("negative critical rank");
    for (const auto& task : tasks)
        if (task.release < 0 || task.duration <= 0)
            throw std::invalid_argument("tasks require nonnegative release and positive duration");

    RunResult result;
    result.tasks.resize(count);
    result.busy_ticks.resize(config.compute_units, 0);
    EngineStats counters;
    BinaryHeap<Event, EventBefore> events;
    events.reserve(count);
    std::vector<TaskRuntime> runtime(count);
    std::vector<std::optional<TaskId>> running(config.compute_units);
    std::vector<std::size_t> ready_by_owner(config.compute_units, 0);
    std::vector<bool> candidate_mark(count, false);
    std::vector<TaskId> candidates;
    std::size_t sequence = 0, completed = 0, ready_count = 0;
    for (TaskId id = 0; id < count; ++id) {
        runtime[id].remaining_predecessors = dependencies.indegrees[id];
        events.push({tasks[id].release, EventKind::arrival, id, 0, sequence++});
    }
    const auto consider = [&](TaskId id) {
        if (!candidate_mark[id]) {
            candidate_mark[id] = true;
            candidates.push_back(id);
        }
    };

    while (completed < count) {
        if (events.empty())
            throw std::runtime_error("unfinished tasks with no future events; check dependency metadata");
        const Tick now = events.top().time;
        ++counters.timestamp_batches;
        std::vector<Event> batch;
        while (!events.empty() && events.top().time == now) batch.push_back(events.pop());

        // Finish the entire timestamp before evaluating readiness or dispatching.
        for (const auto& event : batch) {
            if (event.kind != EventKind::completion) continue;
            auto& state = runtime[event.task];
            if (state.phase != TaskPhase::running || !state.finish || *state.finish != now ||
                !running[event.unit] || *running[event.unit] != event.task)
                throw std::logic_error("completion does not match a running task");
            state.phase = TaskPhase::completed;
            running[event.unit].reset();
            ++completed;
            result.makespan = now;
            result.busy_ticks[event.unit] = checked_add(result.busy_ticks[event.unit], tasks[event.task].duration);
            dependencies.for_each_successor(event.task, [&](TaskId successor) {
                if (successor >= count) throw std::invalid_argument("successor task index out of range");
                auto& next = runtime[successor];
                if (next.phase != TaskPhase::blocked || next.remaining_predecessors == 0)
                    throw std::invalid_argument("successor indegree or lifecycle is inconsistent");
                --next.remaining_predecessors;
                ++counters.successor_visits;
                consider(successor);
            });
        }
        for (const auto& event : batch) {
            if (event.kind != EventKind::arrival) continue;
            runtime[event.task].released = true;
            consider(event.task);
        }

        std::vector<ReadyKey> newly_ready;
        for (TaskId id : candidates) {
            candidate_mark[id] = false;
            auto& state = runtime[id];
            if (state.phase != TaskPhase::blocked || !state.released || state.remaining_predecessors != 0)
                continue;
            state.phase = TaskPhase::ready;
            state.ready = now;
            newly_ready.push_back({id, tasks[id].duration, critical_ranks.empty() ? 0 : critical_ranks[id], now});
            ++ready_count;
            ++ready_by_owner[id % config.compute_units];
            ++counters.ready_tasks;
        }
        candidates.clear();
        std::sort(newly_ready.begin(), newly_ready.end(),
                  [](const ReadyKey& a, const ReadyKey& b) { return a.task < b.task; });
        if (!newly_ready.empty()) scheduler.enqueue_ready(newly_ready);

        std::vector<UnitId> idle_units;
        for (UnitId unit = 0; unit < config.compute_units; ++unit)
            if (!running[unit]) idle_units.push_back(unit);
        if (ready_count == 0 || idle_units.empty()) continue;
        ++counters.dispatch_calls;
        auto assignments = scheduler.dispatch(idle_units);

        // Check the complete returned batch before starting any of its tasks.
        // Sorting unit IDs also canonicalizes an owner-first stealing dispatch.
        std::sort(assignments.begin(), assignments.end(),
                  [](const Assignment& a, const Assignment& b) { return a.unit < b.unit; });
        std::vector<TaskId> assigned_tasks;
        std::vector<Tick> finishes;
        for (std::size_t i = 0; i < assignments.size(); ++i) {
            const auto& a = assignments[i];
            if (a.task >= count || a.unit >= config.compute_units)
                throw std::logic_error("scheduler returned an out-of-range assignment");
            if (runtime[a.task].phase != TaskPhase::ready || running[a.unit])
                throw std::logic_error("scheduler assigned an ineligible task or busy unit");
            if (i > 0 && assignments[i - 1].unit == a.unit)
                throw std::logic_error("scheduler assigned a unit more than once");
            const auto owner = a.task % config.compute_units;
            if (config.distribution == Distribution::central) {
                if (a.stolen) throw std::logic_error("central assignments cannot be stolen");
            } else if (config.distribution == Distribution::static_round_robin) {
                if (a.unit != owner || a.stolen)
                    throw std::logic_error("static assignment does not match the task owner");
            } else if (a.stolen != (a.unit != owner)) {
                throw std::logic_error("steal flag does not match the task owner");
            }
            assigned_tasks.push_back(a.task);
            finishes.push_back(checked_add(now, tasks[a.task].duration));
        }
        std::sort(assigned_tasks.begin(), assigned_tasks.end());
        if (std::adjacent_find(assigned_tasks.begin(), assigned_tasks.end()) != assigned_tasks.end())
            throw std::logic_error("scheduler assigned a task more than once");
        std::size_t expected_assignments = std::min(ready_count, idle_units.size());
        if (config.distribution == Distribution::static_round_robin) {
            expected_assignments = 0;
            for (UnitId unit : idle_units)
                if (ready_by_owner[unit] != 0) ++expected_assignments;
        }
        if (assignments.size() != expected_assignments)
            throw std::logic_error("scheduler left eligible work idle");

        for (std::size_t i = 0; i < assignments.size(); ++i) {
            const auto& a = assignments[i];
            auto& state = runtime[a.task];
            state.phase = TaskPhase::running;
            state.start = now;
            state.finish = finishes[i];
            state.unit = a.unit;
            running[a.unit] = a.task;
            --ready_count;
            --ready_by_owner[a.task % config.compute_units];
            const auto owner = config.distribution == Distribution::central ? a.unit : a.task % config.compute_units;
            result.tasks[a.task] = {a.task, *state.ready, now, finishes[i], a.unit, owner, a.stolen};
            events.push({finishes[i], EventKind::completion, a.task, a.unit, sequence++});
        }
    }
    counters.events = events.stats();
    if (stats) *stats = counters;
    return result;
}
} // namespace gpu
