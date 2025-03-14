#ifndef RELAXED_FIFO_H_INCLUDED
#define RELAXED_FIFO_H_INCLUDED

#include <array>
#include <memory>
#include <atomic>
#include <random>
#include <new>
#include <optional>
#include <ostream>

#include "fifo.h"
#include "atomic_bitset.h"

#define LOG_WINDOW_MOVE 0

#if LOG_WINDOW_MOVE
#include <iostream>
#endif

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winterference-size"
#endif // __GNUC__

template <typename T, std::size_t BLOCKS_PER_WINDOW_RAW = 1, std::size_t CELLS_PER_BLOCK = 7, typename BITSET_TYPE = uint8_t>
class block_based_queue {
private:
	static constexpr std::size_t make_po2(std::size_t size) {
		std::size_t ret = 1;
		while (size > ret) {
			ret *= 2;
		}
		return ret;
	}

	// PO2 for modulo performance and at least as big as the bitset type.
	static constexpr std::size_t BLOCKS_PER_WINDOW = std::max(sizeof(BITSET_TYPE) * 8, make_po2(BLOCKS_PER_WINDOW_RAW));

	const std::size_t window_count;
	const std::size_t window_count_mod_mask;

	std::size_t size() const {
		return window_count * BLOCKS_PER_WINDOW * CELLS_PER_BLOCK;
	}

	static_assert(sizeof(T) == 8);
	static_assert(sizeof(std::atomic<T>) == 8);

	struct header_t {
		// 16 bits epoch, 16 bits read started index, 16 bits read finished index, 16 bits write index
		std::atomic_uint64_t epoch_and_indices;
	};
	static_assert(sizeof(header_t) == 8);

	// We use 64 bit return types here to avoid potential deficits through 16-bit comparisons.
	static constexpr std::uint16_t get_epoch(std::uint64_t ei) { return ei >> 48; }
	static constexpr std::uint16_t get_read_started_index(std::uint64_t ei) { return (ei >> 32) & 0xffff; }
	static constexpr std::uint16_t get_read_finished_index(std::uint64_t ei) { return (ei >> 16) & 0xffff; }
	static constexpr std::uint16_t get_write_index(std::uint64_t ei) { return ei & 0xffff; }
	static constexpr std::uint64_t epoch_to_header(std::uint64_t epoch) { return epoch << 48; }

	struct block_t {
		header_t header;
		std::array<std::atomic<T>, CELLS_PER_BLOCK> cells;
	};
	static_assert(sizeof(block_t) == CELLS_PER_BLOCK * sizeof(T) + sizeof(header_t));

	struct window_t {
		alignas(std::hardware_destructive_interference_size) atomic_bitset<BLOCKS_PER_WINDOW, BITSET_TYPE> filled_set;
		alignas(std::hardware_destructive_interference_size) block_t blocks[BLOCKS_PER_WINDOW];
	};

	const std::unique_ptr<window_t[]> buffer;

	window_t& get_window(std::size_t index) const {
		return buffer[index & window_count_mod_mask];
	}

	alignas(std::hardware_destructive_interference_size) std::atomic_uint64_t read_window;
	alignas(std::hardware_destructive_interference_size) std::atomic_uint64_t write_window;

public:
	// TODO: Remove unused parameter!!
	block_based_queue([[maybe_unused]] int thread_count, std::size_t size) :
			window_count(std::max<std::size_t>(4, make_po2(size / BLOCKS_PER_WINDOW / CELLS_PER_BLOCK))),
			window_count_mod_mask(window_count - 1),
			buffer(std::make_unique<window_t[]>(window_count)) {
		read_window = window_count;
		write_window = window_count + 1;
		for (std::size_t i = 1; i < window_count; i++) {
			window_t& window = buffer[i];
			for (std::size_t j = 0; j < BLOCKS_PER_WINDOW; j++) {
				header_t& header = window.blocks[j].header;
				header.epoch_and_indices = (window_count + i) << 48;
			}
		}
		window_t& window = buffer[0];
		for (std::size_t j = 0; j < BLOCKS_PER_WINDOW; j++) {
			header_t& header = window.blocks[j].header;
			header.epoch_and_indices = (window_count * 2) << 48;
		}
	}

	std::ostream& operator<<(std::ostream& os) const {
		os << "Printing block_based_queue:\n"
			<< "Read: " << read_window << "; Write: " << write_window << '\n';
		for (std::size_t i = 0; i < window_count; i++) {
			for (std::size_t j = 0; j < BLOCKS_PER_WINDOW; j++) {
				std::uint64_t val = buffer[i].blocks[j].header.epoch_and_indices;
				os << get_epoch(val) << " " << get_read_started_index(val) << " " << get_read_finished_index(val) << " " << get_write_index(val) << " | ";
			}
			os << "\n======================\n";
		}
		return os;
	}

