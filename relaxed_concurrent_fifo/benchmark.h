#ifndef BENCHMARK_H_INCLUDED
#define BENCHMARK_H_INCLUDED

#include "fifo.h"

#include <cmath>
#include <vector>
#include <barrier>
#include <thread>
#include <atomic>
#include <chrono>
#include <numeric>
#include <future>
#include <iostream>
#include <execution>

#ifdef _POSIX_VERSION
#include <pthread.h>
#endif // _POSIX_VERSION

#include "replay_tree.hpp"

#include "block_based_queue.h"
#include "cylinder_fifo.hpp"
#include "contenders/scal/scal_wrapper.h"
#include "contenders/multififo/multififo.hpp"
#include "contenders/multififo/util/termination_detection.hpp"
#include "contenders/multififo/util/graph.hpp"

#ifdef __GNUC__

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wvla"

#include "contenders/LCRQ/LCRQueue.hpp"

template <typename T>
using LCRQWrapped = LCRQueue<T>;

#pragma GCC diagnostic pop
#endif // __GNUC__

// There are two components to a benchmark:
// The benchmark itself which dictates what each thread does and what exactly is being measured;
// and the benchmark provider, which effectively provides a concrete queue implementation to the benchmark.

struct benchmark_info {
	int num_threads;
	int test_time_seconds;
};

struct benchmark_info_prodcon : public benchmark_info {
	int producers;
	int consumers;
};

struct benchmark_info_graph : public benchmark_info {
	Graph* graph;
};

template <bool HAS_TIMEOUT_T = true, bool RECORD_TIME_T = false, bool PREFILL_IN_ORDER_T = false, std::size_t SIZE_T = 0>
struct benchmark_base {
	static constexpr bool HAS_TIMEOUT = HAS_TIMEOUT_T;
	static constexpr bool RECORD_TIME = RECORD_TIME_T;
	static constexpr bool PREFILL_IN_ORDER = PREFILL_IN_ORDER_T;

	// Make sure we have enough space for at least 4 (not 3 so it's PO2) windows where each window supports HW threads with HW blocks each with HW cells each.
	static const inline std::size_t SIZE = SIZE_T != 0 ? SIZE_T : 4 * std::thread::hardware_concurrency() * std::thread::hardware_concurrency() * std::thread::hardware_concurrency();
};

template <bool PREFILL_IN_ORDER = false, bool HAS_TIMEOUT = false, std::size_t SIZE = 0>
struct benchmark_timed : benchmark_base<HAS_TIMEOUT, true, PREFILL_IN_ORDER, SIZE> {
	std::uint64_t time_nanos;
};

struct benchmark_default : benchmark_base<> {
	std::vector<std::size_t> results;
	std::size_t test_time_seconds;

	benchmark_default(const benchmark_info& info) : results(info.num_threads), test_time_seconds(info.test_time_seconds) { }

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

struct benchmark_fill : benchmark_timed<false, true, 1 << 28> {
	std::vector<std::uint64_t> results;

	benchmark_fill(const benchmark_info& info) : results(info.num_threads) { }

	template <typename T>
	void per_thread(int thread_index, typename T::handle& handle, std::barrier<>& a, std::atomic_bool& over) {
		a.arrive_and_wait();
		while (handle.push(thread_index + 1) && !over) {
			results[thread_index]++;
		}
	}

	template <typename T>
	void output(T& stream) {
		stream << static_cast<double>(std::reduce(results.begin(), results.end())) / time_nanos;
	}
};

struct benchmark_empty : benchmark_fill {
	template <typename T>
	void per_thread(int thread_index, typename T::handle& handle, std::barrier<>& a, std::atomic_bool& over) {
		a.arrive_and_wait();
		while (handle.pop().has_value() && !over) {
			results[thread_index]++;
		}
	}
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

	Graph* graph;
	std::vector<AtomicDistance> distances;
	termination_detection::TerminationDetection termination_detection;
	std::vector<Counter> counters;

