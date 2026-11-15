// Integration tests: Aayush's PRODUCTION schedulers (make_scheduler) driven by Pranav's
// real event engine, on Shaswitha's real graph, checked by Shaswitha's independent trace
// verifier. Expected values come from the hand-calculated oracles in docs/CONTRACTS.md and
// from closed-form facts about the owner-skew family, not from the code under test.
#include "gpu/contracts.hpp"
#include "gpu/ds_metrics.hpp"
#include "gpu/scheduler.hpp"
#include "gpu/self_test.hpp"
#include "gpu/simulator.hpp"
#include "gpu/workload_generators.hpp"
#include "gpu/workloads.hpp"
#include "oracle_sim.hpp"
#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace gpu {
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Config { Policy policy; Distribution distribution; };
const std::vector<Config> kAllConfigs = {
    {Policy::fifo, Distribution::central},
    {Policy::shortest_job_first, Distribution::central},
    {Policy::critical_path, Distribution::central},
    {Policy::fifo, Distribution::static_round_robin},
    {Policy::fifo, Distribution::work_stealing},
};
RunConfig run_config(std::size_t units, const Config& c) { return RunConfig{units, c.policy, c.distribution}; }

ValidatedWorkload six_task_workload() {
    return validate_workload({{{"A", 0, 2}, {"B", 0, 3}, {"C", 0, 6}, {"D", 0, 2}, {"E", 0, 4}, {"F", 0, 1}},
                              {{0, 3}, {1, 3}, {2, 4}, {3, 5}, {4, 5}}});
}
struct Placement { Tick start, finish; UnitId unit; };
void expect_trace(const RunResult& result, const std::vector<Placement>& want, const char* message) {
    require(result.tasks.size() == want.size(), message);
    for (std::size_t i = 0; i < want.size(); ++i)
        require(result.tasks[i].start == want[i].start && result.tasks[i].finish == want[i].finish &&
                result.tasks[i].unit == want[i].unit, message);
}

void six_task_oracles_with_production_schedulers() {
    const ValidatedWorkload w = six_task_workload();
    // Hand traces (CONTRACTS.md / report): order A,B,C,D,E,F; P = 2.
    const std::vector<Placement> fifo = {{0, 2, 0}, {0, 3, 1}, {2, 8, 0}, {3, 5, 1}, {8, 12, 0}, {12, 13, 0}};
    const std::vector<Placement> cp = {{3, 5, 1}, {0, 3, 1}, {0, 6, 0}, {5, 7, 1}, {6, 10, 0}, {10, 11, 0}};
    const std::vector<Placement> owner = {{0, 2, 0}, {0, 3, 1}, {2, 8, 0}, {3, 5, 1}, {8, 12, 0}, {12, 13, 1}};
    for (const Config& c : kAllConfigs) {
        const RunConfig config = run_config(2, c);
        const RunResult result = simulate(w, config);   // the two-argument wrapper
        verify_result(w, config, result);               // Shaswitha's independent verifier
        if (c.distribution != Distribution::central) {
            expect_trace(result, owner, "static/stealing trace: F runs on its owner, unit 1");
            require(result.makespan == 13 && result.busy_ticks == std::vector<Tick>({12, 6}), "static/stealing totals");
            for (const TaskRecord& r : result.tasks)
                require(r.initial_owner == r.task % 2 && !r.stolen, "owner and steal flag");
        } else if (c.policy == Policy::critical_path) {
            expect_trace(result, cp, "CP trace");
            require(result.makespan == 11 && result.busy_ticks == std::vector<Tick>({11, 7}), "CP totals");
        } else {
            expect_trace(result, fifo, "FIFO/SJF trace");
            require(result.makespan == 13 && result.busy_ticks == std::vector<Tick>({13, 5}), "FIFO/SJF totals");
        }
    }
}

// Same decisions as my test-only mini simulator? Cross-checks engine and harness semantics.
std::vector<oracle::OracleTask> oracle_tasks(const ValidatedWorkload& w) {
    std::vector<oracle::OracleTask> tasks;
    for (TaskId i = 0; i < w.tasks.size(); ++i)
        tasks.push_back(oracle::OracleTask{w.tasks[i].duration, w.critical_ranks[i], {}});
    for (TaskId u = 0; u < w.tasks.size(); ++u)
        w.graph.for_each_successor(u, [&](TaskId v) { tasks[v].preds.push_back(u); });
    return tasks;
}
void generators_all_configs_verified_and_match_mini_simulator() {
    const std::vector<ParsedInput> inputs = {
        make_chain_workload(40, 0, 3), make_fork_join_workload(30, 0, 2),
        make_layered_workload({3, 5, 2, 6, 1}, 0, 2)};
    for (const ParsedInput& input : inputs) {
        const ValidatedWorkload w = validate_workload(input);
        for (std::size_t units : {1u, 2u, 4u, 8u})
            for (const Config& c : kAllConfigs) {
                const RunConfig config = run_config(units, c);
                const RunResult result = simulate(w, config);
                verify_result(w, config, result);
                // Central runs must equal the mini simulator driven by the same scheduler class.
                if (c.distribution == Distribution::central) {
                    auto scheduler = make_scheduler(config);
                    const auto placed = oracle::run_tasks(oracle_tasks(w), *scheduler, units);
                    for (TaskId i = 0; i < w.tasks.size(); ++i)
                        require(result.tasks[i].start == placed[i].start && result.tasks[i].finish == placed[i].finish &&
                                result.tasks[i].unit == placed[i].unit, "engine and mini simulator disagree");
                }
            }
    }
}

