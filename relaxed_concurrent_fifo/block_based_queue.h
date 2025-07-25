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
#include "atomic_bitset_no_epoch.h"

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
	std::size_t blocks_per_window;
	std::uniform_int_distribution<int> window_block_distribution;

	std::size_t window_count;
	std::size_t window_count_mod_mask;
	std::size_t window_count_log2;

	std::size_t cells_per_block;
	std::size_t block_size;

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

	atomic_bitset_no_epoch<BITSET_T> touched_set;
	atomic_bitset<BITSET_T> filled_set;
	std::unique_ptr<std::byte[]> buffer;

	std::uint64_t window_to_epoch(std::uint64_t window) const {
		return window >> window_count_log2;
	}

	std::uint64_t window_to_index(std::uint64_t index) const {
		return index & window_count_mod_mask;
	}

	block_t get_block(std::uint64_t window_index, std::uint64_t block_index) {
		return &buffer[(window_index * blocks_per_window + block_index) * block_size];
	}

	std::size_t block_index(std::uint64_t window_index, block_t block) {
		return (block.ptr - get_block(window_index, 0).ptr) / block_size;
	}

	block_t try_get_write_block(std::uint64_t window_index, int starting_bit, std::uint64_t epoch) {
		auto index = window_to_index(window_index);
		std::size_t free_bit = filled_set.template claim_bit<claim_value::ZERO, claim_mode::READ_WRITE>(index, starting_bit, epoch, std::memory_order_relaxed);
		if (free_bit == std::numeric_limits<std::size_t>::max()) {
			return nullptr;
		}
		// The touched set update can be missed, which might trigger a reader to attempt to move,
		// but the filled set will prevent the move from occuring.
		touched_set.set(index, free_bit, std::memory_order_relaxed);
		return get_block(index, free_bit);
	}

	block_t try_get_free_read_block(std::uint64_t window_index, int starting_bit) {
		auto index = window_to_index(window_index);
		std::size_t free_bit = touched_set.template claim_bit<claim_value::ONE, claim_mode::READ_WRITE>(index, starting_bit, std::memory_order_relaxed);
		if (free_bit == std::numeric_limits<std::size_t>::max()) {
			return nullptr;
		}
		return get_block(index, free_bit);
	}

	block_t try_get_any_read_block(std::uint64_t window_index, int starting_bit, std::uint64_t epoch) {
		auto index = window_to_index(window_index);
		std::size_t free_bit = filled_set.template claim_bit<claim_value::ONE, claim_mode::READ_ONLY>(index, starting_bit, epoch, std::memory_order_relaxed);
		if (free_bit == std::numeric_limits<std::size_t>::max()) {
			return nullptr;
		}
		return get_block(index, free_bit);
	}

	alignas(std::hardware_destructive_interference_size) std::atomic_uint64_t global_read_window = 0;
	alignas(std::hardware_destructive_interference_size) std::atomic_uint64_t global_write_window = 1;

	static constexpr std::size_t align_cache_line_size(std::size_t size) {
		std::size_t ret = std::hardware_destructive_interference_size;
		while (ret < size) {
			ret += std::hardware_destructive_interference_size;
		}
		return ret;
	}

