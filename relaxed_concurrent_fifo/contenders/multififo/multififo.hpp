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

#include "handle.hpp"
#include "queue_guard.hpp"
#include "ring_buffer.hpp"

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <memory>

namespace multififo {

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
                ((sizeof(guard_type) + size_per_queue_ * sizeof(Element)) *
                 num_queues_));
            auto guard_alloc = guard_allocator_type(alloc_);
            auto elem_alloc = elem_allocator_type(alloc_);
            for (int i = 0; i < num_queues_; ++i) {
                std::byte *ptr = reinterpret_cast<std::byte *>(buffer_) +
                                 i * (sizeof(guard_type) +
                                      size_per_queue_ * sizeof(Element));
                std::allocator_traits<guard_allocator_type>::construct(
                    guard_alloc, reinterpret_cast<guard_type *>(ptr),
                    guard_type{});
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

        void pop(std::byte *ptr) {
            auto &guard = *std::launder(reinterpret_cast<guard_type *>(ptr));
            assert(!guard.unsafe_empty());
            ++guard.queue_index.tail;
        }

        void push(std::byte *ptr, element_type const& value) {
            auto &guard = *std::launder(reinterpret_cast<guard_type *>(ptr));
            assert(guard.queue_index.head - guard.queue_index.tail <
                   size_per_queue_);
            *std::launder(reinterpret_cast<Element *>(
                ptr + sizeof(guard_type) +
                (guard.queue_index.head & mask_) * sizeof(Element))) = value;
            ++guard.queue_index.head;
        }

        void push(std::byte *ptr, element_type &&value) {
            auto &guard = *std::launder(reinterpret_cast<guard_type *>(ptr));
            assert(guard.queue_index.head - guard.queue_index.tail <
                   size_per_queue_);
            *std::launder(reinterpret_cast<Element *>(
                ptr + sizeof(guard_type) +
                (guard.queue_index.head & mask_) * sizeof(Element))) =
                std::move(value);
            ++guard.queue_index.head;
        }

        void popped(std::byte *ptr) {
            auto &guard = *std::launder(reinterpret_cast<guard_type *>(ptr));
            auto tick = (guard.unsafe_empty()
                             ? std::numeric_limits<std::uint64_t>::max()
                             : top_elem(ptr).tick);
        }

        void pushed(std::byte *ptr) {
            auto &guard = *std::launder(reinterpret_cast<guard_type *>(ptr));
            auto tick = top_elem(ptr).tick;
            if (tick != guard.top_tick.load(std::memory_order_relaxed)) {
                guard.top_tick.store(tick, std::memory_order_relaxed);
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
