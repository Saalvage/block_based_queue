﻿#include "benchmark.h"

#include "lock_fifo.h"
#include "block_based_queue.h"
#include "concurrent_fifo.h"

#include "contenders/LCRQ/wrapper.h"
#include "contenders/LCRQ/MichaelScottQueue.hpp"

#include <thread>
#include <functional>
#include <ranges>
#include <filesystem>
#include <fstream>
#include <unordered_set>
#include <iostream>

#include "thread_pool.h"

#ifdef __GNUC__
static constexpr size_t make_po2(size_t size) {
	size_t ret = 1;
	while (size > ret) {
		ret *= 2;
	}
	return ret;
}

static std::pair<uint64_t, uint32_t> sequential_bfs(const Graph& graph) {
	multififo::RingBuffer<uint32_t> nodes(make_po2(graph.num_nodes()));
	std::vector<uint32_t> distances(graph.num_nodes(), std::numeric_limits<uint32_t>::max());
	distances[0] = 0;

	nodes.push(graph.nodes[0]);

	auto now = std::chrono::steady_clock::now().time_since_epoch().count();
	while (!nodes.empty()) {
		auto node_id = nodes.top();
		nodes.pop();
		auto d = distances[node_id] + 1;
		for (auto i = graph.nodes[node_id]; i < graph.nodes[node_id + 1]; ++i) {
			auto node_id = graph.edges[i].target;
			if (distances[node_id] == std::numeric_limits<uint32_t>::max()) {
				distances[node_id] = d;
				nodes.push(node_id);
			}
		}
	}
	auto end = std::chrono::steady_clock::now().time_since_epoch().count();
	return std::pair(end - now, *std::max_element(distances.begin(), distances.end()));
}

static std::pair<uint64_t, uint32_t> sequential_bfs_padded(const Graph& graph) {
	multififo::RingBuffer<uint32_t> nodes(make_po2(graph.num_nodes()));
	std::vector<benchmark_bfs::AtomicDistance> distances(graph.num_nodes());
	distances[0].value = 0;

	nodes.push(graph.nodes[0]);

	auto now = std::chrono::steady_clock::now().time_since_epoch().count();
	while (!nodes.empty()) {
		auto node_id = nodes.top();
		nodes.pop();
		auto d = distances[node_id].value.load(std::memory_order_relaxed) + 1;
		for (auto i = graph.nodes[node_id]; i < graph.nodes[node_id + 1]; ++i) {
			auto node_id = graph.edges[i].target;
			if (distances[node_id].value.load(std::memory_order_relaxed) == std::numeric_limits<uint32_t>::max()) {
				distances[node_id].value.store(d, std::memory_order_relaxed);
				nodes.push(node_id);
			}
		}
	}
	auto end = std::chrono::steady_clock::now().time_since_epoch().count();
	return std::pair(end - now, std::max_element(distances.begin(), distances.end(), [](auto const& a, auto const& b) {
		auto a_val = a.value.load(std::memory_order_relaxed);
		auto b_val = b.value.load(std::memory_order_relaxed);
		if (b_val == std::numeric_limits<long long>::max()) {
			return false;
		}
		if (a_val == std::numeric_limits<long long>::max()) {
			return true;
		}
		return a_val < b_val;
		})->value.load());
}

static std::pair<uint64_t, uint32_t> sequential_bfs_multififo(const Graph& graph) {
	multififo::MultiFifo<uint32_t> nodes_(1, make_po2(graph.num_nodes()), 2, 2);
	auto nodes = nodes_.get_handle();
	std::vector<benchmark_bfs::AtomicDistance> distances(graph.num_nodes());
	distances[0].value = 0;

	std::cout << "HALLO " << graph.nodes[0] << std::endl;
	nodes.push(graph.nodes[0]);

	auto now = std::chrono::steady_clock::now().time_since_epoch().count();
	std::optional<uint64_t> node;
	while ((node = nodes.pop()).has_value()) {
		auto node_id = node.value();
		auto d = distances[node_id].value.load(std::memory_order_relaxed) + 1;
		for (auto i = graph.nodes[node_id]; i < graph.nodes[node_id + 1]; ++i) {
			auto node_id = graph.edges[i].target;
			if (distances[node_id].value.load(std::memory_order_relaxed) == std::numeric_limits<uint32_t>::max()) {
				distances[node_id].value.store(d, std::memory_order_relaxed);
				nodes.push(node_id);
			}
		}
	}
	auto end = std::chrono::steady_clock::now().time_since_epoch().count();
	return std::pair(end - now, std::max_element(distances.begin(), distances.end(), [](auto const& a, auto const& b) {
		auto a_val = a.value.load(std::memory_order_relaxed);
		auto b_val = b.value.load(std::memory_order_relaxed);
		if (b_val == std::numeric_limits<long long>::max()) {
			return false;
		}
		if (a_val == std::numeric_limits<long long>::max()) {
			return true;
		}
		return a_val < b_val;
		})->value.load());
}
#endif // __GNUC__

