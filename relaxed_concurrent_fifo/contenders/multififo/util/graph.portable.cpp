#include "graph.hpp"

#include <fstream>
#include <numeric>

Graph::Graph(std::filesystem::path const& graph_file) {
    std::ifstream file{graph_file};

    char c;
    std::string skip;

    std::size_t num_nodes = 0;
    std::size_t num_edges = 0;
    file >> c;

    while (c == 'c') {
        file.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        file >> c;
    }

    if (c != 'p') {
        throw std::runtime_error("Invalid graph format");
    }

    file >> skip;
    file >> num_nodes;
    file >> num_edges;

    file >> c;
    while (c == 'c') {
        file.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        file >> c;
    }
    file.unget();

    std::vector<std::pair<std::size_t, Edge>> edge_list;
    nodes.resize(num_nodes + 1);
    edge_list.reserve(num_edges);

    for (std::size_t i = 0; i != num_edges; ++i) {
        std::pair<std::size_t, Edge> edge;
        file >> c;
        if (c != 'a') {
            throw std::runtime_error("Invalid edge format");
        }
        file >> edge.first;
        --edge.first;
        if (edge.first >= num_nodes) {
            throw std::runtime_error("Invalid edge source");
        }
        ++nodes[edge.first + 1];
        file >> edge.second.target;
        --edge.second.target;
        if (edge.second.target >= num_nodes) {
            throw std::runtime_error("Invalid edge target");
        }
        file >> edge.second.weight;
        edge_list.push_back(edge);
    }

    std::exclusive_scan(nodes.begin() + 1, nodes.end(), nodes.begin() + 1, static_cast<std::size_t>(0));
    edges.resize(edge_list.size());
    for (auto& edge : edge_list) {
        edges[nodes[edge.first + 1]++] = edge.second;
    }
}
