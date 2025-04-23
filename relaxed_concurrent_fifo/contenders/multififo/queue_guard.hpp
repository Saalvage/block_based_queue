/**
******************************************************************************
* @file:   queue_guard.hpp
*
* @author: Marvin Williams
* @date:   2021/11/09 18:05
* @brief:
*******************************************************************************
**/

#pragma once

#include "build_config.hpp"

#include <atomic>
#include <chrono>
#include <type_traits>

namespace multififo {

struct alignas(build_config::l1_cache_line_size) QueueIndex {
    std::uint64_t head{0};
    std::uint64_t tail{0};
};

struct alignas(build_config::l1_cache_line_size) QueueGuard {
    using size_type = std::size_t;
    std::atomic<std::uint64_t> top_tick =
        std::numeric_limits<std::uint64_t>::max();
    std::atomic_uint32_t lock = 0;
    QueueIndex queue_index;

   public:

    QueueGuard() = default;
    QueueGuard(QueueGuard const &) = delete;
    QueueGuard(QueueGuard &&) {
        queue_index = {};
        top_tick.store(std::numeric_limits<std::uint64_t>::max(),
                        std::memory_order_relaxed);
    }
    [[nodiscard]] bool empty() const noexcept {
        return top_tick.load(std::memory_order_relaxed) ==
               std::numeric_limits<std::uint64_t>::max();
    }

    bool try_lock() noexcept {
        // Test first to not invalidate the cache line
        return (lock.load(std::memory_order_relaxed) & 1U) == 0U &&
               (lock.exchange(1U, std::memory_order_acquire) & 1) == 0U;
    }

    [[nodiscard]] constexpr bool unsafe_empty() const noexcept {
        return queue_index.head == queue_index.tail;
    }

    constexpr size_type unsafe_size() const noexcept {
        return queue_index.head - queue_index.tail;
    }

    void unlock() { lock.store(0U, std::memory_order_release); }
};

}  // namespace multififo
