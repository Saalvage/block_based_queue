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

	BLOCK_T* try_get_write_block(int starting_bit) {
		std::size_t free_bit = filled_set.template claim_bit<claim_value::ZERO, claim_mode::READ_WRITE>(starting_bit);
		if (free_bit == std::numeric_limits<std::size_t>::max()) {
			return nullptr;
		}
		return &blocks[free_bit];
	}

	BLOCK_T* try_get_read_block(int starting_bit) {
		std::size_t free_bit = filled_set.template claim_bit<claim_value::ONE, claim_mode::READ_ONLY>(starting_bit);
		if (free_bit == std::numeric_limits<std::size_t>::max()) {
			return nullptr;
		}
		return &blocks[free_bit];
	}
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
	std::size_t window_count_log2;

	std::size_t capacity() const {
		return window_count * blocks_per_window * CELLS_PER_BLOCK;
	}

#if BBQ_DEBUG_FUNCTIONS
	std::size_t size_full() const {
		std::size_t filled_cells = 0;
		for (std::size_t i = 0; i < window_count; i++) {
			for (std::size_t j = 0; j < blocks_per_window; j++) {
				auto ei = buffer[i].blocks[j].header.epoch_and_indices.load();
				filled_cells += get_write_index(ei) - get_read_finished_index(ei);
			}
		}
		return filled_cells;
	}

	std::size_t size() const {
		std::size_t filled_cells = 0;
		for (std::size_t i = read_window; i <= write_window; i++) {
			for (std::size_t j = 0; j < blocks_per_window; j++) {
				auto ei = buffer[i].blocks[j].header.epoch_and_indices.load();
				filled_cells += get_write_index(ei) - get_read_index(ei);
			}
		}
		return filled_cells;
	}
#endif // BBQ_RELAXED_DEBUG_FUNCTIONS

	// We use 64 bit return types here to avoid potential deficits through 16-bit comparisons.
	static constexpr std::uint64_t get_epoch(std::uint64_t ei) { return ei >> 48 & 0xffff; }
	static constexpr std::uint64_t mask_epoch(std::uint64_t window_index) { return window_index & 0xffff; }
	static constexpr std::uint64_t get_read_index(std::uint64_t ei) { return (ei >> 32) & 0xffff; }
	static constexpr std::uint64_t get_write_index(std::uint64_t ei) { return ei & 0xffff; }
	static constexpr std::uint64_t increment_write_index(std::uint64_t ei) { return ei + 1; }
	static constexpr std::uint64_t increment_read_index(std::uint64_t ei) { return ei + (1ull << 32); }
	static constexpr std::uint64_t epoch_to_header(std::uint64_t epoch) { return epoch << 48; }

	using block_t = block<T, CELLS_PER_BLOCK>;
	static_assert(sizeof(block_t) == CELLS_PER_BLOCK * sizeof(T) + sizeof(header_t));

	// Doing it like this avoids having to have a special case for first-time initialization, while only claiming a block on first use.
	static inline block_t dummy_block{ header_t{0xffffull << 48}, {} };

	using window_t = window<block_t, blocks_per_window, BITSET_T>;
	std::unique_ptr<window_t[]> buffer;

	std::uint64_t window_to_epoch(std::uint64_t window) const {
		return window >> window_count_log2;
	}

	window_t& index_to_window(std::size_t index) const {
		return buffer[index & window_count_mod_mask];
	}

	window_t& block_to_window(block_t* block) const {
		return buffer[(reinterpret_cast<char*>(block) - reinterpret_cast<const char*>(buffer.get())) / sizeof(window_t)];
	}

	alignas(std::hardware_destructive_interference_size) std::atomic_uint64_t read_window = 0;
	alignas(std::hardware_destructive_interference_size) std::atomic_uint64_t write_window = 1;

public:
	// TODO: Remove unused parameter!!
	block_based_queue([[maybe_unused]] int thread_count, std::size_t min_size) :
			window_count(std::max<std::size_t>(4, std::bit_ceil(min_size / blocks_per_window / CELLS_PER_BLOCK))),
			window_count_mod_mask(window_count - 1),
			window_count_log2(std::bit_width(window_count) - 1),
			buffer(std::make_unique<window_t[]>(window_count)) {
#if BBQ_LOG_CREATION_SIZE
		std::cout << "Window count: " << window_count << std::endl;
		std::cout << "Block count: " << blocks_per_window << std::endl;
#endif // BBQ_LOG_CREATION_SIZE

		window_t& window = buffer[0];
		for (std::size_t j = 0; j < blocks_per_window; j++) {
			header_t& header = window.blocks[j].header;
			header.epoch_and_indices = epoch_to_header(1);
		}
	}

