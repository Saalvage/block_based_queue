#ifndef BENCHMARK_PROVIDER_OTHER_HPP_INCLUDED
#define BENCHMARK_PROVIDER_OTHER_HPP_INCLUDED

#include "benchmark_provider_generic.hpp"

#include "block_based_queue.h"
#include "contenders/scal/scal_wrapper.h"
#include "contenders/multififo/multififo.hpp"
#include "contenders/multififo/stick_random.hpp"
#include "contenders/multififo/stick_swap.hpp"
#include "contenders/multififo/stick_random_symmetric.hpp"
#include "contenders/FAAArrayQueue/wrapper.hpp"

#if defined(__GNUC__) && !(defined(__arm__) || defined(__aarch64__))
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wvariadic-macros"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wvolatile"

#include "contenders/2D/wrapper_dcbo.hpp"
#include "contenders/2D/wrapper_2D.hpp"

#pragma GCC diagnostic pop
#endif // __GNUC__

template <typename BENCHMARK>
using benchmark_provider_bbq = benchmark_provider_generic<block_based_queue<std::uint64_t>, BENCHMARK, double, std::size_t>;

template <typename BENCHMARK>
using benchmark_provider_kfifo = benchmark_provider_generic<ws_k_fifo<std::uint64_t>, BENCHMARK, double>;

template <typename BENCHMARK, int POP_CANDIDATES = 2>
using benchmark_provider_multififo = benchmark_provider_generic<multififo::MultiFifo<std::uint64_t, multififo::mode::StickRandom<POP_CANDIDATES>>, BENCHMARK, int, int>;

template <typename BENCHMARK, int POP_CANDIDATES = 2>
using benchmark_provider_multififo_swap = benchmark_provider_generic<multififo::MultiFifo<std::uint64_t, multififo::mode::StickSwap<POP_CANDIDATES>>, BENCHMARK, int, int>;

template <typename BENCHMARK, int POP_CANDIDATES = 2>
using benchmark_provider_multififo_symmetric = benchmark_provider_generic<multififo::MultiFifo<std::uint64_t, multififo::mode::StickRandomSymmetric<POP_CANDIDATES>>, BENCHMARK, int, int>;

template <typename BENCHMARK>
using benchmark_provider_faaaqueue = benchmark_provider_generic<wrapper_faaaqueue<std::uint64_t>, BENCHMARK>;

#if defined(__GNUC__) && !(defined(__arm__) || defined(__aarch64__))
template <typename BENCHMARK>
using benchmark_provider_2Dd = benchmark_provider_generic<wrapper_2Dd_queue, BENCHMARK, width_t, std::uint64_t>;

template <typename BENCHMARK>
using benchmark_provider_dcbo = benchmark_provider_generic<wrapper_dcbo_queue, BENCHMARK, double>;
#endif // __GNUC__

#endif // BENCHMARK_PROVIDER_OTHER_HPP_INCLUDED
