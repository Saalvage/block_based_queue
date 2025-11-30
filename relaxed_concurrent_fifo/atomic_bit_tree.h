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
	// TODO Don't need this, bit_count is constexpr.
	static constexpr std::size_t bit_count_log_2 = std::bit_width(bit_count) - 1;
	std::unique_ptr<cache_aligned_t<std::atomic<std::uint64_t>>[]> data;

	static constexpr std::size_t calculate_fragment_count(std::size_t leaves) {
		auto height = (std::bit_width(leaves) - 1) / bit_count_log_2;
		return ((1ull << ((height + 1) * bit_count_log_2)) - 1) / (bit_count - 1);
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
		ARR_TYPE modified = modify<VALUE>(leaf_val, bit_idx);
		// TODO: These conditions are not always needed.
		while (modified != EPOCH::get_bits(leaf_val) && EPOCH::compare_epochs(leaf_val, epoch)) {
			bool advanced_epoch = modified == static_cast<ARR_TYPE>(VALUE == claim_value::ONE ? 0 : ~0);
			if (leaf.compare_exchange_strong(leaf_val, advanced_epoch && VALUE == claim_value::ONE
				? (EPOCH::make_unit(epoch + 1))
				: (EPOCH::make_unit(epoch) | modified), order)) {
				return {true, advanced_epoch};
			}
			modified = modify<VALUE>(leaf_val, bit_idx);
		}
		return {false, false};
	}

	template <claim_value VALUE>
	bool has_valid_bit(std::uint64_t value) {
		return VALUE == claim_value::ONE ? EPOCH::get_bits(value) : static_cast<ARR_TYPE>(~value);
	}

	static inline thread_local std::minstd_rand rng{std::random_device()()};

	template <claim_value VALUE>
	static int select_random_bit_index(ARR_TYPE value) {
		//unsigned value32 = value;
		//return VALUE == claim_value::ZERO ? std::countr_one(value32) : std::countr_zero(value32);

		// TODO: Don't randomize? (FIFO semantic on fragment level??)
		if constexpr (VALUE == claim_value::ZERO) {
			value = ~value;
		}

		if (value == 0) {
			return 32;
		}

		auto valid_bits = std::popcount(value);
		auto nth_bit = std::uniform_int_distribution<>{0, valid_bits - 1}(rng);
		return std::countr_zero(_pdep_u32(1 << nth_bit, value));
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
			while (idx > 0 && (!EPOCH::compare_epochs(leaf_val, epoch) || !has_valid_bit<VALUE>(leaf_val))) {
				idx = get_parent(idx);
				leaf = &root[idx];
				leaf_val = leaf->value.load(order);
				// TODO: Automatically fix parent here if child is erroneously marked?
			}

			if (!EPOCH::compare_epochs(leaf_val, epoch) || !has_valid_bit<VALUE>(leaf_val)) {
				// Root is invalid as well.
				return std::numeric_limits<std::size_t>::max();
			}

			bool advanced_epoch = false;
			while (idx < leaves_start_index) {
				auto new_idx = get_random_child<VALUE>(static_cast<ARR_TYPE>(leaf_val), idx);
				if (new_idx == -1) {
					// We walked into an out-of-date node. Let's propagate this information up.
					advanced_epoch = true;
					break;
				}
				idx = new_idx;
				leaf = &root[idx];
				leaf_val = leaf->value.load(order);
				// TODO: Check for has_valid_bit here (avoids random call)?
				if (!EPOCH::compare_epochs(leaf_val, epoch)) {
					advanced_epoch = true;
					break;
				}
			}

			// Skip if we didn't find a leaf but stepped into an invalid node.
			if (!advanced_epoch) {
				do {
					auto bit_idx = select_random_bit_index<VALUE>(static_cast<ARR_TYPE>(leaf_val));
					if (bit_idx == 32 || !EPOCH::compare_epochs(leaf_val, epoch)) {
						// Leaf empty, need to move up again.
						advanced_epoch = true;
						break;
					}

					ret = (idx - leaves_start_index) * bit_count + bit_idx;
					if constexpr (MODE == claim_mode::READ_ONLY) {
						return ret;
					}
					auto bit_change_ret = try_change_bit<VALUE>(epoch, *leaf, leaf_val, bit_idx, order);
					success = bit_change_ret.first;
					advanced_epoch = bit_change_ret.second;
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
	int get_random_child(ARR_TYPE node, int index) {
		auto offset = select_random_bit_index<VALUE>(node);
		if (offset == 32) {
			return -1;
		}
		return index * bit_count + offset + 1;
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
		leaves_per_window(blocks_per_window / bit_count),
		fragments_per_window(calculate_fragment_count(leaves_per_window)),
		leaves_start_index(static_cast<int>(fragments_per_window - leaves_per_window)),
		data(std::make_unique<cache_aligned_t<std::atomic<std::uint64_t>>[]>(fragments_per_window * window_count)) {
		// Must be a perfect k-ary tree.
		assert(blocks_per_window == 1ull << ((std::bit_width(blocks_per_window) - 1) / bit_count_log_2 * bit_count_log_2));
	}

	template <claim_value VALUE, claim_mode MODE>
	std::size_t claim_bit(std::size_t window_index, int starting_bit, std::uint64_t epoch, std::memory_order order = BITSET_DEFAULT_MEMORY_ORDER) {
		// We use modified epochs.
		auto ret = claim_bit_singular<VALUE, MODE>(&data[window_index * fragments_per_window], starting_bit, epoch, order);

		/*std::cout << window_index << "  " << (int)VALUE << " " << (int)MODE << " ";
		for (auto i = 0; i < fragments_per_window; i++) {
			auto val = data[window_index * fragments_per_window + i]->load();
			std::cout << get_epoch(val) << " " << std::bitset<bit_count>(get_bits(val)) << " | ";
		}
		std::cout << std::endl;*/
		return ret;
	}

	void set_epoch_if_empty(std::size_t window_index, std::uint64_t epoch, std::memory_order order = BITSET_DEFAULT_MEMORY_ORDER) {
		std::uint64_t next_eb = EPOCH::make_unit(epoch + 1);
		for (std::size_t i = 0; i < fragments_per_window; i++) {
			std::uint64_t eb = EPOCH::make_unit(epoch);
			data[window_index * fragments_per_window + i]->compare_exchange_strong(eb, next_eb, order);
		}
	}

	void set(std::size_t window_index, std::size_t index, std::uint64_t epoch, std::memory_order order = BITSET_DEFAULT_MEMORY_ORDER) {
		return change_bit<claim_value::ZERO>(window_index, index, epoch, order);
	}

	void reset(std::size_t window_index, std::size_t index, std::uint64_t epoch, std::memory_order order = BITSET_DEFAULT_MEMORY_ORDER) {
		return change_bit<claim_value::ONE>(window_index, index, epoch, order);
	}
};

#endif // ATOMIC_BINARY_TREE_H_INCLUDED
