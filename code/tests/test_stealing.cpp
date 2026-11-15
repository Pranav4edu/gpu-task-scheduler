// Tests for WorkStealingFifoScheduler. Expected values are worked out by hand in
// the comments; owner of task t is t % P.
#include "gpu/scheduler.hpp"
#include "gpu/self_test.hpp"
#include "gpu/workloads.hpp"
#include "oracle_sim.hpp"
#include <random>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace gpu {
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Exception, class Action>
void throws(Action action) {
    try { action(); } catch (const Exception&) { return; }
    throw std::runtime_error("expected exception was not thrown");
}
std::vector<ReadyKey> keys(std::initializer_list<TaskId> ids) {
    std::vector<ReadyKey> out;
    for (TaskId id : ids) out.push_back(ReadyKey{id, 1, 0, 0});
    return out;
}
bool is(const Assignment& a, TaskId task, UnitId unit, bool stolen) {
    return a.task == task && a.unit == unit && a.stolen == stolen;
}

void owners_take_their_own_front() {
    // P=2: unit 0 owns 0,2,4; unit 1 owns 1. Both idle -> (0 on u0), (1 on u1); no stealing.
    WorkStealingFifoScheduler s(2);
    s.enqueue_ready(keys({0, 1, 2, 4}));
    auto a = s.dispatch({0, 1});
    require(a.size() == 2 && is(a[0], 0, 0, false) && is(a[1], 1, 1, false), "owner dispatch");
    require(s.steal_stats().searches == 0 && s.steal_stats().probes == 0 && s.steal_stats().steals == 0,
            "no search when every idle unit has its own work");
}
void thief_steals_from_the_back() {
    // u0 queue [0,2,4], u1 empty. u0 runs 0; u1 steals the BACK of u0 = 4. Task 2 stays.
    WorkStealingFifoScheduler s(2);
    s.enqueue_ready(keys({0, 2, 4}));
    auto a = s.dispatch({0, 1});
    require(a.size() == 2 && is(a[0], 0, 0, false) && is(a[1], 4, 1, true), "steal from back");
    require(s.queued(0) == 1 && s.queued(1) == 0 && s.queued() == 1, "remaining queue");
    require(s.steal_stats().searches == 1 && s.steal_stats().probes == 1 && s.steal_stats().steals == 1,
            "steal counters");
}
void owner_dispatch_precedes_lower_id_thief() {
    // Only task 1 exists (owned by u1). Idle {0,1}. If thief u0 went first it would take
    // task 1 and leave its owner idle; the contract says owners dispatch first.
    WorkStealingFifoScheduler s(2);
    s.enqueue_ready(keys({1}));
    auto a = s.dispatch({0, 1});
    require(a.size() == 1 && is(a[0], 1, 1, false), "owner must keep its own task");
    require(s.steal_stats().steals == 0 && s.steal_stats().searches == 1 && s.steal_stats().probes == 1,
            "u0 searched once, probed u1 once, found nothing");
}
void victims_are_probed_cyclically() {
    // P=4, tasks: 1 (u1), 3 (u3), 4 (u0). Thief u2 probes u3, then u0, then u1.
    WorkStealingFifoScheduler s(4);
    s.enqueue_ready(keys({1, 3, 4}));
    require(is(s.dispatch({2})[0], 3, 2, true), "first victim after u2 is u3");
    require(is(s.dispatch({2})[0], 4, 2, true), "then wraps around to u0");
    require(is(s.dispatch({2})[0], 1, 2, true), "then u1");
    // Probes: 1 (found at u3) + 2 (u3 empty, found at u0) + 3 (u3, u0 empty, found at u1) = 6.
    require(s.steal_stats().probes == 6 && s.steal_stats().steals == 3 && s.steal_stats().searches == 3,
            "cyclic probe counts");
    require(s.dispatch({2}).empty() && s.steal_stats().probes == 9, "a failed search probes P-1 = 3 victims");
}
void each_victim_loses_its_back_first() {
    // P=4: u2 owns [2,6,10]. Thief u0 probes u1 (empty) then u2 -> steals 10, then 6, then 2.
    WorkStealingFifoScheduler s(4);
    s.enqueue_ready(keys({2, 6, 10}));
    for (TaskId expected : {10u, 6u, 2u}) {
        auto a = s.dispatch({0});
        require(a.size() == 1 && is(a[0], expected, 0, true), "back-to-front stealing order");
    }
}
void probes_are_bounded() {
    WorkStealingFifoScheduler four(4);
    require(four.dispatch({0, 1, 2, 3}).empty(), "nothing queued");
    require(four.steal_stats().searches == 4 && four.steal_stats().probes == 12 &&
            four.steal_stats().steals == 0, "4 thieves x (P-1)=3 probes");
    WorkStealingFifoScheduler one(1);
    require(one.dispatch({0}).empty() && one.steal_stats().probes == 0, "P=1 has no victims");
}
void rejects_bad_idle_lists_atomically() {
    WorkStealingFifoScheduler s(2);
    s.enqueue_ready(keys({0, 1}));
    throws<std::invalid_argument>([&] { s.dispatch({1, 0}); });
    throws<std::invalid_argument>([&] { s.dispatch({0, 0}); });
    throws<std::invalid_argument>([&] { s.dispatch({0, 2}); });
    require(s.queued() == 2 && s.steal_stats().searches == 0, "failed dispatch must have no effect");
    throws<std::invalid_argument>([] { WorkStealingFifoScheduler s0(0); });
}
void random_runs_dispatch_each_task_once_with_correct_flags() {
    std::mt19937 rng(202);
    for (std::size_t units : {1u, 2u, 3u, 8u}) {
        WorkStealingFifoScheduler s(units);
        std::vector<int> seen(300, 0);
        TaskId next = 0;
        std::size_t guard = 0;
        while ((next < 300 || s.queued() > 0) && guard++ < 100000) {
            std::vector<ReadyKey> batch;
            const std::size_t burst = rng() % 7;
            for (std::size_t i = 0; i < burst && next < 300; ++i) batch.push_back(ReadyKey{next++, 1, 0, 0});
            s.enqueue_ready(batch);
            std::vector<UnitId> idle;                       // a random ascending subset of units
            for (UnitId u = 0; u < units; ++u) if (rng() % 2) idle.push_back(u);
            auto assignments = s.dispatch(idle);
            require(assignments.size() <= idle.size(), "more assignments than idle units");
            std::vector<int> unit_used(units, 0);
            for (const Assignment& a : assignments) {
                ++seen[a.task];
                ++unit_used[a.unit];
                require(unit_used[a.unit] == 1, "unit assigned twice in one dispatch");
                require(a.stolen == (StaticFifoScheduler::owner_of(a.task, units) != a.unit),
                        "stolen flag must mean 'not on its owner unit'");
            }
        }
        for (int count : seen) require(count == 1, "task dispatched zero or multiple times");
    }
}
void factory_builds_stealing_scheduler() {
    auto s = make_scheduler(RunConfig{2, Policy::fifo, Distribution::work_stealing});
    s->enqueue_ready(keys({0, 2}));
    auto a = s->dispatch({0, 1});   // u0 takes 0; u1 steals 2
    require(a.size() == 2 && is(a[0], 0, 0, false) && is(a[1], 2, 1, true), "factory stealing behavior");
}
void oracle_skew_example_with_stealing() {
    // Durations 5,1,5,1 on P=2 (owners u0:{0,2}, u1:{1,3}).
    // t=0: 0->u0 [0,5), 1->u1 [0,1). t=1: u1 runs its own 3 [1,2). t=2: u1 is idle with an
    // empty queue and steals the back of u0's queue = task 2 -> u1 [2,7). t=5: nothing left.
    // Makespan 7: between central FIFO (6) and static (10).
    const std::vector<oracle::OracleTask> tasks = {{5, 5, {}}, {1, 1, {}}, {5, 5, {}}, {1, 1, {}}};
    WorkStealingFifoScheduler s(2);
    auto trace = oracle::run_tasks(tasks, s, 2);
    require(trace[0].start == 0 && trace[0].unit == 0 && trace[1].unit == 1 &&
            trace[3].start == 1 && trace[3].unit == 1, "early placements");
    require(trace[2].start == 2 && trace[2].finish == 7 && trace[2].unit == 1, "task 2 must be stolen by u1 at t=2");
    require(oracle::makespan_of(trace) == 7, "stealing makespan must be 7");
}
void skew_family_stealing_beats_static() {
    // N=100 and 1000 at every P. P=1: identical to static (= total work). P>=2: idle units
    // steal heavy tasks from unit 0's back, so the makespan beats static 10*ceil(N/P) and
    // is at least the lower bound max(ceil(W/P), 10).
    for (std::size_t n : {100u, 1000u})
        for (std::size_t p : {1u, 2u, 4u, 8u}) {
            OwnerSkewSettings cfg;
            cfg.tasks = n;
            cfg.units = p;
            std::vector<oracle::OracleTask> tasks;
            std::istringstream in(owner_skew_csv(cfg));
            std::string line;
            std::getline(in, line);  // header
            while (std::getline(in, line)) {
                const std::size_t last = line.rfind(',');
                const std::size_t first = line.rfind(',', last - 1);
                const Tick d = std::stoll(line.substr(first + 1, last - first - 1));
                tasks.push_back(oracle::OracleTask{d, d, {}});
            }
            const Tick heavy = static_cast<Tick>((n + p - 1) / p);
            const Tick work = 10 * heavy + static_cast<Tick>(n) - heavy;
            WorkStealingFifoScheduler s(p);
            const Tick span = oracle::makespan_of(oracle::run_tasks(tasks, s, p));
            const Tick lower = std::max<Tick>((work + static_cast<Tick>(p) - 1) / static_cast<Tick>(p), 10);
            require(span >= lower, "stealing beat the theoretical lower bound");
            if (p == 1) require(span == work, "P=1 equals total work");
            else        require(span < 10 * heavy, "stealing must beat static on the skew family");
        }
}
} // namespace

std::vector<std::pair<const char*, std::function<void()>>> stealing_tests() {
    return {
        {"stealing owners take their own front first", owners_take_their_own_front},
        {"stealing thief takes the back of a victim queue", thief_steals_from_the_back},
        {"stealing owner dispatch precedes lower-id thief", owner_dispatch_precedes_lower_id_thief},
        {"stealing probes victims cyclically", victims_are_probed_cyclically},
        {"stealing takes victim tasks back to front", each_victim_loses_its_back_first},
        {"stealing probes are bounded by P-1 per search", probes_are_bounded},
        {"stealing rejects bad idle lists atomically", rejects_bad_idle_lists_atomically},
        {"stealing random runs: exactly once and correct flags", random_runs_dispatch_each_task_once_with_correct_flags},
        {"stealing factory builds the scheduler", factory_builds_stealing_scheduler},
        {"stealing oracle: skew example makespan 7", oracle_skew_example_with_stealing},
        {"stealing beats static on the owner-skew family", skew_family_stealing_beats_static},
    };
}
} // namespace gpu
