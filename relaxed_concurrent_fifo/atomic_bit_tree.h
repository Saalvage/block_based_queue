#ifndef ATOMIC_BINARY_TREE_H_INCLUDED
#define ATOMIC_BINARY_TREE_H_INCLUDED

#include <atomic>
#include <mutex>
#include <bitset>

#include <immintrin.h>

#include "atomic_bitset.h"

enum class op {
	READ,
	WRITE,
};

template <typename ARR_TYPE = std::uint8_t, epoch_handling EPOCH = default_epoch_handling>
struct atomic_bit_tree {
private:
	static_assert(sizeof(ARR_TYPE) <= 4, "Inner bitset type must be 4 bytes or smaller to allow for storing epoch.");

	std::size_t leaves;
	std::size_t fragments;
	std::size_t leaves_start_index;

	static constexpr std::size_t bit_count = sizeof(ARR_TYPE) * 8;
	// 32 bits epoch, 8 bits unused, 8 bits +1 epoch, 8 bits filled?, 8 bits any elements?
	// 00 => no elements
	// 01 => contains elements, still slots free
	// 11 => completely filled
	// 10 => impossible configuration
	std::unique_ptr<cache_aligned_t<std::atomic<std::uint64_t>>[]> data;

	template <op OP>
	bool mark_done(std::size_t node_index, std::size_t leaf_index, std::uint64_t ei, std::uint32_t used_epoch) {
		std::uint64_t epoch = get_epoch(ei);
		std::uint64_t epoch_mask = (1 << leaf_index) << 16;
		bool is_in_next_epoch = used_epoch == epoch + 1;
		bool succ = false;
		if constexpr (OP == op::WRITE) {
			std::uint64_t mask = (1 << leaf_index) << 8;
			while (!succ && (used_epoch == epoch || is_in_next_epoch) && !(ei & mask) && static_cast<bool>(ei & epoch_mask) == is_in_next_epoch) {
				succ = data[node_index]->compare_exchange_weak(ei, ei | mask, std::memory_order_relaxed);
				epoch = get_epoch(ei);
				is_in_next_epoch = used_epoch == epoch + 1;
			}
		} else {
			std::uint64_t mask = ((1 << leaf_index) << 8) | (1 << leaf_index);
			while (!succ && (used_epoch == epoch || is_in_next_epoch) && static_cast<bool>(ei & epoch_mask) == is_in_next_epoch) {
				if (is_in_next_epoch) {
					// TODO ???
					throw 2;
				} else if (get_epoch_mask(ei | epoch_mask) == 0xff) {
					// All bits in the next epoch, advance the node's epoch.
					succ = data[node_index]->compare_exchange_weak(ei, (ei & ~(mask | (0xff << 16) | (0xffff'ffffull << 32))) | ((epoch + 1) << 32), std::memory_order_relaxed);
				} else {
					succ = data[node_index]->compare_exchange_weak(ei, (ei & ~mask) | epoch_mask, std::memory_order_relaxed);
				}
				epoch = get_epoch(ei);
				is_in_next_epoch = used_epoch == epoch + 1;
			}
		}
		return succ;
	}

	bool mark_begun(std::size_t node_index, std::size_t leaf_index, std::uint64_t ei, std::uint32_t used_epoch) {
		std::uint64_t epoch = get_epoch(ei);
		std::uint64_t epoch_mask = (1 << leaf_index) << 16;
		bool is_in_next_epoch = used_epoch == epoch + 1;
		std::uint64_t mask = 1ull << leaf_index;
		bool succ = false;
		while (!succ && (epoch == used_epoch || is_in_next_epoch) && !(ei & mask) && static_cast<bool>(ei & epoch_mask) == is_in_next_epoch) {
			succ = data[node_index]->compare_exchange_weak(ei, ei | mask, std::memory_order_relaxed);
			epoch = get_epoch(ei);
			is_in_next_epoch = used_epoch == epoch + 1;
		}
		return succ;
	}

	static inline thread_local std::minstd_rand rng{std::random_device()()};

	template <claim_value VALUE>
	static int select_random_bit_index(std::uint64_t value) {
		ARR_TYPE bits = static_cast<ARR_TYPE>(value);

		if constexpr (VALUE == claim_value::ZERO) {
			bits = ~bits;
		}

		bits = (value >> bit_count) & bits;

		assert(bits);

		auto valid_bits = std::popcount(bits);
		auto nth_bit = std::uniform_int_distribution<>{0, valid_bits - 1}(rng);
		return std::countr_zero(_pdep_u32(1 << nth_bit, bits));
	}

	std::size_t get_parent(std::size_t index) {
		return (index - 1) / bit_count;
	}

	template <claim_value VALUE>
	std::size_t get_random_child(std::uint64_t node, std::size_t index) {
		auto offset = select_random_bit_index<VALUE>(node);
		return index * bit_count + offset + 1;
	}

	std::uint32_t get_epoch(std::uint64_t node) {
		return node >> 32;
	}

	std::uint8_t get_epoch_mask(std::uint64_t node) {
		return (node >> 16) & 0xFF;
	}

	std::uint8_t get_all_bits(std::uint64_t node) {
		return (node >> 8) & 0xFF;
	}

	std::uint8_t get_any_bits(std::uint64_t node) {
		return node & 0xFF;
	}

	template <op OP>
	std::uint8_t determine_valid_bits(std::uint64_t node, std::uint32_t epoch) {
		auto node_epoch = get_epoch(node);
		auto mask = get_epoch_mask(node);
		std::uint8_t bits = OP == op::WRITE ? ~get_all_bits(node) : get_any_bits(node);
		if (epoch == node_epoch) {
			return bits & ~mask;
		} else if (epoch == node_epoch + 1) {
			return bits & mask;
		} else {
			return 0;
		}
	}

	template <op OP>
	std::size_t get_leftmost_child(std::uint64_t node, std::size_t index, std::uint32_t epoch) {
		auto bits = determine_valid_bits<OP>(node, epoch);
		auto bit = std::countr_zero(bits);
		if (bit == 8) { return std::numeric_limits<std::size_t>::max(); }
		return index * 8 + bit + 1;
	}

	std::size_t get_child_idx(std::uint64_t parent, std::uint64_t child) {
		return child - 1 - parent * bit_count;
	}

public:
	atomic_bit_tree(std::size_t blocks) :
		leaves(blocks / bit_count) {
		assert(std::has_single_bit(blocks));
		auto bits_per_level = std::bit_width(bit_count) - 1;
		auto bits = std::bit_width(leaves) - 1;
		auto rounded_up_bits = bits + bits_per_level - 1;
		auto rounded_up_height = rounded_up_bits / bits_per_level;
		fragments = ((1ull << ((rounded_up_height + 1) * bits_per_level)) - 1) / (bit_count - 1);
		leaves_start_index = static_cast<int>(fragments - leaves);
		data = std::make_unique<cache_aligned_t<std::atomic<std::uint64_t>>[]>(fragments);
	}

	template <op OP>
	std::size_t claim_bit(std::size_t previous_block, std::uint32_t& epoch, std::memory_order order = BITSET_DEFAULT_MEMORY_ORDER) {
		std::size_t tree_idx;
		std::uint64_t node;
		std::uint32_t used_epoch;

		if (previous_block != std::numeric_limits<std::size_t>::max()) {
			tree_idx = get_parent(fragments + previous_block);
			node = data[tree_idx]->load(order);
			used_epoch = epoch;
		} else {
			tree_idx = 0;
			node = data[tree_idx]->load(order);
			used_epoch = std::max(epoch, get_epoch(node));
		}

		while (tree_idx < fragments) {
			std::size_t new_tree_idx = get_leftmost_child<OP>(node, tree_idx, used_epoch);
			if (new_tree_idx == std::numeric_limits<std::size_t>::max()) {
				if (tree_idx > 0) {
					// TODO: Marking this node as done and propagating that upwards here requires assuring that all children
					// are within the next epoch, either by simply checking whether all children are actually empty and waiting for the next epoch,
					// or manually setting them (and all downstream dependents) to be so.
					// For now we just retry if this is the case, this WILL cause locking if any thread falls asleep during up-propagation.
					tree_idx = get_parent(tree_idx);
					node = data[tree_idx]->load(order);
					// Epoch change/inconsistency, propagate upwards.
					/*std::size_t parent_idx = get_parent(tree_idx);
					std::uint64_t parent_node = data[parent_idx]->load(order);
					mark_done<OP>(parent_idx, get_child_idx(parent_idx, tree_idx), parent_node, used_epoch);*/
					continue;
				} else {
					if (get_epoch(node) == used_epoch) {
						++used_epoch;
						continue;
					} else if (get_epoch(node) + 1 == used_epoch) {
						return std::numeric_limits<std::size_t>::max();
					} else {
						// Outdated epoch.
						used_epoch = get_epoch(node);
						continue;
					}
				}
			}
			if (OP == op::WRITE) {
				mark_begun(tree_idx, get_child_idx(tree_idx, new_tree_idx), node, used_epoch);
			}
			tree_idx = new_tree_idx;
			if (tree_idx < fragments) {
				node = data[tree_idx]->load(order);
			}
		}

		epoch = used_epoch;
		return tree_idx - fragments;
	}

	template <op OP>
	void mark_leaf_done(std::size_t leaf_idx, std::uint32_t epoch) {
		leaf_idx += fragments;
		auto parent_idx = get_parent(leaf_idx);

		std::uint64_t node = data[parent_idx]->load(std::memory_order_relaxed);
		while (mark_done<OP>(parent_idx, get_child_idx(parent_idx, leaf_idx), node, epoch)) {
			node = data[parent_idx]->load(std::memory_order_relaxed);
			if (determine_valid_bits<OP>(node, epoch) || parent_idx == 0) {
				break;
			}
			leaf_idx = parent_idx;
			parent_idx = get_parent(leaf_idx);
			node = data[parent_idx]->load(std::memory_order_relaxed);
		}
	}

	void debug_print() {
		static std::mutex print_mutex;
		std::lock_guard lock{ print_mutex };

		int depth = 0;
		int x = 0;
		while (x < fragments) {
			for (int i = 0; i < std::pow(8, depth); ++i) {
				auto data = this->data[x++]->load();
				std::cout << get_epoch(data)
					<< " " << std::bitset<8>(get_epoch_mask(data))
					<< " " << std::bitset<8>(get_all_bits(data))
					<< " " << std::bitset<8>(get_any_bits(data))
					<< '\n';
			}
			std::cout << '\n';
			++depth;
		}
	}
};

#endif // ATOMIC_BINARY_TREE_H_INCLUDED
