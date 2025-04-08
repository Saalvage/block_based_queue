#pragma once

#include <filesystem>
#include <vector>

struct Graph {
    using weight_type = long long;
    struct Edge {
        std::size_t target;
        weight_type weight;
    };
    std::vector<std::size_t> nodes;
    std::vector<Edge> edges;

    Graph() = default;
    Graph(std::filesystem::path const& graph_file);

    [[nodiscard]] std::size_t num_nodes() const noexcept {
        return nodes.empty() ? 0 : nodes.size() - 1;
    }

    [[nodiscard]] std::size_t num_edges() const noexcept {
        return edges.size();
    }
};
