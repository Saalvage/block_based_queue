#ifndef RELAXED_FIFO_H_INCLUDED
#define RELAXED_FIFO_H_INCLUDED

#include <array>
#include <memory>
#include <atomic>
#include <random>
#include <new>
#include <optional>

#include "fifo.h"
#include "atomic_bit_tree.h"

#ifndef BBQ_LOG_WINDOW_MOVE
#define BBQ_LOG_WINDOW_MOVE 0
#endif

#ifndef BBQ_LOG_CREATION_SIZE
#define BBQ_LOG_CREATION_SIZE 0
#endif

#define BBQ_DEBUG_FUNCTIONS 0

#if BBQ_DEBUG_FUNCTIONS
#include <ostream>
#endif

#if BBQ_LOG_WINDOW_MOVE || BBQ_LOG_CREATION_SIZE
#include <iostream>
#endif

#if defined(__GNUC__) && defined(unix)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winterference-size"
#endif

template <typename T>
struct block {
	// Can allocate without alignment considerations.
	static_assert(alignof(T) == sizeof(T));

	std::byte* ptr;

	block() = default;
	block(std::byte* ptr) : ptr(ptr) { }

	// 32 bit epoch, 16 bit read index, 16 bit write index.
	std::atomic_uint64_t& get_header() {
		return *std::launder(reinterpret_cast<std::atomic_uint64_t*>(ptr));
	}

	std::atomic<T>& get_cell(std::size_t cell) {
		return *std::launder(reinterpret_cast<std::atomic<T>*>(ptr + sizeof(std::atomic_uint64_t) + cell * sizeof(T)));
	}

	// No need to explicitly call dtor.
	static_assert(std::is_trivially_destructible_v<std::atomic_uint64_t>);
	static_assert(std::is_trivially_destructible_v<std::atomic<T>>);
};

template <typename T, typename BITSET_T = std::uint8_t>
class block_based_queue {
private:
	std::size_t cells_per_block;
	std::size_t block_size;
	std::size_t block_count;

	// We use 64 bit return types here to avoid potential deficits through 16-bit comparisons.
	static constexpr std::uint64_t get_epoch(std::uint64_t ei) { return ei >> 32; }
	static constexpr std::uint64_t get_read_index(std::uint64_t ei) { return (ei >> 16) & 0xffff; }
	static constexpr std::uint64_t get_write_index(std::uint64_t ei) { return ei & 0xffff; }
	static constexpr std::uint64_t increment_write_index(std::uint64_t ei) { return ei + 1; }
	static constexpr std::uint64_t increment_read_index(std::uint64_t ei) { return ei + (1ull << 16); }
	static constexpr std::uint64_t epoch_to_header(std::uint64_t epoch) { return epoch << 32; }

	using block_t = block<T>;
	static_assert(std::is_trivial_v<block_t>);

