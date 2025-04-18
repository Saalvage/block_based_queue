#include "timestamp.hpp"

#ifdef _WIN32
#include <intrin.h>
#elif !(defined(__arm__) || defined(__aarch64__))
#include <x86intrin.h>
#endif

namespace multififo {

std::uint64_t get_timestamp() {
#if defined(__arm__) || defined(__aarch64__)
    std::uint64_t val;
    asm volatile("mrs %0, cntvct_el0" : "=r" (val));
    return val;
#else
    return __rdtsc();
#endif
}

}
