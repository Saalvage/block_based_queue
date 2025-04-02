#ifndef WRAPPER_DCBO_QUEUE_H_INCLUDED
#define WRAPPER_DCBO_QUEUE_H_INCLUDED

extern "C" {
    #include "d-balanced-queue.h"
}

struct wrapper_dcbo_queue {
private:
    dbco_queue* queue;

public:
    struct handle {
        dbco_queue* queue;

        handle(dbco_queue* queue) : queue(d_balanced_register(queue, gettid())) {
            free(seeds);
            seeds = seed_rand();
        }

        bool push(std::uint64_t value) {
            return enqueue_dcbo(queue, value, value);
        }

        std::optional<std::uint64_t> pop() {
            auto ret = dequeue_dcbo(queue);
            return ret == 0 ? std::nullopt : std::optional<std::uint64_t>(ret);
        }
    };

    wrapper_dcbo_queue(int num_threads, std::size_t size, std::uint32_t queue_factor) {
        queue = create_queue_dcbo(num_threads * queue_factor, 2, num_threads);
    }

    handle get_handle() const {
        return handle(queue);
    }

    ~wrapper_dcbo_queue() {
        // How can you not offer this functionality built-in??
        auto handle = get_handle();
        while (handle.pop().has_value()) { }
    }
};

#endif // WRAPPER_DCBO_QUEUE_H_INCLUDED
