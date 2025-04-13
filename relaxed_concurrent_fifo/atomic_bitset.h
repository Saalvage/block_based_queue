#ifndef ATOMIC_BITSET_H_INCLUDED
#define ATOMIC_BITSET_H_INCLUDED

#include <cstdint>
#include <atomic>
#include <cassert>
#include <limits>
#include <random>

#include "utility.h"

#ifndef BITSET_DEFAULT_MEMORY_ORDER
#define BITSET_DEFAULT_MEMORY_ORDER std::memory_order_relaxed
#endif

enum class claim_value {
    ZERO = 0,
    ONE = 1,
};

enum class claim_mode {
    READ_WRITE,
    READ_ONLY,
};

template <typename ARR_TYPE = std::uint8_t>
class atomic_bitset {
private:
    static_assert(sizeof(ARR_TYPE) <= 4, "Inner bitset type must be 4 bytes or smaller to allow for storing epoch.");

#ifndef NDEBUG
    std::size_t window_count;
#endif
    std::size_t blocks_per_window;
    std::size_t units_per_window_mod_mask;

    static constexpr std::size_t bit_count = sizeof(ARR_TYPE) * 8;
    std::unique_ptr<cache_aligned_t<std::atomic<std::uint64_t>>[]> data;

    static constexpr std::uint64_t get_epoch(std::uint64_t epoch_and_bits) { return epoch_and_bits >> 32; }
    static constexpr std::uint64_t get_bits(std::uint64_t epoch_and_bits) { return epoch_and_bits & 0xffff'ffff; }
    static constexpr std::uint64_t make_unit(std::uint64_t epoch) { return epoch << 32; }

    template <bool SET>
    static constexpr void set_bit_atomic(std::atomic<std::uint64_t>& epoch_and_bits, std::size_t index, std::uint64_t epoch, std::memory_order order) {
        std::uint64_t eb = epoch_and_bits.load(order);
        std::uint64_t test;
        std::uint64_t stencil = 1ull << index;
        do {
            if (get_epoch(eb) != epoch) {
                return;
            }
            if constexpr (SET) {
                test = eb | stencil;
            } else {
                // TODO: Special case handling like this is probably bad.
                // We basically want to increment the epoch when the last filled bit has been reset.
                test = eb & ~stencil;
                if (get_bits(test) == 0) {
                    test = make_unit(epoch + 1);
                }
            }
        } while (!epoch_and_bits.compare_exchange_strong(eb, test, order));
    }

    template <claim_value VALUE, claim_mode MODE>
    static constexpr std::size_t claim_bit_singular(std::atomic<std::uint64_t>& epoch_and_bits, int initial_rot, std::uint64_t epoch, std::memory_order order) {
        std::uint64_t eb = epoch_and_bits.load(order);
        if (get_epoch(eb) != epoch) {
            return std::numeric_limits<std::size_t>::max();
        }
        while (true) {
            ARR_TYPE raw = static_cast<ARR_TYPE>(eb);
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
                    if (epoch_and_bits.compare_exchange_weak(eb,
                        VALUE == claim_value::ONE && test == 0
                            ? make_unit(epoch + 1)
                            : (make_unit(epoch) | test), order)) {
                        return original_index;
                    }
                    if (get_epoch(eb) != epoch) [[unlikely]] {
                        return std::numeric_limits<std::size_t>::max();
                    }
                    raw = static_cast<ARR_TYPE>(eb);
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
    atomic_bitset(std::size_t window_count, std::size_t blocks_per_window) :
#ifndef NDEBUG
            window_count(window_count),
#endif
            blocks_per_window(blocks_per_window),
            units_per_window_mod_mask((blocks_per_window / bit_count) - 1),
            data(std::make_unique<cache_aligned_t<std::atomic<std::uint64_t>>[]>(window_count * blocks_per_window)) {
        assert(blocks_per_window % bit_count == 0);
    }

    constexpr void set(std::size_t window_index, std::size_t index, std::uint64_t epoch, std::memory_order order = BITSET_DEFAULT_MEMORY_ORDER) {
        assert(window_index < window_count);
        assert(index < blocks_per_window);
        set_bit_atomic<true>(data[window_index * blocks_per_window + index / bit_count], index % bit_count, epoch, order);
    }

    constexpr void reset(std::size_t window_index, std::size_t index, std::uint64_t epoch, std::memory_order order = BITSET_DEFAULT_MEMORY_ORDER) {
        assert(window_index < window_count);
        assert(index < blocks_per_window);
        set_bit_atomic<false>(data[window_index * blocks_per_window + index / bit_count], index % bit_count, epoch, order);
    }

    [[nodiscard]] constexpr bool test(std::size_t window_index, std::size_t index, std::memory_order order = BITSET_DEFAULT_MEMORY_ORDER) const {
        assert(window_index < window_count);
        assert(index < blocks_per_window);
        return data[window_index * blocks_per_window + index / bit_count]->load(order) & (1ull << (index % bit_count));
    }

    [[nodiscard]] constexpr bool operator[](std::size_t index) const {
        return test(index);
    }

    [[nodiscard]] constexpr bool any(std::size_t window_index, std::uint64_t epoch, std::memory_order order = BITSET_DEFAULT_MEMORY_ORDER) const {
        for (std::size_t i = 0; i < blocks_per_window; i++) {
            std::uint64_t eb = data[window_index * blocks_per_window + i]->load(order);
            if (get_epoch(eb) == epoch && get_bits(eb)) {
                return true;
            }
        }
        return false;
    }

    void set_epoch_if_empty(std::size_t window_index, std::uint64_t epoch, std::memory_order order = BITSET_DEFAULT_MEMORY_ORDER) {
        std::uint64_t next_eb = make_unit(epoch + 1);
        for (std::size_t i = 0; i < blocks_per_window; i++) {
            std::uint64_t eb = make_unit(epoch);
            data[window_index * blocks_per_window + i]->compare_exchange_strong(eb, next_eb, order);
        }
    }

    template <claim_value VALUE, claim_mode MODE>
    std::size_t claim_bit(std::size_t window_index, int starting_bit, std::uint64_t epoch, std::memory_order order = BITSET_DEFAULT_MEMORY_ORDER) {
        assert(window_index < window_count);
        assert(static_cast<std::size_t>(starting_bit) < blocks_per_window);
        int off = starting_bit / bit_count;
        int initial_rot = starting_bit % bit_count;
        for (std::size_t i = 0; i < blocks_per_window; i++) {
            auto index = (i + off) & units_per_window_mod_mask;
            if (auto ret = claim_bit_singular<VALUE, MODE>(data[window_index * blocks_per_window + index], initial_rot, epoch, order);
                    ret != std::numeric_limits<std::size_t>::max()) {
                return ret + index * bit_count;
            }
        }
        return std::numeric_limits<std::size_t>::max();
    }
};

#endif // ATOMIC_BITSET_H_INCLUDED
