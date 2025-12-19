#ifndef ATOMIC_BINARY_TREE_H_INCLUDED
#define ATOMIC_BINARY_TREE_H_INCLUDED

#include <atomic>

#include <immintrin.h>

#include "atomic_bitset.h"
#include "epoch_handling.hpp"

template <typename ARR_TYPE = std::uint8_t, epoch_handling EPOCH = default_epoch_handling>
struct atomic_bit_tree {
private:
	static_assert(sizeof(ARR_TYPE) <= 4, "Inner bitset type must be 4 bytes or smaller to allow for storing epoch.");

	std::size_t leaves_per_window;
	std::size_t fragments_per_window;
	// TODO: int or std::size_t?
	int leaves_start_index;

	static constexpr std::size_t bit_count = sizeof(ARR_TYPE) * 8;
	std::unique_ptr<cache_aligned_t<std::atomic<std::uint64_t>>[]> data;

	template <claim_value VALUE>
	constexpr bool has_valid_bit(std::uint64_t eb) {
		// TODO: Using the double-epochs this can likely be avoided by always assuming 1 = desired and flipping the semantic accordingly when incrementing the epoch.
		// This is true except for when there is no epoch handling and as such no decider for the semantic.
		auto bits = get_bits(eb);
		if constexpr (VALUE == claim_value::ZERO) {
			bits = ~bits;
		}
		return static_cast<ARR_TYPE>(bits & (eb >> bit_count));
	}

	constexpr std::uint64_t get_bits(std::uint64_t eb) {
		return eb & ((1 << bit_count) - 1);
	}

	template <claim_value VALUE>
	ARR_TYPE modify(std::uint64_t value, int bit_idx) {
		ARR_TYPE raw = static_cast<ARR_TYPE>(value);
		if constexpr (VALUE == claim_value::ONE) {
			return raw & ~(1ull << bit_idx);
		} else {
			return raw | (1ull << bit_idx);
		}
	}

	template <claim_value VALUE>
	std::pair<bool, bool> try_change_bit(std::uint64_t epoch, std::atomic_uint64_t& leaf, std::uint64_t& leaf_val, int bit_idx, std::memory_order order) {
		ARR_TYPE target = static_cast<ARR_TYPE>(leaf_val >> bit_count);
		std::uint64_t valid_mask = target << bit_count;
		ARR_TYPE modified = modify<VALUE>(leaf_val, bit_idx);
		// TODO: These conditions are not always needed.
		while (modified != get_bits(leaf_val) && compare_epoch<VALUE>(leaf_val, epoch)) {
			bool advanced_epoch = modified == static_cast<ARR_TYPE>(VALUE == claim_value::ONE ? 0 : target);
			if (leaf.compare_exchange_strong(leaf_val, advanced_epoch
				? (EPOCH::make_unit(epoch + 1) | valid_mask | (VALUE == claim_value::ONE ? 0 : target))
				: (EPOCH::make_unit(epoch) | valid_mask | modified), order)) {
				return {true, advanced_epoch};
			}
			modified = modify<VALUE>(leaf_val, bit_idx);
		}
		return {false, false};
	}

	static inline thread_local std::minstd_rand rng{std::random_device()()};

	template <claim_value VALUE>
	static int select_random_bit_index(std::uint64_t value) {
		//unsigned value32 = value;
		//return VALUE == claim_value::ZERO ? std::countr_one(value32) : std::countr_zero(value32);

		ARR_TYPE bits = static_cast<ARR_TYPE>(value);

		// TODO: Don't randomize? (FIFO semantic on fragment level??)
		if constexpr (VALUE == claim_value::ZERO) {
			bits = ~bits;
		}

		bits = (value >> bit_count) & bits;

		assert(bits);

		auto valid_bits = std::popcount(bits);
		auto nth_bit = std::uniform_int_distribution<>{0, valid_bits - 1}(rng);
		return std::countr_zero(_pdep_u32(1 << nth_bit, bits));
	}