/*static constexpr int COUNT = 512;

template <template <typename, size_t> typename T>
void test_full_capacity() {
	T<int, COUNT> buf;
	for (int i : std::views::iota(0, COUNT)) {
		assert(buf.push(i));
	}
	for (int i : std::views::iota(0, COUNT)) {
		assert(buf.pop() == i);
	}
}

template <template <typename, size_t> typename T>
void test_single_element() {
	T<int, COUNT> buf;
	for (int i : std::views::iota(0, COUNT * 10)) {
		assert(buf.push(i));
		assert(buf.pop() == i);
	}
}

template <template <typename, size_t> typename T>
void test_empty_pop() {
	T<int, COUNT> buf;
	assert(!buf.pop().has_value());
	assert(buf.push(1));
	buf.pop();
	assert(!buf.pop().has_value());
	for (int i : std::views::iota(0, COUNT * 10)) {
		buf.push(i);
		buf.pop();
	}
	assert(!buf.pop().has_value());
}

template <template <typename, size_t> typename T>
void test_full_push() {
	T<int, 1> buf;
	buf.push(1);
	assert(!buf.push(1));
}

template <template <typename, size_t> typename T>
void test_all() {
	test_full_capacity<T>();
	test_single_element<T>();
	test_empty_pop<T>();
	test_full_push<T>();
}*/

template <size_t THREAD_COUNT, size_t BLOCK_MULTIPLIER>
void test_consistency(size_t fifo_size, size_t elements_per_thread, double prefill) {
	block_based_queue<uint64_t, THREAD_COUNT * BLOCK_MULTIPLIER> fifo{ THREAD_COUNT, fifo_size };
	auto handle = fifo.get_handle();

	size_t pre_push = static_cast<size_t>(fifo_size * prefill);
	std::unordered_multiset<uint64_t> test_ints;
	for (size_t index = 0; index < pre_push; index++) {
		auto i = index | (1ull << 63);
		handle.push(i);
		test_ints.emplace(i);
	}

	std::barrier a{ (ptrdiff_t)(THREAD_COUNT + 1) };
	std::vector<std::jthread> threads(THREAD_COUNT);
	std::vector<std::vector<uint64_t>> test(THREAD_COUNT);
	std::vector<std::vector<uint64_t>> popped(THREAD_COUNT);
	for (size_t i = 0; i < THREAD_COUNT; i++) {
		threads[i] = std::jthread([&, i]() {
			auto handle = fifo.get_handle();
			a.arrive_and_wait();
			for (uint64_t j = 0; j < elements_per_thread; j++) {
				auto val = (i << 32) | (j + 1);
				test[i].push_back(val);
				while (!handle.push(val)) {}
				std::optional<uint64_t> pop;
				do {
					pop = handle.pop();
				} while (!pop.has_value());
				popped[i].push_back(pop.value());
			}
		});
	}
	a.arrive_and_wait();
	for (auto& thread : threads) {
		thread.join();
	}

	std::unordered_multiset<uint64_t> popped_ints;
	for (size_t index = 0; index < pre_push; index++) {
		popped_ints.emplace(handle.pop().value());
	}

	for (size_t i = 0; i < THREAD_COUNT; i++) {
		for (auto i : popped[i]) {
			popped_ints.emplace(i);
		}
		for (auto i : test[i]) {
			test_ints.emplace(i);
		}
	}

	if (handle.pop().has_value()) {
		throw std::runtime_error("Invalid element left!");
	}

	if (popped_ints != test_ints) {
		throw std::runtime_error("Sets did not match!");
	}
}