ValidatedWorkload owner_skew_workload(std::size_t n, std::size_t p) {
    OwnerSkewSettings settings;
    settings.tasks = n;
    settings.units = p;
    std::istringstream in(owner_skew_csv(settings));
    std::string line;
    std::getline(in, line);  // header
    ParsedInput input;
    while (std::getline(in, line)) {
        const std::size_t c1 = line.find(',');
        const std::size_t c2 = line.find(',', c1 + 1);
        const std::size_t c3 = line.find(',', c2 + 1);
        input.tasks.push_back(TaskSpec{line.substr(0, c1), std::stoll(line.substr(c1 + 1, c2 - c1 - 1)),
                                       std::stoll(line.substr(c2 + 1, c3 - c2 - 1))});
    }
    return validate_workload(input);
}
void owner_skew_family_through_real_pipeline() {
    for (std::size_t n : {100u, 1000u, 10000u})
        for (std::size_t p : {1u, 2u, 4u, 8u}) {
            const ValidatedWorkload w = owner_skew_workload(n, p);
            const Tick heavy = static_cast<Tick>((n + p - 1) / p);
            const Tick work = 10 * heavy + static_cast<Tick>(n) - heavy;
            const Tick lower = std::max<Tick>((work + static_cast<Tick>(p) - 1) / static_cast<Tick>(p), 10);
            const Tick upper = work / static_cast<Tick>(p) + 10;
            Tick central = 0, stat = 0, steal = 0;
            for (const Config& c : kAllConfigs) {
                const RunConfig config = run_config(p, c);
                const RunResult result = simulate(w, config);
                verify_result(w, config, result);
                Tick busy = 0;
                for (Tick b : result.busy_ticks) busy += b;
                require(busy == work, "busy ticks must sum to total work (conservation)");
                if (c.distribution == Distribution::central && c.policy == Policy::fifo) central = result.makespan;
                if (c.distribution == Distribution::static_round_robin) stat = result.makespan;
                if (c.distribution == Distribution::work_stealing) steal = result.makespan;
            }
            require(stat == 10 * heavy, "static makespan = 10 * ceil(N/P)");
            if (p == 1) {
                require(central == work && stat == work && steal == work, "P=1: every strategy equals total work");
            } else {
                require(central >= lower && central <= upper, "central FIFO within list-scheduling bounds");
                require(steal >= lower && steal < stat, "stealing beats static on skew and respects the lower bound");
                require(central < stat, "central beats static on skew");
            }
        }
}
void measured_deque_counters_from_the_real_engine() {
    // Real-engine counterpart of the hand-derived mini-simulator numbers (start capacity 8,
    // all N tasks enqueued in one batch at t=0): N=100 -> 4 resizes, 120 copies; N=1000 -> 7, 1016.
    struct Case { std::size_t n, resizes, copies; };
    for (Case c : {Case{100, 4, 120}, Case{1000, 7, 1016}}) {
        const ValidatedWorkload w = owner_skew_workload(c.n, 4);
        const RunConfig config{4, Policy::fifo, Distribution::central};
        auto scheduler = make_scheduler(config);
        const RunResult result = simulate_with_scheduler(w, config, *scheduler);
        verify_result(w, config, result);
        const auto rows = collect_ds_metrics(*scheduler, "owner_skew_N" + std::to_string(c.n) + "_P4", config);
        require(rows.size() == 1 && *rows[0].pushes == c.n && *rows[0].pops == c.n && *rows[0].peak_size == c.n,
                "real-engine push/pop/peak counts");
        require(*rows[0].resizes == c.resizes && *rows[0].elements_copied == c.copies, "real-engine growth counters");
    }
}
void repeated_runs_are_identical() {
    const ValidatedWorkload w = owner_skew_workload(100, 4);
    for (const Config& c : kAllConfigs) {
        const RunConfig config = run_config(4, c);
        const RunResult a = simulate(w, config);
        const RunResult b = simulate(w, config);
        require(a.makespan == b.makespan && a.busy_ticks == b.busy_ticks, "run totals differ");
        for (TaskId i = 0; i < a.tasks.size(); ++i)
            require(a.tasks[i].start == b.tasks[i].start && a.tasks[i].unit == b.tasks[i].unit &&
                    a.tasks[i].stolen == b.tasks[i].stolen, "run traces differ");
    }
}
void invalid_configuration_rejected_by_wrapper() {
    const ValidatedWorkload w = six_task_workload();
    try { simulate(w, RunConfig{2, Policy::critical_path, Distribution::static_round_robin}); }
    catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("invalid policy/distribution pair must be rejected");
}
} // namespace

std::vector<std::pair<const char*, std::function<void()>>> scheduler_engine_tests() {
    return {
        {"engine+schedulers: six-task oracle, all five production configurations",
         six_task_oracles_with_production_schedulers},
        {"engine+schedulers: generators x P x configs verified; central equals mini simulator",
         generators_all_configs_verified_and_match_mini_simulator},
        {"engine+schedulers: owner-skew N=100/1000/10000 through real graph and engine",
         owner_skew_family_through_real_pipeline},
        {"engine+schedulers: measured deque counters from the real engine",
         measured_deque_counters_from_the_real_engine},
        {"engine+schedulers: repeated runs are identical", repeated_runs_are_identical},
        {"engine+schedulers: simulate rejects invalid configuration", invalid_configuration_rejected_by_wrapper},
    };
}
} // namespace gpu
