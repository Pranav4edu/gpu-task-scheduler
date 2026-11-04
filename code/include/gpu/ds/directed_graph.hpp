#pragma once
#include "gpu/types.hpp"

namespace gpu {
// Shaswitha's implementation contract. Declarations only: NOT implemented yet.
// build validates endpoints, self-edges and duplicate pairs; DAG checking is
// separate. head/next are indices, not owning pointers. Freeze after build.
class DirectedGraph {
public:
    static DirectedGraph build(std::size_t vertices, std::vector<EdgePair> edges);
    std::size_t vertex_count() const noexcept;
    std::size_t edge_count() const noexcept;
    template<class Visitor>
    void for_each_successor(TaskId from, Visitor visit) const;
    const std::vector<std::size_t>& indegrees() const noexcept;
private:
    struct Edge { TaskId to; std::size_t next; };
    std::vector<std::size_t> heads_, indegrees_;
    std::vector<Edge> edges_;
};
} // namespace gpu
