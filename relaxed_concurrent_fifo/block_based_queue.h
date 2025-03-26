#ifndef RELAXED_FIFO_H_INCLUDED
#define RELAXED_FIFO_H_INCLUDED

#include <array>
#include <memory>
#include <atomic>
#include <random>
#include <new>
#include <optional>

#include "fifo.h"
#include "atomic_bitset.h"

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

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winterference-size"
#endif // __GNUC__

struct header_t {
	// 16 bits epoch, 16 bits read started index, 16 bits read finished index, 16 bits write index
	std::atomic_uint64_t epoch_and_indices;
};

template <typename T, std::size_t CELLS_PER_BLOCK>
struct block {
	static_assert(sizeof(header_t) == 8);
	header_t header;
	std::array<std::atomic<T>, CELLS_PER_BLOCK> cells;
};

template <typename BLOCK_T, std::size_t BLOCKS_PER_WINDOW, typename BITSET_T>
struct window {
	alignas(std::hardware_destructive_interference_size) atomic_bitset<BLOCKS_PER_WINDOW, BITSET_T> filled_set;
	alignas(std::hardware_destructive_interference_size) BLOCK_T blocks[BLOCKS_PER_WINDOW];
};

template <typename T, std::size_t LOG_BLOCKS_PER_WINDOW, std::size_t CELLS_PER_BLOCK, typename BITSET_T>
class block_based_queue {
private:
	// PO2 for modulo performance.
	static constexpr std::size_t blocks_per_window = 1ull << LOG_BLOCKS_PER_WINDOW;
	// At least as big as the bitset's type.
	static_assert(blocks_per_window >= sizeof(BITSET_T) * 8);

	std::size_t window_count;
	std::size_t window_count_mod_mask;

	std::size_t capacity() const {
		return window_count * blocks_per_window * CELLS_PER_BLOCK;
	}

#if BBQ_DEBUG_FUNCTIONS
	std::size_t size_full() const {
		std::size_t filled_cells = 0;
		for (std::size_t i = 0; i < window_count; i++) {
			for (std::size_t j = 0; j < blocks_per_window; j++) {
				auto ei = get_window(i).blocks[j].header.epoch_and_indices.load();
				filled_cells += get_write_index(ei) - get_read_finished_index(ei);
			}
		}
		return filled_cells;
	}

	std::size_t size() const {
		std::size_t filled_cells = 0;
		for (std::size_t i = read_window; i <= write_window; i++) {
			for (std::size_t j = 0; j < blocks_per_window; j++) {
				auto ei = get_window(i).blocks[j].header.epoch_and_indices.load();
				filled_cells += get_write_index(ei) - get_read_finished_index(ei);
			}
		}
		return filled_cells;
	}
#endif // BBQ_RELAXED_DEBUG_FUNCTIONS

	// We use 64 bit return types here to avoid potential deficits through 16-bit comparisons.
	static constexpr std::uint64_t get_epoch(std::uint64_t ei) { return ei >> 48 & 0xffff; }
	static constexpr std::uint64_t mask_epoch(std::uint64_t window_index) { return window_index & 0xffff; }
	static constexpr std::uint64_t get_read_started_index(std::uint64_t ei) { return (ei >> 32) & 0xffff; }
	static constexpr std::uint64_t get_read_finished_index(std::uint64_t ei) { return (ei >> 16) & 0xffff; }
	static constexpr std::uint64_t get_write_index(std::uint64_t ei) { return ei & 0xffff; }
	static constexpr std::uint64_t epoch_to_header(std::uint64_t epoch) { return epoch << 48; }

	using block_t = block<T, CELLS_PER_BLOCK>;
	static_assert(sizeof(block_t) == CELLS_PER_BLOCK * sizeof(T) + sizeof(header_t));

	// Doing it like this avoids having to have a special case for first-time initialization, while only claiming a block on first use.
	static inline block_t dummy_block{ header_t{0xffffull << 48}, {} };

	using window_t = window<block_t, blocks_per_window, BITSET_T>;
	std::unique_ptr<window_t[]> buffer;

	window_t& get_window(std::size_t index) const {
		return buffer[index & window_count_mod_mask];
	}