	template <claim_value VALUE, claim_mode MODE>
	std::size_t claim_bit_singular(cache_aligned_t<std::atomic<std::uint64_t>>* root, int starting_bit, std::uint64_t epoch, std::memory_order order = BITSET_DEFAULT_MEMORY_ORDER) {
		int off = starting_bit / bit_count;
		// TODO: Rotate.
		//int initial_rot = starting_bit % bit_count;
		auto idx = leaves_start_index + off;
		auto* leaf = &root[idx];
		auto leaf_val = leaf->value.load(order);

		bool success = false;
		std::size_t ret = 0;
		do {
			// TODO: Potentially directly use countl_xxx here to avoid it later?
			// TODO: Epoch check more explicit (+1).
			while (idx > 0 && !compare_epoch<VALUE>(leaf_val, epoch)) {
				idx = get_parent(idx);
				leaf = &root[idx];
				leaf_val = leaf->value.load(order);
				// TODO: Automatically fix parent here if child is erroneously marked?
			}

			if (!compare_epoch<VALUE>(leaf_val, epoch)) {
				// Root is invalid as well.
				return std::numeric_limits<std::size_t>::max();
			}

			bool advanced_epoch = false;
			while (idx < leaves_start_index) {
				idx = get_random_child<VALUE>(leaf_val, idx);
				leaf = &root[idx];
				leaf_val = leaf->value.load(order);
				if (!compare_epoch<VALUE>(leaf_val, epoch)) {
					advanced_epoch = true;
					break;
				}
			}

			// Skip if we didn't find a leaf but stepped into an invalid node.
			if (!advanced_epoch) {
				do {
					auto bit_idx = select_random_bit_index<VALUE>(leaf_val);
					ret = (idx - leaves_start_index) * bit_count + bit_idx;
					if constexpr (MODE == claim_mode::READ_ONLY) {
						return ret;
					}
					auto bit_change_ret = try_change_bit<VALUE>(epoch, *leaf, leaf_val, bit_idx, order);
					success = bit_change_ret.first;
					advanced_epoch = bit_change_ret.second;
					// TODO: This check is already done in try_change_bit, try merging it.
					if (!compare_epoch<VALUE>(leaf_val, epoch)) {
						// Leaf empty, need to move up again.
						advanced_epoch = true;
						break;
					}
				} while (!success);
			}

			while (advanced_epoch && idx > 0) {
				// idx = bit_count * parent + child_idx + 1
				int child_idx = idx - 1 - get_parent(idx) * bit_count;
				idx = get_parent(idx);
				leaf = &root[idx];
				leaf_val = leaf->value.load(order);
				auto bit_change_ret = try_change_bit<VALUE>(epoch, *leaf, leaf_val, child_idx, order);
				advanced_epoch = bit_change_ret.second;
				// TODO: Set idx to restart?
			}
		} while (!success);
		return ret;
	}

	int get_parent(int index) {
		return (index - 1) / bit_count;
	}

	template <claim_value VALUE>
	int get_random_child(std::uint64_t node, int index) {
		auto offset = select_random_bit_index<VALUE>(node);
		return index * bit_count + offset + 1;
	}

	template <claim_value VALUE>
	bool compare_epoch(std::uint64_t eb, std::uint64_t epoch) {
		if constexpr (EPOCH::uses_epochs) {
			return EPOCH::compare_epochs(eb, epoch);
		} else {
			return has_valid_bit<VALUE>(eb);
		}
	}

