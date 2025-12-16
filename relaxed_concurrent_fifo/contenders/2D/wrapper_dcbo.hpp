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

        ~handle() {
            d_balanced_free();
        }

        bool push(std::uint64_t value) {
            return enqueue_dcbo(queue, value, value);
        }

        std::optional<std::uint64_t> pop() {
            auto ret = dequeue_dcbo(queue);
            return ret == 0 ? std::nullopt : std::optional<std::uint64_t>(ret);
        }
    };

    wrapper_dcbo_queue(int num_threads, std::size_t size, double queue_factor, std::uint32_t stick) {
        queue = create_queue_dcbo(std::max<long>(1, std::lround(num_threads * queue_factor)), stick, num_threads);
    }

    handle get_handle() const {
        return handle(queue);
    }

    ~wrapper_dcbo_queue() {
        // How can you not offer this functionality built-in??
        for (std::size_t i = 0; i < queue->width; i++) {
            queue_t& partial_queue = queue->queues[i];
            RingQueue* head = partial_queue.head;
            while (head != nullptr) {
                RingQueue* tmp = head->next;
                ssfree(head);
                head = tmp;
            }
        }
        ssfree(queue->queues);
        ssfree(queue);
    }
};

#endif // WRAPPER_DCBO_QUEUE_H_INCLUDED
