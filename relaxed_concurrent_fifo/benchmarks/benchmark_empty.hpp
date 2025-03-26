#ifndef BENCHMARK_EMPTY_HPP_INCLUDED
#define BENCHMARK_EMPTY_HPP_INCLUDED

#include "benchmark_fill.hpp"

struct benchmark_empty : benchmark_fill {
	template <typename T>
	void per_thread(int thread_index, typename T::handle& handle, std::barrier<>& a, std::atomic_bool& over) {
		a.arrive_and_wait();
		while (handle.pop().has_value() && !over) {
			results[thread_index]++;
		}
	}
};

#endif // BENCHMARK_EMPTY_HPP_INCLUDED