template <size_t BITSET_SIZE = 128>
void test_continuous_bitset_claim() {
	auto gen = std::bind(std::uniform_int_distribution<>(0, 1), std::default_random_engine());
	while (true) {
		atomic_bitset<BITSET_SIZE> a;
		std::vector<bool> b(BITSET_SIZE);
		for (int i = 0; i < BITSET_SIZE; i++) {
			if (gen()) {
				a.set(i);
				b[i] = true;
			}
		}
		auto result = a.template claim_bit<true, true>();
		if (result != std::numeric_limits<size_t>::max() && (a[result] || !b[result])) {
			throw std::runtime_error("Incorrect!");
		}
	}
}

template <typename BENCHMARK, typename BENCHMARK_DATA_TYPE = benchmark_info, typename... Args>
void run_benchmark(thread_pool& pool, const std::string& test_name, const std::vector<std::unique_ptr<benchmark_provider<BENCHMARK>>>& instances, double prefill,
	const std::vector<int>& processor_counts, int test_iterations, int test_time_seconds, const Args&... args) {
	constexpr const char* format = "fifo-{}-{}-{:%FT%H-%M-%S}.csv";

	if (BENCHMARK::HAS_TIMEOUT) {
		std::cout << "Expected running time: ";
		auto running_time_seconds = test_iterations * test_time_seconds * processor_counts.size() * instances.size();
		if (running_time_seconds >= 60) {
			auto running_time_minutes = running_time_seconds / 60;
			running_time_seconds %= 60;
			if (running_time_minutes >= 60) {
				auto running_time_hours = running_time_minutes / 60;
				running_time_minutes %= 60;
				if (running_time_hours >= 24) {
					auto running_time_days = running_time_hours / 24;
					running_time_hours %= 24;
					std::cout << running_time_days << " days, ";
				}
				std::cout << running_time_hours << " hours, ";
			}
			std::cout << running_time_minutes << " minutes, ";
		}
		std::cout << running_time_seconds << " seconds" << std::endl;
	}

	std::ofstream file{ std::format(format, test_name, prefill, std::chrono::round<std::chrono::seconds>(std::chrono::file_clock::now())) };
	for (auto i : std::views::iota(0, test_iterations)) {
		std::cout << "Test run " << (i + 1) << " of " << test_iterations << std::endl;
		for (const auto& imp : instances) {
			std::cout << "Testing " << imp->get_name() << std::endl;
			for (auto threads : processor_counts) {
				std::cout << "With " << threads << " processors" << std::endl;
				file << imp->get_name() << "," << threads << ',';
				BENCHMARK_DATA_TYPE data{threads, test_time_seconds, args...};
				imp->test(pool, data, prefill).output(file);
				file << '\n';
			}
		}
	}
}

