#ifndef UTILITY_H_INCLUDED
#define UTILITY_H_INCLUDED

static constexpr bool is_po2(std::size_t size) {
	std::size_t it = 1;
	while (it < size) {
		it *= 2;
	}
	return it == size;
}

static constexpr std::size_t modulo_po2(std::size_t dividend, std::size_t divisor) {
	return dividend & (divisor - 1);
}

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winterference-size"
#endif // __GNUC__

template <typename T>
struct alignas(std::hardware_destructive_interference_size) cache_aligned_t {
	T value;
	T* operator->() { return &value; }
	const T* operator->() const { return &value; }
	operator T& () { return value; }
	operator const T& () const { return value; }
};

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif // __GNUC__

template <template <typename> typename Fifo, typename Elem>
class wrapper_handle {
	friend Fifo<Elem>;

private:
	Fifo<Elem>* fifo;

	wrapper_handle(Fifo<Elem>* fifo) : fifo(fifo) { }

public:
	bool push(Elem t) { return fifo->push(std::move(t)); }
	std::optional<Elem> pop() { return fifo->pop(); }
};

#endif // UTILITY_H_INCLUDED
