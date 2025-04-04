﻿#ifndef LOCK_FIFO_H_INCLUDED
#define LOCK_FIFO_H_INCLUDED

#include "fifo.h"

#include "utility.h"

#include <utility>
#include <optional>
#include <mutex>
#include <memory>

template <typename T>
class lock_fifo {
private:
	std::unique_ptr<T[]> buffer;

	std::size_t head = 0;
	std::size_t tail = 0;

	std::size_t capacity;

	std::mutex mut;

public:
	lock_fifo([[maybe_unused]] int thread_count, std::size_t capacity) : capacity(capacity) {
		if (!is_po2(capacity)) {
			throw std::runtime_error("Please only use capacities that are a power of two");
		}

		buffer = std::make_unique<T[]>(capacity);
	}

	lock_fifo(const lock_fifo& other) : lock_fifo(0, other.capacity) { }

	bool push(T t) {
		std::scoped_lock lock(mut);

		if (head - tail == capacity) {
			return false;
		}

		head++;
		buffer[modulo_po2(head, capacity)] = std::move(t);
		return true;
	}

	std::optional<T> pop() {
		std::scoped_lock lock(mut);

		if (head == tail) {
			return std::nullopt;
		}

		tail++;
		return std::move(buffer[modulo_po2(tail, capacity)]);
	}
	
	using handle = wrapper_handle<lock_fifo, T>;

	handle get_handle() { return handle(this); }
};
static_assert(fifo<lock_fifo<std::uint64_t>, std::uint64_t>);

#endif // LOCK_FIFO_H_INCLUDED
