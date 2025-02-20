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

        handle(mqueue_t* queue) : queue(queue) { }

        bool push(uint64_t value) {
            return enqueue(queue, 0, value);
        }

        std::optional<uint64_t> pop() {
            return dequeue(queue);
        }
    };

    wrapper_2Dd_queue(int num_threads, int size, width_t width, uint64_t relaxation_bound) {
        queue = create_queue(num_threads, width, 0, 3, relaxation_bound, 0);
    }

    handle get_handle() const {
        return handle(queue);
    }
};

#endif // WRAPPER_2Dd_QUEUE_H_INCLUDED
