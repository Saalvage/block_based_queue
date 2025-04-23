#pragma once

#include <atomic>
#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable : 4146)
#endif  // _WIN32
#include "pcg_random.hpp"
#ifdef _WIN32
#pragma warning(pop)
#endif  // _WIN32

#include "timestamp.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <random>

namespace multififo::mode {

template <int num_pop_candidates = 2>
class StickSwap {
    static_assert(num_pop_candidates > 0);

    struct alignas(build_config::l1_cache_line_size) AlignedIndex {
        std::atomic<std::size_t> value;
    };

    using permutation_type = std::vector<AlignedIndex>;

   public:
    struct SharedData {
        permutation_type permutation;

        explicit SharedData(std::size_t num_fifos) : permutation(num_fifos) {
            for (std::size_t i = 0; i < num_fifos; ++i) {
                permutation[i].value = i;
            }
        }
    };

    struct QueueData {
        std::atomic<std::uint64_t> oldest_tick{
            std::numeric_limits<std::uint64_t>::max()};
    };

   private:
    pcg32 rng_{};
    int count_{};
    std::size_t offset_{};

    void swap_assignment(permutation_type& perm, std::size_t index) noexcept {
        static constexpr std::size_t swapping =
            std::numeric_limits<std::size_t>::max();
        assert(index < num_pop_candidates);
        std::size_t old_target = perm[offset_ + index].value.exchange(
            swapping, std::memory_order_relaxed);
        std::size_t perm_index{};
        std::size_t new_target{};
        do {
            perm_index = rng_() & (perm.size() - 1);
            new_target = perm[perm_index].value.load(std::memory_order_relaxed);
        } while (new_target == swapping ||
                 !perm[perm_index].value.compare_exchange_weak(
                     new_target, old_target, std::memory_order_relaxed));
        perm[offset_ + index].value.store(new_target,
                                          std::memory_order_relaxed);
    }

    template <typename Context>
    auto* best_pop_queue(Context& ctx) noexcept {
        auto* best_ptr =
            ctx.queue_storage(ctx.shared_data().permutation[offset_].value.load(
                std::memory_order_relaxed));
        auto best_tick = ctx.queue_guard(best_ptr)->queue_data.oldest_tick.load(
            std::memory_order_relaxed);
        for (std::size_t i = 1;
             i < static_cast<std::size_t>(num_pop_candidates); ++i) {
            auto* ptr = ctx.queue_storage(
                ctx.shared_data().permutation[offset_ + i].value.load(
                    std::memory_order_relaxed));
            auto tick = ctx.queue_guard(ptr)->queue_data.oldest_tick.load(
                std::memory_order_relaxed);
            if (tick < best_tick) {
                best_ptr = ptr;
                best_tick = tick;
            }
        }
        return best_tick == std::numeric_limits<std::uint64_t>::max()
                   ? nullptr
                   : best_ptr;
    }

   protected:
    explicit StickSwap(int seed, int id) noexcept {
        auto seq = std::seed_seq{seed, id};
        rng_.seed(seq);
        offset_ = static_cast<std::size_t>(id * num_pop_candidates);
    }

    template <typename Context>
    void popped(Context& ctx, std::byte* ptr) {
        ctx.queue_guard(ptr)->queue_data.oldest_tick.store(
            (ctx.unsafe_empty(ptr) ? std::numeric_limits<std::uint64_t>::max()
                                   : ctx.top_elem(ptr).tick),
            std::memory_order_relaxed);
    }

    template <typename Context>
    void pushed(Context& ctx, std::byte* ptr) {
        auto& oldest_tick = ctx.queue_guard(ptr)->queue_data.oldest_tick;
        if (oldest_tick.load(std::memory_order_relaxed) ==
            std::numeric_limits<std::uint64_t>::max()) {
            oldest_tick.store(ctx.top_elem(ptr).tick,
                              std::memory_order_relaxed);
        }
    }

    template <typename Context>
    std::optional<typename Context::value_type> try_pop(Context& ctx) {
        if (count_ == 0) {
            for (std::size_t i = 0;
                 i < static_cast<std::size_t>(num_pop_candidates); ++i) {
                swap_assignment(ctx.shared_data().permutation, i);
            }
            count_ = ctx.stickiness();
        }
        while (true) {
            auto* best_ptr = best_pop_queue(ctx);
            if (best_ptr == nullptr) {
                count_ = 0;
                return std::nullopt;
            }
            if (ctx.try_lock(best_ptr)) {
                if (ctx.unsafe_empty(best_ptr)) {
                    ctx.unlock(best_ptr);
                    count_ = 0;
                    return std::nullopt;
                }
                auto v = ctx.top(best_ptr);
                ctx.pop(best_ptr);
                popped(ctx, best_ptr);
                ctx.unlock(best_ptr);
                --count_;
                return v;
            }
            for (std::size_t i = 0;
                 i < static_cast<std::size_t>(num_pop_candidates); ++i) {
                swap_assignment(ctx.shared_data().permutation, i);
            }
            count_ = ctx.stickiness();
        }
    }

    template <typename Context>
    bool try_push(Context& ctx, typename Context::value_type const& v) {
        if (count_ == 0) {
            for (std::size_t i = 0;
                 i < static_cast<std::size_t>(num_pop_candidates); ++i) {
                swap_assignment(ctx.shared_data().permutation, i);
            }
            count_ = ctx.stickiness();
        }
        std::size_t push_index = rng_() % num_pop_candidates;
        while (true) {
            auto* ptr = ctx.queue_storage(
                ctx.shared_data().permutation[offset_ + push_index].value.load(
                    std::memory_order_relaxed));
            if (ctx.try_lock(ptr)) {
                if (ctx.unsafe_size(ptr) == ctx.size_per_queue()) {
                    ctx.unlock(ptr);
                    count_ = 0;
                    return false;
                }
                auto tick = get_timestamp();
                ctx.push(ptr, {tick, v});
                pushed(ctx, ptr);
                ctx.unlock(ptr);
                --count_;
                return true;
            }
            swap_assignment(ctx.shared_data().permutation, push_index);
        }
    }
};

}  // namespace multififo::mode
