#ifndef ATOMIC_BITSET_H_INCLUDED
#define ATOMIC_BITSET_H_INCLUDED

#include <cstdint>
#include <array>
#include <atomic>
#include <cassert>
#include <limits>
#include <random>

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winterference-size"
#endif // __GNUC__

enum class claim_value {
    ZERO = 0,
    ONE = 1,
};

enum class claim_mode {
    READ_WRITE,
    READ_ONLY,
};

template <typename T>
struct alignas(std::hardware_destructive_interference_size) cache_aligned_t {
    std::atomic<T> atomic;
    std::atomic<T>* operator->() { return &atomic; }
    const std::atomic<T>* operator->() const { return &atomic; }
    operator std::atomic<T>& () { return atomic; }
    operator const std::atomic<T>& () const { return atomic; }
};

template <std::size_t N, typename ARR_TYPE = std::uint8_t>
class atomic_bitset {
private:
    static_assert(sizeof(ARR_TYPE) < 4, "Inner bitset type must be smaller than 4 bytes to allow for storing epoch.");

    static constexpr std::size_t bit_count = sizeof(ARR_TYPE) * 8;
    static constexpr std::uint64_t epoch_mask = ~((1ull << bit_count) - 1);
    static constexpr std::size_t array_members = N / bit_count;
    std::array<cache_aligned_t<std::uint64_t>, array_members> data;

    // This requirement could be lifted in exchange for a more complicated implementation of the claim bit function.
    static_assert(N % bit_count == 0, "Bit count must be divisible by size of array type!");

    static constexpr std::uint64_t mask_epoch(std::uint64_t epoch) {
        return epoch << bit_count;
    }

    template <bool SET>
    static constexpr void set_bit_atomic(std::atomic<std::uint64_t>& epoch_and_bits, std::size_t index, std::uint64_t epoch_masked, std::memory_order order = std::memory_order_seq_cst) {
        std::uint64_t eb = epoch_and_bits.load(order);
        std::uint64_t test;
        std::uint64_t stencil = 1ull << index;
        do {
            if ((eb & epoch_mask) != epoch_masked) {
                return;
            }
            if constexpr (SET) {
                test = eb | stencil;
            } else {
                // TODO: Special case handling like this is probably bad.
                // We basically want to increment the epoch when the last filled bit has been reset.
                test = eb & ~stencil;
                if ((test & ~epoch_mask) == 0) {
                    test = epoch_masked + mask_epoch(1);
                }
            }
        } while (!epoch_and_bits.compare_exchange_strong(eb, test, order));
    }

    template <claim_value VALUE, claim_mode MODE>
    static constexpr std::size_t claim_bit_singular(std::atomic<std::uint64_t>& epoch_and_bits, int initial_rot, std::uint64_t epoch_masked, std::memory_order order) {
        auto eb = epoch_and_bits.load(order);
        if ((eb & epoch_mask) != epoch_masked) {
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
                    if (epoch_and_bits.compare_exchange_weak(eb, epoch_masked | test, order)) {
                        return original_index;
                    }
                    if ((eb & epoch_mask) != epoch_masked) [[unlikely]] {
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
    [[nodiscard]] static constexpr std::size_t size() { return N; }

    /// <summary>
    /// Sets a specified bit in the bitset to 1.
    /// </summary>
    /// <param name="index">The index of the bit to set.</param>
    /// <returns>Whether the bit has been newly set. false means the bit had already been 1.</returns>
    constexpr void set(std::size_t index, std::uint64_t epoch, std::memory_order order = std::memory_order_seq_cst) {
        assert(index < size());
        set_bit_atomic<true>(data[index / bit_count].atomic, index % bit_count, mask_epoch(epoch), order);
    }

    /// <summary>
    /// Resets a specified bit in the bitset to 0.
    /// </summary>
    /// <param name="index">The index of the bit to reset.</param>
    /// <returns>Whether the bit has been newly reset. false means the bit had already been 0.</returns>
    constexpr void reset(std::size_t index, std::uint64_t epoch, std::memory_order order = std::memory_order_seq_cst) {
        assert(index < size());
        set_bit_atomic<false>(data[index / bit_count].atomic, index % bit_count, mask_epoch(epoch), order);
    }

    [[nodiscard]] constexpr bool test(std::size_t index, std::memory_order order = std::memory_order_seq_cst) const {
        assert(index < size());
        return data[index / bit_count]->load(order) & (1ull << (index % bit_count));
    }

    [[nodiscard]] constexpr bool operator[](std::size_t index) const {
        return test(index);
    }

    [[nodiscard]] constexpr bool any(std::uint64_t epoch, std::memory_order order = std::memory_order_seq_cst) const {
        std::uint64_t epoch_masked = mask_epoch(epoch);
        for (auto& elem : data) {
            std::uint64_t ep = elem->load(order);
            if ((ep & epoch_mask) == epoch_masked && (ep & ~epoch_mask)) {
                return true;
            }
        }
        return false;
    }

    void set_epoch_if_empty(std::uint64_t epoch, std::uint64_t next_epoch, std::memory_order order = std::memory_order_seq_cst) {
        std::uint64_t next_epoch_masked = mask_epoch(next_epoch);
        for (auto& elem : data) {
            std::uint64_t epoch_masked = mask_epoch(epoch);
            elem->compare_exchange_strong(epoch_masked, next_epoch_masked, order);
        }
    }

    template <claim_value VALUE, claim_mode MODE>
    std::size_t claim_bit(int starting_bit, std::uint64_t epoch, std::memory_order order = std::memory_order_seq_cst) {
        assert(starting_bit < size());
        int off;
        int initial_rot;
        std::uint64_t epoch_masked = mask_epoch(epoch);
        if constexpr (array_members > 1) {
            off = starting_bit / bit_count;
            initial_rot = starting_bit % bit_count;
        } else {
            initial_rot = starting_bit;
            off = 0;
        }
        for (std::size_t i = 0; i < data.size(); i++) {
            auto index = (i + off) % data.size();
            if (auto ret = claim_bit_singular<VALUE, MODE>(data[index], initial_rot, epoch_masked, order);
                    ret != std::numeric_limits<std::size_t>::max()) {
                return ret + index * bit_count;
            }
        }
        return std::numeric_limits<std::size_t>::max();
    }
};

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif // __GNUC__

#endif // ATOMIC_BITSET_H_INCLUDED