template <typename BENCHMARK>
void add_all_parameter_tuning(std::vector<std::unique_ptr<benchmark_provider<BENCHMARK>>>& instances) {
	instances.push_back(std::make_unique<benchmark_provider_relaxed<BENCHMARK, 1, 1>>("1,1,bbq"));
	instances.push_back(std::make_unique<benchmark_provider_relaxed<BENCHMARK, 1, 3>>("1,3,bbq"));
	instances.push_back(std::make_unique<benchmark_provider_relaxed<BENCHMARK, 1, 7>>("1,7,bbq"));
	instances.push_back(std::make_unique<benchmark_provider_relaxed<BENCHMARK, 1, 15>>("1,15,bbq"));
	instances.push_back(std::make_unique<benchmark_provider_relaxed<BENCHMARK, 1, 31>>("1,31,bbq"));
	instances.push_back(std::make_unique<benchmark_provider_relaxed<BENCHMARK, 1, 63>>("1,63,bbq"));
	instances.push_back(std::make_unique<benchmark_provider_relaxed<BENCHMARK, 1, 127>>("1,127,bbq"));
	instances.push_back(std::make_unique<benchmark_provider_relaxed<BENCHMARK, 2, 7>>("2,7,bbq"));
	instances.push_back(std::make_unique<benchmark_provider_relaxed<BENCHMARK, 2, 15>>("2,15,bbq"));
	instances.push_back(std::make_unique<benchmark_provider_relaxed<BENCHMARK, 2, 31>>("2,31,bbq"));
	instances.push_back(std::make_unique<benchmark_provider_relaxed<BENCHMARK, 2, 63>>("2,63,bbq"));
	instances.push_back(std::make_unique<benchmark_provider_relaxed<BENCHMARK, 2, 127>>("2,127,bbq"));
	instances.push_back(std::make_unique<benchmark_provider_relaxed<BENCHMARK, 4, 7>>("4,7,bbq"));
	instances.push_back(std::make_unique<benchmark_provider_relaxed<BENCHMARK, 4, 15>>("4,15,bbq"));
	instances.push_back(std::make_unique<benchmark_provider_relaxed<BENCHMARK, 4, 31>>("4,31,bbq"));
	instances.push_back(std::make_unique<benchmark_provider_relaxed<BENCHMARK, 4, 63>>("4,63,bbq"));
	instances.push_back(std::make_unique<benchmark_provider_relaxed<BENCHMARK, 4, 127>>("4,127,bbq"));
	instances.push_back(std::make_unique<benchmark_provider_relaxed<BENCHMARK, 8, 7>>("8,7,bbq"));
	instances.push_back(std::make_unique<benchmark_provider_relaxed<BENCHMARK, 8, 15>>("8,15,bbq"));
	instances.push_back(std::make_unique<benchmark_provider_relaxed<BENCHMARK, 8, 31>>("8,31,bbq"));
	instances.push_back(std::make_unique<benchmark_provider_relaxed<BENCHMARK, 8, 63>>("8,63,bbq"));
	instances.push_back(std::make_unique<benchmark_provider_relaxed<BENCHMARK, 8, 127>>("8,127,bbq"));
	instances.push_back(std::make_unique<benchmark_provider_relaxed<BENCHMARK, 16, 7>>("16,7,bbq"));
	instances.push_back(std::make_unique<benchmark_provider_relaxed<BENCHMARK, 16, 15>>("16,15,bbq"));
	instances.push_back(std::make_unique<benchmark_provider_relaxed<BENCHMARK, 16, 31>>("16,31,bbq"));
	instances.push_back(std::make_unique<benchmark_provider_relaxed<BENCHMARK, 16, 63>>("16,63,bbq"));
	instances.push_back(std::make_unique<benchmark_provider_relaxed<BENCHMARK, 16, 127>>("16,127,bbq"));
#ifdef __GNUC__
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("2,1,multififo", 2, 1));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("2,2,multififo", 2, 2));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("2,4,multififo", 2, 4));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("2,8,multififo", 2, 8));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("2,16,multififo", 2, 16));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("2,32,multififo", 2, 32));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("2,64,multififo", 2, 64));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("2,128,multififo", 2, 128));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("2,256,multififo", 2, 256));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("2,512,multififo", 2, 512));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("2,1024,multififo", 2, 1024));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("2,2048,multififo", 2, 2048));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("2,4096,multififo", 2, 4096));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("4,1,multififo", 4, 1));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("4,2,multififo", 4, 2));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("4,4,multififo", 4, 4));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("4,8,multififo", 4, 8));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("4,16,multififo", 4, 16));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("4,32,multififo", 4, 32));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("4,64,multififo", 4, 64));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("4,128,multififo", 4, 128));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("4,256,multififo", 4, 256));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("4,512,multififo", 4, 512));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("4,1024,multififo", 4, 1024));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("4,2048,multififo", 4, 2048));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("4,4096,multififo", 4, 4096));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("8,1,multififo", 8, 1));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("8,2,multififo", 8, 2));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("8,4,multififo", 8, 4));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("8,8,multififo", 8, 8));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("8,16,multififo", 8, 16));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("8,32,multififo", 8, 32));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("8,64,multififo", 8, 64));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("8,128,multififo", 8, 128));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("8,256,multififo", 8, 256));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("8,512,multififo", 8, 512));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("8,1024,multififo", 8, 1024));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("8,2048,multififo", 8, 2048));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("8,4096,multififo", 8, 4096));
	instances.push_back(std::make_unique<benchmark_provider_ss_kfifo<BENCHMARK>>("1,kfifo", 1));
	instances.push_back(std::make_unique<benchmark_provider_ss_kfifo<BENCHMARK>>("2,kfifo", 2));
	instances.push_back(std::make_unique<benchmark_provider_ss_kfifo<BENCHMARK>>("4,kfifo", 4));
	instances.push_back(std::make_unique<benchmark_provider_ss_kfifo<BENCHMARK>>("8,kfifo", 8));
	instances.push_back(std::make_unique<benchmark_provider_ss_kfifo<BENCHMARK>>("16,kfifo", 16));
	instances.push_back(std::make_unique<benchmark_provider_ss_kfifo<BENCHMARK>>("32,kfifo", 32));
	instances.push_back(std::make_unique<benchmark_provider_ss_kfifo<BENCHMARK>>("64,kfifo", 64));
	instances.push_back(std::make_unique<benchmark_provider_ss_kfifo<BENCHMARK>>("128,kfifo", 128));
	instances.push_back(std::make_unique<benchmark_provider_ss_kfifo<BENCHMARK>>("256,kfifo", 256));
	instances.push_back(std::make_unique<benchmark_provider_ss_kfifo<BENCHMARK>>("512,kfifo", 512));
	instances.push_back(std::make_unique<benchmark_provider_ss_kfifo<BENCHMARK>>("1024,kfifo", 1024));
	instances.push_back(std::make_unique<benchmark_provider_ss_kfifo<BENCHMARK>>("2048,kfifo", 2048));
	instances.push_back(std::make_unique<benchmark_provider_ss_kfifo<BENCHMARK>>("4096,kfifo", 4096));
	instances.push_back(std::make_unique<benchmark_provider_ss_kfifo<BENCHMARK>>("8192,kfifo", 8192));
	instances.push_back(std::make_unique<benchmark_provider_generic<adapter<uint64_t, LCRQWrapped>, BENCHMARK>>("lcrq"));
