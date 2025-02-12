#ifndef CONFIG_H_INCLUDED
#define CONFIG_H_INCLUDED

#include <vector>
#include <memory>
#include <unordered_set>

#include "benchmark.h"

// By default, include all.
#if !defined(INCLUDE_BBQ) \
	&& !defined(INCLUDE_MULTIFIFO) \
	&& !defined(INCLUDE_LCRQ) \
	&& !defined(INCLUDE_CYLINDER) \
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
	instances.push_back(std::make_unique<benchmark_provider_cylinder<BENCHMARK>>("cfifo-8-256", 8, 256));
#endif

#if defined(__GNUC__) && (defined(INCLUDE_MULTIFIFO) || defined(INCLUDE_ALL))
#ifdef PARAMETER_TUNING
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
#else // PARAMETER_TUNING
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("2-2-multififo", 2, 2));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("4-16-multififo", 4, 16));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("4-128-multififo", 4, 128));
	instances.push_back(std::make_unique<benchmark_provider_multififo<BENCHMARK>>("4-256-multififo", 4, 256));
#endif // PARAMETER_TUNING
#endif

#if defined (__GNUC__) && (defined(INCLUDE_KFIFO) || defined(INCLUDE_ALL))
#ifdef PARAMETER_TUNING
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
#else // PARAMETER_TUNING
	instances.push_back(std::make_unique<benchmark_provider_ws_kfifo<BENCHMARK>>("ws-1-kfifo", 1));
	instances.push_back(std::make_unique<benchmark_provider_ss_kfifo<BENCHMARK>>("ss-512-kfifo", 512));
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
