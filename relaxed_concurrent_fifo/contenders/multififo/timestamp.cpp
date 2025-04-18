#include "timestamp.hpp"

#ifdef _WIN32
#include <intrin.h>
#elif !(defined(__arm__) || defined(__aarch64__))
#include <x86intrin.h>
#else
#include <chrono>
#endif

namespace multififo {

std::uint64_t get_timestamp() {
#if defined(__arm__) || defined(__aarch64__)
    return std::chrono::steady_clock::now().time_since_epoch().count();
#else
    return __rdtsc();
#endif
}

}