#endif // __GNUC__
}

template <typename BENCHMARK>
void add_all_benchmarking(std::vector<std::unique_ptr<benchmark_provider<BENCHMARK>>>& instances) {
	instances.push_back(std::make_unique<benchmark_provider_generic<ringbuffer_wrapper<uint64_t>, BENCHMARK>>("sequential"));
	instances.push_back(std::make_unique<benchmark_provider_generic<ringbuffer_wrapper<uint64_t, std::vector<Padded<uint64_t>>>, BENCHMARK>>("sequential-padded"));
	instances.push_back(std::make_unique<benchmark_provider_relaxed<BENCHMARK, 1, 7>>("bbq-1-7"));
	instances.push_back(std::make_unique<benchmark_provider_relaxed<BENCHMARK, 2, 63>>("bbq-2-63"));
	instances.push_back(std::make_unique<benchmark_provider_relaxed<BENCHMARK, 4, 127>>("bbq-4-127"));
	instances.push_back(std::make_unique<benchmark_provider_relaxed<BENCHMARK, 8, 127>>("bbq-8-127"));
#ifdef __GNUC__
	instances.push_back(std::make_unique<benchmark_provider_ws_kfifo<BENCHMARK>>("ws-1-kfifo", 1));
	instances.push_back(std::make_unique<benchmark_provider_ss_kfifo<BENCHMARK>>("ss-512-kfifo", 512));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("2-2-multififo", 2, 2));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("4-16-multififo", 4, 16));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("4-128-multififo", 4, 128));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("4-256-multififo", 4, 256));
	instances.push_back(std::make_unique<benchmark_provider_generic<adapter<uint64_t, LCRQWrapped>, BENCHMARK>>("lcrq"));
#endif // __GNUC__
}

