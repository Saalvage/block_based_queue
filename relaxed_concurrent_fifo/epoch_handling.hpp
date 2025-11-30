#ifndef EPOCH_HANDLING
#define EPOCH_HANDLING

#include <cstdint>

template <typename T>
concept epoch_handling = requires(std::uint64_t u64) {
	{ T::compare_epochs(u64, u64) } -> std::same_as<bool>;
	{ T::uses_epochs } -> std::convertible_to<bool>;
	{ T::make_unit(u64) } -> std::same_as<std::uint64_t>;
	{ T::get_bits(u64) } -> std::same_as<std::uint64_t>;
};

struct default_epoch_handling {
	static constexpr bool uses_epochs = true;
	static constexpr bool compare_epochs(std::uint64_t epoch_and_bits, std::uint64_t epoch) {
		return (epoch_and_bits >> 32) == epoch;
	}
	static constexpr std::uint64_t make_unit(std::uint64_t epoch) {
		return epoch << 32;
	}
	static constexpr std::uint64_t get_bits(std::uint64_t bits) {
		return bits & 0xffff'ffff;
	}
};
static_assert(epoch_handling<default_epoch_handling>);

struct no_epoch_handling {
	static constexpr bool uses_epochs = false;
	static constexpr bool compare_epochs(std::uint64_t, std::uint64_t) {
		return true;
	}
	static constexpr std::uint64_t make_unit(std::uint64_t) {
		return 0;
	}
	static constexpr std::uint64_t get_bits(std::uint64_t bits) {
		return bits;
	}
};
static_assert(epoch_handling<no_epoch_handling>);

#endif // EPOCH_HANDLING
