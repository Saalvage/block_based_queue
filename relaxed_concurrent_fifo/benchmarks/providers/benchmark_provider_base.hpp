#ifndef BENCHMARK_PROVIDER_BASE_HPP_INCLUDED
#define BENCHMARK_PROVIDER_BASE_HPP_INCLUDED

#include <string>
#include <atomic>
#include <barrier>
#include <thread>
#include <future>
#include <vector>
#include <stdexcept>
#include <iostream>

#ifdef _POSIX_VERSION
#include <pthread.h>
#endif // _POSIX_VERSION

#include <papi.h>
#include <iostream>
#include <format>

#include "../benchmark_base.hpp"
#include "../../fifo.h"

static inline std::atomic<long unsigned int> global_papi_id;
static thread_local inline long unsigned int my_papi_id;

static long unsigned int get_my_papi_id() {
    return my_papi_id;
}

template <typename BENCHMARK>
class benchmark_provider {
public:
    virtual ~benchmark_provider() = default;
    virtual BENCHMARK test(const benchmark_info& info, double prefill_amount) = 0;
    virtual const std::string& get_name() const = 0;

public:
    template <fifo FIFO>
    void test_single(FIFO& fifo, BENCHMARK& b, const benchmark_info& info, double prefill_amount) {
        std::barrier a{info.num_threads + 1};
        std::atomic_bool over = false;
        std::vector<std::jthread> threads(info.num_threads);

        for (int i = 0; i < info.num_threads; i++) {
            threads[i] = std::jthread([&, i]() {
#ifdef _POSIX_VERSION
                cpu_set_t cpu_set;
                CPU_ZERO(&cpu_set);
                CPU_SET(i, &cpu_set);
                if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpu_set)) {
                    throw std::runtime_error("Failed to set thread affinity!");
                }
#endif // _POSIX_VERSION

                // Make sure handle is acquired on the thread it will be used on.
                typename FIFO::handle handle = fifo.get_handle();

                // If PREFILL_IN_ORDER is set we sequentially fill the queue from a single handle.
                std::size_t prefill = static_cast<std::size_t>(BENCHMARK::PREFILL_IN_ORDER
                    ? (i == 0 ? prefill_amount * b.fifo_size : 0)
                    : prefill_amount * b.fifo_size / info.num_threads);

                // We prefill from all handles since this may improve performance for certain implementations.
                for (std::size_t j = 0; j < prefill; j++) {
                    if (!handle.push(j + 1)) {
                        break;
                    }
                }

                my_papi_id = global_papi_id++;

                auto name = std::format("{}_thread_{}", get_name(), i);

                auto retval = PAPI_hl_region_begin(name.c_str());
                if (retval != PAPI_OK) {
                    std::cout << "Error initializing PAPI\n";
                }

                if constexpr (BENCHMARK::HAS_TIMEOUT) {
                    b.template per_thread<FIFO>(i, handle, a, over);
                } else {
                    b.template per_thread<FIFO>(i, handle, a);
                }

                retval = PAPI_hl_region_end(name.c_str());
                if (retval != PAPI_OK) {
                    std::cout << "Error ending PAPI\n";
                }
            });
        }

        // We signal, then start taking the time because some threads might not have arrived at the signal.
        a.arrive_and_wait();
        auto start = std::chrono::steady_clock::now();
        auto joined = std::async([&]() {
            for (auto& thread : threads) {
                thread.join();
            }
        });
        if constexpr (BENCHMARK::HAS_TIMEOUT) {
            if constexpr (BENCHMARK::RECORD_TIME) {
                joined.wait_until(start + std::chrono::seconds(info.test_time_seconds));
                over = true;
                joined.wait();
            } else {
                std::this_thread::sleep_until(start + std::chrono::seconds(info.test_time_seconds));
                over = true;

                if (joined.wait_for(std::chrono::seconds(10)) == std::future_status::timeout) {
                    std::cout << "Threads did not complete within timeout, assuming deadlock!" << std::endl;
                    std::exit(1);
                }
            }
        } else {
            joined.wait();
        }
        if constexpr (BENCHMARK::RECORD_TIME) {
            b.time_nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
        }
    }
};

#endif // BENCHMARK_PROVIDER_BASE_HPP_INCLUDED
