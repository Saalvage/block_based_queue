﻿#include "config.hpp"

#include "block_based_queue.h"


#include <ranges>
#include <chrono>
#include <thread>
#include <filesystem>
#include <fstream>
#include <unordered_set>
#include <iostream>

#ifdef WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

template <std::size_t THREAD_COUNT, std::size_t BLOCK_MULTIPLIER>
void test_consistency(std::size_t fifo_size, std::size_t elements_per_thread, double prefill) {
	block_based_queue<std::uint64_t> fifo{ THREAD_COUNT, fifo_size, BLOCK_MULTIPLIER, 7 };
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

std::ofstream setup_file(const std::string& test_name, double prefill, bool print_header, const std::string& header) {
	constexpr const char* format = "fifo-{}-{}-{:%FT%H-%M-%S}.csv";

	std::string filename = std::format(format, test_name, prefill, std::chrono::round<std::chrono::seconds>(std::chrono::file_clock::now()));
	std::ofstream file{ filename };
	if (print_header) {
		// TODO: Doesn't take into account parameter tuning.
		file << "queue,thread_count," << header << '\n';
	}

	std::cout << "Writing results to:" << std::endl;
	std::cerr << filename << std::endl;
	return file;
}

template <typename BENCHMARK, typename BENCHMARK_DATA_TYPE = benchmark_info, typename... Args>
void run_benchmark(const std::string& test_name, const std::vector<std::unique_ptr<benchmark_provider<BENCHMARK>>>& instances, double prefill,
	const std::vector<int>& processor_counts, int test_iterations, int test_time_seconds, bool print_header, bool quiet, const Args&... args) {
    std::ofstream file = setup_file(test_name, prefill, print_header, BENCHMARK::header);
	run_benchmark_raw<BENCHMARK, BENCHMARK_DATA_TYPE, Args...>(file, instances, prefill, processor_counts, test_iterations, test_time_seconds,
		quiet, args...);
}

template <typename BENCHMARK, typename BENCHMARK_DATA_TYPE, typename... Args>
void run_benchmark_raw(std::ofstream& file, const std::vector<std::unique_ptr<benchmark_provider<BENCHMARK>>>& instances, double prefill,
	const std::vector<int>& processor_counts, int test_iterations, int test_time_seconds, bool quiet, const Args&... args) {
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

	for (auto i : std::views::iota(0, test_iterations)) {
		if (!quiet) {
			std::cout << "Test run " << (i + 1) << " of " << test_iterations << std::endl;
		}
		for (const auto& imp : instances) {
			if (!quiet) {
				std::cout << "Testing " << imp->get_name() << std::endl;
			}
			for (auto threads : processor_counts) {
				if (!quiet) {
					std::cout << "With " << threads << " processors" << std::endl;
				}
				file << imp->get_name() << "," << threads << ',';
				BENCHMARK_DATA_TYPE data{threads, test_time_seconds, args...};
				imp->test(data, prefill).output(file);
				file << '\n';
			}
		}
	}
}

static std::tuple<std::filesystem::path, Graph> read_and_test_graph(int argc, const char** argv) {
	std::filesystem::path graph_file;
	if (argc > 2) {
		graph_file = argv[2];
	} else {
		std::cout << "Please enter your graph file: ";
		std::cin >> graph_file;
	}
	Graph graph{ graph_file };
	return { graph_file, std::move(graph) };
}

std::size_t get_total_system_memory_bytes() {
#ifdef WIN32
	MEMORYSTATUSEX status;
	status.dwLength = sizeof(status);
	GlobalMemoryStatusEx(&status);
	return status.ullTotalPhys;
#else
	auto page_count = sysconf(_SC_PHYS_PAGES);
	auto page_size = sysconf(_SC_PAGE_SIZE);
	return page_count * page_size;
#endif
}

int main(int argc, const char** argv) {
#ifndef NDEBUG
	std::cout << "Running in debug mode!" << std::endl;
#endif // NDEBUG

	//test_consistency<8, 16>(20000, 200000, 0);

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
			"[8] BFS multistart (weak scaling)\n"
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
			"[--bfs-multistart-fixed <count>]"
			"[-f | --prefill <factor>]"
			"[-p | --parameter-tuning]"
			"[-n | --no-header]"
			" ([-i | --include <fifo>]* | [-e | --exclude <fifo>]*)\n";
		return 0;
	}

	input = std::strtol(argv[1], nullptr, 10);

	std::vector<int> processor_counts;
	if (input == 6) {
		processor_counts.emplace_back(std::thread::hardware_concurrency());
	} else {
		for (int i = 1; i < static_cast<int>(std::thread::hardware_concurrency()); i *= 2) {
			processor_counts.emplace_back(i);
		}
		processor_counts.emplace_back(std::thread::hardware_concurrency());
	}

	std::optional<double> prefill_override;
	auto test_its = TEST_ITERATIONS_DEFAULT;
	auto test_time_secs = TEST_TIME_SECONDS_DEFAULT;

	std::unordered_set<std::string> fifo_set;
	bool include_header = true;
	bool parameter_tuning = false;
	bool is_exclude = true;
	bool quiet = false;
	int bfs_multistart_fixed = -1;

	for (int i = input == 7 || input == 8 ? 3 : 2; i < argc; i++) {
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
		} else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--test_time_seconds") == 0) {
			i++;
			test_time_secs = std::strtol(argv[i], nullptr, 10);
		} else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--prefill") == 0) {
			i++;
			prefill_override = std::strtod(argv[i], nullptr);
		} else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--parameter-tuning") == 0) {
			parameter_tuning = true;
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
		} else if (strcmp(argv[i], "--bfs-multistart-fixed") == 0) {
			i++;
			bfs_multistart_fixed = std::strtol(argv[i], nullptr, 10);
		} else if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--no-header") == 0) {
			include_header = false;
		} else if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quiet") == 0) {
			quiet = true;
		} else {
			std::cerr << std::format("Unknown argument \"{}\"!", argv[i]) << std::endl;
			return 1;
		}
	}

	switch (input) {
	case 1: {
		std::vector<std::unique_ptr<benchmark_provider<benchmark_default>>> instances;
		add_instances(instances, parameter_tuning, fifo_set, is_exclude);
		run_benchmark("comp", instances, prefill_override.value_or(0.5), processor_counts, test_its, test_time_secs, include_header, quiet);
		} break;
	case 2: {
		std::vector<std::unique_ptr<benchmark_provider<benchmark_quality<>>>> instances;
		add_instances(instances, parameter_tuning, fifo_set, is_exclude);
		run_benchmark("quality", instances, prefill_override.value_or(0.5), processor_counts, test_its, test_time_secs, include_header, quiet);
		} break;
	case 3: {
		std::vector<std::unique_ptr<benchmark_provider<benchmark_quality<true>>>> instances;
		add_instances(instances, parameter_tuning, fifo_set, is_exclude);
		run_benchmark("quality-max", instances, prefill_override.value_or(0.5), { processor_counts.back() }, 1, test_time_secs, include_header, quiet);
	} break;
	case 4: {
		std::vector<std::unique_ptr<benchmark_provider<benchmark_fill>>> instances;
		add_instances(instances, parameter_tuning, fifo_set, is_exclude);
		run_benchmark("fill", instances, prefill_override.value_or(0), processor_counts, test_its, test_time_secs, include_header, quiet);
		} break;
	case 5: {
		std::vector<std::unique_ptr<benchmark_provider<benchmark_empty>>> instances;
		add_instances(instances, parameter_tuning, fifo_set, is_exclude);
		run_benchmark("empty", instances, prefill_override.value_or(1), processor_counts, test_its, test_time_secs, include_header, quiet);
		} break;
	case 6: {
		std::vector<std::unique_ptr<benchmark_provider<benchmark_prodcon>>> instances;
		add_instances(instances, parameter_tuning, fifo_set, is_exclude);
		if (processor_counts.size() != 1) {
			std::cout << "Notice: Producer-consumer benchmark only considers last provided processor count" << std::endl;
		}
		auto threads = processor_counts.back();
		auto increments = threads / 16;
		if (threads % 16 != 0) {
			std::cout << "Error: Thread count must be divisible by 16 for producer-consumer benchmark!" << std::endl;
			return 6;
		}
		for (int producers = increments; producers < threads; producers += increments) {
			auto consumers = threads - producers;
			run_benchmark<benchmark_prodcon, benchmark_info_prodcon, int, int>(
				std::format("prodcon-{}-{}", producers, consumers), instances, prefill_override.value_or(0.5),
				{ threads }, test_its, test_time_secs, include_header, quiet, producers, consumers);
		}
	} break;
	case 7: {
		auto [graph_file, graph] = read_and_test_graph(argc, argv);

		auto result_file = setup_file(std::format("bfs-{}", graph_file.filename().string()), 0, include_header, benchmark_bfs::header);

		std::vector<std::uint32_t> distances;
		for (int i = 0; i < test_its; i++) {
			auto [time, dist, d] = sequential_bfs(graph);
			result_file << "sequential," << (processor_counts.size() == 1 ? processor_counts[0] : 1) << "," << time << "," << dist << std::endl;
			distances = std::move(d);
		}

		std::vector<std::unique_ptr<benchmark_provider<benchmark_bfs>>> instances;
		add_instances(instances, parameter_tuning, fifo_set, is_exclude);
		run_benchmark_raw<benchmark_bfs, benchmark_info_graph, const Graph&, const std::vector<std::uint32_t>&>(
			result_file, instances, 0, processor_counts, test_its, 0, quiet, graph, distances);
	} break;
	case 8: {
			auto [graph_file, graph] = read_and_test_graph(argc, argv);

			auto avail_bytes = get_total_system_memory_bytes();
			std::erase_if(processor_counts, [&](int p) {
				return p * graph.num_nodes() * std::hardware_destructive_interference_size * 2 >= avail_bytes;
			});

			auto result_file = setup_file(std::format("bfs-multistart-{}", graph_file.filename().string()), 0, include_header, benchmark_bfs_multistart::header);

			std::vector<std::vector<std::uint32_t>> distances(processor_counts.size());
			for (std::size_t i = 0; i < processor_counts.size(); i++) {
				for (int it = 0; it < test_its; it++) {
					std::uint64_t time = 0;
					for (int p = 0; p < processor_counts[i]; p++) {
						auto [from_start_time, dist, d]
							= sequential_bfs(graph, benchmark_bfs_multistart::get_start_node(p, processor_counts[i], graph.num_nodes()));
						//distances[i] = std::move(d);
						time += from_start_time;
					}
					result_file << "sequential," << processor_counts[i] << "," << time << std::endl;
				}
			}

			std::vector<std::unique_ptr<benchmark_provider<benchmark_bfs_multistart>>> instances;
			add_instances(instances, parameter_tuning, fifo_set, is_exclude);
			run_benchmark_raw<benchmark_bfs_multistart, benchmark_info_graph_multistart, const Graph&, const std::vector<std::vector<std::uint32_t>>&>(
				result_file, instances, 0, processor_counts, test_its, 0, quiet, graph, distances, bfs_multistart_fixed);
	} break;
	}

	return 0;
}
