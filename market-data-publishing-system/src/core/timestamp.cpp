/**
 * @file timestamp.cpp
 * @brief Implementation of timestamp utilities
 */

#include "core/timestamp.hpp"
#include <cstdio>
#include <ctime>

namespace hft {

const char* format_timestamp(uint64_t timestamp_ns, char* buffer) noexcept {
    // Convert nanoseconds to seconds and remainder
    time_t seconds = static_cast<time_t>(timestamp_ns / 1000000000ULL);
    uint64_t nanos = timestamp_ns % 1000000000ULL;
    
    // Convert to local time
    struct tm tm_info;
#ifdef _WIN32
    localtime_s(&tm_info, &seconds);
#else
    localtime_r(&seconds, &tm_info);
#endif
    
    // Format: [HH:MM:SS.nnnnnnnnn]
    // Using snprintf for safety
    std::snprintf(buffer, 32, "[%02d:%02d:%02d.%09lu]",
                  tm_info.tm_hour,
                  tm_info.tm_min,
                  tm_info.tm_sec,
                  static_cast<unsigned long>(nanos));
    
    return buffer;
}

}  // namespace hft
