#pragma once

#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable:4146)
#endif // _WIN32
#include "pcg_random.hpp"
#ifdef _WIN32
#pragma warning(pop)
#endif // _WIN32

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

template <int stick_queues = 2>
class StickRandom {
    static_assert(stick_queues > 0);

   private:
    pcg32 rng_{};
    std::array<std::size_t, static_cast<std::size_t>(stick_queues)> queue_index_{};
    int count_{};

    void refresh_queues(std::size_t num_queues) noexcept {
        for (auto it = queue_index_.begin(); it != queue_index_.end(); ++it) {
            do {
                *it = std::uniform_int_distribution<std::size_t>{0, num_queues - 1}(rng_);
            } while (std::find(queue_index_.begin(), it, *it) != it);
        }
    }

   protected:
    explicit StickRandom(int seed, int id) noexcept {
        auto seq = std::seed_seq{seed, id};
        rng_.seed(seq);
    }

    template <typename Context>
    bool try_push(Context& ctx, typename Context::value_type const& v) {
        if (count_ == 0) {
            refresh_queues(ctx.num_queues());
            count_ = ctx.stickiness();
        }
        while (true) {
            std::size_t best = queue_index_[0];
            auto best_push_count = ctx.queue_guards()[best].size();
            for (std::size_t i = 1; i < static_cast<std::size_t>(stick_queues); ++i) {
                auto push_count = ctx.queue_guards()[queue_index_[i]].push_count();
                if (push_count < best_push_count) {
                    best = queue_index_[i];
                    best_push_count = push_count;
                }
            }
            auto& guard = ctx.queue_guards()[best];
            if (guard.try_lock()) {
                if (guard.get_queue().full()) {
                    guard.unlock();
                    count_ = 0;
                    return false;
                }
                guard.get_queue().push(v);
                guard.pushed();
                guard.unlock();
                --count_;
                return true;
            }
            refresh_queues(ctx.num_queues());
            count_ = ctx.stickiness();
        }
    }

    template <typename Context>
    std::optional<typename Context::value_type> try_pop(Context& ctx) {
        if (count_ == 0) {
            refresh_queues(ctx.num_queues());
            count_ = ctx.stickiness();
        }
        while (true) {
            std::size_t best = queue_index_[0];
            auto best_pop_count = ctx.queue_guards()[best].pop_count();
            for (std::size_t i = 1; i < static_cast<std::size_t>(stick_queues); ++i) {
                auto pop_count = ctx.queue_guards()[queue_index_[i]].pop_count();
                if (pop_count < best_pop_count) {
                    best = queue_index_[i];
                    best_pop_count = pop_count;
                }
            }
            auto & guard = ctx.queue_guards()[best];
            if (guard.try_lock()) {
                if (guard.get_queue().empty()) {
                    guard.unlock();
                    count_ = 0;
                    return std::nullopt;
                }
                auto v = guard.get_queue().top();
                guard.get_queue().pop();
                guard.popped();
                guard.unlock();
                --count_;
                return v;
            }
            refresh_queues(ctx.num_queues());
            count_ = ctx.stickiness();
        }
    }
};

}  // namespace multififo::mode
