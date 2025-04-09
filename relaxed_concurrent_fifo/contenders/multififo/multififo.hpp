/**
******************************************************************************
* @file:   multififo.hpp
*
* @author: Marvin Williams
* @date:   2021/03/29 17:19
* @brief:
*******************************************************************************
**/
#pragma once

#include "build_config.hpp"
#include "handle.hpp"
#include "ring_buffer.hpp"

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <memory>

namespace multififo {

struct alignas(build_config::l1_cache_line_size) QueueIndex {
    std::uint64_t head{0};
    std::uint64_t tail{0};
};

struct alignas(build_config::l1_cache_line_size) QueueGuard {
    std::atomic<std::uint64_t> top_tick =
        std::numeric_limits<std::uint64_t>::max();
    std::atomic_uint32_t lock = 0;
    QueueIndex queue_index;
    /* QueueGuard() = default; */
    /* QueueGuard(QueueGuard const &) = delete; */
    /* QueueGuard(QueueGuard &&) { */
    /*     queue_index = {}; */
    /*     top_tick.store(std::numeric_limits<std::uint64_t>::max(), */
    /*                    std::memory_order_relaxed); */
    /* } */
};
template <typename T, typename Allocator = std::allocator<T>>
class MultiFifo {
   public:
    using value_type = T;
    using reference = value_type &;
    using const_reference = value_type const &;
    using size_type = std::size_t;
    using allocator_type = Allocator;

   private:
    struct Element {
        std::uint64_t tick;
        value_type value;
    };
    using queue_type = RingBuffer<Element>;
    using guard_type = QueueGuard;
    using buffer_allocator_type = typename std::allocator_traits<
        allocator_type>::template rebind_alloc<std::byte>;
    using guard_allocator_type = typename std::allocator_traits<
        allocator_type>::template rebind_alloc<guard_type>;
    using elem_allocator_type = typename std::allocator_traits<
        allocator_type>::template rebind_alloc<Element>;

    class Context {
        friend MultiFifo;

       public:
        using element_type = Element;
        using value_type = MultiFifo::value_type;
        using guard_type = MultiFifo::guard_type;

       private:
        int num_queues_{};
        std::size_t size_per_queue_{};
        std::byte *buffer_{nullptr};
        std::uint64_t mask_{0};
        /* guard_type *queue_guards_{nullptr}; */
        std::atomic_int id_count{0};
        int stickiness_{16};
        int seed_{1};
        [[no_unique_address]] buffer_allocator_type alloc_;

        static constexpr size_t make_po2(size_t size) {
            size_t ret = 1;
            while (size > ret) {
                ret *= 2;
            }
            return ret;
        }

        explicit Context(int queue_count, size_t size, int stickiness, int seed,
                         allocator_type const &alloc)
            : num_queues_{queue_count},
              size_per_queue_{make_po2((size + num_queues_ - 1) / num_queues_)},
              stickiness_{stickiness},
              seed_{seed},
              alloc_{alloc} {
            assert(num_queues_ > 0);

            assert((size_per_queue_ & (size_per_queue_ - 1)) == 0);

            buffer_ = std::allocator_traits<buffer_allocator_type>::allocate(
                alloc_,
                (sizeof(guard_type) + size_per_queue_ * sizeof(Element)) *
                    num_queues_);
            auto guard_alloc = guard_allocator_type(alloc_);
            auto elem_alloc = elem_allocator_type(alloc_);
            for (int i = 0; i < num_queues_; ++i) {
                std::byte *ptr =
                    buffer_ + (i * (sizeof(guard_type) +
                                    size_per_queue_ * sizeof(Element)));
                std::allocator_traits<guard_allocator_type>::construct(
                    guard_alloc, reinterpret_cast<guard_type *>(ptr));
                for (int j = 0; j < size_per_queue_; ++j) {
                    std::allocator_traits<elem_allocator_type>::construct(
                        elem_alloc,
                        reinterpret_cast<Element *>(ptr + sizeof(guard_type) +
                                                    j * sizeof(Element)),
                        Element{});
                }
            }
        }

        ~Context() noexcept {
            std::allocator_traits<buffer_allocator_type>::deallocate(
                alloc_, buffer_,
                ((sizeof(guard_type) + size_per_queue_ * sizeof(Element)) *
                 num_queues_));
        }

       public:
        Context(const Context &) = delete;
        Context(Context &&) = delete;
        Context &operator=(const Context &) = delete;
        Context &operator=(Context &&) = delete;

        constexpr element_type const &top_elem(std::byte const *ptr) const {
            auto tail = std::launder(reinterpret_cast<guard_type const *>(ptr))
                            ->queue_index.tail;
            return *std::launder(reinterpret_cast<Element const *>(
                ptr + sizeof(guard_type) + (tail & mask_) * sizeof(Element)));
        }

