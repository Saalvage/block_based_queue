#ifndef FAAAQUEUE_WRAPPER_HPP_INCLUDED
#define FAAAQUEUE_WRAPPER_HPP_INCLUDED

#include "FAAArrayQueue.hpp"

#include <optional>


template <typename T>
struct wrapper_faaaqueue {
    FAAArrayQueue<void> queue;

    std::atomic_int next_thread_id;

    wrapper_faaaqueue(int thread_count, size_t) : queue(thread_count) { }

    struct handle {
        FAAArrayQueue<void>* queue;
        int tid;

        bool push(const T& value) {
            queue->enqueue(reinterpret_cast<void*>(value), tid);
            return true;
        }

        std::optional<T> pop() {
            void* ret = queue->dequeue(tid);
            return ret == nullptr ? std::nullopt : std::optional{ reinterpret_cast<T>(ret) };
        }
    };

    handle get_handle() {
        return { &queue, next_thread_id++ };
    }
};

#endif // FAAAQUEUE_WRAPPER_HPP_INCLUDED
