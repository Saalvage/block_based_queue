#ifndef CYLINDER_FIFO_H_INCLUDED
#define CYLINDER_FIFO_H_INCLUDED

#include <atomic>
#include "concurrent_fifo.h"
#include "contenders/multififo/ring_buffer.hpp"
#include "contenders/multififo/queue_guard.hpp"

template <typename T>
class cylinder_fifo {
private:
	const int queue_count;
	const int stickiness;

	alignas(std::hardware_destructive_interference_size) std::atomic<int> read_index;
	alignas(std::hardware_destructive_interference_size) std::atomic<int> write_index;

	using fifo_t = multififo::RingBuffer<T>;
	using guard_t = multififo::QueueGuard<fifo_t>;
	using alloc_t = std::allocator<guard_t>;
	using alloc_traits_t = std::allocator_traits<alloc_t>;
	[[no_unique_address]] alloc_t alloc;
	guard_t* queues;

public:
	class handle {
	private:
		cylinder_fifo& fifo;

		int read_index;
		int write_index;

		guard_t* read_fifo;
		guard_t* write_fifo;

		int read_stick = 0;
		int write_stick = 0;

	public:
		handle(cylinder_fifo& fifo) : fifo(fifo) { }

		bool push(T t) {
			while (write_stick-- == 0 || !write_fifo->try_lock()) {
				write_index = fifo.write_index.fetch_add(1, std::memory_order_relaxed) % fifo.queue_count;
				write_stick = fifo.stickiness;
				write_fifo = &fifo.queues[write_index];
			}

			if (write_fifo->get_queue().full()) {
				write_fifo->unlock();
				return false;
			}

			write_fifo->get_queue().push(std::move(t));
			write_fifo->unlock();
			return true;
		}

		std::optional<T> pop() {
			while (read_stick-- == 0 || !read_fifo->try_lock()) {
				read_index = fifo.read_index.fetch_add(1, std::memory_order_relaxed) % fifo.queue_count;
				read_stick = fifo.stickiness;
				read_fifo = &fifo.queues[read_index];
			}

			if (read_fifo->get_queue().empty()) {
				read_fifo->unlock();
				return std::nullopt;
			}

			auto ret = read_fifo->get_queue().top();
			read_fifo->get_queue().pop();
			read_fifo->unlock();
			return ret;
		}
	};

	cylinder_fifo(int num_threads, std::size_t size, int queues_per_thread, int stickiness)
			: queue_count(num_threads * queues_per_thread), stickiness(stickiness) {
		queues = alloc_traits_t::allocate(alloc, queue_count);
		for (auto it = queues; it != queues + queue_count; ++it) {
			alloc_traits_t::construct(alloc, it, fifo_t(size / queues_per_thread));
		}
	}

	~cylinder_fifo() {
		for (auto* it = queues; it != queues + queue_count; ++it) {
			alloc_traits_t::destroy(alloc, it);
		}
		alloc_traits_t::deallocate(alloc, queues, queue_count);
	}

	handle get_handle() {
		return handle(*this);
	}
};

#endif // CYLINDER_FIFO_H_INCLUDED
