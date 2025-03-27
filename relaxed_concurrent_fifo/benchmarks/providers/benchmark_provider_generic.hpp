#ifndef BENCHMARK_PROVIDER_GENERIC_HPP_INCLUDED
#define BENCHMARK_PROVIDER_GENERIC_HPP_INCLUDED

#include "benchmark_provider_base.hpp"

#include <format>

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

#endif // BENCHMARK_PROVIDER_GENERIC_HPP_INCLUDED