	benchmark_bfs(const benchmark_info& info) :
			graph(reinterpret_cast<const benchmark_info_graph&>(info).graph),
			distances(graph->num_nodes()),
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
		for (auto i = graph->nodes[node_id]; i < graph->nodes[node_id + 1]; ++i) {
			auto target = graph->edges[i].target;
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
		auto total_counts =
			std::accumulate(counters.begin(), counters.end(), Counter{}, [](auto sum, auto const& counter) {
			sum.pushed_nodes += counter.pushed_nodes;
			sum.processed_nodes += counter.processed_nodes;
			sum.ignored_nodes += counter.ignored_nodes;
			sum.err |= counter.err;
			return sum;
		});

		if (total_counts.err) {
			stream << "ERR";
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

template <typename BENCHMARK>
class benchmark_provider {
public:
	virtual ~benchmark_provider() = default;
	virtual BENCHMARK test(const benchmark_info& info, double prefill_amount) const = 0;
	virtual const std::string& get_name() const = 0;

protected:
	template <fifo FIFO>
	static BENCHMARK test_single(FIFO& fifo, const benchmark_info& info, double prefill_amount) {
		std::vector<typename FIFO::handle> handles;
		handles.reserve(info.num_threads);
		for (int i = 0; i < info.num_threads; i++) {
			handles.push_back(fifo.get_handle());
		}
		// We prefill from all handles since this may improve performance for certain implementations.
		// TODO: Is there the possibility of the performance improvement coming from actually being filled concurrently?
		for (std::size_t i = 0; i < prefill_amount * BENCHMARK::SIZE; i++) {
			// If PREFILL_IN_ORDER is set we sequentially fill the queue from a single handle.
			if (!handles[BENCHMARK::PREFILL_IN_ORDER ? 0 : (i % info.num_threads)].push(i + 1)) {
				break;
			}
		}

		std::barrier a{info.num_threads + 1};
		std::atomic_bool over = false;
		std::vector<std::jthread> threads(info.num_threads);
		BENCHMARK b{info};

		for (int i = 0; i < info.num_threads; i++) {
			threads[i] = std::jthread([&, i]() {
#ifdef _POSIX_VERSION
				cpu_set_t cpu_set;
				CPU_ZERO(&cpu_set);
				CPU_SET(i, &cpu_set);
				if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpu_set)) {
					throw std::runtime_error("Failed to set thread affinity!");
				}
#endif // _POSIX_VERSION
				// Make sure handle is on the thread's stack.
				typename FIFO::handle handle = std::move(handles[i]);
				if constexpr (BENCHMARK::HAS_TIMEOUT) {
					b.template per_thread<FIFO>(i, handle, a, over);
				} else {
					b.template per_thread<FIFO>(i, handle, a);
				}
			});
		}

		// We signal, then start taking the time because some threads might not have arrived at the signal.
		a.arrive_and_wait();
		auto start = std::chrono::steady_clock::now();
		auto joined = std::async([&]() {
			for (auto& thread : threads) {
				thread.join();
			}
		});
		if constexpr (BENCHMARK::HAS_TIMEOUT) {
			std::this_thread::sleep_until(start + std::chrono::seconds(info.test_time_seconds));
			over = true;

			if (joined.wait_for(std::chrono::seconds(10)) == std::future_status::timeout) {
				std::cout << "Threads did not complete within timeout, assuming deadlock!" << std::endl;
				std::exit(1);
			}
		} else {
			joined.wait();
		}
		if constexpr (BENCHMARK::RECORD_TIME) {
			b.time_nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
		}

		return b;
	}
};

template <fifo FIFO, typename BENCHMARK, typename... Args>
class benchmark_provider_generic : public benchmark_provider<BENCHMARK> {
public:
	benchmark_provider_generic(std::string_view name, Args... args) : name(std::vformat(name, std::make_format_args(args...))), args(args...) {}

	const std::string& get_name() const override {
		return name;
	}

