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

#include "../benchmark_base.hpp"
#include "../../fifo.h"

template <typename BENCHMARK>
class benchmark_provider {
public:
    virtual ~benchmark_provider() = default;
    virtual BENCHMARK test(const benchmark_info& info, double prefill_amount) const = 0;
    virtual const std::string& get_name() const = 0;

protected:
    template <fifo FIFO>
    static void test_single(FIFO& fifo, BENCHMARK& b, const benchmark_info& info, double prefill_amount) {
        std::barrier a{1 + 1};
        std::atomic_bool over = false;
        std::vector<std::jthread> threads(1);

        for (int i = 0; i < 1; i++) {
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

                if constexpr (BENCHMARK::HAS_TIMEOUT) {
                    b.template per_thread<FIFO>(i, handle, a, over);
                } else {
                    b.template per_thread<FIFO>(i, handle, a);
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