	class handle {
	private:
		block_based_queue& fifo;

		// Doing it like this avoids having to have a special case for first-time initialization, while only claiming a block on first use.
		// TODO: Could this cause issues? Even though it is thread_local it may be shared across handles!
		static inline thread_local block_t dummy_block{header_t{0xffffull << 48}, {}};
	
		block_t* read_block = &dummy_block;
		block_t* write_block = &dummy_block;

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
#if LOG_WINDOW_MOVE
					std::cout << "Write move " << (window_index + 1) << std::endl;
#endif // LOG_WINDOW_MOVE
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
						for (std::size_t i = 0; i < BLOCKS_PER_WINDOW; i++) {
							// We can't rely on the bitset here because it might be experiencing a spurious claim.

							std::uint64_t ei = epoch_to_header(write_window); // All empty with current epoch.
							new_window.blocks[i].header.epoch_and_indices.compare_exchange_strong(ei, next_epoch, std::memory_order_relaxed);
						}
						fifo.write_window.compare_exchange_strong(write_window, write_window + 1, std::memory_order_relaxed);
#if LOG_WINDOW_MOVE
						std::cout << "Write force move " << (write_window + 1) << std::endl;
#endif // LOG_WINDOW_MOVE
					}

					fifo.read_window.compare_exchange_strong(window_index, window_index + 1, std::memory_order_relaxed);
#if LOG_WINDOW_MOVE
					std::cout << "Read move " << (window_index + 1) << std::endl;
#endif // LOG_WINDOW_MOVE
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
			std::uint16_t index;

			bool failure = true;
			while (failure) {
				T old = 0;
				while (get_epoch(ei) != static_cast<std::uint16_t>(write_window) || (index = get_write_index(ei)) == CELLS_PER_BLOCK
					|| !write_block->cells[index].compare_exchange_weak(old, std::move(t), std::memory_order_relaxed)) {
					if (!claim_new_block_write()) {
						return false;
					}
					header = &write_block->header;
					ei = header->epoch_and_indices.load(std::memory_order_relaxed);
					old = 0;
				}

				failure = !header->epoch_and_indices.compare_exchange_strong(ei, ei + 1, std::memory_order_relaxed);
				if (failure) {
					// The header changed, we need to undo our write and try again.
					write_block->cells[index].store(0, std::memory_order_relaxed);
					// We do NOT unclaim the block's bit here, readers handle empty blocks by themselves.
				}
			}

			return true;
		}

		std::optional<T> pop() {
			header_t* header = &read_block->header;
			std::uint64_t ei = header->epoch_and_indices.load(std::memory_order_relaxed);
			std::uint16_t index;

			while (get_epoch(ei) != static_cast<std::uint16_t>(read_window) || (index = get_read_started_index(ei)) == get_write_index(ei)
				|| !header->epoch_and_indices.compare_exchange_weak(ei, ei + (1ull << 32), std::memory_order_relaxed)) {
				if (!claim_new_block_read()) {
					return std::nullopt;
				}
				header = &read_block->header;
				// TODO: With 2 threads there seems to exist a condition where suspiciously low epochs are encountered
				// and the blocks immediately abandoned. Is this just because of overflowing? Investigate.
				ei = header->epoch_and_indices.load(std::memory_order_relaxed);
				if (get_write_index(ei) == 0 && get_epoch(ei) == static_cast<std::uint16_t>(read_window)) {
					// We need this in case of a spurious claim where a bit was claimed, but the writer couldn't place an element inside,
					// because the write window was already forced-moved.
					if (header->epoch_and_indices.compare_exchange_strong(ei, (read_window + fifo.window_count) << 48, std::memory_order_relaxed)) {
						// We're abandoning an empty block!
						window_t& window = fifo.get_window(read_window);
						auto diff = read_block - window.blocks;
						window.filled_set.reset(diff, std::memory_order_relaxed);
					}
					// If the CAS fails, the only thing that could've occurred was the write index being increased,
					// making us able to read an element from the block.
				}
			}

			T ret = read_block->cells[index].exchange(0, std::memory_order_relaxed);
			assert(ret != 0);

			std::uint16_t finished_index = get_read_finished_index(header->epoch_and_indices.fetch_add(1 << 16, std::memory_order_relaxed)) + 1;
			// We need the >= here because between the read of ei and the fetch_add above both a write and a finished read might have occurred
			// that make our finished_index > our (outdated) write index.
			if (finished_index >= get_write_index(ei)) {
				// Apply local read index update.
				ei = (ei & (0xffffull << 48)) | (static_cast<std::uint64_t>(finished_index) << 32) | (static_cast<std::uint64_t>(finished_index) << 16) | finished_index;
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
static_assert(fifo<block_based_queue<std::uint64_t>, std::uint64_t>);

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif // __GNUC__

#endif // RELAXED_FIFO_H_INCLUDED
