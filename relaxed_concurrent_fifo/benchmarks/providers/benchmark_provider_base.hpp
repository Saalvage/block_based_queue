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
    static BENCHMARK test_single(FIFO& fifo, const benchmark_info& info, double prefill_amount) {
        std::vector<typename FIFO::handle> handles;
        handles.reserve(info.num_threads);
        for (int i = 0; i < info.num_threads; i++) {
            handles.push_back(fifo.get_handle());
        }
        // We prefill from all handles since this may improve performance for certain implementations.
        // TODO: Is there the possibility of the performance improvement coming from actually being filled concurrently?
        for (std::size_t i = 0; i < prefill_amount * BENCHMARK::SIZE; i++) {
            // If PREFILL_IN_ORDER is set we sequentially fill the queue from a single handle.
            if (!handles[BENCHMARK::PREFILL_IN_ORDER ? 0 : (i % info.num_threads)].push(i + 1)) {
                break;
            }
        }

        std::barrier a{info.num_threads + 1};
        std::atomic_bool over = false;
        std::vector<std::jthread> threads(info.num_threads);
        BENCHMARK b{info};

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
                    ? (i == 0 ? prefill_amount * BENCHMARK::SIZE : 0)
                    : prefill_amount * BENCHMARK::SIZE / info.num_threads);

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
            std::this_thread::sleep_until(start + std::chrono::seconds(info.test_time_seconds));
            over = true;

            if (joined.wait_for(std::chrono::seconds(10)) == std::future_status::timeout) {
                std::cout << "Threads did not complete within timeout, assuming deadlock!" << std::endl;
                std::exit(1);
            }
        } else {
            joined.wait();
        }
        if constexpr (BENCHMARK::RECORD_TIME) {
            b.time_nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
        }

        return b;
    }
};

#endif // BENCHMARK_PROVIDER_BASE_HPP_INCLUDED
