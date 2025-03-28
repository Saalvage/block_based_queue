#ifndef BENCHMARK_PROVIDER_OTHER_HPP_INCLUDED
#define BENCHMARK_PROVIDER_OTHER_HPP_INCLUDED

#include "benchmark_provider_generic.hpp"

#include "../../contenders/scal/scal_wrapper.h"
#include "../../contenders/multififo/multififo.hpp"
#include "../../cylinder_fifo.hpp"

template <typename BENCHMARK>
using benchmark_provider_ws_kfifo = benchmark_provider_generic<ws_k_fifo<std::uint64_t>, BENCHMARK, std::size_t>;

template <typename BENCHMARK>
using benchmark_provider_ss_kfifo = benchmark_provider_generic<ss_k_fifo<std::uint64_t>, BENCHMARK, std::size_t>;

template <typename BENCHMARK>
using benchmark_provider_multififo = benchmark_provider_generic<multififo::MultiFifo<std::uint64_t>, BENCHMARK, int, int>;

template <typename BENCHMARK>
using benchmark_provider_cylinder = benchmark_provider_generic<cylinder_fifo<std::uint64_t>, BENCHMARK, int, int>;

#endif // BENCHMARK_PROVIDER_OTHER_HPP_INCLUDED