	alignas(std::hardware_destructive_interference_size) std::atomic_uint64_t read_window;
	alignas(std::hardware_destructive_interference_size) std::atomic_uint64_t write_window;

public:
	// TODO: Remove unused parameter!!
	block_based_queue([[maybe_unused]] int thread_count, std::size_t min_size) :
			window_count(std::max<std::size_t>(4, std::bit_ceil(min_size / blocks_per_window / CELLS_PER_BLOCK))),
			window_count_mod_mask(window_count - 1),
			buffer(std::make_unique<window_t[]>(window_count)) {
#if BBQ_LOG_CREATION_SIZE
		std::cout << "Window count: " << window_count << std::endl;
		std::cout << "Block count: " << blocks_per_window << std::endl;
#endif // BBQ_LOG_CREATION_SIZE

		read_window = 0;
		write_window = 1;
		window_t& window = buffer[0];
		for (std::size_t j = 0; j < blocks_per_window; j++) {
			header_t& header = window.blocks[j].header;
			header.epoch_and_indices = window_count << 48;
		}
		for (std::size_t i = 1; i < window_count; i++) {
			window_t& window = buffer[i];
			for (std::size_t j = 0; j < blocks_per_window; j++) {
				header_t& header = window.blocks[j].header;
				header.epoch_and_indices = i << 48;
			}
		}
	}

#if BBQ_DEBUG_FUNCTIONS
	std::ostream& operator<<(std::ostream& os) const {
		os << "Printing block_based_queue:\n"
			<< "Read: " << read_window << "; Write: " << write_window << '\n';
		for (std::size_t i = 0; i < window_count; i++) {
			for (std::size_t j = 0; j < blocks_per_window; j++) {
				std::uint64_t val = buffer[i].blocks[j].header.epoch_and_indices;
				os << get_epoch(val) << " " << get_read_started_index(val) << " " << get_read_finished_index(val) << " " << get_write_index(val) << " | ";
			}
			os << "\n======================\n";
		}
		return os;
	}
#endif // BBQ_RELAXED_DEBUG_FUNCTIONS

	class handle {
	private:
		block_based_queue& fifo;
	
		block_t* read_block = &dummy_block;
		block_t* write_block = &dummy_block;

		// These need to be 64-bit because we use them index into the window buffer.
		std::uint64_t write_window = 0;
		std::uint64_t read_window = 0;

		handle(block_based_queue& fifo) : fifo(fifo) { }

		friend block_based_queue;

		bool claim_new_block_write() {
			std::size_t free_bit;
			std::uint64_t window_index;
			window_t* window;
			do {
				window_index = fifo.write_window.load(std::memory_order_relaxed);
				window = &fifo.get_window(window_index);
				free_bit = window->filled_set.template claim_bit<false, true>(std::memory_order_relaxed);
				if (free_bit == std::numeric_limits<std::size_t>::max()) {
					// No more free bits, we move.
					if (window_index + 1 - fifo.read_window.load(std::memory_order_relaxed) == fifo.window_count) {
						return false;
					}
					fifo.write_window.compare_exchange_strong(window_index, window_index + 1, std::memory_order_relaxed);
#if BBQ_LOG_WINDOW_MOVE
					std::cout << "Write move " << (window_index + 1) << std::endl;
#endif // BBQ_LOG_WINDOW_MOVE
				} else {
					break;
				}
			} while (true);

			write_window = window_index;
			write_block = &window->blocks[free_bit];
			return true;
		}

		bool claim_new_block_read() {
			std::size_t free_bit;
			std::uint64_t window_index;
			window_t* window;
			do {
				window_index = fifo.read_window.load(std::memory_order_relaxed);
				window = &fifo.get_window(window_index);
				free_bit = window->filled_set.template claim_bit<true, false>(std::memory_order_relaxed);
				if (free_bit == std::numeric_limits<std::size_t>::max()) {
					std::uint64_t write_window = fifo.write_window.load(std::memory_order_relaxed);
					if (write_window == window_index + 1) {
						if (!fifo.get_window(write_window).filled_set.any(std::memory_order_relaxed)) {
							return false;
						}
						// TODO: This should be simplifiable? Spurious block claims only occur when force-moving.
						// Before we force-move the write window, there might be unclaimed blocks in the current one.
						// We need to make sure we clean those up BEFORE we move the write window in order to prevent
						// the read window from being moved before all blocks have either been claimed or invalidated.
						window_t& new_window = fifo.get_window(write_window);
						std::uint64_t next_epoch = epoch_to_header(write_window + fifo.window_count);
						for (std::size_t i = 0; i < blocks_per_window; i++) {
							// We can't rely on the bitset here because it might be experiencing a spurious claim.

							std::uint64_t ei = epoch_to_header(write_window); // All empty with current epoch.
							new_window.blocks[i].header.epoch_and_indices.compare_exchange_strong(ei, next_epoch, std::memory_order_relaxed);
						}
						fifo.write_window.compare_exchange_strong(write_window, write_window + 1, std::memory_order_relaxed);
#if BBQ_LOG_WINDOW_MOVE
						std::cout << "Write force move " << (write_window + 1) << std::endl;
#endif // BBQ_LOG_WINDOW_MOVE
					}

					fifo.read_window.compare_exchange_strong(window_index, window_index + 1, std::memory_order_relaxed);
#if BBQ_LOG_WINDOW_MOVE
					std::cout << "Read move " << (window_index + 1) << std::endl;
#endif // BBQ_LOG_WINDOW_MOVE
				} else {
					break;
				}
			} while (true);

			read_window = window_index;
			read_block = &window->blocks[free_bit];
			return true;
		}

