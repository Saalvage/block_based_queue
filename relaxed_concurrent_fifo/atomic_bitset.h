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

template <bool SET, typename T>
constexpr bool set_bit_atomic(std::atomic<T>& data, std::size_t index, std::memory_order order = std::memory_order_seq_cst) {
    T mask = static_cast<T>(1) << index;
    if constexpr (SET) {
        return !(data.fetch_or(mask, order) & mask);
    } else {
        return data.fetch_and(~mask, order) & mask;
    }
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
    std::array<cache_aligned_t<ARR_TYPE>, array_members> data;

    // This requirement could be lifted in exchange for a more complicated implementation of the claim bit function.
    static_assert(N % bit_count == 0, "Bit count must be dividable by size of array type!");

    template <claim_value VALUE, claim_mode MODE>
    static constexpr std::size_t claim_bit_singular(std::atomic<ARR_TYPE>& data, int initial_rot, std::memory_order order) {
        auto raw = data.load(order);
        ARR_TYPE rotated;
        while (true) {
            rotated = std::rotr(raw, initial_rot);
            int counted = VALUE == claim_value::ONE ? std::countr_zero(rotated) : std::countr_one(rotated);
            if (counted == bit_count) {
                return std::numeric_limits<std::size_t>::max();
            }
            std::size_t original_index = (initial_rot + counted) % bit_count;
            if constexpr (MODE == claim_mode::READ_WRITE) {
                ARR_TYPE test;
                while (true) {
                    if constexpr (VALUE == claim_value::ONE) {
                        test = raw & ~(1ull << original_index);
                    } else {
                        test = raw | (1ull << original_index);
                    }
                    if (test == raw) {
                        break;
                    }
                    if (data.compare_exchange_weak(raw, test, order)) {
                        return original_index;
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
    constexpr bool set(std::size_t index, std::memory_order order = std::memory_order_seq_cst) {
        assert(index < size());
        return set_bit_atomic<true>(data[index / bit_count].atomic, index % bit_count, order);
    }

    /// <summary>
    /// Resets a specified bit in the bitset to 0.
    /// </summary>
    /// <param name="index">The index of the bit to reset.</param>
    /// <returns>Whether the bit has been newly reset. false means the bit had already been 0.</returns>
    constexpr bool reset(std::size_t index, std::memory_order order = std::memory_order_seq_cst) {
        assert(index < size());
        return set_bit_atomic<false>(data[index / bit_count].atomic, index % bit_count, order);
    }

    [[nodiscard]] constexpr bool test(std::size_t index, std::memory_order order = std::memory_order_seq_cst) const {
        assert(index < size());
        return data[index / bit_count]->load(order) & (1ull << (index % bit_count));
    }

    [[nodiscard]] constexpr bool operator[](std::size_t index) const {
        return test(index);
    }

    [[nodiscard]] constexpr bool any(std::memory_order order = std::memory_order_seq_cst) const {
        for (auto& elem : data) {
            if (elem->load(order)) {
                return true;
            }
        }
        return false;
    }

	constexpr void set_all(std::memory_order order = std::memory_order_seq_cst) {
		for (auto& elem : data) {
			elem->store(std::numeric_limits<ARR_TYPE>::max(), order);
		}
	}

    template <claim_value VALUE, claim_mode MODE>
    std::size_t claim_bit(int starting_bit, std::memory_order order = std::memory_order_seq_cst) {
        assert(starting_bit < size());
        int off;
        int initial_rot;
        if constexpr (array_members > 1) {
            off = starting_bit / bit_count;
            initial_rot = starting_bit % bit_count;
        } else {
            initial_rot = starting_bit;
            off = 0;
        }
        for (std::size_t i = 0; i < data.size(); i++) {
            auto index = (i + off) % data.size();
            if (auto ret = claim_bit_singular<VALUE, MODE>(data[index], initial_rot, order);
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
