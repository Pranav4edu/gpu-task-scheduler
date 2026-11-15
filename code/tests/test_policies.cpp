// Tests for the SJF and critical-path schedulers, plus a test-only mini simulator
// that replays the hand-calculated six-task oracle (docs/CONTRACTS.md) through
// the scheduler classes. The mini simulator is NOT the project engine: it scans
// arrays and exists only to check policy decisions against known answers.
#include "gpu/scheduler.hpp"
#include "gpu/self_test.hpp"
#include "oracle_sim.hpp"
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <vector>

namespace gpu {
namespace {
using oracle::OracleTask;
using oracle::Placed;
using oracle::run_tasks;
using oracle::makespan_of;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
// Fails the test (instead of indexing an empty vector) when nothing was dispatched.
Assignment first_of(const std::vector<Assignment>& assignments) {
    if (assignments.empty()) throw std::runtime_error("expected an assignment but got none");
    return assignments.front();
}
template<class Exception, class Action>
void throws(Action action) {
    try { action(); } catch (const Exception&) { return; }
    throw std::runtime_error("expected exception was not thrown");
}
// ReadyKey{task, duration, critical_rank, ready_time}
std::vector<TaskId> drain_one_unit(Scheduler& s, std::size_t count) {
    std::vector<TaskId> order;
    for (std::size_t i = 0; i < count; ++i) {
        auto a = s.dispatch({0});
        require(a.size() == 1, "expected one assignment");
        order.push_back(a[0].task);
    }
    return order;
}

void sjf_orders_by_duration_ready_then_id() {
    // Durations: t0=5, t1=2(ready 4), t2=9, t3=2(ready 1), t4=2(ready 1).
    // Duration 2 first: ready 1 beats ready 4, and 3 beats 4 on id -> 3,4,1, then 0, 2.
    CentralSjfScheduler s;
    s.enqueue_ready({{0, 5, 0, 0}, {1, 2, 0, 4}, {2, 9, 0, 0}, {3, 2, 0, 1}, {4, 2, 0, 1}});
    require(drain_one_unit(s, 5) == std::vector<TaskId>({3, 4, 1, 0, 2}), "SJF order");
}
void cp_orders_by_rank_ready_then_id() {
    // Ranks: t0=3, t1=8 (ready 5), t2=8 (ready 2), t3=8 (ready 2), t4=1.
    // Rank 8 first: ready 2 beats 5; 2 beats 3 on id -> 2,3,1, then 0, then 4.
    CentralCpScheduler s;
    s.enqueue_ready({{0, 1, 3, 0}, {1, 1, 8, 5}, {2, 1, 8, 2}, {3, 1, 8, 2}, {4, 1, 1, 0}});
    require(drain_one_unit(s, 5) == std::vector<TaskId>({2, 3, 1, 0, 4}), "CP order");
}
void cp_compares_extreme_ranks_without_negation() {
    CentralCpScheduler s;
    const Tick big = std::numeric_limits<Tick>::max();
    s.enqueue_ready({{1, 1, 1, 0}, {0, 1, big, 0}});
    require(drain_one_unit(s, 2) == std::vector<TaskId>({0, 1}), "huge rank must go first");
}
void insertion_order_does_not_matter() {
    std::vector<ReadyKey> keys = {{0, 4, 2, 0}, {1, 1, 9, 0}, {2, 7, 5, 0}, {3, 1, 9, 3}, {4, 4, 2, 1}};
    CentralSjfScheduler first_sjf;
    CentralCpScheduler first_cp;
    first_sjf.enqueue_ready(keys);
    first_cp.enqueue_ready(keys);
    const auto expected_sjf = drain_one_unit(first_sjf, 5);
    const auto expected_cp = drain_one_unit(first_cp, 5);
    std::sort(keys.begin(), keys.end(), [](const ReadyKey& a, const ReadyKey& b) { return a.task < b.task; });
    do {
        CentralSjfScheduler sjf;
        CentralCpScheduler cp;
        sjf.enqueue_ready(keys);
        cp.enqueue_ready(keys);
        require(drain_one_unit(sjf, 5) == expected_sjf, "SJF depends on insertion order");
        require(drain_one_unit(cp, 5) == expected_cp, "CP depends on insertion order");
    } while (std::next_permutation(keys.begin(), keys.end(),
             [](const ReadyKey& a, const ReadyKey& b) { return a.task < b.task; }));
}
void waiting_tasks_compete_with_new_batch() {
    // Epoch 1: durations 8 and 6 waiting; one unit takes the 6 (task 1).
    // Epoch 2: a new task with duration 3 must beat the still-waiting 8.
    CentralSjfScheduler s;
    s.enqueue_ready({{0, 8, 0, 0}, {1, 6, 0, 0}});
    require(first_of(s.dispatch({0})).task == 1, "epoch 1 pick");
    s.enqueue_ready({{2, 3, 0, 4}});
    require(first_of(s.dispatch({0})).task == 2, "new short task should beat old long task");
}
void dispatch_shape_and_validation() {
    CentralCpScheduler s;
    s.enqueue_ready({{0, 1, 5, 0}, {1, 1, 9, 0}, {2, 1, 7, 0}});
    // Ranks 9,7,5 -> tasks 1,2,0. Idle units 2 and 4 get tasks 1 and 2; task 0 stays.
    auto a = s.dispatch({2, 4});
    require(a.size() == 2 && a[0].task == 1 && a[0].unit == 2 && a[1].task == 2 && a[1].unit == 4 &&
            !a[0].stolen && !a[1].stolen, "wrong assignments");
    require(s.queued() == 1, "one task should remain");
    throws<std::invalid_argument>([&] { s.dispatch({3, 1}); });
    throws<std::invalid_argument>([&] { s.dispatch({1, 1}); });
    require(s.queued() == 1 && s.queue_stats().pops == 2, "failed dispatch must not pop");
    require(s.dispatch({}).empty(), "no idle units");
    auto b = s.dispatch({0, 1});
    require(b.size() == 1 && b[0].task == 0 && b[0].unit == 0, "more idle units than tasks");
    require(s.dispatch({0}).empty(), "empty heap");
}
void works_through_base_interface_and_counts() {
    std::unique_ptr<Scheduler> s = std::make_unique<CentralSjfScheduler>();
    s->enqueue_ready({{0, 3, 0, 0}, {1, 1, 0, 0}, {2, 2, 0, 0}});
    require(first_of(s->dispatch({0})).task == 1, "base interface");
    CentralSjfScheduler direct;
    direct.enqueue_ready({{0, 3, 0, 0}, {1, 1, 0, 0}, {2, 2, 0, 0}});
    direct.dispatch({0, 1});
    require(direct.queue_stats().pushes == 3 && direct.queue_stats().pops == 2 &&
            direct.queue_stats().peak_size == 3 && direct.queue_stats().comparisons > 0,
            "heap counters not reflecting usage");
}

// ---- six-task oracle through a test-only mini simulator ----
const std::vector<OracleTask>& six_tasks() {
    // A..F = ids 0..5. Ranks A=5 B=6 C=11 D=3 E=5 F=1 are the hand-calculated values.
    static const std::vector<OracleTask> tasks = {
        {2, 5, {}}, {3, 6, {}}, {6, 11, {}}, {2, 3, {0, 1}}, {4, 5, {2}}, {1, 1, {3, 4}}};
    return tasks;
}
std::vector<Placed> run_six(Scheduler& scheduler, std::size_t units) {
    return run_tasks(six_tasks(), scheduler, units);
}
void expect_trace(const std::vector<Placed>& got, const std::vector<Placed>& want, const char* msg) {
    for (std::size_t i = 0; i < want.size(); ++i)
        require(got[i].start == want[i].start && got[i].finish == want[i].finish &&
                got[i].unit == want[i].unit, msg);
}
// Hand trace from the contract/report. Order is A,B,C,D,E,F.
void oracle_fifo_and_sjf() {
    const std::vector<Placed> want = {{0, 2, 0}, {0, 3, 1}, {2, 8, 0}, {3, 5, 1}, {8, 12, 0}, {12, 13, 0}};
    CentralFifoScheduler fifo;
    auto fifo_trace = run_six(fifo, 2);
    expect_trace(fifo_trace, want, "FIFO trace differs from hand oracle");
    require(makespan_of(fifo_trace) == 13, "FIFO makespan must be 13");
    CentralSjfScheduler sjf;
    auto sjf_trace = run_six(sjf, 2);
    expect_trace(sjf_trace, want, "SJF trace differs from hand oracle");
    require(makespan_of(sjf_trace) == 13, "SJF makespan must be 13");
}
void oracle_critical_path() {
    const std::vector<Placed> want = {{3, 5, 1}, {0, 3, 1}, {0, 6, 0}, {5, 7, 1}, {6, 10, 0}, {10, 11, 0}};
    CentralCpScheduler cp;
    auto trace = run_six(cp, 2);
    expect_trace(trace, want, "CP trace differs from hand oracle");
    require(makespan_of(trace) == 11, "CP makespan must be 11");
}
void oracle_single_unit_is_serial() {
    // P=1: total duration 18 regardless of policy; no overlap possible.
    CentralFifoScheduler fifo;
    CentralCpScheduler cp;
    require(makespan_of(run_six(fifo, 1)) == 18, "FIFO P=1 makespan must be 18");
    require(makespan_of(run_six(cp, 1)) == 18, "CP P=1 makespan must be 18");
}
void oracle_static_fifo() {
    // owner = id % 2: A,C,E -> unit 0; B,D,F -> unit 1.
    // t=0 A[0,2)u0 B[0,3)u1 | t=2 C[2,8)u0 | t=3 D[3,5)u1 | t=8 E[8,12)u0 | t=12 F[12,13)u1.
    // Same makespan (13) as central FIFO, but F runs on unit 1 because it is owned by 1.
    const std::vector<Placed> want = {{0, 2, 0}, {0, 3, 1}, {2, 8, 0}, {3, 5, 1}, {8, 12, 0}, {12, 13, 1}};
    StaticFifoScheduler s(2);
    auto trace = run_six(s, 2);
    expect_trace(trace, want, "static FIFO trace differs from hand oracle");
    require(makespan_of(trace) == 13, "static FIFO makespan must be 13");
}
void oracle_static_skew_idles_capacity() {
    // Four independent tasks, durations 5,1,5,1; owners: unit 0 gets ids 0,2 (5+5), unit 1 gets 1,3 (1+1).
    // Static: unit 0 runs [0,5),[5,10); unit 1 runs [0,1),[1,2) and then sits idle -> makespan 10.
    // Central FIFO: 0->u0, 1->u1; at t=1 task 2->u1 [1,6); at t=5 task 3->u0 [5,6) -> makespan 6.
    const std::vector<OracleTask> tasks = {{5, 5, {}}, {1, 1, {}}, {5, 5, {}}, {1, 1, {}}};
    StaticFifoScheduler stat(2);
    CentralFifoScheduler central;
    require(makespan_of(run_tasks(tasks, stat, 2)) == 10, "static skew makespan must be 10");
    require(makespan_of(run_tasks(tasks, central, 2)) == 6, "central skew makespan must be 6");
}
} // namespace

std::vector<std::pair<const char*, std::function<void()>>> policy_tests() {
    return {
        {"SJF orders by duration, ready time, task id", sjf_orders_by_duration_ready_then_id},
        {"CP orders by rank, ready time, task id", cp_orders_by_rank_ready_then_id},
        {"CP compares extreme ranks without negation", cp_compares_extreme_ranks_without_negation},
        {"priority schedulers ignore insertion order", insertion_order_does_not_matter},
        {"SJF waiting tasks compete with new batch", waiting_tasks_compete_with_new_batch},
        {"priority dispatch shape and validation", dispatch_shape_and_validation},
        {"priority schedulers use interface and heap counters", works_through_base_interface_and_counts},
        {"six-task oracle: FIFO and SJF (makespan 13)", oracle_fifo_and_sjf},
        {"six-task oracle: critical path (makespan 11)", oracle_critical_path},
        {"six-task oracle: one unit is serial (makespan 18)", oracle_single_unit_is_serial},
        {"six-task oracle: static FIFO (makespan 13, F on unit 1)", oracle_static_fifo},
        {"skew oracle: static idles capacity (10) vs central (6)", oracle_static_skew_idles_capacity},
    };
}
} // namespace gpu