#if BBQ_DEBUG_FUNCTIONS
	std::ostream& operator<<(std::ostream& os) const {
		os << "Printing block_based_queue:\n"
			<< "Read: " << read_window << "; Write: " << write_window << '\n';
		for (std::size_t i = 0; i < window_count; i++) {
			for (std::size_t j = 0; j < blocks_per_window; j++) {
				std::uint64_t val = buffer[i].blocks[j].header.epoch_and_indices;
				os << get_epoch(val) << " " << get_read_index(val) << " " << " " << get_write_index(val) << " | ";
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

		std::uint64_t write_epoch = 0;
		std::uint64_t read_epoch = 0;

		std::minstd_rand rng;

		handle(block_based_queue& fifo, std::random_device::result_type seed) : fifo(fifo), rng(seed) { }

		friend block_based_queue;

		int random_bit_index() {
			return std::uniform_int_distribution<int>(0, blocks_per_window - 1)(rng);
		}

		bool claim_new_block_write() {
			block_t* new_block;
			std::uint64_t window_index;
			do {
				window_index = fifo.write_window.load(std::memory_order_relaxed);
				new_block = fifo.index_to_window(window_index).try_get_write_block(random_bit_index());
				if (new_block == nullptr) {
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

			write_epoch = fifo.window_to_epoch(window_index);
			write_block = new_block;
			return true;
		}

		bool claim_new_block_read() {
			block_t* new_block;
			std::uint64_t window_index;
			do {
				window_index = fifo.read_window.load(std::memory_order_relaxed);
				new_block = fifo.index_to_window(window_index).try_get_read_block(random_bit_index());
				if (new_block == nullptr) {
					std::uint64_t write_window = fifo.write_window.load(std::memory_order_relaxed);
					if (write_window == window_index + 1) {
						if (!fifo.index_to_window(write_window).filled_set.any(std::memory_order_relaxed)) {
							return false;
						}
						// TODO: This should be simplifiable? Spurious block claims only occur when force-moving.
						// Before we force-move the write window, there might be unclaimed blocks in the current one.
						// We need to make sure we clean those up BEFORE we move the write window in order to prevent
						// the read window from being moved before all blocks have either been claimed or invalidated.
						window_t& new_window = fifo.index_to_window(write_window);
						std::uint64_t next_epoch = epoch_to_header(fifo.window_to_epoch(write_window) + 1);
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

					// TODO: Remove this.
					bool all_correct = true;
					window_t& new_window = fifo.index_to_window(window_index);
					std::uint64_t next_epoch = epoch_to_header(fifo.window_to_epoch(window_index) + 1);
					for (std::size_t i = 0; i < blocks_per_window; i++) {
						if (get_epoch(new_window.blocks[i].header.epoch_and_indices) != get_epoch(next_epoch)) {
							new_window.filled_set.set(i);
							all_correct = false;
							break;
						}
					}
					if (all_correct) {
						fifo.read_window.compare_exchange_strong(window_index, window_index + 1, std::memory_order_relaxed);
					}
#if BBQ_LOG_WINDOW_MOVE
					std::cout << "Read move " << (window_index + 1) << std::endl;
#endif // BBQ_LOG_WINDOW_MOVE
				} else {
					break;
				}
			} while (true);

			read_epoch = fifo.window_to_epoch(window_index);
			read_block = new_block;
			return true;
		}

	public:
		bool push(T t) {
			assert(t != 0);

			header_t* header = &write_block->header;
			std::uint64_t ei = header->epoch_and_indices.load(std::memory_order_relaxed);
			std::uint64_t index;

			bool failure = true;
			while (failure) {
				T old = 0;
				while (get_epoch(ei) != mask_epoch(write_epoch) || (index = get_write_index(ei)) == CELLS_PER_BLOCK
					|| !write_block->cells[index].compare_exchange_weak(old, std::move(t), std::memory_order_relaxed)) {
					if (!claim_new_block_write()) {
						return false;
					}
					header = &write_block->header;
					ei = header->epoch_and_indices.load(std::memory_order_relaxed);
					old = 0;
				}

				failure = !header->epoch_and_indices.compare_exchange_strong(ei, increment_write_index(ei), std::memory_order_release);
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
			std::uint64_t index;

			while (get_epoch(ei) != mask_epoch(read_epoch) || (index = get_read_index(ei)) == get_write_index(ei)
				|| !header->epoch_and_indices.compare_exchange_weak(ei, increment_read_index(ei), std::memory_order_acquire)) {
				if (!claim_new_block_read()) {
					return std::nullopt;
				}
				header = &read_block->header;
				// TODO: With 2 threads there seems to exist a condition where suspiciously low epochs are encountered
				// and the blocks immediately abandoned. Is this just because of overflowing? Investigate.
				ei = header->epoch_and_indices.load(std::memory_order_relaxed);
				// TODO: The problem here is that if epoch == (read_window + fifo.window_count) we only reset the bit, which might lead to lost
				// writes, because for the write the block header didn't change, so it fills the block, but the bit is unset.
				// But if we simply ignore that case, then we're stuck because the bit will be set forever.
				// We cannot differentiate if it is a spurious claim from the LAST epoch (must reset bit),
				// or a regular one from the current one (must NOT reset bit).
				if (get_write_index(ei) == get_read_index(ei)) {
					// We need this in case of a spurious claim where a bit was claimed, but the writer couldn't place an element inside,
					// because the write window was already forced-moved.
					if (header->epoch_and_indices.compare_exchange_strong(ei, epoch_to_header(read_epoch + 1), std::memory_order_relaxed)) {
						// We're abandoning an empty block!
						window_t& window = fifo.block_to_window(read_block);
						auto diff = read_block - window.blocks;
						window.filled_set.reset(diff, std::memory_order_relaxed);
					}
					// If the CAS fails, the only thing that could've occurred was the write index being increased,
					// making us able to read an element from the block.
				}
			}

			T ret = read_block->cells[index].exchange(0, std::memory_order_relaxed);
			assert(ret != 0);

			if (index == get_write_index(ei)) {
				// Apply local read index update.
				ei = (ei & 0xffff'0000'ffff'ffffull) | (index << 32);
				// Before we mark this block as empty, we make it unavailable for other readers and writers of this epoch.
				if (header->epoch_and_indices.compare_exchange_strong(ei, epoch_to_header(read_epoch + 1), std::memory_order_relaxed)) {
					window_t& window = fifo.block_to_window(read_block);
					auto diff = read_block - window.blocks;
					window.filled_set.reset(diff, std::memory_order_relaxed);
				}
				// else a different read thread has already wrapped up this block.
			}

			return ret;
		}
	};

	handle get_handle() { return handle(*this, std::random_device()()); }
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