int main() {
#ifndef NDEBUG
	std::cout << "Running in debug mode!" << std::endl;
#endif // NDEBUG

	//test_consistency<8, 16>(20000, 200000, 0);

	// We need this because scal does really weird stuff to have thread locals.
	thread_pool pool;

	std::vector<int> processor_counts;
	for (int i = 1; i <= static_cast<int>(std::thread::hardware_concurrency()); i *= 2) {
		processor_counts.emplace_back(i);
	}

	constexpr int TEST_ITERATIONS = 5;
	constexpr int TEST_TIME_SECONDS = 5;

	int input;
	std::cout << "Which experiment to run?\n"
		"[1] FIFO Comparison\n"
		"[2] Parameter Tuning\n"
		"[3] Quality\n"
		"[4] Quality distribution\n"
		"[5] Fill\n"
		"[6] Empty\n"
		"[7] Strong Scaling\n"
		"[8] Bitset Size Comparison\n"
		"[9] Producer-Consumer\n"
#ifdef __GNUC__
		"[10] BFS\n"
#endif // __GNUC__
		"Input: ";
	std::cin >> input;

	switch (input) {
	case 1: {
		std::vector<std::unique_ptr<benchmark_provider<benchmark_default>>> instances;
		add_all_benchmarking(instances);
		run_benchmark(pool, "comp", instances, 0.5, processor_counts, TEST_ITERATIONS, TEST_TIME_SECONDS);
		} break;
	case 2: {
		std::cout << "Benchmarking performance" << std::endl;
		std::vector<std::unique_ptr<benchmark_provider<benchmark_default>>> instances;
		add_all_parameter_tuning(instances);
		run_benchmark(pool, "pt-block", instances, 0.5, { processor_counts.back() }, TEST_ITERATIONS, TEST_TIME_SECONDS);

		std::cout << "Benchmarking quality" << std::endl;
		std::vector<std::unique_ptr<benchmark_provider<benchmark_quality<>>>> instances_q;
		add_all_parameter_tuning(instances_q);
		run_benchmark(pool, "pt-quality", instances_q, 0.5, { processor_counts.back() }, TEST_ITERATIONS, 0);
		} break;
	case 3: {
		std::vector<std::unique_ptr<benchmark_provider<benchmark_quality<>>>> instances;
		add_all_benchmarking(instances);
		run_benchmark(pool, "quality", instances, 0.5, processor_counts, TEST_ITERATIONS, TEST_TIME_SECONDS);
		} break;
	case 4: {
		std::vector<std::unique_ptr<benchmark_provider<benchmark_quality<true>>>> instances;
		add_all_benchmarking(instances);
		run_benchmark(pool, "quality-max", instances, 0.5, { processor_counts.back()}, 1, TEST_TIME_SECONDS);
	} break;
	case 5: {
		std::vector<std::unique_ptr<benchmark_provider<benchmark_fill>>> instances;
		add_all_benchmarking(instances);
		run_benchmark(pool, "fill", instances, 0, processor_counts, TEST_ITERATIONS, 10);
		} break;
	case 6: {
		std::vector<std::unique_ptr<benchmark_provider<benchmark_empty>>> instances;
		add_all_benchmarking(instances);
		run_benchmark(pool, "empty", instances, 1, processor_counts, TEST_ITERATIONS, 10);
		} break;
	case 7: {
		static constexpr size_t THREADS = 128;

		std::cout << "Benchmarking performance" << std::endl;
		std::vector<std::unique_ptr<benchmark_provider<benchmark_default>>> instances;
		instances.push_back(std::make_unique<benchmark_provider_generic<block_based_queue<uint64_t, THREADS, 7>, benchmark_default>>("bbq-1-7"));
		instances.push_back(std::make_unique<benchmark_provider_generic<block_based_queue<uint64_t, 2 * THREADS, 63>, benchmark_default>>("bbq-2-63"));
		instances.push_back(std::make_unique<benchmark_provider_generic<block_based_queue<uint64_t, 4 * THREADS, 127>, benchmark_default>>("bbq-4-127"));
		instances.push_back(std::make_unique<benchmark_provider_generic<block_based_queue<uint64_t, 8 * THREADS, 127>, benchmark_default>>("bbq-8-127"));
		run_benchmark(pool, "ss-performance", instances, 0.5, processor_counts, TEST_ITERATIONS, TEST_TIME_SECONDS);

		std::cout << "Benchmarking quality" << std::endl;
		std::vector<std::unique_ptr<benchmark_provider<benchmark_quality<>>>> instances_q;
		instances_q.push_back(std::make_unique<benchmark_provider_generic<block_based_queue<uint64_t, THREADS, 7>, benchmark_quality<>>>("bbq-1-7"));
		instances_q.push_back(std::make_unique<benchmark_provider_generic<block_based_queue<uint64_t, 2 * THREADS, 63>, benchmark_quality<>>>("bbq-2-63"));
		instances_q.push_back(std::make_unique<benchmark_provider_generic<block_based_queue<uint64_t, 4 * THREADS, 127>, benchmark_quality<>>>("bbq-4-127"));
		instances_q.push_back(std::make_unique<benchmark_provider_generic<block_based_queue<uint64_t, 8 * THREADS, 127>, benchmark_quality<>>>("bbq-8-127"));
		run_benchmark(pool, "ss-quality", instances_q, 0.5, processor_counts, TEST_ITERATIONS, 0);
		} break;
	case 8: {
		std::vector<std::unique_ptr<benchmark_provider<benchmark_default>>> instances;
		instances.push_back(std::make_unique<benchmark_provider_relaxed<benchmark_default, 1, 7, uint8_t>>("8,bbq-1-7"));
		instances.push_back(std::make_unique<benchmark_provider_relaxed<benchmark_default, 2, 63, uint8_t>>("8,bbq-2-63"));
		instances.push_back(std::make_unique<benchmark_provider_relaxed<benchmark_default, 4, 127, uint8_t>>("8,bbq-4-127"));
		instances.push_back(std::make_unique<benchmark_provider_relaxed<benchmark_default, 8, 127, uint8_t>>("8,bbq-8-127"));
		instances.push_back(std::make_unique<benchmark_provider_relaxed<benchmark_default, 1, 7, uint16_t>>("16,bbq-1-7"));
		instances.push_back(std::make_unique<benchmark_provider_relaxed<benchmark_default, 2, 63, uint16_t>>("16,bbq-2-63"));
		instances.push_back(std::make_unique<benchmark_provider_relaxed<benchmark_default, 4, 127, uint16_t>>("16,bbq-4-127"));
		instances.push_back(std::make_unique<benchmark_provider_relaxed<benchmark_default, 8, 127, uint16_t>>("16,bbq-8-127"));
		instances.push_back(std::make_unique<benchmark_provider_relaxed<benchmark_default, 1, 7, uint32_t>>("32,bbq-1-7"));
		instances.push_back(std::make_unique<benchmark_provider_relaxed<benchmark_default, 2, 63, uint32_t>>("32,bbq-2-63"));
		instances.push_back(std::make_unique<benchmark_provider_relaxed<benchmark_default, 4, 127, uint32_t>>("32,bbq-4-127"));
		instances.push_back(std::make_unique<benchmark_provider_relaxed<benchmark_default, 8, 127, uint32_t>>("32,bbq-8-127"));
		instances.push_back(std::make_unique<benchmark_provider_relaxed<benchmark_default, 1, 7, uint64_t>>("64,bbq-1-7"));
		instances.push_back(std::make_unique<benchmark_provider_relaxed<benchmark_default, 2, 63, uint64_t>>("64,bbq-2-63"));
		instances.push_back(std::make_unique<benchmark_provider_relaxed<benchmark_default, 4, 127, uint64_t>>("64,bbq-4-127"));
		instances.push_back(std::make_unique<benchmark_provider_relaxed<benchmark_default, 8, 127, uint64_t>>("64,bbq-8-127"));
		run_benchmark(pool, "bitset-sizes", instances, 0.5, processor_counts, TEST_ITERATIONS, TEST_TIME_SECONDS);
		} break;
	case 9: {
		std::vector<std::unique_ptr<benchmark_provider<benchmark_prodcon>>> instances;
		add_all_benchmarking(instances);
		// TODO: This can be done nicer to account for different thread counts.
		for (int producers = 8; producers < 128; producers += 8) {
			auto consumers = 128 - producers;
			run_benchmark<benchmark_prodcon, benchmark_info_prodcon, int, int>(pool,
				std::format("prodcon-{}-{}", producers, consumers), instances, 0.5,
				{ processor_counts.back() }, TEST_ITERATIONS, TEST_TIME_SECONDS, producers, consumers);
		}
	} break;


#ifdef __GNUC__
		case 10: {
			std::filesystem::path graph_file;
			std::cout << "Please enter your graph file: ";
			std::cin >> graph_file;
			Graph graph{ graph_file };

			for (int i = 0; i < TEST_ITERATIONS; i++) {
				auto [time, dist] = sequential_bfs(graph);
				std::cout << "Sequential time: " << time << "; Dist: " << dist + 1 << std::endl;
			}

			for (int i = 0; i < TEST_ITERATIONS; i++) {
				auto [time, dist] = sequential_bfs_padded(graph);
				std::cout << "Sequential padded time: " << time << "; Dist: " << dist + 1 << std::endl;
			}

			for (int i = 0; i < TEST_ITERATIONS; i++) {
				auto [time, dist] = sequential_bfs_multififo(graph);
				std::cout << "Sequential multififo time: " << time << "; Dist: " << dist + 1 << std::endl;
			}

			std::vector<std::unique_ptr<benchmark_provider<benchmark_bfs>>> instances;
			add_all_benchmarking(instances);
			run_benchmark<benchmark_bfs, benchmark_info_graph, Graph*>(pool, std::format("bfs-{}", graph_file.filename().string()), instances, 0, { 1 }, TEST_ITERATIONS, 0, & graph);
	} break;
#endif // __GNUC__
	}

	return 0;
}
