#pragma once
#include "gpu/types.hpp"
#include <tuple>

namespace gpu {
struct EventBefore {
    bool operator()(const Event& a, const Event& b) const noexcept {
        return std::tie(a.time, a.kind, a.task, a.unit, a.sequence) <
               std::tie(b.time, b.kind, b.task, b.unit, b.sequence);
    }
};
struct SjfBefore {
    bool operator()(const ReadyKey& a, const ReadyKey& b) const noexcept {
        return std::tie(a.duration, a.ready_time, a.task) <
               std::tie(b.duration, b.ready_time, b.task);
    }
};
struct CriticalPathBefore {
    bool operator()(const ReadyKey& a, const ReadyKey& b) const noexcept {
        if (a.critical_rank != b.critical_rank) return a.critical_rank > b.critical_rank;
        return std::tie(a.ready_time, a.task) < std::tie(b.ready_time, b.task);
    }
};
} // namespace gpu
