#include "gpu/scheduler.hpp"
#include <optional>
#include <stdexcept>

namespace gpu {
namespace {
// idle units must be strictly ascending and each < unit_count (no duplicates).
void validate_idle_units(const std::vector<UnitId>& idle_units, std::size_t unit_count) {
    for (std::size_t i = 0; i < idle_units.size(); ++i) {
        if (idle_units[i] >= unit_count) throw std::invalid_argument("idle unit id out of range");
        if (i > 0 && idle_units[i - 1] >= idle_units[i])
            throw std::invalid_argument("idle_units must be strictly ascending");
    }
}
} // namespace

void CentralFifoScheduler::enqueue_ready(const std::vector<ReadyKey>& batch) {
    // Batch order is the engine's ascending-TaskId order; keep it as-is.
    for (const ReadyKey& key : batch) queue_.push_back(key.task);
}

std::vector<Assignment> CentralFifoScheduler::dispatch(const std::vector<UnitId>& idle_units) {
    // Validate first so a bad call cannot leave the queue half-drained.
    for (std::size_t i = 1; i < idle_units.size(); ++i)
        if (idle_units[i - 1] >= idle_units[i])
            throw std::invalid_argument("idle_units must be strictly ascending");

    std::vector<Assignment> assignments;
    for (UnitId unit : idle_units) {
        if (queue_.empty()) break;  // never invent work for the remaining idle units
        assignments.push_back(Assignment{queue_.pop_front(), unit, false});
    }
    return assignments;
}

StaticFifoScheduler::StaticFifoScheduler(std::size_t compute_units) {
    if (compute_units == 0) throw std::invalid_argument("compute_units must be positive");
    local_.resize(compute_units);
}

std::size_t StaticFifoScheduler::queued() const noexcept {
    std::size_t total = 0;
    for (const auto& queue : local_) total += queue.size();
    return total;
}

void StaticFifoScheduler::enqueue_ready(const std::vector<ReadyKey>& batch) {
    for (const ReadyKey& key : batch)
        local_[owner_of(key.task, local_.size())].push_back(key.task);
}

std::vector<Assignment> StaticFifoScheduler::dispatch(const std::vector<UnitId>& idle_units) {
    // Validate everything first so a bad call cannot leave queues half-drained.
    validate_idle_units(idle_units, local_.size());
    std::vector<Assignment> assignments;
    for (UnitId unit : idle_units) {
        RingDeque<TaskId>& mine = local_[unit];
        if (mine.empty()) continue;  // never take another unit's work (that is stealing)
        assignments.push_back(Assignment{mine.pop_front(), unit, false});
    }
    return assignments;
}

WorkStealingFifoScheduler::WorkStealingFifoScheduler(std::size_t compute_units) {
    if (compute_units == 0) throw std::invalid_argument("compute_units must be positive");
    local_.resize(compute_units);
}

std::size_t WorkStealingFifoScheduler::queued() const noexcept {
    std::size_t total = 0;
    for (const auto& queue : local_) total += queue.size();
    return total;
}

void WorkStealingFifoScheduler::enqueue_ready(const std::vector<ReadyKey>& batch) {
    for (const ReadyKey& key : batch)
        local_[StaticFifoScheduler::owner_of(key.task, local_.size())].push_back(key.task);
}

std::vector<Assignment> WorkStealingFifoScheduler::dispatch(const std::vector<UnitId>& idle_units) {
    validate_idle_units(idle_units, local_.size());
    std::vector<std::optional<Assignment>> chosen(idle_units.size());

    // Phase 1: every idle owner takes the front of its own queue.
    for (std::size_t k = 0; k < idle_units.size(); ++k) {
        RingDeque<TaskId>& mine = local_[idle_units[k]];
        if (!mine.empty()) chosen[k] = Assignment{mine.pop_front(), idle_units[k], false};
    }

    // Phase 2: remaining idle units (own queue empty) steal from the back of the first
    // non-empty victim, probing cyclically after themselves, at most P-1 probes.
    const std::size_t unit_count = local_.size();
    for (std::size_t k = 0; k < idle_units.size(); ++k) {
        if (chosen[k]) continue;
        const UnitId thief = idle_units[k];
        ++steal_stats_.searches;
        for (std::size_t step = 1; step < unit_count; ++step) {
            const UnitId victim = (thief + step) % unit_count;
            ++steal_stats_.probes;
            if (local_[victim].empty()) continue;
            chosen[k] = Assignment{local_[victim].pop_back(), thief, true};
            ++steal_stats_.steals;
            break;
        }
    }

    std::vector<Assignment> assignments;
    for (const auto& a : chosen)
        if (a) assignments.push_back(*a);
    return assignments;
}

std::unique_ptr<Scheduler> make_scheduler(const RunConfig& config) {
    validate_config(config);
    switch (config.distribution) {
        case Distribution::central:
            switch (config.policy) {
                case Policy::fifo: return std::make_unique<CentralFifoScheduler>();
                case Policy::shortest_job_first: return std::make_unique<CentralSjfScheduler>();
                case Policy::critical_path: return std::make_unique<CentralCpScheduler>();
            }
            break;
        case Distribution::static_round_robin:
            return std::make_unique<StaticFifoScheduler>(config.compute_units);
        case Distribution::work_stealing:
            return std::make_unique<WorkStealingFifoScheduler>(config.compute_units);
    }
    throw std::invalid_argument("unsupported scheduler configuration");
}

} // namespace gpu
