#ifndef BENCHMARK_GRAPH_HPP_INCLUDED
#define BENCHMARK_GRAPH_HPP_INCLUDED

#include "benchmark_base.hpp"

#include <optional>

#include <fstream>

#include "../contenders/multififo/ring_buffer.hpp"
#include "../contenders/multififo/util/graph.hpp"
#include "../contenders/multififo/util/termination_detection.hpp"

static constexpr std::size_t make_po2(std::size_t size) {
    std::size_t ret = 1;
    while (size > ret) {
        ret *= 2;
    }
    return ret;
}

std::tuple<std::uint64_t, std::uint32_t, std::vector<std::uint32_t>> sequential_bfs(const Graph& graph) {
    multififo::RingBuffer<std::uint32_t> nodes(make_po2(graph.num_nodes()));
    // max() - 1 so it holds that for all nodes par_dist = seq_dist + 1.
    std::vector<std::uint32_t> distances(graph.num_nodes(), std::numeric_limits<std::uint32_t>::max() - 1);
    distances[0] = 0;
    std::cout << distances.size() << "???" << std::endl;
    std::cout << distances[1] << "???" << std::endl;

    nodes.push(static_cast<std::uint32_t>(graph.nodes[0]));

	std::cout << nodes.size() << "???" << std::endl;

    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    while (!nodes.empty()) {
        auto node_id = nodes.top();
        nodes.pop();
        auto d = distances[node_id] + 1;
        for (auto i = graph.nodes[node_id]; i < graph.nodes[node_id + 1]; ++i) {
            auto node_id = graph.edges[i].target;
            if (distances[node_id] == std::numeric_limits<std::uint32_t>::max()) {
                distances[node_id] = d;
                nodes.push(static_cast<std::uint32_t>(node_id));
            }
        }
    }
    auto end = std::chrono::steady_clock::now().time_since_epoch().count();
    std::ofstream a{ "seq.csv" };
    for (auto i : distances) {
        a << i << '\n';
    }
    return std::tuple(end - now, *std::max_element(distances.begin(), distances.end()), distances);
}

struct benchmark_info_graph : public benchmark_info {
    const Graph& graph;
    const std::vector<std::uint32_t>& distances;
};

struct benchmark_bfs : benchmark_timed<> {
    struct Counter {
        long long pushed_nodes{ 0 };
        long long ignored_nodes{ 0 };
        long long processed_nodes{ 0 };
        bool err{ false };
    };

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winterference-size"
#endif // __GNUC__
    struct alignas(std::hardware_destructive_interference_size) AtomicDistance {
        std::atomic<std::uint32_t> value{ std::numeric_limits<std::uint32_t>::max() };
    };
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif // __GNUC__

    const benchmark_info_graph& info;
    const Graph& graph;
    std::vector<AtomicDistance> distances;
    termination_detection::TerminationDetection termination_detection;
    std::vector<Counter> counters;

    benchmark_bfs(const benchmark_info& info_base) :
            info(reinterpret_cast<const benchmark_info_graph&>(info_base)),
            graph(info.graph),
            distances(graph.num_nodes()),
            termination_detection(info.num_threads),
            counters(info.num_threads) { }

    template <typename FIFO>
    void process_node(std::uint64_t node, typename FIFO::handle& handle, Counter& counter) {
        std::uint64_t node_id = node & 0xffff'ffff;
        std::uint32_t node_dist = node >> 32;
        auto current_distance = distances[node_id].value.load(std::memory_order_relaxed);
        if (node_dist > current_distance) {
            ++counter.ignored_nodes;
            return;
        }
        for (auto i = graph.nodes[node_id]; i < graph.nodes[node_id + 1]; ++i) {
            auto target = graph.edges[i].target;
            auto d = node_dist + 1;
            auto old_d = distances[target].value.load(std::memory_order_relaxed);
            while (d < old_d) {
                if (distances[target].value.compare_exchange_weak(old_d, d, std::memory_order_relaxed)) {
                    if (!handle.push((static_cast<std::uint64_t>(d) << 32) | target)) {
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
            distances[0].value = 1;
            handle.push(1ull << 32);
            ++counter.pushed_nodes;
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

    template <typename T>
    void output(T& stream) {
        std::ofstream a{ "par.csv" };
        for (auto& i : distances) {
            a << i.value << '\n';
        }

        for (std::size_t i = 0; i < info.distances.size(); i++) {
            if (distances[i].value != info.distances[i] + 1) {
                std::cout << "Node " << i << " has distance " << distances[i].value << ", should be " << info.distances[i] + 1 << std::endl;
                stream << "ERR_DIST_WRONG";
                return;
            }
        }

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

        auto longest_distance =
        std::max_element(distances.begin(), distances.end(), [](auto const& a, auto const& b) {
            auto a_val = a.value.load(std::memory_order_relaxed);
            auto b_val = b.value.load(std::memory_order_relaxed);
            if (b_val == std::numeric_limits<long long>::max()) {
                return false;
            }
            if (a_val == std::numeric_limits<long long>::max()) {
                return true;
            }
            return a_val < b_val;
        })->value.load();

        stream << time_nanos << ',' << longest_distance << ',' << total_counts.pushed_nodes << ',' << total_counts.processed_nodes << ',' << total_counts.ignored_nodes;
    }
};

#endif // BENCHMARK_GRAPH_HPP_INCLUDED
