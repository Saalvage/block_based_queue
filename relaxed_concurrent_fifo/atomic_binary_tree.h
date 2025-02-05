#ifndef ATOMIC_BINARY_TREE_H_INCLUDED
#define ATOMIC_BINARY_TREE_H_INCLUDED

#include <atomic>

template <size_t SIZE>
struct atomic_binary_tree {
	struct alignas(std::hardware_destructive_interference_size) tree_fragment {
		std::atomic<std::uint8_t> data = 0;
	};

	static constexpr size_t FRAGMENT_COUNT = SIZE / 4 + SIZE / 4 / 4;
	static constexpr size_t LEAF_COUNT = (FRAGMENT_COUNT + 1) / 2;
	static constexpr size_t LEAF_START = LEAF_COUNT - 1;

	tree_fragment fragments[FRAGMENT_COUNT];

	int claim_bit() {
		static thread_local std::random_device dev;
		static thread_local std::minstd_rand rng{ dev() };
		static thread_local std::uniform_int_distribution dist_outer{ 0, static_cast<int>(LEAF_COUNT - 1) };
		static thread_local std::uniform_int_distribution dist_inner{ 0, 3 };

		// Select random starting leaf.
		int idx = LEAF_START + dist_outer(rng);
		int inner_idx = 3 + dist_inner(rng);

		// Ascend to find highest 0 node.
		bool succ = false;
		while (!succ && idx > 0) {
			auto loaded = fragments[idx].data.load();
			while (loaded & (1 << inner_idx) && inner_idx > 0) {
				inner_idx = parent(inner_idx);
			}
			if (!(loaded & (1 << inner_idx))) {
				succ = true;
			} else {
				// Position in parent.
				// We could immediately take the parent, because we know that the entire child fragment is filled.
				inner_idx = 3 + (idx - 1) % 4;
				idx = parent<4>(idx);
			}
		}

		auto remember_idx = idx;
		auto remember_inner_idx = inner_idx;

		// Descend to find bit.

		// Ascend further to fulfill invariants.

		return 0;
	}

private:
	static int parent(int index) {
		return (index - 1) / 2;
	}

	template <size_t N>
	static int parent(int index) {
		return (index - 1) / N;
	}

	static int left_child(int index) {
		return 2 * index + 1;
	}

	static int right_child(int index) {
		return 2 * index + 2;
	}

	static int sibling(int index) {
		return index % 2 == 0 ? index - 1 : index + 1;
	}
};

#endif // ATOMIC_BINARY_TREE_H_INCLUDED
