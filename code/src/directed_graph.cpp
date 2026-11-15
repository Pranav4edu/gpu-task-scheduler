#include "gpu/ds/directed_graph.hpp"
#include <algorithm>
#include <stdexcept>

namespace gpu {
DirectedGraph DirectedGraph::build(std::size_t vertices, std::vector<EdgePair> edges) {
    for (const EdgePair& edge : edges) {
        if (edge.from >= vertices || edge.to >= vertices)
            throw std::invalid_argument("edge endpoint out of range");
        if (edge.from == edge.to)
            throw std::invalid_argument("self-edges are not allowed");
    }

    std::sort(edges.begin(), edges.end(), [](const EdgePair& left, const EdgePair& right) {
        if (left.from != right.from) return left.from < right.from;
        return left.to < right.to;
    });
    for (std::size_t i = 1; i < edges.size(); ++i) {
        if (edges[i - 1].from == edges[i].from && edges[i - 1].to == edges[i].to)
            throw std::invalid_argument("duplicate edge");
    }

    DirectedGraph graph;
    graph.heads_.assign(vertices, no_edge);
    graph.indegrees_.assign(vertices, 0);
    graph.edges_.reserve(edges.size());
    for (const EdgePair& edge : edges) {
        graph.edges_.push_back({edge.to, graph.heads_[edge.from]});
        graph.heads_[edge.from] = graph.edges_.size() - 1;
        ++graph.indegrees_[edge.to];
    }
    return graph;
}

std::size_t DirectedGraph::vertex_count() const noexcept {
    return heads_.size();
}

std::size_t DirectedGraph::edge_count() const noexcept {
    return edges_.size();
}

const std::vector<std::size_t>& DirectedGraph::indegrees() const noexcept {
    return indegrees_;
}
} // namespace gpu