        constexpr const_reference top(std::byte const *ptr) const {
            return top_elem(ptr).value;
        }

        [[nodiscard]] bool empty(std::byte *ptr) const noexcept {
            return std::launder(reinterpret_cast<guard_type *>(ptr))
                       ->top_tick.load(std::memory_order_relaxed) ==
                   std::numeric_limits<std::uint64_t>::max();
        }

        bool try_lock(std::byte *ptr) noexcept {
            auto &lock =
                std::launder(reinterpret_cast<guard_type *>(ptr))->lock;
            // Test first to not invalidate the cache line
            return (lock.load(std::memory_order_relaxed) & 1U) == 0U &&
                   (lock.exchange(1U, std::memory_order_acquire) & 1) == 0U;
        }

        [[nodiscard]] constexpr bool unsafe_empty(
            std::byte *ptr) const noexcept {
            auto const &queue_index =
                std::launder(reinterpret_cast<guard_type *>(ptr))->queue_index;
            return queue_index.head == queue_index.tail;
        }

        constexpr size_type unsafe_size(std::byte *ptr) const noexcept {
            auto const &queue_index =
                std::launder(reinterpret_cast<guard_type *>(ptr))->queue_index;
            return queue_index.head - queue_index.tail;
        }

        void unlock(std::byte *ptr) {
            std::launder(reinterpret_cast<guard_type *>(ptr))
                ->lock.store(0U, std::memory_order_release);
        }

        void pop(std::byte *ptr) {
            assert(!unsafe_empty(ptr));
            ++(std::launder(reinterpret_cast<guard_type *>(ptr))
                   ->queue_index.tail);
        }

        void push(std::byte *ptr, element_type const &value) {
            auto &queue_index =
                std::launder(reinterpret_cast<guard_type *>(ptr))->queue_index;
            assert(queue_index.head - queue_index.tail < size_per_queue_);
            *std::launder(reinterpret_cast<Element *>(
                ptr + sizeof(guard_type) +
                (queue_index.head & mask_) * sizeof(Element))) = value;
            ++queue_index.head;
        }

        void push(std::byte *ptr, element_type &&value) {
            auto &head = std::launder(reinterpret_cast<guard_type *>(ptr))
                             ->queue_index.head;
            *std::launder(reinterpret_cast<Element *>(
                ptr + sizeof(guard_type) + (head & mask_) * sizeof(Element))) =
                std::move(value);
            ++head;
        }

        void popped(std::byte *ptr) {
            auto &top_tick =
                std::launder(reinterpret_cast<guard_type *>(ptr))->top_tick;
            top_tick.store(
                (unsafe_empty(ptr) ? std::numeric_limits<std::uint64_t>::max()
                                   : top_elem(ptr).tick),
                std::memory_order_relaxed);
        }

        void pushed(std::byte *ptr) {
            auto &top_tick =
                std::launder(reinterpret_cast<guard_type *>(ptr))->top_tick;
            auto tick = top_elem(ptr).tick;
            if (tick != top_tick.load(std::memory_order_relaxed)) {
                top_tick.store(tick, std::memory_order_relaxed);
            }
        }

        [[nodiscard]] std::byte *queue_storage(std::size_t i) noexcept {
            return buffer_ +
                   i * (sizeof(guard_type) + size_per_queue_ * sizeof(Element));
        }

        guard_type const *queue_guard(std::byte *ptr) const noexcept {
            return std::launder(reinterpret_cast<guard_type const *>(ptr));
        }

        guard_type *queue_guard(std::byte *ptr) noexcept {
            return std::launder(reinterpret_cast<guard_type *>(ptr));
        }

        [[nodiscard]] size_type size_per_queue() const noexcept {
            return size_per_queue_;
        }

        [[nodiscard]] int num_queues() const noexcept { return num_queues_; }

        [[nodiscard]] int stickiness() const noexcept { return stickiness_; }

        [[nodiscard]] int seed() const noexcept { return seed_; }

        [[nodiscard]] int new_id() noexcept {
            return id_count.fetch_add(1, std::memory_order_relaxed);
        }
    };

    Context context_;

   public:
    using handle = Handle<Context>;

    explicit MultiFifo(int num_threads, size_t size, int thread_multiplier,
                       int stickiness = 16, int seed = 1,
                       allocator_type const &alloc = {})
        : context_{num_threads * thread_multiplier, size, stickiness, seed,
                   buffer_allocator_type(alloc)} {}

    handle get_handle() noexcept { return handle(context_); }

    [[nodiscard]] constexpr size_type num_queues() const noexcept {
        return context_.num_queues_;
    }

    [[nodiscard]] allocator_type get_allocator() const {
        return allocator_type_(context_.alloc_);
    }
};
}  // namespace multififo
