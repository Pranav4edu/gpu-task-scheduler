// Tests for StaticFifoScheduler and make_scheduler. Expected values are worked out
// by hand in the comments.
#include "gpu/scheduler.hpp"
#include "gpu/self_test.hpp"
#include <stdexcept>
#include <vector>

namespace gpu {
namespace {
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
std::vector<ReadyKey> keys(std::initializer_list<TaskId> ids) {
    std::vector<ReadyKey> out;
    for (TaskId id : ids) out.push_back(ReadyKey{id, 1, 0, 0});
    return out;
}
bool is(const Assignment& a, TaskId task, UnitId unit) {
    return a.task == task && a.unit == unit && !a.stolen;
}

void owner_is_task_mod_p() {
    // P=3: ids 0..7 -> owners 0,1,2,0,1,2,0,1.
    const UnitId want[] = {0, 1, 2, 0, 1, 2, 0, 1};
    for (TaskId t = 0; t < 8; ++t)
        require(StaticFifoScheduler::owner_of(t, 3) == want[t], "owner mapping");
    StaticFifoScheduler s(3);
    s.enqueue_ready(keys({0, 1, 2, 3, 4, 5}));
    // Queues: unit0 [0,3], unit1 [1,4], unit2 [2,5].
    require(s.queued(0) == 2 && s.queued(1) == 2 && s.queued(2) == 2 && s.queued() == 6,
            "tasks not placed in owner queues");
    auto first = s.dispatch({0, 1, 2});
    require(first.size() == 3 && is(first[0], 0, 0) && is(first[1], 1, 1) && is(first[2], 2, 2),
            "first round");
    auto second = s.dispatch({0, 1, 2});
    require(second.size() == 3 && is(second[0], 3, 0) && is(second[1], 4, 1) && is(second[2], 5, 2),
            "second round");
}
void idle_unit_cannot_take_other_units_work() {
    // P=2; tasks 0,2,4 are ALL owned by unit 0. Unit 1 is idle but must get nothing.
    StaticFifoScheduler s(2);
    s.enqueue_ready(keys({0, 2, 4}));
    auto a = s.dispatch({0, 1});
    require(a.size() == 1 && is(a[0], 0, 0), "only the owner may run the task");
    require(s.queued(0) == 2 && s.queued(1) == 0, "queue sizes after dispatch");
    require(s.dispatch({1}).empty(), "unit 1 must not steal from unit 0");
    require(s.queued() == 2, "nothing may be removed by a non-owner");
}
void local_fifo_keeps_old_entries_ahead() {
    // Unit 0 queue [0,2]; takes 0. New batch 4,6 appended -> [2,4,6].
    StaticFifoScheduler s(2);
    s.enqueue_ready(keys({0, 2}));
    require(is(first_of(s.dispatch({0})), 0, 0), "first pick");
    s.enqueue_ready(keys({4, 6}));
    for (TaskId expected : {2u, 4u, 6u}) {
        auto a = s.dispatch({0});
        require(a.size() == 1 && is(a[0], expected, 0), "local FIFO order");
    }
}
void rejects_bad_idle_lists_atomically() {
    StaticFifoScheduler s(2);
    s.enqueue_ready(keys({0, 1}));
    throws<std::invalid_argument>([&] { s.dispatch({1, 0}); });   // descending
    throws<std::invalid_argument>([&] { s.dispatch({0, 0}); });   // duplicate
    throws<std::invalid_argument>([&] { s.dispatch({0, 2}); });   // unit 2 does not exist (P=2)
    require(s.queued() == 2 && s.queue_stats(0).pops == 0 && s.queue_stats(1).pops == 0,
            "failed dispatch must not pop");
    require(s.dispatch({}).empty(), "no idle units");
}
void zero_units_rejected() {
    throws<std::invalid_argument>([] { StaticFifoScheduler s(0); });
}
void one_unit_behaves_like_central_fifo() {
    StaticFifoScheduler stat(1);
    CentralFifoScheduler central;
    stat.enqueue_ready(keys({3, 1, 2}));
    central.enqueue_ready(keys({3, 1, 2}));
    for (int i = 0; i < 3; ++i) {
        auto a = stat.dispatch({0});
        auto b = central.dispatch({0});
        require(a.size() == 1 && b.size() == 1 && a[0].task == b[0].task, "P=1 must equal central FIFO");
    }
}
void every_task_dispatched_exactly_once() {
    StaticFifoScheduler s(4);
    std::vector<int> seen(200, 0);
    TaskId next = 0;
    for (int epoch = 0; epoch < 80; ++epoch) {
        std::vector<ReadyKey> batch;
        for (int i = 0; i < 5 && next < 200; ++i) batch.push_back(ReadyKey{next++, 1, 0, 0});
        s.enqueue_ready(batch);
        for (const Assignment& a : s.dispatch({0, 1, 2, 3})) ++seen[a.task];
    }
    while (s.queued() > 0)
        for (const Assignment& a : s.dispatch({0, 1, 2, 3})) ++seen[a.task];
    for (int count : seen) require(count == 1, "task dispatched zero or multiple times");
}
void per_queue_counters() {
    StaticFifoScheduler s(2);
    s.enqueue_ready(keys({0, 1, 2, 3, 4}));   // unit 0 gets 0,2,4; unit 1 gets 1,3
    s.dispatch({0, 1});
    require(s.queue_stats(0).pushes == 3 && s.queue_stats(0).pops == 1 && s.queue_stats(0).peak_size == 3,
            "unit 0 counters");
    require(s.queue_stats(1).pushes == 2 && s.queue_stats(1).pops == 1, "unit 1 counters");
}
void factory_builds_each_configuration() {
    // Central FIFO: arrival order. Tasks (id,duration,rank): 0:(9,1) 1:(2,5) 2:(5,9).
    const std::vector<ReadyKey> batch = {{0, 9, 1, 0}, {1, 2, 5, 0}, {2, 5, 9, 0}};
    auto pick = [&](Policy p, Distribution d, std::size_t units) {
        auto s = make_scheduler(RunConfig{units, p, d});
        s->enqueue_ready(batch);
        return first_of(s->dispatch({0})).task;
    };
    require(pick(Policy::fifo, Distribution::central, 2) == 0, "central FIFO takes first arrival");
    require(pick(Policy::shortest_job_first, Distribution::central, 2) == 1, "SJF takes duration 2");
    require(pick(Policy::critical_path, Distribution::central, 2) == 2, "CP takes rank 9");
    // Static P=2: unit 0 owns tasks 0 and 2 -> its front is task 0.
    require(pick(Policy::fifo, Distribution::static_round_robin, 2) == 0, "static FIFO unit 0 front");
}
void factory_rejects_bad_configs() {
    throws<std::invalid_argument>([] { make_scheduler(RunConfig{0, Policy::fifo, Distribution::central}); });
    throws<std::invalid_argument>([] {
        make_scheduler(RunConfig{2, Policy::shortest_job_first, Distribution::static_round_robin}); });
    throws<std::invalid_argument>([] {
        make_scheduler(RunConfig{2, Policy::critical_path, Distribution::work_stealing}); });
    // Work stealing is FIFO-only and now supported.
    require(make_scheduler(RunConfig{2, Policy::fifo, Distribution::work_stealing}) != nullptr,
            "stealing FIFO should build");
}
} // namespace

std::vector<std::pair<const char*, std::function<void()>>> static_scheduler_tests() {
    return {
        {"static FIFO owner is task id mod P", owner_is_task_mod_p},
        {"static FIFO idle unit cannot take another unit's work", idle_unit_cannot_take_other_units_work},
        {"static FIFO local queues keep old entries ahead", local_fifo_keeps_old_entries_ahead},
        {"static FIFO rejects bad idle lists atomically", rejects_bad_idle_lists_atomically},
        {"static FIFO rejects zero units", zero_units_rejected},
        {"static FIFO with one unit equals central FIFO", one_unit_behaves_like_central_fifo},
        {"static FIFO dispatches every task exactly once", every_task_dispatched_exactly_once},
        {"static FIFO per-queue deque counters", per_queue_counters},
        {"scheduler factory builds each configuration", factory_builds_each_configuration},
        {"scheduler factory rejects invalid configurations", factory_rejects_bad_configs},
    };
}
} // namespace gpu
