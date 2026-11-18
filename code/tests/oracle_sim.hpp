// Test-only mini simulator shared by several test files. It is NOT the project
// engine: it scans arrays (O(N^2) worst case) and only exists to replay small
// workloads through the Scheduler classes against hand-calculated answers.
// Semantics follow docs/CONTRACTS.md: process completions, then newly ready tasks
// (ascending id), enqueue the batch, dispatch idle units in ascending order.
// All release times are zero.
#pragma once
#include "gpu/scheduler.hpp"
#include "gpu/workloads.hpp"
#include <algorithm>
#include <sstream>
#include <string>
#include <stdexcept>
#include <vector>

namespace gpu {
namespace oracle {
struct OracleTask { Tick duration, rank; std::vector<std::size_t> preds; };
struct Placed { Tick start, finish; UnitId unit; };
inline std::vector<Placed> run_tasks(const std::vector<OracleTask>& tasks, Scheduler& scheduler,
                              std::size_t units) {
    const std::size_t n = tasks.size();
    std::vector<std::size_t> remaining(n);
    for (std::size_t i = 0; i < n; ++i) remaining[i] = tasks[i].preds.size();
    std::vector<bool> queued(n, false);
    std::vector<Placed> placed(n, Placed{-1, -1, 0});
    std::vector<long> running(units, -1);
    std::size_t completed = 0;
    Tick now = 0;
    while (completed < n) {
        for (std::size_t u = 0; u < units; ++u) {      // 1. completions at `now`
            if (running[u] < 0 || placed[static_cast<std::size_t>(running[u])].finish != now) continue;
            const std::size_t done = static_cast<std::size_t>(running[u]);
            running[u] = -1;
            ++completed;
            for (std::size_t w = 0; w < n; ++w)
                for (std::size_t p : tasks[w].preds) if (p == done) --remaining[w];
        }
        std::vector<ReadyKey> batch;                    // 2. newly ready, ascending id (all releases are 0)
        for (std::size_t i = 0; i < n; ++i)
            if (!queued[i] && remaining[i] == 0) {
                queued[i] = true;
                batch.push_back(ReadyKey{i, tasks[i].duration, tasks[i].rank, now});
            }
        scheduler.enqueue_ready(batch);
        std::vector<UnitId> idle;                       // 3. dispatch idle units ascending
        for (std::size_t u = 0; u < units; ++u) if (running[u] < 0) idle.push_back(u);
        for (const Assignment& a : scheduler.dispatch(idle)) {
            placed[a.task] = Placed{now, now + tasks[a.task].duration, a.unit};
            running[a.unit] = static_cast<long>(a.task);
        }
        Tick next = -1;                                  // 4. jump to next completion
        for (std::size_t u = 0; u < units; ++u)
            if (running[u] >= 0) {
                const Tick f = placed[static_cast<std::size_t>(running[u])].finish;
                if (next < 0 || f < next) next = f;
            }
        if (next < 0 && completed < n) throw std::runtime_error("mini simulator deadlocked");
        now = next;
    }
    return placed;
}
inline Tick makespan_of(const std::vector<Placed>& trace) {
    Tick m = 0;
    for (const auto& p : trace) m = std::max(m, p.finish);
    return m;
}

// Independent tasks (rank = duration) of the owner-skew workload with N tasks for P units,
// parsed back from the generated CSV text.
inline std::vector<OracleTask> owner_skew_tasks(std::size_t n, std::size_t p) {
    OwnerSkewSettings settings;
    settings.tasks = n;
    settings.units = p;
    std::vector<OracleTask> tasks;
    std::istringstream in(owner_skew_csv(settings));
    std::string line;
    std::getline(in, line);  // header
    while (std::getline(in, line)) {
        const std::size_t last = line.rfind(',');
        const std::size_t first = line.rfind(',', last - 1);
        const Tick duration = std::stoll(line.substr(first + 1, last - first - 1));
        tasks.push_back(OracleTask{duration, duration, {}});
    }
    return tasks;
}

} // namespace oracle
} // namespace gpu
