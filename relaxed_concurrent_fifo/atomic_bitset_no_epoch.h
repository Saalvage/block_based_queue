#ifndef ATOMIC_BITSET_NO_EPOCH_H_INCLUDED
#define ATOMIC_BITSET_NO_EPOCH_H_INCLUDED

#include "atomic_bitset.h"

#include <cstdint>
#include <array>
#include <atomic>
#include <cassert>
#include <limits>
#include <random>

#include "utility.h"

template <typename ARR_TYPE = std::uint8_t>
class atomic_bitset_no_epoch {
private:
#ifndef NDEBUG
    std::size_t window_count;
#endif
    std::size_t blocks_per_window;
    std::size_t units_per_window_mod_mask;

    static constexpr std::size_t bit_count = sizeof(ARR_TYPE) * 8;
    cache_aligned_t<std::atomic<ARR_TYPE>>* data;

    template <bool SET>
    static constexpr void set_bit_atomic(std::atomic<ARR_TYPE>& bits, std::size_t index, std::memory_order order) {
        ARR_TYPE mask = static_cast<ARR_TYPE>(1) << index;
        if constexpr (SET) {
            bits.fetch_or(mask, order);
        } else {
            bits.fetch_and(~mask, order);
        }
    }

    template <claim_value VALUE, claim_mode MODE>
    static constexpr std::size_t claim_bit_singular(std::atomic<ARR_TYPE>& bits, int initial_rot, std::memory_order order) {
        ARR_TYPE raw = bits.load(order);
        while (true) {
            ARR_TYPE rotated = std::rotr(raw, initial_rot);
            int counted = VALUE == claim_value::ONE ? std::countr_zero(rotated) : std::countr_one(rotated);
            if (counted == bit_count) {
                return std::numeric_limits<std::size_t>::max();
            }
            std::size_t original_index = (initial_rot + counted) % bit_count;
            if constexpr (MODE == claim_mode::READ_WRITE) {
                ARR_TYPE test;
                if constexpr (VALUE == claim_value::ONE) {
                    test = raw & ~(1ull << original_index);
                } else {
                    test = raw | (1ull << original_index);
                }
                // Keep retrying until the bit we are trying to claim has changed.
                while (true) {
                    if (bits.compare_exchange_weak(raw, test, order)) {
                        return original_index;
                    }
                    if constexpr (VALUE == claim_value::ONE) {
                        test = raw & ~(1ull << original_index);
                    } else {
                        test = raw | (1ull << original_index);
                    }
                    if (test == raw) [[unlikely]] {
                        break;
                    }
                }
            } else {
                return original_index;
            }
        }
    }

public:
    atomic_bitset_no_epoch() = default;
    atomic_bitset_no_epoch(std::size_t window_count, std::size_t blocks_per_window, cache_aligned_t<std::atomic<ARR_TYPE>>* data) :
#ifndef NDEBUG
            window_count(window_count),
#endif
            blocks_per_window(blocks_per_window),
            units_per_window_mod_mask((blocks_per_window / bit_count) - 1),
            data(data) {
        assert(blocks_per_window % bit_count == 0);
    }

    constexpr void set(std::size_t window_index, std::size_t index, std::memory_order order = BITSET_DEFAULT_MEMORY_ORDER) {
        assert(window_index < window_count);
        assert(index < blocks_per_window);
        set_bit_atomic<true>(data[window_index * blocks_per_window / bit_count + index / bit_count], index % bit_count, order);
    }

    constexpr void reset(std::size_t window_index, std::size_t index, std::memory_order order = BITSET_DEFAULT_MEMORY_ORDER) {
        assert(window_index < window_count);
        assert(index < blocks_per_window);
        set_bit_atomic<false>(data[window_index * blocks_per_window / bit_count + index / bit_count], index % bit_count, order);
    }

    template <claim_value VALUE, claim_mode MODE>
    std::size_t claim_bit(std::size_t window_index, int starting_bit, std::memory_order order = BITSET_DEFAULT_MEMORY_ORDER) {
        assert(window_index < window_count);
        assert(static_cast<std::size_t>(starting_bit) < blocks_per_window);
        int off = starting_bit / bit_count;
        int initial_rot = starting_bit % bit_count;
        for (std::size_t i = 0; i < blocks_per_window; i++) {
            auto index = (i + off) & units_per_window_mod_mask;
            if (auto ret = claim_bit_singular<VALUE, MODE>(data[window_index * blocks_per_window / bit_count + index], initial_rot, order);
                    ret != std::numeric_limits<std::size_t>::max()) {
                return ret + index * bit_count;
            }
        }
        return std::numeric_limits<std::size_t>::max();
    }
};

#endif // ATOMIC_BITSET_NO_EPOCH_H_INCLUDED
