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

template <bool SET>
constexpr void set_bit_atomic(std::atomic<std::uint64_t>& data, std::size_t index, std::uint64_t epoch, std::memory_order order = std::memory_order_seq_cst) {
    std::uint64_t ed = data.load(order);
    std::uint64_t test;
    std::uint64_t mask = 1ull << index;
    do {
        if ((ed & 0xffff'ffff'ffff'ff00ull) != epoch) {
            return;
        }
        if constexpr (SET) {
            test = ed | mask;
        } else {
            // TODO: Special case handling like this is probably bad.
            // We basically want to increment the epoch when the last filled bit has been reset.
            test = ed & ~mask;
            if (static_cast<std::uint8_t>(test) == 0) {
                test = epoch + (1 << 8);
            }
        }
    } while (!data.compare_exchange_strong(ed, test, order));
}

template <typename T>
struct cache_aligned_t {
    alignas(std::hardware_destructive_interference_size) std::atomic<T> atomic;
    std::atomic<T>* operator->() { return &atomic; }
    const std::atomic<T>* operator->() const { return &atomic; }
    operator std::atomic<T>& () { return atomic; }
    operator const std::atomic<T>& () const { return atomic; }
};

template <std::size_t N, typename ARR_TYPE = uint8_t>
class atomic_bitset {
private:
    static constexpr std::size_t bit_count = sizeof(ARR_TYPE) * 8;
    static constexpr std::size_t array_members = N / bit_count;
    std::array<cache_aligned_t<std::uint64_t>, array_members> data;

    // This requirement could be lifted in exchange for a more complicated implementation of the claim bit function.
    static_assert(N % bit_count == 0, "Bit count must be dividable by size of array type!");

    template <claim_value VALUE, claim_mode MODE>
    static constexpr std::size_t claim_bit_singular(std::atomic<std::uint64_t>& data, int initial_rot, std::uint64_t epoch_masked, std::memory_order order) {
        auto eb = data.load(order);
        while (true) {
            if ((eb & 0xffff'ffff'ffff'ff00ull) != epoch_masked) {
                return std::numeric_limits<std::size_t>::max();
            }
            auto raw = static_cast<std::uint8_t>(eb);
            ARR_TYPE rotated = std::rotr(raw, initial_rot);
            int counted = VALUE == claim_value::ONE ? std::countr_zero(rotated) : std::countr_one(rotated);
            if (counted == bit_count) {
                return std::numeric_limits<std::size_t>::max();
            }
            std::size_t original_index = (initial_rot + counted) % bit_count;
            if constexpr (MODE == claim_mode::READ_WRITE) {
                ARR_TYPE test;
                // Keep retrying until the bit we are trying to claim has changed.
                while (true) {
                    if constexpr (VALUE == claim_value::ONE) {
                        test = raw & ~(1ull << original_index);
                    } else {
                        test = raw | (1ull << original_index);
                    }
                    if (test == raw) {
                        break;
                    }
                    if (data.compare_exchange_weak(eb, epoch_masked | test, order)) {
                        return original_index;
                    }
                    raw = static_cast<std::uint8_t>(eb);
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
        set_bit_atomic<true>(data[index / bit_count].atomic, index % bit_count, epoch << 8, order);
    }

    /// <summary>
    /// Resets a specified bit in the bitset to 0.
    /// </summary>
    /// <param name="index">The index of the bit to reset.</param>
    /// <returns>Whether the bit has been newly reset. false means the bit had already been 0.</returns>
    constexpr void reset(std::size_t index, std::uint64_t epoch, std::memory_order order = std::memory_order_seq_cst) {
        assert(index < size());
        set_bit_atomic<false>(data[index / bit_count].atomic, index % bit_count, epoch << 8, order);
    }

    [[nodiscard]] constexpr bool test(std::size_t index, std::memory_order order = std::memory_order_seq_cst) const {
        assert(index < size());
        return data[index / bit_count]->load(order) & (1ull << (index % bit_count));
    }

    [[nodiscard]] constexpr bool operator[](std::size_t index) const {
        return test(index);
    }

    [[nodiscard]] constexpr bool any(std::uint64_t epoch, std::memory_order order = std::memory_order_seq_cst) const {
        std::uint64_t epoch_masked = epoch << 8;
        for (auto& elem : data) {
            std::uint64_t ep = elem->load(order);
            if ((ep & 0xffff'ffff'ffff'ff00ull) == epoch_masked && (ep & 0xff)) {
                return true;
            }
        }
        return false;
    }

    void set_epoch_if_empty(std::uint64_t epoch, std::uint64_t next_epoch, std::memory_order order = std::memory_order_seq_cst) {
		std::uint64_t next_epoch_masked = next_epoch << 8;
        for (auto& elem : data) {
            std::uint64_t epoch_masked = epoch << 8;
            elem->compare_exchange_strong(epoch_masked, next_epoch_masked, order);
        }
    }

    template <claim_value VALUE, claim_mode MODE>
    std::size_t claim_bit(int starting_bit, std::uint64_t epoch, std::memory_order order = std::memory_order_seq_cst) {
        assert(starting_bit < size());
        int off;
        int initial_rot;
        std::uint64_t epoch_mask = epoch << 8;
        if constexpr (array_members > 1) {
            off = starting_bit / bit_count;
            initial_rot = starting_bit % bit_count;
        } else {
            initial_rot = starting_bit;
            off = 0;
        }
        for (std::size_t i = 0; i < data.size(); i++) {
            auto index = (i + off) % data.size();
            if (auto ret = claim_bit_singular<VALUE, MODE>(data[index], initial_rot, epoch_mask, order);
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