	BENCHMARK test(const benchmark_info& info, double prefill_amount) const override {
		FIFO fifo = std::apply([&](Args... args) { return FIFO{ info.num_threads, BENCHMARK::SIZE, args... }; }, args);
		return benchmark_provider<BENCHMARK>::template test_single<FIFO>(fifo, info, prefill_amount);
	}

private:
	std::string name;
	std::tuple<Args...> args;
};

template <typename BENCHMARK>
using benchmark_provider_ws_kfifo = benchmark_provider_generic<ws_k_fifo<std::uint64_t>, BENCHMARK, std::size_t>;

template <typename BENCHMARK>
using benchmark_provider_ss_kfifo = benchmark_provider_generic<ss_k_fifo<std::uint64_t>, BENCHMARK, std::size_t>;

template <typename BENCHMARK>
using benchmark_provider_multififo = benchmark_provider_generic<multififo::MultiFifo<std::uint64_t>, BENCHMARK, int, int>;

template <typename BENCHMARK>
using benchmark_provider_cylinder = benchmark_provider_generic<cylinder_fifo<std::uint64_t>, BENCHMARK, int, int>;

template <typename BENCHMARK, std::size_t BLOCK_MULTIPLIER, std::size_t CELLS_PER_BLOCK, typename BITSET_TYPE = uint8_t>
class benchmark_provider_relaxed : public benchmark_provider<BENCHMARK> {
	public:
		benchmark_provider_relaxed(std::string name) : name(std::move(name)) { }

		const std::string& get_name() const override {
			return name;
		}

		BENCHMARK test(const benchmark_info& info, double prefill_amount) const override {
			switch (info.num_threads) {
			case 1: return test_helper<bbq_min_block_count<std::size_t, 1 * BLOCK_MULTIPLIER, CELLS_PER_BLOCK, BITSET_TYPE>>(info, prefill_amount);
			case 2: return test_helper<bbq_min_block_count<std::size_t, 2 * BLOCK_MULTIPLIER, CELLS_PER_BLOCK, BITSET_TYPE>>(info, prefill_amount);
			case 4: return test_helper<bbq_min_block_count<std::size_t, 4 * BLOCK_MULTIPLIER, CELLS_PER_BLOCK, BITSET_TYPE>>(info, prefill_amount);
			case 8: return test_helper<bbq_min_block_count<std::size_t, 8 * BLOCK_MULTIPLIER, CELLS_PER_BLOCK, BITSET_TYPE>>(info, prefill_amount);
			case 16: return test_helper<bbq_min_block_count<std::size_t, 16 * BLOCK_MULTIPLIER, CELLS_PER_BLOCK, BITSET_TYPE>>(info, prefill_amount);
			case 32: return test_helper<bbq_min_block_count<std::size_t, 32 * BLOCK_MULTIPLIER, CELLS_PER_BLOCK, BITSET_TYPE>>(info, prefill_amount);
			case 64: return test_helper<bbq_min_block_count<std::size_t, 64 * BLOCK_MULTIPLIER, CELLS_PER_BLOCK, BITSET_TYPE>>(info, prefill_amount);
			case 128: return test_helper<bbq_min_block_count<std::size_t, 128 * BLOCK_MULTIPLIER, CELLS_PER_BLOCK, BITSET_TYPE>>(info, prefill_amount);
			case 256: return test_helper<bbq_min_block_count<std::size_t, 256 * BLOCK_MULTIPLIER, CELLS_PER_BLOCK, BITSET_TYPE>>(info, prefill_amount);
			default: throw std::runtime_error("Unsupported thread count!");
			}
		}

	private:
		std::string name;

		template <typename FIFO>
		static BENCHMARK test_helper(const benchmark_info& info, double prefill_amount) {
			FIFO fifo{ info.num_threads, BENCHMARK::SIZE };
			return benchmark_provider<BENCHMARK>::template test_single(fifo, info, prefill_amount);
		}
};

#endif // BENCHMARK_H_INCLUDED
