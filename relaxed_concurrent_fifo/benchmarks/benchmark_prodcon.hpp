#ifndef BENCHMARK_PRODCON_HPP_INCLUDED
#define BENCHMARK_PRODCON_HPP_INCLUDED

#include "benchmark_base.hpp"

#include <iostream>

struct benchmark_info_prodcon : public benchmark_info {
    int producers;
    int consumers;
};

struct benchmark_prodcon : benchmark_default {
    int thread_switch;

    benchmark_prodcon(const benchmark_info& info) : benchmark_default(info) {
        const benchmark_info_prodcon& info_prodcon = reinterpret_cast<const benchmark_info_prodcon&>(info);
        thread_switch = info.num_threads * info_prodcon.producers / (info_prodcon.consumers + info_prodcon.producers);
        std::cout << thread_switch << std::endl;
    }

    template <typename T>
    void per_thread(int thread_index, typename T::handle& handle, std::barrier<>& a, std::atomic_bool& over) {
        std::size_t its = 0;
        a.arrive_and_wait();
        while (!over) {
            if (thread_index < thread_switch) {
                if (handle.push(5)) {
                    its++;
                }
            } else {
                if (handle.pop().has_value()) {
                    its++;
                }
            }
        }
        results[thread_index] = its;
    }

    template <typename T>
    void output(T& stream) {
        stream << std::min(
            std::reduce(results.begin(), results.begin() + thread_switch),
            std::reduce(results.begin() + thread_switch, results.end()))
        / test_time_seconds;
    }
};

#endif // BENCHMARK_PRODCON_HPP_INCLUDED