	// Doing it like this avoids having to have a special case for first-time initialization, while only claiming a block on first use.
	static inline std::atomic_uint64_t dummy_block_value{ epoch_to_header(0x1000'0000ull) };
	static inline block_t dummy_block{ reinterpret_cast<std::byte*>(&dummy_block_value) };

	atomic_bit_tree<BITSET_T> tree;
	std::unique_ptr<std::byte[]> buffer;

	block_t get_block(std::uint64_t block_index) {
		return &buffer[block_index * block_size];
	}

	static constexpr std::size_t align_cache_line_size(std::size_t size) {
		std::size_t ret = std::hardware_destructive_interference_size;
		while (ret < size) {
			ret += std::hardware_destructive_interference_size;
		}
		return ret;
	}

	static constexpr std::size_t make_po8(std::size_t sz) {
		std::size_t acc = 1;
		while (acc < sz) {
			acc *= 8;
		}
		return acc;
	}

public:
	block_based_queue(int thread_count, std::size_t min_size, double blocks_per_window_per_thread, std::size_t cells_per_block) :
			cells_per_block(cells_per_block),
			block_size(align_cache_line_size(sizeof(std::atomic_uint64_t) + cells_per_block * sizeof(T))),
			block_count(make_po8(min_size / cells_per_block)),
			tree(block_count),
			buffer(std::make_unique<std::byte[]>(block_count * block_size)) {
#if BBQ_LOG_CREATION_SIZE
		std::cout << "Window count: " << window_count << std::endl;
		std::cout << "Block count: " << blocks_per_window << std::endl;
#endif // BBQ_LOG_CREATION_SIZE

		(void)thread_count;
		(void)blocks_per_window_per_thread;
	}

	std::size_t capacity() const {
		return block_count * cells_per_block;
	}

#if BBQ_DEBUG_FUNCTIONS
	std::ostream& operator<<(std::ostream& os) {
		os << "Printing block_based_queue:\n"
			<< "Read: " << global_read_window << "; Write: " << global_write_window << '\n';
		for (std::size_t i = 0; i < window_count; i++) {
			for (std::size_t j = 0; j < blocks_per_window; j++) {
				std::uint64_t ei = get_block(i, j).get_header();
				os << get_epoch(ei) << " " << get_read_index(ei) << " " << " " << get_write_index(ei) << " | ";
			}
			os << "\n======================\n";
		}
		return os;
	}
#endif // BBQ_RELAXED_DEBUG_FUNCTIONS

	class handle {
	private:
		block_based_queue& fifo;

		// TODO: Find some workaround to these having to start out as -1.
		std::size_t write_block_index = -1;
		std::size_t read_block_index = -1;

		std::uint32_t write_epoch = 0;
		std::uint32_t read_epoch = 0;

		block_t read_block = dummy_block;
		block_t write_block = dummy_block;

		std::minstd_rand rng;

		handle(block_based_queue& fifo, std::random_device::result_type seed) : fifo(fifo), rng(seed) { }

		friend block_based_queue;

		// They're typed as uint64_t, but only hold 32 bits of data.
		static constexpr bool epoch_valid(std::uint64_t check, std::uint64_t curr) {
			return (curr - check) < std::numeric_limits<std::uint32_t>::max() / 2;
		}

		bool claim_new_block_write() {
			auto new_index = fifo.tree.claim_bit<op::WRITE>(write_block_index, write_epoch);
			if (new_index == std::numeric_limits<std::size_t>::max()) {
				return false;
			}
			write_block_index = new_index;
			write_block = fifo.get_block(write_block_index);
			return true;
		}

		bool claim_new_block_read() {
			auto new_index = fifo.tree.claim_bit<op::READ>(read_block_index, read_epoch);
			if (new_index == std::numeric_limits<std::size_t>::max()) {
				return false;
			}
			read_block_index = new_index;
			read_block = fifo.get_block(read_block_index);
			return true;
		}

	public:
		bool push(T t) {
			assert(t != 0);

			std::atomic_uint64_t* header = &write_block.get_header();
			std::uint64_t ei = header->load(std::memory_order_relaxed);
			std::uint64_t index;

			bool failure = true;
			while (failure) {
				T old = 0;
				while (!epoch_valid(get_epoch(ei), write_epoch) || (index = get_write_index(ei)) == fifo.cells_per_block
					|| !write_block.get_cell(index).compare_exchange_weak(old, t, std::memory_order_relaxed)) {
					if (!claim_new_block_write()) {
						return false;
					}
					header = &write_block.get_header();
					ei = header->load(std::memory_order_relaxed);
					old = 0;
				}

				failure = !header->compare_exchange_strong(ei, increment_write_index(ei),
					std::memory_order_release, std::memory_order_relaxed);
				if (failure) {
					// The header changed, we need to undo our write and try again.
					write_block.get_cell(index).store(0, std::memory_order_relaxed);
					// We do NOT unclaim the block's bit here, readers handle empty blocks by themselves.
				}
			}

			return true;
		}

		void debug_print() {
			fifo.tree.debug_print();
		}

		std::optional<T> pop() {
			std::atomic_uint64_t* header = &read_block.get_header();
			std::uint64_t ei = header->load(std::memory_order_relaxed);
			std::uint64_t index;

			while (true) {
				if (epoch_valid(get_epoch(ei), read_epoch)) {
					if ((index = get_read_index(ei)) + 1 == get_write_index(ei)) {
						if (header->compare_exchange_weak(ei, epoch_to_header(read_epoch + 1), std::memory_order_acquire, std::memory_order_relaxed)) {
							fifo.tree.mark_leaf_done<op::READ>(read_block_index, read_epoch);
							break;
						}
					} else {
						if (header->compare_exchange_weak(ei, increment_read_index(ei), std::memory_order_acquire, std::memory_order_relaxed)) {
							break;
						}
					}
				}
				if (!claim_new_block_read()) {
					return std::nullopt;
				}
				header = &read_block.get_header();
				ei = header->load(std::memory_order_relaxed);
				if (get_write_index(ei) == 0 && epoch_valid(get_epoch(ei), read_epoch) && header->compare_exchange_strong(ei, epoch_to_header(read_epoch + 1), std::memory_order_relaxed)) {
					fifo.tree.mark_leaf_done<op::READ>(read_block_index, read_epoch);
				}
			}

			T ret = read_block.get_cell(index).exchange(0, std::memory_order_relaxed);
			assert(ret != 0);
			return ret;
		}
	};

	handle get_handle() { return handle(*this, std::random_device()()); }
};
static_assert(fifo<block_based_queue<std::uint64_t>, std::uint64_t>);

#if defined(__GNUC__) && defined(unix)
#pragma GCC diagnostic pop
#endif

#endif // RELAXED_FIFO_H_INCLUDED
