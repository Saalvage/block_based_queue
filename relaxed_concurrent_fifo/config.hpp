#ifndef CONFIG_H_INCLUDED
#define CONFIG_H_INCLUDED

#include <vector>
#include <memory>
#include <unordered_set>
#include <regex>

#include "benchmark.h"

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"

#include "contenders/LCRQ/wrapper.h"
#include "contenders/LCRQ/LCRQueue.hpp"

template <typename T>
using LCRQWrapped = LCRQueue<T>;

#pragma GCC diagnostic pop
#endif // __GNUC__

// By default, include all.
#if !defined(INCLUDE_BBQ) \
	&& !defined(INCLUDE_MULTIFIFO) \
	&& !defined(INCLUDE_LCRQ) \
	&& !defined(INCLUDE_KFIFO) \
	&& !defined(INCLUDE_DCBO) \
	&& !defined(INCLUDE_2D)
#define INCLUDE_ALL
#endif

// Useful when wanting to test a single custom configuration.
#ifdef INCLUDE_NONE
#undef INCLUDE_ALL
#endif

template <typename BENCHMARK>
static void add_instances(std::vector<std::unique_ptr<benchmark_provider<BENCHMARK>>>& instances, std::unordered_set<std::string>& filter_set, bool are_exclude_filters) {
#if defined(INCLUDE_BBQ) || defined(INCLUDE_ALL)
#ifdef PARAMETER_TUNING
	for (int b = 1; b < 16; i *= 2) {
		instances.push_back(std::make_unique<benchmark_provider_bbq<BENCHMARK, 1>>("{},1,bbq", b));
		instances.push_back(std::make_unique<benchmark_provider_bbq<BENCHMARK, 3>>("{},3,bbq", b));
		instances.push_back(std::make_unique<benchmark_provider_bbq<BENCHMARK, 7>>("{},7,bbq", b));
		instances.push_back(std::make_unique<benchmark_provider_bbq<BENCHMARK, 15>>("{},15,bbq", b));
		instances.push_back(std::make_unique<benchmark_provider_bbq<BENCHMARK, 31>>("{},31,bbq", b));
		instances.push_back(std::make_unique<benchmark_provider_bbq<BENCHMARK, 63>>("{},63,bbq", b));
		instances.push_back(std::make_unique<benchmark_provider_bbq<BENCHMARK, 127>>("{},127,bbq", b));
	}
#else // PARAMETER_TUNING
	instances.push_back(std::make_unique<benchmark_provider_bbq<BENCHMARK, 7>>("bbq-{}-7", 1));
	instances.push_back(std::make_unique<benchmark_provider_bbq<BENCHMARK, 63>>("bbq-{}-63", 2));
	instances.push_back(std::make_unique<benchmark_provider_bbq<BENCHMARK, 127>>("bbq-{}-127", 4));
	instances.push_back(std::make_unique<benchmark_provider_bbq<BENCHMARK, 127>>("bbq-{}-127", 8));
#endif // PARAMETER_TUNING
#endif

#if defined(INCLUDE_MULTIFIFO) || defined(INCLUDE_ALL)
#ifdef PARAMETER_TUNING
	for (int queues_per_thread = 2; queues_per_thread <= 8; queues_per_thread *= 2) {
		for (int stickiness = 1; stickiness <= 4096; stickiness *= 2) {
			instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("{},{},multififo", queues_per_thread, stickiness));
		}
	}
#else // PARAMETER_TUNING
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("{}-{}-multififo", 2, 2));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("{}-{}-multififo", 4, 16));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("{}-{}-multififo", 4, 128));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("{}-{}-multififo", 4, 256));
#endif // PARAMETER_TUNING
#endif

#if defined(INCLUDE_KFIFO) || defined(INCLUDE_ALL)
#ifdef PARAMETER_TUNING
	for (int k = 1; k <= 8192; k *= 2) {
		instances.push_back(std::make_unique<benchmark_provider_ss_kfifo<BENCHMARK>>("{},kfifo", k));
	}
#else // PARAMETER_TUNING
	instances.push_back(std::make_unique<benchmark_provider_ws_kfifo<BENCHMARK>>("ws-{}-kfifo", 1));
	instances.push_back(std::make_unique<benchmark_provider_ss_kfifo<BENCHMARK>>("ss-{}-kfifo", 512));
#endif // PARAMETER_TUNING
#endif

#if defined(__GNUC__) && (defined(INCLUDE_DCBO) || defined(INCLUDE_ALL))
	for (int w = 1; w <= 8; w *= 2) {
		instances.push_back(std::make_unique<benchmark_provider_dcbo<BENCHMARK>>("{}-dcbo", w));
	}
#endif

#if defined (__GNUC__) && (defined(INCLUDE_2D) || defined(INCLUDE_ALL))
	for (int w = 1; w <= 8; w *= 2) {
		for (int k = 1; k <= 8192; k *= 2) {
			instances.push_back(std::make_unique<benchmark_provider_2Dd<BENCHMARK>>("{},{}-2Dqueue", w, k));
		}
	}
#endif

#if defined (__GNUC__) && (defined(INCLUDE_LCRQ) || defined(INCLUDE_ALL))
	instances.push_back(std::make_unique<benchmark_provider_generic<adapter<std::uint64_t, LCRQWrapped>, BENCHMARK>>("lcrq"));
#endif

	for (std::size_t i = 0; i < instances.size(); i++) {
		const std::string& name = instances[i]->get_name();
		bool any_match = false;
		std::smatch m;
		for (const std::string& filter : filter_set) {
			if (std::regex_match(name, m, std::regex{filter})) {
				any_match = true;
				break;
			}
		}
		if (any_match == are_exclude_filters) {
			instances.erase(instances.begin() + i);
			i--;
		}
	}
}

#endif // CONFIG_H_INCLUDED
