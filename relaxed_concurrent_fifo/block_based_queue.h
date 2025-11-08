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

struct queue_header {
	std::atomic_uint64_t write_index = 1;
	std::atomic_uint64_t read_index;
};

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

template <typename T, typename BITSET_T = std::uint8_t, std::size_t SAMPLE_COUNT = 2>
class block_based_queue {
private:
	std::size_t queue_count;
	std::size_t blocks_per_queue;
	std::size_t blocks_per_queue_log2;
	std::size_t blocks_per_queue_mod_mask;
	std::uniform_int_distribution<int> queue_distribution;

	std::size_t cells_per_block;
	std::size_t block_size;
	std::size_t queue_size;

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

	std::unique_ptr<queue_header[]> headers;
	std::unique_ptr<std::byte[]> buffer;

	std::uint64_t block_to_epoch(std::uint64_t block) const {
		return block >> blocks_per_queue_log2;
	}

	std::uint64_t block_to_index(std::uint64_t block) const {
		return block & blocks_per_queue_mod_mask;
	}

	block_t get_block(std::uint64_t queue_index, std::uint64_t block_index) {
		return &buffer[(queue_index * blocks_per_queue + block_index) * block_size];
	}

	std::size_t block_index(std::uint64_t window_index, block_t block) {
		return (block.ptr - get_block(window_index, 0).ptr) / block_size;
	}

	static constexpr std::size_t align_cache_line_size(std::size_t size) {
		std::size_t ret = std::hardware_destructive_interference_size;
		while (ret < size) {
			ret += std::hardware_destructive_interference_size;
		}
		return ret;
	}

public:
	block_based_queue(int thread_count, std::size_t min_size, double queues_per_thread, std::size_t cells_per_block) :
			queue_count(std::max(1l, std::lround(queues_per_thread * thread_count))),
			blocks_per_queue(std::bit_ceil(min_size / cells_per_block / queue_count)),
			blocks_per_queue_log2(std::bit_width(blocks_per_queue) - 1),
			blocks_per_queue_mod_mask(blocks_per_queue - 1),
			queue_distribution(0, static_cast<int>(queue_count - 1)),
			cells_per_block(cells_per_block),
			block_size(align_cache_line_size(sizeof(std::atomic_uint64_t) + cells_per_block * sizeof(T))),
			headers(std::make_unique<queue_header[]>(queue_count)),
			buffer(std::make_unique<std::byte[]>(queue_count * blocks_per_queue * block_size)) {
#if BBQ_LOG_CREATION_SIZE
		std::cout << "Window count: " << window_count << std::endl;
		std::cout << "Block count: " << blocks_per_queue << std::endl;
#endif // BBQ_LOG_CREATION_SIZE

		// At least as big as the bitset's type.
		assert(blocks_per_queue >= sizeof(BITSET_T) * 8);
		assert(std::bit_ceil(blocks_per_queue) == blocks_per_queue);

		for (std::size_t i = 0; i < queue_count * blocks_per_queue; i++) {
			auto ptr = buffer.get() + i * block_size;
			new (ptr) std::atomic_uint64_t{ 0 };
			for (std::size_t j = 0; j < cells_per_block; j++) {
				new (ptr + sizeof(std::atomic_uint64_t) + j * sizeof(T)) std::atomic<T>{ };
			}
		}

		for (std::size_t j = 0; j < queue_count; j++) {
			get_block(j, 0).get_header() = epoch_to_header(1);
		}
	}

	std::size_t capacity() const {
		return queue_count * blocks_per_queue * cells_per_block;
	}

	std::size_t size() {
		std::size_t filled_cells = 0;
		for (std::size_t i = 0; i < queue_count; i++) {
			// TODO.
			for (std::size_t j = 0; j < blocks_per_queue; j++) {
				std::uint64_t ei = get_block(i, j).get_header();
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
			for (std::size_t j = 0; j < blocks_per_queue; j++) {
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

		queue_header* read_header;
		queue_header* write_header;

		std::uint64_t write_epoch = 0;
		std::uint64_t read_epoch = 0;

		std::uint64_t read_block_index;
		std::uint64_t write_block_index;

		block_t read_block = dummy_block;
		block_t write_block = dummy_block;

		std::minstd_rand rng;

		handle(block_based_queue& fifo, std::random_device::result_type seed) : fifo(fifo), rng(seed) { }

		friend block_based_queue;

		// They're typed as uint64_t, but only hold 32 bits of data.
		static constexpr bool epoch_valid(std::uint64_t check, std::uint64_t curr) {
			return (curr - check) < std::numeric_limits<std::uint32_t>::max() / 2;
		}

		int random_queue() {
			return fifo.queue_distribution(rng);
		}

		bool claim_new_block_write() {
			auto min_queue = random_queue();
			auto min_header = &fifo.headers[min_queue];
			auto min_index = min_header->write_index.load(std::memory_order_relaxed);
			for (std::size_t i = 1; i < SAMPLE_COUNT; i++) {
				auto new_queue = random_queue();
				auto new_header = &fifo.headers[new_queue];
				auto new_index = new_header->write_index.load(std::memory_order_relaxed);
				if (new_index < min_index) {
					min_queue = new_queue;
					min_header = new_header;
					min_index = new_index;
				}
			}

			write_header = min_header;
			write_block_index = min_index;
			write_epoch = fifo.block_to_epoch(min_index);
			write_block = fifo.get_block(min_queue, fifo.block_to_index(min_index));
			return true;
		}

		bool claim_new_block_read() {
			auto min_queue = random_queue();
			auto min_header = &fifo.headers[min_queue];
			auto min_index = min_header->read_index.load(std::memory_order_relaxed);
			for (std::size_t i = 1; i < SAMPLE_COUNT; i++) {
				auto new_queue = random_queue();
				auto new_header = &fifo.headers[new_queue];
				auto new_index = new_header->read_index.load(std::memory_order_relaxed);
				if (new_index < min_index) {
					min_queue = new_queue;
					min_header = new_header;
					min_index = new_index;
				}
			}

			read_header = min_header;
			read_block_index = min_index;
			read_epoch = fifo.block_to_epoch(min_index);
			read_block = fifo.get_block(min_queue, fifo.block_to_index(min_index));
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
				while (true) {
					if (epoch_valid(get_epoch(ei), write_epoch)) {
						if ((index = get_write_index(ei)) == fifo.cells_per_block) {
							write_header->write_index.compare_exchange_strong(write_block_index, write_block_index + 1, std::memory_order_relaxed);
						} else if (write_block.get_cell(index).compare_exchange_weak(old, t, std::memory_order_relaxed)) {
							break;
						}
					}
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
				} else if (index + 1 == fifo.cells_per_block) {
					write_header->write_index.compare_exchange_strong(write_block_index, write_block_index + 1, std::memory_order_relaxed);
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
							read_header->read_index.compare_exchange_strong(read_block_index, read_block_index + 1, std::memory_order_relaxed);
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
						read_header->read_index.compare_exchange_strong(read_block_index, read_block_index + 1, std::memory_order_relaxed);
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
