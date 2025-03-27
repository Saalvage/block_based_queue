#ifndef BENCHMARK_PROVIDER_BBQ_HPP_INCLUDED
#define BENCHMARK_PROVIDER_BBQ_HPP_INCLUDED

#include "benchmark_provider_base.hpp"

#include "../../block_based_queue.h"

template <typename BENCHMARK, std::size_t BLOCK_MULTIPLIER, std::size_t CELLS_PER_BLOCK, typename BITSET_TYPE = uint8_t>
class benchmark_provider_bbq : public benchmark_provider<BENCHMARK> {
    public:
        benchmark_provider_bbq(std::string name) : name(std::move(name)) { }

        const std::string& get_name() const override {
            return name;
        }

        BENCHMARK test(const benchmark_info& info, double prefill_amount) const override {
            switch (info.num_threads) {
            case 1: return test_helper<bbq_min_block_count<std::size_t, 1 * BLOCK_MULTIPLIER, CELLS_PER_BLOCK, BITSET_TYPE>>(info, prefill_amount);
            case 2: return test_helper<bbq_min_block_count<std::size_t, 2 * BLOCK_MULTIPLIER, CELLS_PER_BLOCK, BITSET_TYPE>>(info, prefill_amount);
            case 4: return test_helper<bbq_min_block_count<std::size_t, 4 * BLOCK_MULTIPLIER, CELLS_PER_BLOCK, BITSET_TYPE>>(info, prefill_amount);
            case 8: return test_helper<bbq_min_block_count<std::size_t, 8 * BLOCK_MULTIPLIER, CELLS_PER_BLOCK, BITSET_TYPE>>(info, prefill_amount);
            case 16: return test_helper<bbq_min_block_count<std::size_t, 16 * BLOCK_MULTIPLIER, CELLS_PER_BLOCK, BITSET_TYPE>>(info, prefill_amount);
            case 32: return test_helper<bbq_min_block_count<std::size_t, 32 * BLOCK_MULTIPLIER, CELLS_PER_BLOCK, BITSET_TYPE>>(info, prefill_amount);
            case 64: return test_helper<bbq_min_block_count<std::size_t, 64 * BLOCK_MULTIPLIER, CELLS_PER_BLOCK, BITSET_TYPE>>(info, prefill_amount);
            case 128: return test_helper<bbq_min_block_count<std::size_t, 128 * BLOCK_MULTIPLIER, CELLS_PER_BLOCK, BITSET_TYPE>>(info, prefill_amount);
            case 256: return test_helper<bbq_min_block_count<std::size_t, 256 * BLOCK_MULTIPLIER, CELLS_PER_BLOCK, BITSET_TYPE>>(info, prefill_amount);
            default: throw std::runtime_error("Unsupported thread count!");
            }
        }

    private:
        std::string name;

        template <typename FIFO>
        static BENCHMARK test_helper(const benchmark_info& info, double prefill_amount) {
            FIFO fifo{ info.num_threads, BENCHMARK::SIZE };
            return benchmark_provider<BENCHMARK>::template test_single(fifo, info, prefill_amount);
        }
};

#endif // BENCHMARK_PROVIDER_BBQ_HPP_INCLUDED
