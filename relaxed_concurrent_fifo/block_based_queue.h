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
	// 32 bits epoch, 16 bits read index, 16 bits write index
	std::atomic_uint64_t epoch_and_indices;
};

template <typename T>
struct block {
	static_assert(sizeof(header_t) == 8);
	header_t header;
#pragma warning(disable: 4200)
	std::atomic<T> cells[];  // NOLINT(clang-diagnostic-c99-extensions)
};

template <typename T, typename BITSET_T = std::uint8_t>
class block_based_queue {
private:
	std::size_t blocks_per_window;

	std::size_t window_count;
	std::size_t window_count_mod_mask;
	std::size_t window_count_log2;

	std::size_t cells_per_block;
	std::size_t block_size;

	std::size_t capacity() const {
		return window_count * blocks_per_window * cells_per_block;
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
	static constexpr std::uint64_t get_epoch(std::uint64_t ei) { return ei >> 32; }
	static constexpr std::uint64_t get_read_index(std::uint64_t ei) { return (ei >> 16) & 0xffff; }
	static constexpr std::uint64_t get_write_index(std::uint64_t ei) { return ei & 0xffff; }
	static constexpr std::uint64_t increment_write_index(std::uint64_t ei) { return ei + 1; }
	static constexpr std::uint64_t increment_read_index(std::uint64_t ei) { return ei + (1ull << 16); }
	static constexpr std::uint64_t epoch_to_header(std::uint64_t epoch) { return epoch << 32; }

	using block_t = block<T>;

	// Doing it like this avoids having to have a special case for first-time initialization, while only claiming a block on first use.
	static inline block_t dummy_block{ header_t{epoch_to_header(0xffff'ffffull)}, {} };

	atomic_bitset<BITSET_T> filled_set;
	std::unique_ptr<std::byte[]> buffer;

	std::uint64_t window_to_epoch(std::uint64_t window) const {
		return window >> window_count_log2;
	}

	std::uint64_t window_to_index(std::uint64_t index) const {
		return index & window_count_mod_mask;
	}

	block_t& get_block(std::uint64_t window_index, std::uint64_t block_index) {
		return *std::launder(reinterpret_cast<block_t*>(&buffer[(window_index * blocks_per_window + block_index) * block_size]));
	}

	std::size_t block_index(std::uint64_t window_index, block_t* block) {
		return (reinterpret_cast<std::byte*>(block) - reinterpret_cast<std::byte*>(&get_block(window_index, 0))) / block_size;
	}

	block_t* try_get_write_block(std::uint64_t window_index, int starting_bit, std::uint64_t epoch) {
		auto index = window_to_index(window_index);
		std::size_t free_bit = filled_set.template claim_bit<claim_value::ZERO, claim_mode::READ_WRITE>(index, starting_bit, epoch, std::memory_order_relaxed);
		if (free_bit == std::numeric_limits<std::size_t>::max()) {
			return nullptr;
		}
		return &get_block(index, free_bit);
	}

	block_t* try_get_read_block(std::uint64_t window_index, int starting_bit, std::uint64_t epoch) {
		auto index = window_to_index(window_index);
		std::size_t free_bit = filled_set.template claim_bit<claim_value::ONE, claim_mode::READ_ONLY>(index, starting_bit, epoch, std::memory_order_relaxed);
		if (free_bit == std::numeric_limits<std::size_t>::max()) {
			return nullptr;
		}
		return &get_block(index, free_bit);
	}

	alignas(std::hardware_destructive_interference_size) std::atomic_uint64_t read_window = 0;
	alignas(std::hardware_destructive_interference_size) std::atomic_uint64_t write_window = 1;

	static constexpr std::size_t align_cache_line_size(std::size_t size) {
		std::size_t ret = std::hardware_destructive_interference_size;
		while (ret < size) {
			ret += std::hardware_destructive_interference_size;
		}
		return ret;
	}

public:
	block_based_queue(int thread_count, std::size_t min_size, std::size_t blocks_per_window_per_thread, std::size_t cells_per_block) :
			blocks_per_window(std::bit_ceil(std::max(sizeof(BITSET_T) * 8, thread_count * blocks_per_window_per_thread))),
			window_count(std::max<std::size_t>(4, std::bit_ceil(min_size / blocks_per_window / cells_per_block))),
			window_count_mod_mask(window_count - 1),
			window_count_log2(std::bit_width(window_count) - 1),
			cells_per_block(cells_per_block),
			block_size(align_cache_line_size(sizeof(header_t) + cells_per_block * sizeof(T))),
			filled_set(window_count, blocks_per_window),
			buffer(std::make_unique<std::byte[]>(window_count * blocks_per_window * block_size)) {
#if BBQ_LOG_CREATION_SIZE
		std::cout << "Window count: " << window_count << std::endl;
		std::cout << "Block count: " << blocks_per_window << std::endl;
#endif // BBQ_LOG_CREATION_SIZE

		// At least as big as the bitset's type.
		assert(blocks_per_window >= sizeof(BITSET_T) * 8);
		assert(std::bit_ceil(blocks_per_window) == blocks_per_window);

		for (std::size_t j = 0; j < blocks_per_window; j++) {
			filled_set.set_epoch_if_empty(0, 0);
			header_t& header = get_block(0, j).header;
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

		std::uint64_t read_window = 0;
		std::uint64_t write_window = 0;

		std::uint64_t write_epoch = 0;
		std::uint64_t read_epoch = 0;

		block_t* read_block = &dummy_block;
		block_t* write_block = &dummy_block;

		std::minstd_rand rng;

		handle(block_based_queue& fifo, std::random_device::result_type seed) : fifo(fifo), rng(seed) { }

		friend block_based_queue;

		int random_bit_index() {
			// TODO: Probably want to store this somewhere.
			return std::uniform_int_distribution(0, static_cast<int>(fifo.blocks_per_window - 1))(rng);
		}

		bool claim_new_block_write() {
			block_t* new_block;
			std::uint64_t window_index;
			do {
				window_index = fifo.write_window.load(std::memory_order_relaxed);
				new_block = fifo.try_get_write_block(fifo.window_to_index(window_index), random_bit_index(), fifo.window_to_epoch(window_index));
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

			write_window = fifo.window_to_index(window_index);
			write_epoch = fifo.window_to_epoch(window_index);
			write_block = new_block;
			return true;
		}

		bool claim_new_block_read() {
			block_t* new_block;
			std::uint64_t window_index;
			do {
				window_index = fifo.read_window.load(std::memory_order_relaxed);
				new_block = fifo.try_get_read_block(fifo.window_to_index(window_index), random_bit_index(), fifo.window_to_epoch(window_index));
				if (new_block == nullptr) {
					std::uint64_t write_window = fifo.write_window.load(std::memory_order_relaxed);
					if (write_window == window_index + 1) {
						std::uint64_t write_epoch = fifo.window_to_epoch(write_window);
						std::uint64_t write_window_index = fifo.window_to_index(write_window);
						if (!fifo.filled_set.any(write_window_index, write_epoch, std::memory_order_relaxed)) {
							return false;
						}
						// TODO: This should be simplifiable? Spurious block claims only occur when force-moving.
						// Before we force-move the write window, there might be unclaimed blocks in the current one.
						// We need to make sure we clean those up BEFORE we move the write window in order to prevent
						// the read window from being moved before all blocks have either been claimed or invalidated.
						std::uint64_t next_ei = epoch_to_header(write_epoch + 1);
						fifo.filled_set.set_epoch_if_empty(write_window_index, write_epoch, std::memory_order_relaxed);
						for (std::size_t i = 0; i < fifo.blocks_per_window; i++) {
							std::uint64_t ei = epoch_to_header(write_epoch); // All empty with current epoch.
							fifo.get_block(write_window_index, i).header.epoch_and_indices.compare_exchange_strong(ei, next_ei, std::memory_order_relaxed);
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

			read_window = fifo.window_to_index(window_index);
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
				while (get_epoch(ei) != write_epoch || (index = get_write_index(ei)) == fifo.cells_per_block
					|| !write_block->cells[index].compare_exchange_weak(old, t, std::memory_order_relaxed)) {
					if (!claim_new_block_write()) {
						return false;
					}
					header = &write_block->header;
					ei = header->epoch_and_indices.load(std::memory_order_relaxed);
					old = 0;
				}

				failure = !header->epoch_and_indices.compare_exchange_strong(ei, increment_write_index(ei),
					std::memory_order_release, std::memory_order_relaxed);
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

			while (true) {
				if (get_epoch(ei) == read_epoch) {
					if ((index = get_read_index(ei)) + 1 == get_write_index(ei)) {
						if (header->epoch_and_indices.compare_exchange_weak(ei, epoch_to_header(read_epoch + 1), std::memory_order_acquire, std::memory_order_relaxed)) {
							fifo.filled_set.reset(read_window, fifo.block_index(read_window, read_block), read_epoch, std::memory_order_relaxed);
							break;
						}
					} else {
						if (header->epoch_and_indices.compare_exchange_weak(ei, increment_read_index(ei), std::memory_order_acquire, std::memory_order_relaxed)) {
							break;
						}
					}
				}
				if (!claim_new_block_read()) {
					return std::nullopt;
				}
				header = &read_block->header;
				ei = header->epoch_and_indices.load(std::memory_order_relaxed);
				if (get_write_index(ei) == 0) {
					// We need this in case of a spurious claim where a bit was claimed, but the writer couldn't place an element inside,
					// because the write window was already forced-moved.
					// TODO: Is it necessary to check get_epoch(ei) == read_epoch here?
					// It seems practically irrelevant, but it could theoretically happen that the epoch has advanced
					// twice before the ei load, leading us to set it back by one here.
					// Important: Consider reader becoming dormant after updating header BEFORE resetting bitset.
					if (header->epoch_and_indices.compare_exchange_strong(ei, epoch_to_header(get_epoch(ei) + 1), std::memory_order_relaxed)) {
						// We're abandoning an empty block!
						fifo.filled_set.reset(read_window, fifo.block_index(read_window, read_block), read_epoch, std::memory_order_relaxed);
					}
					// If the CAS fails, the only thing that could've occurred was the write index being increased,
					// making us able to read an element from the block.
				}
			}

			T ret = read_block->cells[index].exchange(0, std::memory_order_relaxed);
			assert(ret != 0);
			return ret;
		}
	};

	handle get_handle() { return handle(*this, std::random_device()()); }
};
static_assert(fifo<block_based_queue<std::uint64_t>, std::uint64_t>);

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif // __GNUC__

#endif // RELAXED_FIFO_H_INCLUDED
