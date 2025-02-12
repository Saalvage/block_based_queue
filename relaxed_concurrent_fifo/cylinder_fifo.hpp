#ifndef CYLINDER_FIFO_H_INCLUDED
#define CYLINDER_FIFO_H_INCLUDED

#include <atomic>
#include "concurrent_fifo.h"

template <typename T, typename FIFO = concurrent_fifo<T>>
class cylinder_fifo {
private:
	const int fifo_count;
	const int stickiness;

	alignas(std::hardware_destructive_interference_size) std::atomic<int> read_index;
	alignas(std::hardware_destructive_interference_size) std::atomic<int> write_index;

	std::vector<FIFO> buffer;

public:
	class handle {
	private:
		cylinder_fifo& fifo;

		int read_index;
		int write_index;

		FIFO* read_fifo;
		FIFO* write_fifo;

		int read_stick = 0;
		int write_stick = 0;

	public:
		handle(cylinder_fifo& fifo) : fifo(fifo) { }

		bool push(T t) {
			if (write_stick-- == 0) {
				write_index = fifo.write_index.fetch_add(1, std::memory_order_relaxed) % fifo.fifo_count;
				write_stick = fifo.stickiness;
				write_fifo = &fifo.buffer[write_index];
			}

			if (write_fifo->push(std::move(t))) {
				return true;
			}

			for (int i = 1; i < fifo.fifo_count; i++) {
				if (fifo.buffer[(write_index + i) % fifo.fifo_count].push(std::move(t))) {
					write_stick--;
					return true;
				}
			}

			return false;
		}

		std::optional<T> pop() {
			if (read_stick-- == 0) {
				read_index = fifo.read_index.fetch_add(1, std::memory_order_relaxed) % fifo.fifo_count;
				read_stick = fifo.stickiness;
				read_fifo = &fifo.buffer[read_index];
			}

			auto popped = read_fifo->pop();
			if (popped) {
				return popped;
			}

			for (int i = 1; i < fifo.fifo_count; i++) {
				popped = fifo.buffer[(read_index + i) % fifo.fifo_count].pop();
				if (popped) {
					return popped;
				}
			}

			return std::nullopt;
		}
	};

	cylinder_fifo(int num_threads, std::size_t size, int queues_per_thread, int stickiness)
			: fifo_count(num_threads * queues_per_thread), stickiness(stickiness) {
		buffer = std::vector<FIFO>(num_threads * queues_per_thread, FIFO(0, size / queues_per_thread));
	}

	handle get_handle() {
		return handle(*this);
	}
};

#endif // CYLINDER_FIFO_H_INCLUDED
