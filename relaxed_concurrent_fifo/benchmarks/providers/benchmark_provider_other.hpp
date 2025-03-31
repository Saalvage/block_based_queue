#ifndef BENCHMARK_PROVIDER_OTHER_HPP_INCLUDED
#define BENCHMARK_PROVIDER_OTHER_HPP_INCLUDED

#include "benchmark_provider_generic.hpp"

#include "../../contenders/scal/scal_wrapper.h"
#include "../../contenders/multififo/multififo.hpp"
#include "../../cylinder_fifo.hpp"

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wvariadic-macros"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wvolatile"

#include "../../contenders/2D/wrapper_dcbo.hpp"
#include "../../contenders/2D/wrapper_2D.hpp"

#pragma GCC diagnostic pop
#endif // __GNUC__

template <typename BENCHMARK>
using benchmark_provider_ws_kfifo = benchmark_provider_generic<ws_k_fifo<std::uint64_t>, BENCHMARK, std::size_t>;

template <typename BENCHMARK>
using benchmark_provider_ss_kfifo = benchmark_provider_generic<ss_k_fifo<std::uint64_t>, BENCHMARK, std::size_t>;

template <typename BENCHMARK>
using benchmark_provider_multififo = benchmark_provider_generic<multififo::MultiFifo<std::uint64_t>, BENCHMARK, int, int>;

template <typename BENCHMARK>
using benchmark_provider_cylinder = benchmark_provider_generic<cylinder_fifo<std::uint64_t>, BENCHMARK, int, int>;

#ifdef __GNUC__
template <typename BENCHMARK>
using benchmark_provider_2Dd = benchmark_provider_generic<wrapper_2Dd_queue, BENCHMARK, width_t, std::uint64_t>;

template <typename BENCHMARK>
using benchmark_provider_dcbo = benchmark_provider_generic<wrapper_dcbo_queue, BENCHMARK, std::uint32_t>;
#endif // __GNUC__

#endif // BENCHMARK_PROVIDER_OTHER_HPP_INCLUDED
