#pragma once

#include "stick_random.hpp"

#include <optional>

namespace multififo {

template <typename Context>
class Handle : public multififo::mode::StickRandom<2> {
    using mode_type = multififo::mode::StickRandom<2>;
    Context *context_;
    using value_type = typename Context::value_type;

    bool scan_push(value_type const &v) {
        for (std::size_t i = 0; i < context_->num_queues(); ++i) {
            auto ptr = context_->queue_storage(i);
            auto* guard = context_->queue_guard(ptr);
            if (!guard->try_lock()) {
                continue;
            }
            if (guard->unsafe_size() == context_->size_per_queue()) {
                guard->unlock();
                continue;
            }
            auto tick = __rdtsc();
            context_->push(ptr, {tick, v});
            context_->pushed(ptr);
            guard->unlock();
            return true;
        }
        return false;
    }

    std::optional<value_type> scan_pop() {
        for (std::size_t i = 0; i < context_->num_queues(); ++i) {
            auto ptr = context_->queue_storage(i);
            auto * guard = context_->queue_guard(ptr);
            if (!guard->try_lock()) {
                continue;
            }
            if (guard->unsafe_empty()) {
                guard->unlock();
                continue;
            }
            auto v = context_->top(ptr);
            context_->pop(ptr);
            context_->popped(ptr);
            guard->unlock();
            return v;
        }
        return std::nullopt;
    }

   public:
    explicit Handle(Context &ctx) noexcept : mode_type{ctx.seed(), ctx.new_id()}, context_{&ctx} {
    }

    Handle(Handle const &) = delete;
    Handle(Handle &&) noexcept = default;
    Handle &operator=(Handle const &) = delete;
    Handle &operator=(Handle &&) noexcept = default;
    ~Handle() = default;

    bool push(value_type const &v) {
        auto success = mode_type::try_push(*context_, v);
        if (success) {
            return true;
        }
        return scan_push(v);
    }

    std::optional<value_type> pop() {
        auto v = mode_type::try_pop(*context_);
        if (v) {
            return *v;
        }
        return scan_pop();
    }
};

}  // namespace multififo
