#ifndef BENCHMARK_BASE_HPP_INCLUDED
#define BENCHMARK_BASE_HPP_INCLUDED

#include <cstddef>
#include <cstdint>
#include <thread>

// There are two components to a benchmark:
// The benchmark itself which dictates what each thread does and what exactly is being measured;
// and the benchmark provider, which effectively provides a concrete queue implementation to the benchmark.

struct benchmark_info {
    int num_threads;
    int test_time_seconds;
};

template <bool HAS_TIMEOUT_T = true, bool RECORD_TIME_T = false, bool PREFILL_IN_ORDER_T = false>
struct benchmark_base {
    static constexpr bool HAS_TIMEOUT = HAS_TIMEOUT_T;
    static constexpr bool RECORD_TIME = RECORD_TIME_T;
    static constexpr bool PREFILL_IN_ORDER = PREFILL_IN_ORDER_T;

    std::size_t fifo_size = static_cast<std::size_t>(4) * std::thread::hardware_concurrency() * std::thread::hardware_concurrency() * std::thread::hardware_concurrency();
};

template <bool PREFILL_IN_ORDER = false, bool HAS_TIMEOUT = false>
struct benchmark_timed : benchmark_base<HAS_TIMEOUT, true, PREFILL_IN_ORDER> {
    std::uint64_t time_nanos;
};

#endif // BENCHMARK_BASE_HPP_INCLUDED
