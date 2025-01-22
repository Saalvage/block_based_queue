#ifndef BSKFIFO_WRAPPER_H_INCLUDED
#define BSKFIFO_WRAPPER_H_INCLUDED

#include "boundedsize_kfifo.h"

template <typename T, template <typename> typename FIFO>
struct scal_wrapper_base {
private:
	std::atomic_int curr_thread_id = 0;

protected:
	FIFO<T> queue;

	template <typename... Args>
	scal_wrapper_base(Args&&... args) : queue{std::forward<Args>(args)...} { }

public:
	struct handle {
	private:
		FIFO<T>* queue;
		int thread_id;

	public:
		handle(FIFO<T>* queue, int thread_id) : queue(queue), thread_id(thread_id) { }

		bool push(T t) {
			return queue->enqueue(std::move(t));
		}

		std::optional<T> pop() {
			T t;
			return queue->dequeue(&t) ? std::optional<T>(t) : std::nullopt;
		}
	};

	handle get_handle() {
		return handle{ &queue, curr_thread_id++ };
	}
};

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winterference-size"
#endif
template <typename T>
struct alignas(std::hardware_destructive_interference_size) Padded {
	T value;
	Padded() = default;
	Padded(T value) : value(std::move(value)) {}
	operator T&() { return value; }
	operator const T&() const { return value; }
	Padded(const Padded& other) = default;
	Padded& operator=(const Padded& other) = default;
};
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

template <typename T, typename Container = std::vector<T>>
struct ringbuffer_wrapper {
protected:
	multififo::RingBuffer<T, Container> queue;

public:
	ringbuffer_wrapper(int, size_t size) : queue{size} { }

	struct handle {
	private:
		multififo::RingBuffer<T, Container>* queue;

	public:
		handle(multififo::RingBuffer<T, Container>* queue) : queue(queue) {}

		bool push(T t) {
			if (queue->full()) {
				return false;
			}
			queue->push(t);
			return true;
		}

		std::optional<T> pop() {
			if (queue->empty()) {
				return std::nullopt;
			}
			T t = queue->top();
			queue->pop();
			return t;
		}
	};

	handle get_handle() {
		return handle{ &queue };
	}
};

template <typename T>
struct ws_k_fifo : scal_wrapper_base<T, scal::BoundedSizeKFifo> {
public:
	ws_k_fifo(int thread_count, size_t size, size_t k) : scal_wrapper_base<T, scal::BoundedSizeKFifo>{thread_count * k, std::max<size_t>(4, size / k / thread_count)} { }
};

template <typename T>
struct ss_k_fifo : scal_wrapper_base<T, scal::BoundedSizeKFifo> {
public:
	ss_k_fifo([[maybe_unused]] int thread_count, size_t size, size_t k) : scal_wrapper_base<T, scal::BoundedSizeKFifo>{ k, std::max<size_t>(4, size / k) } {}
};

#endif // BSKFIFO_WRAPPER_H_INCLUDED
