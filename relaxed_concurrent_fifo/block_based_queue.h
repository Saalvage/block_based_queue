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

#if BBQ_DEBUG_FUNCTIONS
#include <bitset>
#endif

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winterference-size"
#endif // __GNUC__

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
	static constexpr std::size_t blocks_per_superblock = sizeof(BITSET_T) * 8;

	std::size_t blocks_per_window;
	std::size_t superblocks_per_window;

	std::size_t window_count;

	std::size_t superblock_count;
	std::size_t superblock_count_log2;

	std::size_t block_count_mod_mask;

	std::size_t cells_per_block;
	std::size_t block_size;

	std::size_t capacity() const {
		return window_count * blocks_per_window * cells_per_block;
	}

#if BBQ_DEBUG_FUNCTIONS
	std::size_t size_full() {
		std::size_t filled_cells = 0;
		for (std::size_t i = 0; i < window_count; i++) {
			for (std::size_t j = 0; j < blocks_per_window; j++) {
				std::uint64_t ei = get_block(i * blocks_per_window + j).get_header();
				filled_cells += get_write_index(ei) - get_read_index(ei);
			}
		}
		return filled_cells;
	}

	std::size_t size() {
		std::size_t filled_cells = 0;
		for (std::size_t i = global_read_superblock; i < global_write_superblock + superblocks_per_window; i++) {
			for (std::size_t j = 0; j < blocks_per_superblock; j++) {
				std::uint64_t ei = get_block(i * blocks_per_superblock + j).get_header();
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
	static_assert(std::is_trivial_v<block_t>);

	// Doing it like this avoids having to have a special case for first-time initialization, while only claiming a block on first use.
	static inline std::atomic_uint64_t dummy_block_value{ epoch_to_header(0x1000'0000ull) };
	static inline block_t dummy_block{ reinterpret_cast<std::byte*>(&dummy_block_value) };

	atomic_bitset<BITSET_T> filled_set;
	std::unique_ptr<std::byte[]> buffer;

	block_t get_block(std::uint64_t block_index) {
		return &buffer[((block_index & block_count_mod_mask) * block_size)];
	}

	alignas(std::hardware_destructive_interference_size) std::atomic_uint64_t global_read_superblock;
	alignas(std::hardware_destructive_interference_size) std::atomic_uint64_t global_write_superblock;

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
			superblocks_per_window(blocks_per_window / blocks_per_superblock),
			window_count(std::max<std::size_t>(4, std::bit_ceil(min_size / blocks_per_window / cells_per_block))),
			superblock_count(superblocks_per_window * window_count),
			superblock_count_log2(std::bit_width(superblock_count) - 1),
			block_count_mod_mask(window_count * blocks_per_window - 1),
			cells_per_block(cells_per_block),
			block_size(align_cache_line_size(sizeof(std::atomic_uint64_t) + cells_per_block * sizeof(T))),
			filled_set(blocks_per_window * window_count, blocks_per_window),
			buffer(std::make_unique<std::byte[]>(window_count * blocks_per_window * block_size)) {
#if BBQ_LOG_CREATION_SIZE
		std::cout << "Window count: " << window_count << std::endl;
		std::cout << "Block count: " << blocks_per_window << std::endl;
#endif // BBQ_LOG_CREATION_SIZE

		// At least as big as the bitset's type.
		assert(blocks_per_window >= blocks_per_superblock);
		assert(std::bit_ceil(blocks_per_window) == blocks_per_window);

		for (std::size_t i = 0; i < window_count * blocks_per_window; i++) {
			auto ptr = buffer.get() + i * block_size;
			new (ptr) std::atomic_uint64_t{ 0 };
			for (std::size_t j = 0; j < cells_per_block; j++) {
				new (ptr + sizeof(std::atomic_uint64_t) + j * sizeof(T)) std::atomic<T>{ };
			}
		}

		global_read_superblock = 0;
		global_write_superblock = superblocks_per_window;

		for (std::size_t i = 0; i < superblocks_per_window; i++) {
			filled_set.set_epoch_if_empty(i);
		}

		for (std::size_t i = 0; i < blocks_per_window; i++) {
			get_block(i).get_header() = epoch_to_header(1);
		}
	}

#if BBQ_DEBUG_FUNCTIONS
	std::ostream& operator<<(std::ostream& os) {
		os << "Printing block_based_queue:\n"
			<< "Read: " << global_read_superblock << "; Write: " << global_write_superblock << '\n';
		for (std::size_t i = 0; i < superblock_count; i++) {
			std::uint64_t eb = filled_set.data[i]->load();
			os << std::bitset<8>((std::uint8_t)eb) << " " << (eb >> 32) << " | ";
			for (std::size_t j = 0; j < blocks_per_superblock; j++) {
				std::uint64_t val = get_block(i * blocks_per_superblock + j).get_header().load();
				os << get_epoch(val) << "  " << get_read_index(val) << " " << get_write_index(val) << " | ";
			}
			if (i == global_write_superblock % superblock_count) {
				os << " w";
			}
			if (i == global_read_superblock % superblock_count) {
				os << " r";
			}
			os << "\n";
		}
		return os;
	}
#endif // BBQ_RELAXED_DEBUG_FUNCTIONS

	class handle {
	private:
		block_based_queue& fifo;

		std::size_t read_superblock;
		std::size_t read_bit_index;

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

		void try_slide_read(std::uint64_t superblock_start) {
			if (superblock_start + fifo.superblocks_per_window != fifo.global_write_superblock.load(std::memory_order_relaxed)) {
				fifo.global_read_superblock.compare_exchange_strong(superblock_start, superblock_start + 1, std::memory_order_relaxed);
			}
		}

		int random_bit_index() {
			// TODO: Probably want to store this somewhere.
			return std::uniform_int_distribution(0, static_cast<int>(fifo.blocks_per_window - 1))(rng);
		}

		bool claim_new_block_write() {
			while (true) {
				std::uint64_t superblock_start = fifo.global_write_superblock.load(std::memory_order_relaxed);
				auto [new_block, should_advance] = fifo.filled_set.template claim_bit<claim_value::ZERO, claim_mode::READ_WRITE>(superblock_start, random_bit_index(), std::memory_order_relaxed);
				if (new_block == std::numeric_limits<std::size_t>::max()) [[unlikely]] {
					// TODO: Think about this again long and hard.
					auto max_movable = fifo.superblock_count - (superblock_start + fifo.superblocks_per_window - fifo.global_read_superblock.load(std::memory_order_relaxed));
					if (max_movable == 0) {
						return false;
					}
					fifo.global_write_superblock.compare_exchange_strong(superblock_start, superblock_start + std::min(fifo.superblocks_per_window, max_movable), std::memory_order_relaxed);
					continue;
				} else if (should_advance) {
					if (superblock_start + fifo.superblocks_per_window < fifo.global_read_superblock.load(std::memory_order_relaxed) + fifo.superblock_count) {
						fifo.global_write_superblock.compare_exchange_strong(superblock_start, superblock_start + 1, std::memory_order_relaxed);
					}
				}

				write_epoch = (new_block / blocks_per_superblock) >> fifo.superblock_count_log2;
				write_block = fifo.get_block(new_block);
				return true;
			}
		}


		bool claim_new_block_read() {
			while (true) {
				std::uint64_t superblock_start = fifo.global_read_superblock.load(std::memory_order_relaxed);
				auto [new_block, should_advance] = fifo.filled_set.template claim_bit<claim_value::ONE, claim_mode::READ_ONLY>(superblock_start, random_bit_index(), std::memory_order_relaxed);
				if (new_block == std::numeric_limits<std::size_t>::max()) [[unlikely]] {
					std::uint64_t write_superblock_start = fifo.global_write_superblock.load(std::memory_order_relaxed);
					auto max_movable = write_superblock_start - superblock_start - fifo.superblocks_per_window;
					if (max_movable == 0) {
						auto [new_block_desperate, _] = fifo.filled_set.template claim_bit<claim_value::ONE, claim_mode::READ_ONLY>(write_superblock_start, random_bit_index(), std::memory_order_relaxed);
						if (new_block_desperate == std::numeric_limits<std::size_t>::max()) {
							return false;
						}
						for (std::size_t i = 0; i < fifo.superblocks_per_window; i++) {
							fifo.filled_set.set_epoch_if_empty(write_superblock_start + i);
						}
						// TODO: I'm not entirely sure if this is correct.
						// If the CAS fails could we get a situation where we are outside the write window?
						if (fifo.global_write_superblock.compare_exchange_strong(write_superblock_start, write_superblock_start + fifo.superblocks_per_window, std::memory_order_relaxed)) {
							fifo.global_read_superblock.compare_exchange_strong(superblock_start, superblock_start + fifo.superblocks_per_window, std::memory_order_relaxed);
						}
						new_block = new_block_desperate;
					} else {
						fifo.global_read_superblock.compare_exchange_strong(superblock_start, superblock_start + std::min(fifo.superblocks_per_window, max_movable), std::memory_order_relaxed);
						continue;    
					}
				} else if (should_advance) {
					try_slide_read(superblock_start);
				}

				read_superblock = new_block / blocks_per_superblock;
				read_bit_index = new_block % blocks_per_superblock;
				read_epoch = read_superblock >> fifo.superblock_count_log2;
				read_block = fifo.get_block(new_block);
				return true;
			}
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
							if (fifo.filled_set.reset(read_superblock, read_bit_index, read_epoch, std::memory_order_relaxed) && read_bit_index == 0) {
								try_slide_read(read_superblock);
							}
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
						if (fifo.filled_set.reset(read_superblock, read_bit_index, read_epoch, std::memory_order_relaxed) && read_bit_index == 0) {
							try_slide_read(read_superblock);
						}
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

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif // __GNUC__

#endif // RELAXED_FIFO_H_INCLUDED
