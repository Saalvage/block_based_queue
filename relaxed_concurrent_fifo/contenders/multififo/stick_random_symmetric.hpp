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

#ifdef _WIN32
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <random>

namespace multififo::mode {

template <int num_pop_candidates = 2>
class StickRandomSymmetric {
    static_assert(num_pop_candidates > 0);

   public:
    struct SharedData {
        explicit SharedData(std::size_t) {}
    };

    struct QueueData {
        std::atomic<std::uint64_t> oldest_tick{
            std::numeric_limits<std::uint64_t>::max()};
        std::atomic<std::uint64_t> newest_tick{0};
    };

   private:
    pcg32 rng_{};
    std::array<std::size_t, static_cast<std::size_t>(num_pop_candidates)>
        pop_index_{};
    int count_{};

    void refresh_pop_index(std::size_t num_queues) noexcept {
        for (auto it = pop_index_.begin(); it != pop_index_.end(); ++it) {
            do {
                *it = std::uniform_int_distribution<std::size_t>{
                    0, num_queues - 1}(rng_);
            } while (std::find(pop_index_.begin(), it, *it) != it);
        }
    }

   protected:
    explicit StickRandomSymmetric(int seed, int id) noexcept {
        auto seq = std::seed_seq{seed, id};
        rng_.seed(seq);
    }

    template <typename Context>
    void popped(Context& ctx, std::byte* ptr) {
        if (ctx.unsafe_empty(ptr)) {
            ctx.queue_guard(ptr)->queue_data.oldest_tick.store(
                std::numeric_limits<std::uint64_t>::max(),
                std::memory_order_relaxed);
            ctx.queue_guard(ptr)->queue_data.newest_tick.store(
                0, std::memory_order_relaxed);
        } else {
            ctx.queue_guard(ptr)->queue_data.oldest_tick.store(
                ctx.top_elem(ptr).tick, std::memory_order_relaxed);
        }
    }

    template <typename Context>
    void pushed(Context& ctx, std::byte* ptr) {
        auto& newest_tick = ctx.queue_guard(ptr)->queue_data.newest_tick;
        auto tick = ctx.bottom_elem(ptr).tick;
        newest_tick.store(tick, std::memory_order_relaxed);
        if (ctx.unsafe_size(ptr) == 1) {
            ctx.queue_guard(ptr)->queue_data.oldest_tick.store(
                tick, std::memory_order_relaxed);
        }
    }

    template <typename Context>
    std::optional<typename Context::value_type> try_pop(Context& ctx) {
        if (count_ == 0) {
            refresh_pop_index(ctx.num_queues());
            count_ = ctx.stickiness();
        }
        while (true) {
            auto* best_ptr = ctx.queue_storage(pop_index_[0]);
            auto best_tick = ctx.queue_guard(best_ptr)->queue_data.oldest_tick.load(
                std::memory_order_relaxed);
            for (std::size_t i = 1;
                 i < static_cast<std::size_t>(num_pop_candidates); ++i) {
                auto* ptr = ctx.queue_storage(pop_index_[i]);
                auto tick = ctx.queue_guard(ptr)->queue_data.oldest_tick.load(
                    std::memory_order_relaxed);
                if (tick < best_tick) {
                    best_ptr = ptr;
                    best_tick = tick;
                }
            }
            if (best_tick == std::numeric_limits<std::uint64_t>::max()) {
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
            refresh_pop_index(ctx.num_queues());
            count_ = ctx.stickiness();
        }
    }

    template <typename Context>
    bool try_push(Context& ctx, typename Context::value_type const& v) {
        if (count_ == 0) {
            refresh_pop_index(ctx.num_queues());
            count_ = ctx.stickiness();
        }
        while (true) {
            auto* best_ptr = ctx.queue_storage(pop_index_[0]);
            auto best_tick = ctx.queue_guard(best_ptr)->queue_data.newest_tick.load(
                std::memory_order_relaxed);
            for (std::size_t i = 1;
                 i < static_cast<std::size_t>(num_pop_candidates); ++i) {
                auto* ptr = ctx.queue_storage(pop_index_[i]);
                auto tick = ctx.queue_guard(ptr)->queue_data.newest_tick.load(
                    std::memory_order_relaxed);
                if (tick < best_tick) {
                    best_ptr = ptr;
                    best_tick = tick;
                }
            }
            if (ctx.try_lock(best_ptr)) {
                if (ctx.unsafe_size(best_ptr) == ctx.size_per_queue()) {
                    ctx.unlock(best_ptr);
                    count_ = 0;
                    return false;
                }
                auto tick = __rdtsc();
                ctx.push(best_ptr, {tick, v});
                pushed(ctx, best_ptr);
                ctx.unlock(best_ptr);
                --count_;
                return true;
            }
            refresh_pop_index(ctx.num_queues());
            count_ = ctx.stickiness();
        }
    }
};

}  // namespace multififo::mode
