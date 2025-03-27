#ifndef BENCHMARK_QUALITY_HPP_INCLUDED
#define BENCHMARK_QUALITY_HPP_INCLUDED

#include <atomic>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <numeric>
#include <execution>

#include "benchmark_base.hpp"
#include "../replay_tree.hpp"

template <bool INCLUDE_DISTRIBUTION = false>
struct benchmark_quality : benchmark_base<false, false, true> {
private:
    std::atomic_uint64_t chunks_done = 0;

    std::vector<std::vector<std::tuple<std::uint64_t, std::uint64_t>>> results;

    struct pop_op {
        std::uint64_t pop_time;
        std::uint64_t push_time;
    };

    struct data_point {
        double avg;
        double std;
        std::uint64_t max;
        std::unordered_map<std::uint64_t, std::uint64_t> distribution;
    };

    static data_point analyze(const std::vector<std::uint64_t>& data) {
        double avg = std::reduce(std::execution::par_unseq, data.begin(), data.end()) / static_cast<double>(data.size());
        std::unordered_map<std::uint64_t, std::uint64_t> distribution;
        std::uint64_t max = 0;
        double std = std::accumulate(data.begin(), data.end(), 0., [&max, &distribution, avg](double std_it, std::uint64_t new_val) {
            max = std::max(max, new_val);
            if constexpr (INCLUDE_DISTRIBUTION) {
                distribution[new_val]++;
            }
            double diff = new_val - avg;
            return std_it + diff * diff;
        });
        std /= data.size();
        std = std::sqrt(std);
        return { avg, std, max, std::move(distribution) };
    }

public:
    static constexpr int CHUNK_SIZE  = 5'000;
    static constexpr int CHUNK_COUNT = 1'000;

    benchmark_quality(const benchmark_info& info) : results(info.num_threads) {
        // Double the amount of "expected" load for this thread.
        int size_per_thread = CHUNK_SIZE * CHUNK_COUNT / info.num_threads * 2;
        for (auto& vec : results) {
            vec.reserve(size_per_thread);
        }
    }

    benchmark_quality(const benchmark_quality& other) : results(other.results) { }
    benchmark_quality& operator=(const benchmark_quality& other) {
        results = other.results;
        return *this;
    }

    template <typename T>
    void per_thread(int thread_index, typename T::handle& handle, std::barrier<>& a) {
        a.arrive_and_wait();
        do {
            for (int i = 0; i < CHUNK_SIZE; i++) {
                handle.push(std::chrono::steady_clock::now().time_since_epoch().count());
                auto popped = handle.pop();
                assert(popped.has_value());
                results[thread_index].push_back({ popped.value(), std::chrono::steady_clock::now().time_since_epoch().count() });
            }
        } while (chunks_done.fetch_add(1) < CHUNK_COUNT);
    }

    template <typename T>
    void output(T& stream) {
        auto total_count = std::accumulate(results.begin(), results.end(), static_cast<std::size_t>(0), [](std::size_t size, const auto& v) { return size + v.size(); });
        std::vector<pop_op> pops;
        pops.reserve(total_count);
        std::vector<std::uint64_t> pushes;
        pushes.reserve(total_count);
        for (const auto& thread_result : results) {
            for (const auto& [pushed, popped] : thread_result) {
                pops.emplace_back(popped, pushed);
                pushes.emplace_back(pushed);
            }
        }
        std::sort(std::execution::par_unseq, pops.begin(), pops.end(), [](const auto& a, const auto& b) { return a.pop_time < b.pop_time; });
        std::sort(std::execution::par_unseq, pushes.begin(), pushes.end());

        std::vector<std::uint64_t> rank_errors;
        std::vector<std::uint64_t> delays;
        rank_errors.reserve(pushes.size());
        delays.reserve(pushes.size());
        struct id {
            static std::uint64_t const& get(std::uint64_t const& value) { return value; }
        };
        ReplayTree<std::uint64_t, std::uint64_t, id> replay_tree{};
        auto push_it = pushes.begin();
        for (auto const& pop : pops) {
            while (push_it != pushes.end() && *push_it <= pop.pop_time) {
                replay_tree.insert(*push_it);
                ++push_it;
            }
            assert(
                !replay_tree.empty());  // Assume push times are always smaller than
            // pop times, not guaranteed if timestamps
            // are taken in the wrong order
            auto [success, rank_error, delay] = replay_tree.erase_val(
                pop.push_time);   // Points to first element at the
            // same timestamp as the current pop
            assert(success);  // The element to pop has to be in the set
            rank_errors.emplace_back(rank_error);
            delays.emplace_back(delay);
        }

        auto [r_avg, r_std, r_max, r_dist] = analyze(rank_errors);
        stream << r_avg << ',' << r_std << ',' << r_max << ',';
        for (const auto& [x, y] : r_dist) {
            stream << x << ";" << y << "|";
        }
        auto [d_avg, d_std, d_max, d_dist] = analyze(delays);
        stream << ',' << d_avg << ',' << d_std << ',' << d_max << ',';
        for (const auto& [x, y] : d_dist) {
            stream << x << ";" << y << "|";
        }
    }
};

#endif // BENCHMARK_QUALITY_HPP_INCLUDED
