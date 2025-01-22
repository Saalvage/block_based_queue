#ifndef ATOMIC_BINARY_TREE_H_INCLUDED
#define ATOMIC_BINARY_TREE_H_INCLUDED

#include <atomic>

struct atomic_binary_tree {
	std::atomic<std::uint8_t> data = 0;

	int claim_bit() {
		static thread_local std::random_device dev;
		static thread_local std::minstd_rand rng{ dev() };
		static thread_local std::uniform_int_distribution dist{ 0, static_cast<int>(3) };

		std::uint8_t loaded = data.load(std::memory_order_relaxed);
		while (true) {
			int initial = 3 + dist(rng);
			int prev = initial;
			while ((loaded & (1 << initial)) && initial > 0) {
				prev = initial;
				initial = parent(initial);
			}

			if (initial < 0) {
				return -1;
			}

			int mask = 0;
			bool all_set = true;
			if (prev != initial) {
				prev = sibling(prev);
				// Go down
				while (prev < 3) {
					mask |= 1 << prev;
					if (!(loaded & (1 << left_child(prev)))) {
						if (!(loaded & (1 << right_child(prev)))) {
							all_set = false;
							mask = 0;
						}
						// TODO: By default, always left child.
						prev = left_child(prev);
					} else {
						prev = right_child(prev);
					}
				}
			}

			if ((loaded & (1 << prev))) {
				return -1;
			}

			mask |= 1 << prev;

			if (all_set) {
				mask |= 1 << initial;
				auto p = initial;
				while ((loaded & (1 << sibling(p)))) {
					p = parent(p);
					mask |= 1 << p;
				}
			}

			if (data.compare_exchange_strong(loaded, loaded | mask, std::memory_order_relaxed)) {
				return prev - 3;
			}
		}
	}

	bool check_invariants() {
		for (int i = 1; i < 7; i++) {
			if ((data & (1 << i))) {
				if (data & (1 << sibling(i))) {
					if (!(data & (1 << parent(i)))) {
						return false;
					}
				} else {
					if (data & (1 << parent(i))) {
						return false;
					}
				}
			} else {
				if (data & (1 << parent(i))) {
					return false;
				}
			}
		}
		return true;
	}

private:
	int parent(int index) {
		return (index - 1) / 2;
	}

	int left_child(int index) {
		return 2 * index + 1;
	}

	int right_child(int index) {
		return 2 * index + 2;
	}

	int sibling(int index) {
		return index % 2 == 0 ? index - 1 : index + 1;
	}
};

#endif // ATOMIC_BINARY_TREE_H_INCLUDED
