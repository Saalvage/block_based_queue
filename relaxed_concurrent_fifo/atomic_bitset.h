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

    std::size_t superblocks_per_window;
    std::size_t superblocks_per_window_mod_mask;
    std::size_t superblock_count_mod_mask;
    std::size_t superblock_count_log2;

    static constexpr std::size_t bit_count = sizeof(ARR_TYPE) * 8;
    std::unique_ptr<cache_aligned_t<std::atomic<std::uint64_t>>[]> data;

    static constexpr std::uint64_t get_epoch(std::uint64_t epoch_and_bits) { return epoch_and_bits >> 32; }
    static constexpr std::uint64_t get_bits(std::uint64_t epoch_and_bits) { return epoch_and_bits & 0xffff'ffff; }
    static constexpr std::uint64_t make_unit(std::uint64_t epoch) { return epoch << 32; }

    template <bool SET>
    static constexpr bool set_bit_atomic(std::atomic<std::uint64_t>& epoch_and_bits, std::size_t index, std::uint64_t epoch, std::memory_order order) {
        std::uint64_t eb = epoch_and_bits.load(order);
        std::uint64_t test;
        std::uint64_t stencil = 1ull << index;
        bool ret = false;
        do {
            if (get_epoch(eb) != epoch) {
                return false;
            }
            if constexpr (SET) {
                test = eb | stencil;
            } else {
                // TODO: Special case handling like this is probably bad.
                // We basically want to increment the epoch when the last filled bit has been reset.
                test = eb & ~stencil;
                if (!SET && get_bits(test) == 0) {
                    test = make_unit(epoch + 1);
                    ret = true;
                }
            }
        } while (!epoch_and_bits.compare_exchange_strong(eb, test, order));
        return ret;
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
                    // If we ever claim ones with READ_WRITE we need to update the epoch here.
                    // (And let the window slide.)
                    assert(VALUE != claim_value::ONE);
                    if (epoch_and_bits.compare_exchange_weak(eb,
                            make_unit(epoch) | test, order)) {
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
    atomic_bitset(std::size_t block_count, std::size_t window_size) :
            superblocks_per_window(window_size / bit_count),
            superblocks_per_window_mod_mask((window_size / bit_count) - 1),
            superblock_count_mod_mask((block_count / bit_count) - 1),
            superblock_count_log2(std::bit_width(block_count / bit_count) - 1),
            data(std::make_unique<cache_aligned_t<std::atomic<std::uint64_t>>[]>(block_count / bit_count)) {
        assert(block_count % bit_count == 0);
    }

    constexpr bool reset(std::size_t superblock_index, std::size_t index, std::uint64_t epoch, std::memory_order order = BITSET_DEFAULT_MEMORY_ORDER) {
        assert(index < bit_count);
        return set_bit_atomic<false>(data[superblock_index & superblock_count_mod_mask], index, epoch, order);
    }

    void set_epoch_if_empty(std::size_t superblock_index, std::memory_order order = BITSET_DEFAULT_MEMORY_ORDER) {
        std::uint64_t epoch = superblock_index >> superblock_count_log2;
        std::uint64_t eb = make_unit(epoch);
        data[superblock_index & superblock_count_mod_mask]->compare_exchange_strong(eb, make_unit(epoch + 1), order);
    }

    template <claim_value VALUE, claim_mode MODE>
    std::pair<std::size_t, bool> claim_bit(std::size_t superblock_index, int starting_bit, std::memory_order order = BITSET_DEFAULT_MEMORY_ORDER) {
        assert(static_cast<std::size_t>(starting_bit) < superblocks_per_window * bit_count);
        int off = starting_bit / bit_count;
        int initial_rot = starting_bit % bit_count;
        bool should_advance = false;
        for (std::size_t i = 0; i < superblocks_per_window; i++) {
            auto index = (i + off) & superblocks_per_window_mod_mask;
            auto total_index = superblock_index + index;
            if (auto ret = claim_bit_singular<VALUE, MODE>(data[total_index & superblock_count_mod_mask], initial_rot, total_index >> superblock_count_log2, order);
                    ret != std::numeric_limits<std::size_t>::max()) {
                return { ret + total_index * bit_count, should_advance };
            } else {
                should_advance |= index == 0;
            }
        }
        return { std::numeric_limits<std::size_t>::max(), true };
    }
};

#endif // ATOMIC_BITSET_H_INCLUDED
