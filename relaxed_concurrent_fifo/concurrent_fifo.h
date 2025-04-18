#ifndef CONCURRENT_FIFO_H_INCLUDED
#define CONCURRENT_FIFO_H_INCLUDED

#include "fifo.h"

#include "utility.h"

#include <utility>
#include <optional>
#include <mutex>
#include <memory>

template <typename T>
class concurrent_fifo {
private:
	struct slot {
		std::atomic<T> value;
		std::atomic<std::uint64_t> epoch;
	};

	std::unique_ptr<slot[]> buffer;

	std::atomic<std::size_t> head = 0;
	std::atomic<std::size_t> tail = 0;

	std::size_t capacity;

	static constexpr std::uint64_t slot_to_epoch(std::uint64_t index, bool written) {
		return ((index) & ~(1ull << 63)) | (static_cast<std::uint64_t>(written) << 63);
	}

public:
	concurrent_fifo([[maybe_unused]] int thread_count, std::size_t capacity) : capacity(capacity) {
		if (!is_po2(capacity)) {
			throw std::runtime_error("Please only use capacities that are a power of two");
		}

		buffer = std::make_unique<slot[]>(capacity);

		for (std::size_t i = 0; i < capacity; i++) {
			buffer[i].epoch = slot_to_epoch(i, false);
		}
	}

	concurrent_fifo(const concurrent_fifo& other) : concurrent_fifo(0, other.capacity) { }

	bool push(T t) {
		std::size_t slot;

		slot = head.load();
		do {
			if (slot - tail >= capacity) {
				return false;
			}
		} while (!head.compare_exchange_weak(slot, slot + 1));

		auto my_epoch = slot_to_epoch(slot, false);
		auto my_index = modulo_po2(slot, capacity);
		while (buffer[my_index].epoch != my_epoch) { }
		buffer[my_index].value.store(t);
		buffer[my_index].epoch.store(slot_to_epoch(slot, true));

		return true;
	}

	std::optional<T> pop() {
		std::size_t slot = tail.load();
		do {
			if (slot == head) {
				return std::nullopt;
			}
		} while (!tail.compare_exchange_weak(slot, slot + 1));

		auto my_epoch = slot_to_epoch(slot, true);
		auto my_index = modulo_po2(slot, capacity);
		while (buffer[my_index].epoch != my_epoch) { }
		auto ret = buffer[my_index].value.load();
		buffer[my_index].epoch.store(slot_to_epoch(slot + capacity, false));

		return ret;
	}

	using handle = wrapper_handle<concurrent_fifo, T>;

	handle get_handle() { return handle(this); }
};
static_assert(fifo<concurrent_fifo<std::uint64_t>, std::uint64_t>);

#endif // CONCURRENT_FIFO_H_INCLUDED
