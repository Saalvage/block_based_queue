#ifndef BENCHMARK_GRAPH_MULTISTART_HPP_INCLUDED
#define BENCHMARK_GRAPH_MULTISTART_HPP_INCLUDED

#include "benchmark_base.hpp"

#include <optional>
#include <iostream>

#include "benchmark_graph.hpp"

#include "../utility.h"
#include "../contenders/multififo/ring_buffer.hpp"
#include "../contenders/multififo/util/graph.hpp"
#include "../contenders/multififo/util/termination_detection.hpp"

struct benchmark_bfs_multistart : benchmark_timed<> {
    const benchmark_info_graph& info;
    const Graph& graph;
    std::vector<std::vector<AtomicDistance>> distances;
    termination_detection::TerminationDetection termination_detection;
    std::vector<Counter> counters;

    benchmark_bfs_multistart(const benchmark_info& info_base) :
            info(reinterpret_cast<const benchmark_info_graph&>(info_base)),
            graph(info.graph),
            distances(info.num_threads),
            termination_detection(info.num_threads),
            counters(info.num_threads) {
        fifo_size = std::bit_ceil(graph.nodes.size() * std::bit_width(static_cast<std::uint64_t>(info.num_threads)));
        if (info.num_threads > 255) {
            throw std::runtime_error("More bits must be allocated to the arr index to allow for more than 255 threads!");
        }
    }

    template <typename FIFO>
    void process_node(std::uint64_t node, typename FIFO::handle& handle, Counter& counter) {
        std::uint64_t node_id = node & 0xffff'ffff;
        std::uint32_t node_dist = (node >> 32) & 0xff'ffff;
        std::uint64_t idx = node >> 56;
        auto& vec = distances[idx];
        auto current_distance = vec[node_id].value.load(std::memory_order_relaxed);
        if (node_dist > current_distance) {
            ++counter.ignored_nodes;
            return;
        }
        for (auto i = graph.nodes[node_id]; i < graph.nodes[node_id + 1]; ++i) {
            auto target = graph.edges[i].target;
            auto d = node_dist + 1;
            auto old_d = vec[target].value.load(std::memory_order_relaxed);
            while (d < old_d) {
                if (vec[target].value.compare_exchange_weak(old_d, d, std::memory_order_relaxed)) {
                    if (!handle.push((static_cast<std::uint64_t>(d) << 32) | target | (idx << 56))) {
                        counter.err = true;
                    }
                    ++counter.pushed_nodes;
                    break;
                }
            }
        }
        ++counter.processed_nodes;
    }

    template <typename FIFO>
    void per_thread(int thread_index, typename FIFO::handle& handle, std::barrier<>& a) {
        Counter counter;
        if (thread_index == 0) {
            // We can't push 0 to the queues!
            for (std::uint64_t i = 0; i < distances.size(); i++) {
                auto& vec = distances[i];
                vec = std::vector<AtomicDistance>(graph.num_nodes());
                vec[0].value = 1;
                handle.push((1ull << 32) | (i << 56));
                ++counter.pushed_nodes;
            }
        }
        a.arrive_and_wait();
        std::optional<std::uint64_t> node;
        while (termination_detection.repeat([&]() {
                node = handle.pop();
                return node.has_value();
            })) {
            process_node<FIFO>(*node, handle, counter);
        }
        counters[thread_index] = counter;
    }

    static constexpr const char* header = "time_nanoseconds,pushed_nodes,processed_nodes,ignored_nodes";

    template <typename T>
    void output(T& stream) {
        auto total_counts =
            std::accumulate(counters.begin(), counters.end(), Counter{}, [](auto sum, auto const& counter) {
            sum.pushed_nodes += counter.pushed_nodes;
            sum.processed_nodes += counter.processed_nodes;
            sum.ignored_nodes += counter.ignored_nodes;
            sum.err |= counter.err;
            return sum;
        });

        if (total_counts.err) {
            std::cout << "Push failed!" << std::endl;
            stream << "ERR_PUSH_FAIL";
            return;
        }

        auto lost_nodes = total_counts.pushed_nodes - (total_counts.processed_nodes + total_counts.ignored_nodes);
        if (lost_nodes != 0) {
            std::cout << lost_nodes << " lost nodes!" << std::endl;
            stream << "ERR_LOST_NODE";
            return;
        }

        for (const auto& dist : distances) {
            for (std::size_t i = 0; i < info.distances.size(); i++) {
                if (dist[i].value != info.distances[i]) {
                    std::cout << "Node " << i << " has distance " << dist[i].value << ", should be " << info.distances[i] << std::endl;
                    stream << "ERR_DIST_WRONG";
                    return;
                }
            }
        }

        stream << time_nanos << ',' << total_counts.pushed_nodes << ',' << total_counts.processed_nodes << ',' << total_counts.ignored_nodes;
    }
};

#endif // BENCHMARK_GRAPH_MULTISTART_HPP_INCLUDED
