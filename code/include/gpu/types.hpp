#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace gpu {
using Tick = std::int64_t;
using TaskId = std::size_t;
using UnitId = std::size_t;
inline constexpr std::size_t no_edge = std::numeric_limits<std::size_t>::max();

enum class Policy { fifo, shortest_job_first, critical_path };
enum class Distribution { central, static_round_robin, work_stealing };
enum class TaskPhase { blocked, ready, running, completed };
enum class EventKind { completion, arrival };

struct TaskSpec {
    std::string external_id;
    Tick release = 0;
    Tick duration = 1;
};
struct EdgePair { TaskId from; TaskId to; };
struct RunConfig {
    std::size_t compute_units = 2;
    Policy policy = Policy::fifo;
    Distribution distribution = Distribution::central;
};

inline void validate_config(const RunConfig& config) {
    if (config.compute_units == 0) throw std::invalid_argument("compute_units must be positive");
    if (config.policy != Policy::fifo && config.policy != Policy::shortest_job_first &&
        config.policy != Policy::critical_path) throw std::invalid_argument("unknown policy");
    if (config.distribution != Distribution::central &&
        config.distribution != Distribution::static_round_robin &&
        config.distribution != Distribution::work_stealing)
        throw std::invalid_argument("unknown distribution");
    if (config.distribution != Distribution::central && config.policy != Policy::fifo)
        throw std::invalid_argument("local distribution supports FIFO only");
}

inline Tick checked_add(Tick left, Tick right) {
    if (left < 0 || right < 0) throw std::invalid_argument("ticks must be nonnegative");
    if (left > std::numeric_limits<Tick>::max() - right)
        throw std::overflow_error("tick addition overflow");
    return left + right;
}

struct Event {
    Tick time;
    EventKind kind;
    TaskId task;
    UnitId unit;
    std::size_t sequence;
};
struct ReadyKey {
    TaskId task;
    Tick duration;
    Tick critical_rank;
    Tick ready_time;
};
struct Assignment { TaskId task; UnitId unit; bool stolen = false; };
struct TaskRuntime {
    TaskPhase phase = TaskPhase::blocked;
    bool released = false;
    std::size_t remaining_predecessors = 0;
    std::optional<Tick> ready, start, finish;
    std::optional<UnitId> unit;
};
struct TaskRecord {
    TaskId task;
    Tick ready, start, finish;
    UnitId unit, initial_owner;
    bool stolen = false;
};
struct RunResult {
    std::vector<TaskRecord> tasks; // indexed by TaskId; one record per task
    std::vector<Tick> busy_ticks;
    Tick makespan = 0;
};
} // namespace gpu
