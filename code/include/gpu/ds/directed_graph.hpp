#pragma once
#include "gpu/types.hpp"

namespace gpu {
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

template<class Visitor>
void DirectedGraph::for_each_successor(TaskId from, Visitor visit) const {
    if (from >= heads_.size()) throw std::out_of_range("vertex out of range");
    for (std::size_t edge = heads_[from]; edge != no_edge; edge = edges_[edge].next)
        visit(edges_[edge].to);
}
} // namespace gpu
