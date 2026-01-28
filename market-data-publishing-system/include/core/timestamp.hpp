#pragma once

#include <cstdint>
#include <ctime>
#include <chrono>

namespace hft {

inline uint64_t get_timestamp_ns() noexcept {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + 
           static_cast<uint64_t>(ts.tv_nsec);
}

inline uint64_t get_monotonic_ns() noexcept {
    struct timespec ts;
#ifdef __APPLE__
    clock_gettime(CLOCK_MONOTONIC, &ts);
#else
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
#endif
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + 
           static_cast<uint64_t>(ts.tv_nsec);
}

const char* format_timestamp(uint64_t timestamp_ns, char* buffer) noexcept;

inline int64_t calculate_latency_ns(uint64_t start_ns, uint64_t end_ns) noexcept {
    return static_cast<int64_t>(end_ns - start_ns);
}

inline double ns_to_us(int64_t ns) noexcept {
    return static_cast<double>(ns) / 1000.0;
}

inline double ns_to_ms(int64_t ns) noexcept {
    return static_cast<double>(ns) / 1000000.0;
}

#if defined(__x86_64__) || defined(_M_X64)
inline uint64_t rdtsc() noexcept {
    uint32_t lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

inline uint64_t rdtscp() noexcept {
    uint32_t lo, hi, aux;
    __asm__ __volatile__ ("rdtscp" : "=a"(lo), "=d"(hi), "=c"(aux));
    return (static_cast<uint64_t>(hi) << 32) | lo;
}
#endif

}  // namespace hft
