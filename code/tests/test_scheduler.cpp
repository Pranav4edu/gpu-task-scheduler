// Unit tests for the scheduler module (Aayush). Expected values are worked out
// by hand in the comments, not computed with the code under test.
#include "gpu/scheduler.hpp"
#include "gpu/self_test.hpp"
#include <memory>
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
ReadyKey key(TaskId task) { return ReadyKey{task, 1, 0, 0}; }
std::vector<ReadyKey> keys(std::initializer_list<TaskId> ids) {
    std::vector<ReadyKey> out;
    for (TaskId id : ids) out.push_back(key(id));
    return out;
}
bool is(const Assignment& a, TaskId task, UnitId unit) {
    return a.task == task && a.unit == unit && !a.stolen;
}

void empty_queue_dispatches_nothing() {
    CentralFifoScheduler s;
    require(s.dispatch({0, 1, 2}).empty(), "empty queue must give no assignments");
    require(s.dispatch({}).empty(), "no idle units must give no assignments");
}
void fewer_idle_than_queued() {
    // Queue: 4,5,6,7. Idle units 1 and 3 -> (4 on unit 1), (5 on unit 3); 6,7 stay.
    CentralFifoScheduler s;
    s.enqueue_ready(keys({4, 5, 6, 7}));
    auto a = s.dispatch({1, 3});
    require(a.size() == 2 && is(a[0], 4, 1) && is(a[1], 5, 3), "wrong FIFO assignment");
    require(s.queued() == 2, "unassigned tasks must stay queued");
}
void more_idle_than_queued() {
    // Queue: 9. Idle units 0,1,2 -> only (9 on unit 0); units 1,2 get nothing.
    CentralFifoScheduler s;
    s.enqueue_ready(keys({9}));
    auto a = s.dispatch({0, 1, 2});
    require(a.size() == 1 && is(a[0], 9, 0), "lowest idle unit must get the task");
    require(s.queued() == 0, "queue should be empty");
}
void old_entries_stay_ahead_of_new_batch() {
    // Epoch 1: queue 10,11,12; one idle unit takes 10. Queue 11,12.
    // Epoch 2: new batch 3,4 appended -> 11,12,3,4 (NOT sorted globally).
    CentralFifoScheduler s;
    s.enqueue_ready(keys({10, 11, 12}));
    require(is(first_of(s.dispatch({0})), 10, 0), "epoch 1");
    s.enqueue_ready(keys({3, 4}));
    auto a = s.dispatch({0, 1, 2, 3});
    require(a.size() == 4, "all four queued tasks should be assigned");
    require(is(a[0], 11, 0) && is(a[1], 12, 1) && is(a[2], 3, 2) && is(a[3], 4, 3),
            "old waiting tasks must precede the new batch");
}
void every_task_dispatched_exactly_once() {
    CentralFifoScheduler s;
    std::vector<int> seen(100, 0);
    TaskId next = 0;
    // Arrivals of 7 tasks per epoch, two idle units per epoch, until drained.
    for (int epoch = 0; epoch < 60; ++epoch) {
        std::vector<ReadyKey> batch;
        for (int i = 0; i < 7 && next < 100; ++i) batch.push_back(key(next++));
        s.enqueue_ready(batch);
        for (const Assignment& a : s.dispatch({0, 1})) ++seen[a.task];
    }
    while (s.queued() > 0) for (const Assignment& a : s.dispatch({0, 1})) ++seen[a.task];
    for (int count : seen) require(count == 1, "task dispatched zero or multiple times");
}
void rejects_bad_idle_lists_without_side_effects() {
    CentralFifoScheduler s;
    s.enqueue_ready(keys({1, 2, 3}));
    throws<std::invalid_argument>([&] { s.dispatch({2, 1}); });   // descending
    throws<std::invalid_argument>([&] { s.dispatch({0, 0}); });   // duplicate
    require(s.queued() == 3, "failed dispatch must not remove tasks");
    require(s.queue_stats().pops == 0, "failed dispatch must not pop");
}
void works_through_base_interface() {
    std::unique_ptr<Scheduler> s = std::make_unique<CentralFifoScheduler>();
    s->enqueue_ready(keys({2, 0}));
    auto a = s->dispatch({5});
    require(a.size() == 1 && is(a[0], 2, 5), "engine-visible interface broken");
}
void deque_counters_reflect_usage() {
    CentralFifoScheduler s;
    s.enqueue_ready(keys({0, 1, 2, 3, 4}));
    s.dispatch({0, 1});
    require(s.queue_stats().pushes == 5 && s.queue_stats().pops == 2, "push/pop counters");
    require(s.queue_stats().peak_size == 5, "peak queue size");
}
} // namespace

std::vector<std::pair<const char*, std::function<void()>>> scheduler_tests() {
    return {
        {"central FIFO empty queue and no idle units", empty_queue_dispatches_nothing},
        {"central FIFO fewer idle units than tasks", fewer_idle_than_queued},
        {"central FIFO more idle units than tasks", more_idle_than_queued},
        {"central FIFO old entries stay ahead of new batch", old_entries_stay_ahead_of_new_batch},
        {"central FIFO dispatches every task exactly once", every_task_dispatched_exactly_once},
        {"central FIFO rejects bad idle lists atomically", rejects_bad_idle_lists_without_side_effects},
        {"central FIFO works through Scheduler interface", works_through_base_interface},
        {"central FIFO uses RingDeque counters", deque_counters_reflect_usage},
    };
}
} // namespace gpu
