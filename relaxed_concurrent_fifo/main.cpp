#include "benchmark.h"
#include "config.hpp"

#include "lock_fifo.h"
#include "block_based_queue.h"
#include "concurrent_fifo.h"
#include "cylinder_fifo.hpp"

#include "contenders/LCRQ/wrapper.h"
#include "contenders/LCRQ/MichaelScottQueue.hpp"

#include <thread>
#include <functional>
#include <ranges>
#include <filesystem>
#include <fstream>
#include <unordered_set>
#include <iostream>

static constexpr std::size_t make_po2(std::size_t size) {
	std::size_t ret = 1;
	while (size > ret) {
		ret *= 2;
	}
	return ret;
}

static std::pair<std::uint64_t, std::uint32_t> sequential_bfs(const Graph& graph) {
	multififo::RingBuffer<std::uint32_t> nodes(make_po2(graph.num_nodes()));
	std::vector<std::uint32_t> distances(graph.num_nodes(), std::numeric_limits<std::uint32_t>::max());
	distances[0] = 0;

	nodes.push(static_cast<std::uint32_t>(graph.nodes[0]));

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
	return std::pair(end - now, *std::max_element(distances.begin(), distances.end()));
}

/*static constexpr int COUNT = 512;

template <template <typename, std::size_t> typename T>
void test_full_capacity() {
	T<int, COUNT> buf;
	for (int i : std::views::iota(0, COUNT)) {
		assert(buf.push(i));
	}
	for (int i : std::views::iota(0, COUNT)) {
		assert(buf.pop() == i);
	}
}

template <template <typename, std::size_t> typename T>
void test_single_element() {
	T<int, COUNT> buf;
	for (int i : std::views::iota(0, COUNT * 10)) {
		assert(buf.push(i));
		assert(buf.pop() == i);
	}
}

template <template <typename, std::size_t> typename T>
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

template <template <typename, std::size_t> typename T>
void test_full_push() {
	T<int, 1> buf;
	buf.push(1);
	assert(!buf.push(1));
}

template <template <typename, std::size_t> typename T>
void test_all() {
	test_full_capacity<T>();
	test_single_element<T>();
	test_empty_pop<T>();
	test_full_push<T>();
}*/