	template <claim_value VALUE>
	void change_bit(std::size_t window_index, std::size_t index, std::uint64_t epoch, std::memory_order order = BITSET_DEFAULT_MEMORY_ORDER) {
		//assert(window_index < window_count);
		//assert(index < blocks_per_window);
		int idx = leaves_start_index + static_cast<int>(index / bit_count);
		auto root = &data[window_index * fragments_per_window];
		auto* leaf = &root[idx];
		auto leaf_val = leaf->value.load(order);
		auto [success, advanced_epoch] = try_change_bit<VALUE>(epoch, *leaf, leaf_val, index % bit_count, order);
		while (advanced_epoch && idx > 0) {
			// idx = bit_count * parent + child_idx + 1
			int child_idx = idx - 1 - get_parent(idx) * bit_count;
			idx = get_parent(idx);
			leaf = &root[idx];
			leaf_val = leaf->value.load(order);
			auto bit_change_ret = try_change_bit<VALUE>(epoch, *leaf, leaf_val, child_idx, order);
			advanced_epoch = bit_change_ret.second;
		}
	}

public:
	atomic_bit_tree(std::size_t window_count, std::size_t blocks_per_window) :
		leaves_per_window(blocks_per_window / bit_count) {
		// TODO: This restriction can be ever so slighty weakened (6 top level bits also work).
		assert(std::has_single_bit(blocks_per_window));
		auto bits_per_level = std::bit_width(bit_count) - 1;
		auto bits = std::bit_width(leaves_per_window) - 1;
		auto rounded_up_bits = bits + bits_per_level - 1;
		auto bits_required_in_top_level = 2 << (rounded_up_bits % bits_per_level);
		auto rounded_up_height = rounded_up_bits / bits_per_level;
		// TODO: We could save memory by not allocating the leaves for "dead" top level bits (but ONLY leaves).
		fragments_per_window = ((1ull << ((rounded_up_height + 1) * bits_per_level)) - 1) / (bit_count - 1);
		leaves_start_index = static_cast<int>(fragments_per_window - leaves_per_window);
		data = std::make_unique<cache_aligned_t<std::atomic<std::uint64_t>>[]>(fragments_per_window * window_count);
		for (std::size_t i = 0; i < fragments_per_window * window_count; i++) {
			auto bits_in_node = (i % fragments_per_window) == 0 ? bits_required_in_top_level : bit_count;
			data[i]->fetch_or(((1 << bits_in_node) - 1) << (bit_count + bit_count - bits_in_node));
		}
	}

	template <claim_value VALUE, claim_mode MODE>
	std::size_t claim_bit(std::size_t window_index, int starting_bit, std::uint64_t epoch, std::memory_order order = BITSET_DEFAULT_MEMORY_ORDER) {
		// We use modified epochs.
		epoch = epoch * 2 + (VALUE == claim_value::ONE ? 1 : 0);
		auto ret = claim_bit_singular<VALUE, MODE>(&data[window_index * fragments_per_window], starting_bit, epoch, order);

		/*std::cout << window_index << "  " << (int)VALUE << " " << (int)MODE << " ";
		for (auto i = 0; i < fragments_per_window; i++) {
			auto val = data[window_index * fragments_per_window + i]->load();
			std::cout << std::bitset<bit_count>(get_bits(val)) << " | ";
		}
		std::cout << std::endl;*/
		return ret;
	}

	void set_epoch_if_empty(std::size_t window_index, std::uint64_t epoch, std::memory_order order = BITSET_DEFAULT_MEMORY_ORDER) {
		epoch *= 2;
		std::uint64_t next_eb = EPOCH::make_unit(epoch + 2);
		for (std::size_t i = 0; i < fragments_per_window; i++) {
			std::uint64_t eb = EPOCH::make_unit(epoch);
			data[window_index * fragments_per_window + i]->compare_exchange_strong(eb, next_eb, order);
		}
	}

	void set(std::size_t window_index, std::size_t index, std::uint64_t epoch, std::memory_order order = BITSET_DEFAULT_MEMORY_ORDER) {
		return change_bit<claim_value::ZERO>(window_index, index, epoch * 2, order);
	}

	void reset(std::size_t window_index, std::size_t index, std::uint64_t epoch, std::memory_order order = BITSET_DEFAULT_MEMORY_ORDER) {
		return change_bit<claim_value::ONE>(window_index, index, epoch * 2 + 1, order);
	}
};

#endif // ATOMIC_BINARY_TREE_H_INCLUDED
