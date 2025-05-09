#ifndef BENCHMARK_FILL_HPP_INCLUDED
#define BENCHMARK_FILL_HPP_INCLUDED

#include "benchmark_base.hpp"

struct benchmark_fill : benchmark_timed<false, true> {
    std::vector<std::uint64_t> results;

    benchmark_fill(const benchmark_info& info) : results(info.num_threads) {
        fifo_size = 1 << 28;
    }

    template <typename T>
    void per_thread(int thread_index, typename T::handle& handle, std::barrier<>& a, std::atomic_bool& over) {
        a.arrive_and_wait();
        std::size_t its = 0;
        while (handle.push(thread_index + 1) && !over) {
            its++;
        }
        results[thread_index] = its;
    }

    static constexpr const char* header = "operations_per_nanosecond";

    template <typename T>
    void output(T& stream) {
        stream << static_cast<double>(std::reduce(results.begin(), results.end())) / time_nanos;
    }
};

#endif // BENCHMARK_FILL_HPP_INCLUDED
