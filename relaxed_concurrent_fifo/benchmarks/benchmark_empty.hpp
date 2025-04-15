#ifndef BENCHMARK_EMPTY_HPP_INCLUDED
#define BENCHMARK_EMPTY_HPP_INCLUDED

#include "benchmark_fill.hpp"

struct benchmark_empty : benchmark_fill {
    template <typename T>
    void per_thread(int thread_index, typename T::handle& handle, std::barrier<>& a, std::atomic_bool& over) {
        a.arrive_and_wait();
        std::size_t its = 0;
        while (handle.pop().has_value() && !over) {
            its++;
        }
        results[thread_index] = its;
    }
};

#endif // BENCHMARK_EMPTY_HPP_INCLUDED