	public:
		bool push(T t) {
			assert(t != 0);

			header_t* header = &write_block->header;
			std::uint64_t ei = header->epoch_and_indices.load(std::memory_order_relaxed);
			std::uint64_t index;
			bool claimed = false;
			while (get_epoch(ei) != mask_epoch(write_window) || (index = get_write_index(ei)) == CELLS_PER_BLOCK || !header->epoch_and_indices.compare_exchange_weak(ei, ei + 1, std::memory_order_relaxed)) {
				// We need this in case of a spurious claim where we claim a bit, but can't place an element inside,
				// because the write window was already forced-moved.
				// This is safe to do because writers are exclusive.
				if (claimed && (index = get_write_index(ei)) == 0) {
					// We're abandoning an empty block!
					window_t& window = fifo.get_window(write_window);
					auto diff = write_block - window.blocks;
					window.filled_set.reset(diff, std::memory_order_relaxed);
				}
				if (!claim_new_block_write()) {
					return false;
				}
				claimed = true;
				header = &write_block->header;
				ei = header->epoch_and_indices.load(std::memory_order_relaxed);
			}

			write_block->cells[index].store(std::move(t), std::memory_order_relaxed);

			return true;
		}

		std::optional<T> pop() {
			header_t* header = &read_block->header;
			std::uint64_t ei = header->epoch_and_indices.load(std::memory_order_relaxed);
			std::uint64_t index;

			while (get_epoch(ei) != mask_epoch(read_window) || (index = get_read_started_index(ei)) == get_write_index(ei)
				|| !header->epoch_and_indices.compare_exchange_weak(ei, ei + (1ull << 32), std::memory_order_relaxed)) {
				if (!claim_new_block_read()) {
					return std::nullopt;
				}
				header = &read_block->header;
				ei = header->epoch_and_indices.load(std::memory_order_relaxed);
			}

			T ret;
			while ((ret = std::move(read_block->cells[index].load(std::memory_order_relaxed))) == 0) { }
			read_block->cells[index].store(0, std::memory_order_relaxed);

			std::uint64_t finished_index = get_read_finished_index(header->epoch_and_indices.fetch_add(1 << 16, std::memory_order_relaxed)) + 1;
			// We need the >= here because between the read of ei and the fetch_add above both a write and a finished read might have occurred
			// that make our finished_index > our (outdated) write index.
			if (finished_index >= get_write_index(ei)) {
				// Apply local read index update.
				ei = (ei & (0xffffull << 48)) | (finished_index << 32) | (finished_index << 16) | finished_index;
				// Before we mark this block as empty, we make it unavailable for other readers and writers of this epoch.
				if (header->epoch_and_indices.compare_exchange_strong(ei, (read_window + fifo.window_count) << 48, std::memory_order_relaxed)) {
					window_t& window = fifo.get_window(read_window);
					auto diff = read_block - window.blocks;
					window.filled_set.reset(diff, std::memory_order_relaxed);

					// We don't need to invalidate the read window because it has been changed already.	
				} else {
					assert(finished_index < CELLS_PER_BLOCK);
				}
			}

			return ret;
		}
	};

	handle get_handle() { return handle(*this); }
};
static_assert(fifo<block_based_queue<std::uint64_t, 8, 7, std::uint8_t>, std::uint64_t>);

template <typename T, std::size_t MIN_BLOCKS_PER_WINDOW = 1, std::size_t CELLS_PER_BLOCK = 7, typename BITSET_T = std::uint8_t>
using bbq_min_block_count =
	block_based_queue<T, std::bit_width(std::max(8 * sizeof(BITSET_T),
		std::bit_ceil(MIN_BLOCKS_PER_WINDOW))) - 1, CELLS_PER_BLOCK, BITSET_T>;

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif // __GNUC__

#endif // RELAXED_FIFO_H_INCLUDED