template <std::size_t THREAD_COUNT, std::size_t BLOCK_MULTIPLIER>
void test_consistency(std::size_t fifo_size, std::size_t elements_per_thread, double prefill) {
	bbq_min_block_count<std::uint64_t, THREAD_COUNT * BLOCK_MULTIPLIER> fifo{ THREAD_COUNT, fifo_size };
	auto handle = fifo.get_handle();

	std::size_t pre_push = static_cast<std::size_t>(fifo_size * prefill);
	std::unordered_multiset<std::uint64_t> test_ints;
	for (std::size_t index = 0; index < pre_push; index++) {
		auto i = index | (1ull << 63);
		handle.push(i);
		test_ints.emplace(i);
	}

	std::barrier a{ (ptrdiff_t)(THREAD_COUNT + 1) };
	std::vector<std::jthread> threads(THREAD_COUNT);
	std::vector<std::vector<std::uint64_t>> test(THREAD_COUNT);
	std::vector<std::vector<std::uint64_t>> popped(THREAD_COUNT);
	for (std::size_t i = 0; i < THREAD_COUNT; i++) {
		threads[i] = std::jthread([&, i]() {
			auto handle = fifo.get_handle();
			a.arrive_and_wait();
			for (std::uint64_t j = 0; j < elements_per_thread; j++) {
				auto val = (i << 32) | (j + 1);
				test[i].push_back(val);
				while (!handle.push(val)) {}
				std::optional<std::uint64_t> pop;
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

	std::unordered_multiset<std::uint64_t> popped_ints;
	for (std::size_t index = 0; index < pre_push; index++) {
		popped_ints.emplace(handle.pop().value());
	}

	for (std::size_t i = 0; i < THREAD_COUNT; i++) {
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

template <std::size_t BITSET_SIZE = 128>
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
		auto result = a.template claim_bit<claim_value::ONE, claim_mode::READ_WRITE>();
		if (result != std::numeric_limits<std::size_t>::max() && (a[result] || !b[result])) {
			throw std::runtime_error("Incorrect!");
		}
	}
}

template <typename BENCHMARK, typename BENCHMARK_DATA_TYPE = benchmark_info, typename... Args>
void run_benchmark(const std::string& test_name, const std::vector<std::unique_ptr<benchmark_provider<BENCHMARK>>>& instances, double prefill,
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

	std::string filename = std::format(format, test_name, prefill, std::chrono::round<std::chrono::seconds>(std::chrono::file_clock::now()));
	std::ofstream file{ filename };
	for (auto i : std::views::iota(0, test_iterations)) {
		std::cout << "Test run " << (i + 1) << " of " << test_iterations << std::endl;
		for (const auto& imp : instances) {
			std::cout << "Testing " << imp->get_name() << std::endl;
			for (auto threads : processor_counts) {
				std::cout << "With " << threads << " processors" << std::endl;
				file << imp->get_name() << "," << threads << ',';
				BENCHMARK_DATA_TYPE data{threads, test_time_seconds, args...};
				imp->test(data, prefill).output(file);
				file << '\n';
			}
		}
	}
	std::cout << "Results written to " << filename << std::endl;
}

int main(int argc, const char** argv) {
#ifndef NDEBUG
	std::cout << "Running in debug mode!" << std::endl;
#endif // NDEBUG

	//test_consistency<8, 16>(20000, 200000, 0);

	std::vector<int> processor_counts;
	for (int i = 1; i <= static_cast<int>(std::thread::hardware_concurrency()); i *= 2) {
		processor_counts.emplace_back(i);
	}

	constexpr int TEST_ITERATIONS_DEFAULT = 2;
	constexpr int TEST_TIME_SECONDS_DEFAULT = 5;

	int input;
	std::vector<std::string> seglist;
	std::vector<const char*> argv2;
	if (argc <= 1) {
		std::cout << "Which experiment to run? \n"
			"[1] Performance\n"
			"[2] Quality\n"
			"[3] Quality distribution\n"
			"[4] Fill\n"
			"[5] Empty\n"
			"[6] Producer-Consumer\n"
			"[7] BFS\n"
			"Input: ";
		std::string input_str;
		getline(std::cin, input_str);
		std::stringstream strstr{ input_str };
		std::string temp;
		while (std::getline(strstr, temp, ' ')) {
			seglist.push_back(temp);
		}
		argv2.resize(seglist.size() + 1);
		for (std::size_t i = 0; i < seglist.size(); i++) {
			argv2[i + 1] = seglist[i].c_str();
		}
		argc = static_cast<int>(argv2.size());
		argv = argv2.data();
	}

	if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
		std::cout << "Usage: " << argv[0] << " <experiment_no> <graph_file>? [-h | --help] "
			"[-t | --thread_count <count>] "
			"[-s | --test_time_seconds <count> (default " << TEST_TIME_SECONDS_DEFAULT << ")] "
			"[-r | --run_count <count> (default " << TEST_ITERATIONS_DEFAULT << ")]"
			"[-f | --prefill <factor>]"
			" ([-i | --include <fifo>]* | [-e | --exclude <fifo>]*)\n";
		return 0;
	}

	input = std::strtol(argv[1], nullptr, 10);

	std::optional<double> prefill_override;
	auto test_its = TEST_ITERATIONS_DEFAULT;
	auto test_time_secs = TEST_TIME_SECONDS_DEFAULT;

	std::unordered_set<std::string> fifo_set;
	bool is_exclude = true;

	for (int i = input == 7 ? 3 : 2; i < argc; i++) {
		if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--thread_count") == 0) {
			i++;
			const char* arg = argv[i];
			--arg;
			processor_counts.clear();
			do {
				++arg;
				processor_counts.push_back(std::strtol(arg, nullptr, 10));
			} while (*arg == ',');
		} else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--run_count") == 0) {
			i++;
			test_its = std::strtol(argv[i], nullptr, 10);
		} else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--thread_count_seconds") == 0) {
			i++;
			test_time_secs = std::strtol(argv[i], nullptr, 10);
		}  else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--prefill") == 0) {
			i++;
			prefill_override = std::strtod(argv[i], nullptr);
		} else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--include") == 0) {
			i++;
			if (is_exclude) {
				if (!fifo_set.empty()) {
					std::cerr << "Cannot specify -i and -e at the same time!" << std::endl;
					return 1;
				}
				is_exclude = false;
			}
			fifo_set.emplace(argv[i]);
		} else if (strcmp(argv[i], "-e") == 0 || strcmp(argv[i], "--exclude") == 0) {
			i++;
			if (!is_exclude) {
				std::cerr << "Cannot specify -i and -e at the same time!" << std::endl;
				return 1;
			}
			fifo_set.emplace(argv[i]);
		} else {
			std::cerr << std::format("Unknown argument \"{}\"!", argv[i]) << std::endl;
			return 1;
		}
	}

	switch (input) {
	case 1: {
		std::vector<std::unique_ptr<benchmark_provider<benchmark_default>>> instances;
		add_instances(instances, fifo_set, is_exclude);
		run_benchmark("comp", instances, prefill_override.value_or(0.5), processor_counts, test_its, test_time_secs);
		} break;
	case 2: {
		std::vector<std::unique_ptr<benchmark_provider<benchmark_quality<>>>> instances;
		add_instances(instances, fifo_set, is_exclude);
		run_benchmark("quality", instances, prefill_override.value_or(0.5), processor_counts, test_its, test_time_secs);
		} break;
	case 3: {
		std::vector<std::unique_ptr<benchmark_provider<benchmark_quality<true>>>> instances;
		add_instances(instances, fifo_set, is_exclude);
		run_benchmark("quality-max", instances, prefill_override.value_or(0.5), { processor_counts.back() }, 1, test_time_secs);
	} break;
	case 4: {
		std::vector<std::unique_ptr<benchmark_provider<benchmark_fill>>> instances;
		add_instances(instances, fifo_set, is_exclude);
		run_benchmark("fill", instances, prefill_override.value_or(0), processor_counts, test_its, 10);
		} break;
	case 5: {
		std::vector<std::unique_ptr<benchmark_provider<benchmark_empty>>> instances;
		add_instances(instances, fifo_set, is_exclude);
		run_benchmark("empty", instances, prefill_override.value_or(1), processor_counts, test_its, 10);
		} break;
	case 6: {
		std::vector<std::unique_ptr<benchmark_provider<benchmark_prodcon>>> instances;
		add_instances(instances, fifo_set, is_exclude);
		// TODO: This can be done nicer to account for different thread counts.
		for (int producers = 8; producers < 128; producers += 8) {
			auto consumers = 128 - producers;
			run_benchmark<benchmark_prodcon, benchmark_info_prodcon, int, int>(
				std::format("prodcon-{}-{}", producers, consumers), instances, prefill_override.value_or(0.5),
				{ processor_counts.back() }, test_its, test_time_secs, producers, consumers);
		}
	} break;
	case 7: {
			std::filesystem::path graph_file;
			if (argc > 2) {
				graph_file = argv[2];
			} else {
				std::cout << "Please enter your graph file: ";
				std::cin >> graph_file;
			}
			Graph graph{ graph_file };

			for (int i = 0; i < test_its; i++) {
				auto [time, dist] = sequential_bfs(graph);
				std::cout << "Sequential time: " << time << "; Dist: " << dist + 1 << std::endl;
			}

			std::vector<std::unique_ptr<benchmark_provider<benchmark_bfs>>> instances;
			add_instances(instances, fifo_set, is_exclude);
			run_benchmark<benchmark_bfs, benchmark_info_graph, Graph*>(std::format("bfs-{}", graph_file.filename().string()), instances, 0, processor_counts, test_its, 0, &graph);
	} break;
	}

	return 0;
}
