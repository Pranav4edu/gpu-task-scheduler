#pragma once
#include "gpu/contracts.hpp"
#include "gpu/ds/binary_heap.hpp"
#include "gpu/ds/ring_deque.hpp"
#include "gpu/priority.hpp"
#include <memory>
#include <stdexcept>
#include <vector>

namespace gpu {

// Centralized FIFO ready queue (Aayush's scheduling module).
//
// One global RingDeque<TaskId> holds every eligible-but-not-started task.
// enqueue_ready appends the engine's newly ready batch (already sorted by dense
// TaskId) to the back, so tasks that were already waiting stay ahead of new ones.
// dispatch pops from the front, one task per idle unit, lowest unit ID first.
//
// The scheduler only owns queue membership. It never advances the clock, never
// finishes a task and never touches dependency counters: the engine does that.
class CentralFifoScheduler : public Scheduler {
public:
    // Precondition: each task is enqueued at most once (the engine guarantees it).
    void enqueue_ready(const std::vector<ReadyKey>& batch) override;

    // idle_units must be strictly ascending (no duplicates), otherwise
    // std::invalid_argument is thrown BEFORE anything is removed from the queue.
    // Returns min(idle_units.size(), queued()) assignments, stolen = false.
    std::vector<Assignment> dispatch(const std::vector<UnitId>& idle_units) override;

    std::size_t queued() const noexcept { return queue_.size(); }
    // Operation counters of the underlying deque (for ds_metrics.csv).
    const RingDequeStats& queue_stats() const noexcept { return queue_.stats(); }

private:
    RingDeque<TaskId> queue_;
};

// Centralized priority ready queue: one shared BinaryHeap<ReadyKey, Before>.
// Before(a, b) == true means a is dispatched first (heap convention, see
// priority.hpp). Entries are immutable ReadyKeys, so the heap order can never be
// invalidated while a task waits. Unlike FIFO, tasks that were already waiting
// compete with every newly ready task, so batch order does not matter.
//   CentralSjfScheduler: duration, then ready_time, then TaskId (smallest first)
//   CentralCpScheduler:  critical_rank (largest first), then ready_time, then TaskId
template<class Before>
class CentralHeapScheduler : public Scheduler {
public:
    void enqueue_ready(const std::vector<ReadyKey>& batch) override {
        for (const ReadyKey& key : batch) heap_.push(key);
    }

    // Same contract as CentralFifoScheduler::dispatch: idle_units strictly
    // ascending (validated before any pop), one task per idle unit, stolen=false.
    std::vector<Assignment> dispatch(const std::vector<UnitId>& idle_units) override {
        for (std::size_t i = 1; i < idle_units.size(); ++i)
            if (idle_units[i - 1] >= idle_units[i])
                throw std::invalid_argument("idle_units must be strictly ascending");
        std::vector<Assignment> assignments;
        for (UnitId unit : idle_units) {
            if (heap_.empty()) break;
            assignments.push_back(Assignment{heap_.pop().task, unit, false});
        }
        return assignments;
    }

    std::size_t queued() const noexcept { return heap_.size(); }
    // Counters of the shared heap class (pushes, pops, comparisons, swaps, peak).
    const HeapStats& queue_stats() const noexcept { return heap_.stats(); }

private:
    BinaryHeap<ReadyKey, Before> heap_;
};

using CentralSjfScheduler = CentralHeapScheduler<SjfBefore>;
using CentralCpScheduler = CentralHeapScheduler<CriticalPathBefore>;

// Static round-robin placement with local FIFO queues.
//
// Every task has a fixed owner unit, owner = TaskId % P, decided the moment it
// becomes eligible. Each unit has its own RingDeque<TaskId>; a unit can only run
// tasks from its own queue, even if other units' queues are long. That is the
// point of the experiment: it exposes idle capacity caused by placement alone.
// Only eligible tasks enter a queue (blocked tasks never occupy one).
class StaticFifoScheduler : public Scheduler {
public:
    // Throws std::invalid_argument if compute_units == 0.
    explicit StaticFifoScheduler(std::size_t compute_units);

    // Precondition: compute_units > 0. Matches TaskRecord::initial_owner.
    static UnitId owner_of(TaskId task, std::size_t compute_units) noexcept {
        return task % compute_units;
    }

    // Appends each task to the back of its owner's local queue, in batch order.
    void enqueue_ready(const std::vector<ReadyKey>& batch) override;

    // idle_units must be strictly ascending and each < compute_units (otherwise
    // std::invalid_argument, before any pop). Each idle unit takes the FRONT of its
    // OWN queue only; an idle unit with an empty local queue gets nothing.
    std::vector<Assignment> dispatch(const std::vector<UnitId>& idle_units) override;

    std::size_t units() const noexcept { return local_.size(); }
    std::size_t queued() const noexcept;               // total over all local queues
    std::size_t queued(UnitId unit) const { return local_.at(unit).size(); }
    const RingDequeStats& queue_stats(UnitId unit) const { return local_.at(unit).stats(); }

private:
    std::vector<RingDeque<TaskId>> local_;
};

// Counters for the stealing search (per scheduler lifetime).
struct StealStats {
    std::size_t searches = 0;  // times an idle unit had an empty own queue and went looking
    std::size_t probes = 0;    // victim queues examined (at most P-1 per search)
    std::size_t steals = 0;    // searches that found work
};

// Static round-robin placement plus work stealing, local FIFO order (optional
// extension; FIFO only). Same owner = TaskId % P and per-unit RingDeque as
// StaticFifoScheduler, with one change: a unit whose own queue is empty may take
// work from another unit's queue.
//
// dispatch(idle_units) in two phases, exactly as in docs/CONTRACTS.md:
//  1. OWNER PHASE: every idle unit with a non-empty own queue takes the FRONT of it.
//     This runs for ALL idle units first, so a low-numbered thief can never take a
//     task that its higher-numbered owner is about to run.
//  2. THIEF PHASE: remaining idle units, in ascending id, probe the other units in
//     cyclic order after themselves (u+1, u+2, ... mod P), at most P-1 probes. The
//     first non-empty victim loses its BACK element, which is dispatched at once
//     with stolen = true. A failed search just returns nothing and waits for the
//     next real event; there is no retry at the same time stamp.
// Only queued (not yet started) tasks are ever stolen; a running task is no longer
// in any queue, so it can never migrate. Returned assignments are in idle-unit order.
class WorkStealingFifoScheduler : public Scheduler {
public:
    // Throws std::invalid_argument if compute_units == 0.
    explicit WorkStealingFifoScheduler(std::size_t compute_units);

    void enqueue_ready(const std::vector<ReadyKey>& batch) override;
    // Validation as in StaticFifoScheduler::dispatch (ascending, each < P, before any pop).
    std::vector<Assignment> dispatch(const std::vector<UnitId>& idle_units) override;

    std::size_t units() const noexcept { return local_.size(); }
    std::size_t queued() const noexcept;
    std::size_t queued(UnitId unit) const { return local_.at(unit).size(); }
    const RingDequeStats& queue_stats(UnitId unit) const { return local_.at(unit).stats(); }
    const StealStats& steal_stats() const noexcept { return steal_stats_; }

private:
    std::vector<RingDeque<TaskId>> local_;
    StealStats steal_stats_;
};

// Builds the scheduler for a run configuration. Calls validate_config first, so
// invalid combinations (e.g. SJF with static queues, or CP with stealing) throw
// std::invalid_argument. Supported: central FIFO/SJF/CP, static FIFO, stealing FIFO.
std::unique_ptr<Scheduler> make_scheduler(const RunConfig& config);

} // namespace gpu
