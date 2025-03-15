#ifndef CONFIG_H_INCLUDED
#define CONFIG_H_INCLUDED

#include <vector>
#include <memory>
#include <unordered_set>

#include "benchmark.h"

#define INCLUDE_MULTIFIFO

// By default, include all.
#if !defined(INCLUDE_BBQ) \
	&& !defined(INCLUDE_MULTIFIFO) \
	&& !defined(INCLUDE_LCRQ) \
	&& !defined(INCLUDE_CFIFO) \
	&& !defined(INCLUDE_KFIFO)
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
#else // PARAMETER_TUNING
	instances.push_back(std::make_unique<benchmark_provider_relaxed<BENCHMARK, 1, 7>>("bbq-1-7"));
	instances.push_back(std::make_unique<benchmark_provider_relaxed<BENCHMARK, 2, 63>>("bbq-2-63"));
	instances.push_back(std::make_unique<benchmark_provider_relaxed<BENCHMARK, 4, 127>>("bbq-4-127"));
	instances.push_back(std::make_unique<benchmark_provider_relaxed<BENCHMARK, 8, 127>>("bbq-8-127"));
#endif // PARAMETER_TUNING
#endif

#if defined(INCLUDE_CFIFO) || defined(INCLUDE_ALL)
	for (int queues_per_thread = 2; queues_per_thread <= 8; queues_per_thread *= 2) {
		for (int stickiness = 1; stickiness <= 4096; stickiness *= 2) {
			instances.push_back(std::make_unique<benchmark_provider_cylinder<BENCHMARK>>("cfifo-{},{}", queues_per_thread, stickiness));
		}
	}
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

#if defined (__GNUC__) && (defined(INCLUDE_LCRQ) || defined(INCLUDE_ALL))
	instances.push_back(std::make_unique<benchmark_provider_generic<adapter<std::uint64_t, LCRQWrapped>, BENCHMARK>>("lcrq"));
#endif

	for (std::size_t i = 0; i < instances.size(); i++) {
		if (filter_set.contains(instances[i]->get_name()) == are_exclude_filters) {
			instances.erase(instances.begin() + i);
			i--;
		}
	}
}

#endif // CONFIG_H_INCLUDED
