#include "timestamp.hpp"

#ifdef _WIN32
#include <intrin.h>
#elif !(defined(__arm__) || defined(__aarch64__))
#include <x86intrin.h>
#else
#include <ctime>
#endif

namespace multififo {

std::uint64_t get_timestamp() {
#if defined(__arm__) || defined(__aarch64__)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000 + ts.tv_nsec;
#else
    return __rdtsc();
#endif
}

}
