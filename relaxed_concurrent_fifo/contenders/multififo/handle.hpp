#pragma once

#include <optional>

namespace multififo {

template <typename Context>
class Handle : public Context::mode_type {
    using mode_type = typename Context::mode_type;
    Context *context_;
    using value_type = typename Context::value_type;

    bool scan_push(value_type const &v) {
        for (std::size_t i = 0; i < context_->num_queues(); ++i) {
            auto ptr = context_->queue_storage(i);
            if (!context_->try_lock(ptr)) {
                continue;
            }
            if (context_->unsafe_size(ptr) == context_->size_per_queue()) {
                context_->unlock(ptr);
                continue;
            }
            auto tick = __rdtsc();
            context_->push(ptr, {tick, v});
            mode_type::pushed(*context_, ptr);
            context_->unlock(ptr);
            return true;
        }
        return false;
    }

    std::optional<value_type> scan_pop() {
        for (std::size_t i = 0; i < context_->num_queues(); ++i) {
            auto ptr = context_->queue_storage(i);
            if (!context_->try_lock(ptr)) {
                continue;
            }
            if (context_->unsafe_empty(ptr)) {
                context_->unlock(ptr);
                continue;
            }
            auto v = context_->top(ptr);
            context_->pop(ptr);
            mode_type::popped(*context_, ptr);
            context_->unlock(ptr);
            return v;
        }
        return std::nullopt;
    }

   public:
    explicit Handle(Context &ctx) noexcept
        : mode_type{ctx.seed(), ctx.new_id()}, context_{&ctx} {}

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