public:
	block_based_queue(int thread_count, std::size_t min_size, double blocks_per_window_per_thread, std::size_t cells_per_block) :
			blocks_per_window(std::bit_ceil(std::max<std::size_t>(sizeof(BITSET_T) * 8,
				std::lround(thread_count * blocks_per_window_per_thread)))),
			window_block_distribution(0, static_cast<int>(blocks_per_window - 1)),
			window_count(std::max<std::size_t>(4, std::bit_ceil(min_size / blocks_per_window / cells_per_block))),
			window_count_mod_mask(window_count - 1),
			window_count_log2(std::bit_width(window_count) - 1),
			cells_per_block(cells_per_block),
			block_size(align_cache_line_size(sizeof(std::atomic_uint64_t) + cells_per_block * sizeof(T))),
			touched_set(window_count, blocks_per_window),
			filled_set(window_count, blocks_per_window),
			buffer(std::make_unique<std::byte[]>(window_count * blocks_per_window * block_size)) {
#if BBQ_LOG_CREATION_SIZE
		std::cout << "Window count: " << window_count << std::endl;
		std::cout << "Block count: " << blocks_per_window << std::endl;
#endif // BBQ_LOG_CREATION_SIZE

		// At least as big as the bitset's type.
		assert(blocks_per_window >= sizeof(BITSET_T) * 8);
		assert(std::bit_ceil(blocks_per_window) == blocks_per_window);

		for (std::size_t i = 0; i < window_count * blocks_per_window; i++) {
			auto ptr = buffer.get() + i * block_size;
			new (ptr) std::atomic_uint64_t{ 0 };
			for (std::size_t j = 0; j < cells_per_block; j++) {
				new (ptr + sizeof(std::atomic_uint64_t) + j * sizeof(T)) std::atomic<T>{ };
			}
		}

		for (std::size_t j = 0; j < blocks_per_window; j++) {
			filled_set.set_epoch_if_empty(0, 0);
			get_block(0, j).get_header() = epoch_to_header(1);
		}
	}

	std::size_t capacity() const {
		return window_count * blocks_per_window * cells_per_block;
	}

	std::size_t size_full() {
		std::size_t filled_cells = 0;
		for (std::size_t i = 0; i < window_count; i++) {
			for (std::size_t j = 0; j < blocks_per_window; j++) {
				std::uint64_t ei = get_block(i, j).get_header();
				filled_cells += get_write_index(ei) - get_read_index(ei);
			}
		}
		return filled_cells;
	}

	std::size_t size() {
		std::size_t filled_cells = 0;
		for (std::size_t i = global_read_window; i <= global_write_window; i++) {
			for (std::size_t j = 0; j < blocks_per_window; j++) {
				std::uint64_t ei = get_block(window_to_index(i), j).get_header();
				filled_cells += get_write_index(ei) - get_read_index(ei);
			}
		}
		return filled_cells;
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

		// Write equivalent not needed.
		std::uint64_t read_window = 0;

		// Write equivalent not needed.
		std::uint64_t read_window_index = 0;

		std::uint64_t write_epoch = 0;
		std::uint64_t read_epoch = 0;

		block_t read_block = dummy_block;
		block_t write_block = dummy_block;

		std::minstd_rand rng;

		handle(block_based_queue& fifo, std::random_device::result_type seed) : fifo(fifo), rng(seed) { }

		friend block_based_queue;

		// They're typed as uint64_t, but only hold 32 bits of data.
		static constexpr bool epoch_valid(std::uint64_t check, std::uint64_t curr) {
			return (curr - check) < std::numeric_limits<std::uint32_t>::max() / 2;
		}

		int random_bit_index() {
			return fifo.window_block_distribution(rng);
		}

		bool claim_new_block_write() {
			block_t new_block;
			std::uint64_t window_index;
			do {
				window_index = fifo.global_write_window.load(std::memory_order_relaxed);
				new_block = fifo.try_get_write_block(window_index, random_bit_index(), fifo.window_to_epoch(window_index));
				if (new_block.ptr == nullptr) {
					// No more free bits, we move.
					if (window_index + 1 - fifo.global_read_window.load(std::memory_order_relaxed) == fifo.window_count) {
						return false;
					}
					fifo.global_write_window.compare_exchange_strong(window_index, window_index + 1, std::memory_order_relaxed);
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
			block_t new_block;
			std::uint64_t window_index;
			bool dont_advance = false;
			do {
				bool is_ahead = false;
				window_index = fifo.global_read_window.load(std::memory_order_relaxed);
				if (!dont_advance && window_index + 1 == read_window) {
					is_ahead = true;
					window_index = read_window;
				}
				new_block = fifo.try_get_free_read_block(window_index, random_bit_index());
				if (new_block.ptr == nullptr) {
					if (is_ahead) {
						dont_advance = true;
						continue;
					}

					std::uint64_t write_window = fifo.global_write_window.load(std::memory_order_relaxed);

					// Don't go ahead if write_window is just ahead of us (so we don't have to force move).
					if (!dont_advance && window_index + 1 != write_window) {
						read_window = window_index + 1;
						// A few redundant ops, fine for now.
						continue;
					}

					new_block = fifo.try_get_any_read_block(window_index, random_bit_index(), fifo.window_to_epoch(window_index));
					if (new_block.ptr != nullptr) {
						break;
					}

					if (write_window == window_index + 1) {
						std::uint64_t write_epoch = fifo.window_to_epoch(write_window);
						std::uint64_t write_window_index = fifo.window_to_index(write_window);
						if (!fifo.filled_set.any(write_window_index, write_epoch, std::memory_order_relaxed)) {
							return false;
						}

						// Before we force-move the write window, there might be unclaimed blocks in the current one.
						// We need to make sure we clean those up BEFORE we move the write window in order to prevent
						// the read window from being moved before all blocks have either been claimed or invalidated.
						fifo.filled_set.set_epoch_if_empty(write_window_index, write_epoch, std::memory_order_relaxed);
						fifo.global_write_window.compare_exchange_strong(write_window, write_window + 1, std::memory_order_relaxed);
#if BBQ_LOG_WINDOW_MOVE
						std::cout << "Write force move " << (write_window + 1) << std::endl;
#endif // BBQ_LOG_WINDOW_MOVE
					}

					fifo.global_read_window.compare_exchange_strong(window_index, window_index + 1, std::memory_order_relaxed);
#if BBQ_LOG_WINDOW_MOVE
					std::cout << "Read move " << (window_index + 1) << std::endl;
#endif // BBQ_LOG_WINDOW_MOVE
				} else {
					break;
				}
			} while (true);

			read_window = window_index;
			read_window_index = fifo.window_to_index(window_index);
			read_epoch = fifo.window_to_epoch(window_index);
			read_block = new_block;
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

		std::optional<T> pop() {
			std::atomic_uint64_t* header = &read_block.get_header();
			std::uint64_t ei = header->load(std::memory_order_relaxed);
			std::uint64_t index;

			while (true) {
				if (epoch_valid(get_epoch(ei), read_epoch)) {
					if ((index = get_read_index(ei)) + 1 == get_write_index(ei)) {
						if (header->compare_exchange_weak(ei, epoch_to_header(read_epoch + 1), std::memory_order_acquire, std::memory_order_relaxed)) {
							fifo.filled_set.reset(read_window_index, fifo.block_index(read_window_index, read_block), read_epoch, std::memory_order_relaxed);
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
				if (get_write_index(ei) == 0) {
					// We need to consider two situations:
					// 1. A writer in the current epoch claimed this block, but never completed a full push, we update epoch & bitset.
					// 2. A force-move occured, the block had its epoch updated by force, a delayed writer claimed the bit,
					//    but can't write the header, we simply reset the bit (would fail anyway if epoch is incorrect).
					// In case 1. we invalidate both block and bitset, in case 2. block is already invalidated.
					if (!epoch_valid(get_epoch(ei), read_epoch) || header->compare_exchange_strong(ei, epoch_to_header(read_epoch + 1), std::memory_order_relaxed)) {
						fifo.filled_set.reset(read_window_index, fifo.block_index(read_window_index, read_block), read_epoch, std::memory_order_relaxed);
					}
					// If the CAS fails, the only thing that could've occurred was the write index being increased,
					// making us able to read an element from the block.
					// TODO: Maybe it's better to immediately claim a new block here?
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
