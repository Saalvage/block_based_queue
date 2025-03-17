#ifndef WRAPPER_2Dd_QUEUE_H_INCLUDED
#define WRAPPER_2Dd_QUEUE_H_INCLUDED

extern "C" {
    #include "2Dd-queue_optimized.h"
}

struct wrapper_2Dd_queue {
private:
    mqueue_t* queue;

public:
    struct handle {
        mqueue_t* queue;

        handle(mqueue_t* queue) : queue(queue_register(queue, gettid())) {
            free(seeds);
            seeds = seed_rand();
        }

        bool push(std::uint64_t value) {
            return enqueue(queue, value, value);
        }

        std::optional<std::uint64_t> pop() {
            auto ret = dequeue(queue);
            return ret == 0 ? std::nullopt : std::optional<std::uint64_t>(ret);
        }
    };

    wrapper_2Dd_queue(int num_threads, std::size_t size, width_t width, std::uint64_t relaxation_bound) {
        queue = create_queue(num_threads, width, 0, 3, relaxation_bound, 0);
    }

    handle get_handle() const {
        return handle(queue);
    }

    ~wrapper_2Dd_queue() {
        // How can you not offer this functionality built-in??
        auto handle = get_handle();
        while (handle.pop().has_value()) { }
        for (int i = 0; i < queue->width; i++) {
            ssfree(queue->get_array[i].descriptor.node);
        }
        ssfree(queue->get_array);
        ssfree(queue->put_array);
        ssfree(queue);
    }
};

#endif // WRAPPER_2Dd_QUEUE_H_INCLUDED
