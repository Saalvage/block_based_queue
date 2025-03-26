#ifndef BENCHMARK_DEFAULT_HPP_INCLUDED
#define BENCHMARK_DEFAULT_HPP_INCLUDED

#include "benchmark_base.hpp"

#include <vector>
#include <barrier>
#include <numeric>

struct benchmark_default : benchmark_base<> {
	std::vector<std::size_t> results;
	std::size_t test_time_seconds;

	benchmark_default(const benchmark_info& info) : results(info.num_threads), test_time_seconds(info.test_time_seconds) {}

	template <typename T>
	void per_thread(int thread_index, typename T::handle& handle, std::barrier<>& a, std::atomic_bool& over) {
		std::size_t its = 0;
		a.arrive_and_wait();
		while (!over) {
			handle.push(5);
			handle.pop();
			its++;
		}
		results[thread_index] = its;
	}

	template <typename T>
	void output(T& stream) {
		stream << std::reduce(results.begin(), results.end()) / test_time_seconds;
	}
};

#endif // BENCHMARK_DEFAULT_HPP_INCLUDED
