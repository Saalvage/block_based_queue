#include "graph.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <charconv>
#include <numeric>

Graph::Graph(std::filesystem::path const& graph_file) {
    int fd = open(graph_file.c_str(), O_RDONLY);
    if (fd == -1) {
        throw std::runtime_error{"Could not open file"};
    }

    struct stat sb {};

    if (fstat(fd, &sb) == -1) {
        close(fd);
        throw std::runtime_error{"Could not get file size"};
    }

    auto* addr = mmap(nullptr, static_cast<std::size_t>(sb.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
    if (addr == MAP_FAILED) {
        close(fd);
        throw std::runtime_error{"mmap failed"};
    }
    close(fd);
    madvise(addr, static_cast<std::size_t>(sb.st_size), MADV_SEQUENTIAL);

    const auto* it = static_cast<char const*>(addr);
    const auto* end = it + sb.st_size;
    while (*it == 'c') {
        ++it;
        while (*it++ != '\n') {
        }
    }
    std::size_t num_nodes = 0;
    std::size_t num_edges = 0;
    if (*it == 'p') {
        while (std::isspace(*++it));
        if (!std::isdigit(*it)) {
            while (!std::isspace(*++it));
            while (std::isspace(*++it));
        }
        auto res = std::from_chars(it, end, num_nodes);
        if (res.ec != std::errc{}) {
            throw std::runtime_error("Failed to parse number of nodes");
        }
        it = res.ptr;
        while (std::isspace(*++it) != 0) {
        }
        res = std::from_chars(it, end, num_edges);
        if (res.ec != std::errc{}) {
            throw std::runtime_error("Failed to parse number of edges");
        }
        it = res.ptr;
        while (std::isspace(*it) != 0) {
            ++it;
        }
    }
    while (*it == 'c') {
        ++it;
        while (*it++ != '\n') {
        }
    }
    std::vector<std::pair<std::size_t, Graph::Edge>> edge_list;
    nodes.resize(num_nodes + 1);
    edge_list.reserve(num_edges);
    for (std::size_t i = 0; i != num_edges; ++i) {
        std::pair<std::size_t, Graph::Edge> edge;
        while (std::isspace(*it) != 0) {
            ++it;
        }
        if ((*it != 'a' && *it != 'e') || !std::isspace(*(it + 1))) {
            throw std::runtime_error("Invalid edge format");
        }
        it += 2;
        auto res = std::from_chars(it, end, edge.first);
        if (res.ec != std::errc{}) {
            throw std::runtime_error("Failed to parse edge source");
        }
        --edge.first;
        if (edge.first >= num_nodes) {
            throw std::runtime_error("Invalid edge source");
        }
        ++nodes[edge.first + 1];
        it = res.ptr;
        while (std::isspace(*it)) {
            ++it;
        }
        res = std::from_chars(it, end, edge.second.target);
        if (res.ec != std::errc{}) {
            throw std::runtime_error("Failed to parse edge target");
        }
        --edge.second.target;
        if (edge.second.target >= num_nodes) {
            throw std::runtime_error("Invalid edge target");
        }
        it = res.ptr;
        while (std::isspace(*it)) {
            ++it;
        }
        res = std::from_chars(it, end, edge.second.weight);
        it = res.ptr;
        edge_list.push_back(edge);
    }
    munmap(addr, static_cast<std::size_t>(sb.st_size));
    std::exclusive_scan(nodes.begin() + 1, nodes.end(), nodes.begin() + 1, static_cast<std::size_t>(0));
    edges.resize(edge_list.size());
    for (auto& edge : edge_list) {
        edges[nodes[edge.first + 1]++] = edge.second;
    }
}
