/**
 * @file cpu_affinity.cpp
 * @brief Implementation of CPU affinity and NUMA utilities
 */

#include "core/cpu_affinity.hpp"
#include <fmt/core.h>
#include <fmt/format.h>

#include <thread>
#include <cstring>

// Platform-specific includes
#ifdef __linux__
    #include <sched.h>
    #include <pthread.h>
    #include <unistd.h>
    #include <sys/syscall.h>
#elif defined(__APPLE__)
    #include <pthread.h>
    #include <mach/thread_policy.h>
    #include <mach/thread_act.h>
    #include <sys/sysctl.h>
#endif

namespace hft {

bool set_thread_affinity(int cpu_core) {
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_core, &cpuset);
    
    int result = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (result != 0) {
        fmt::print(stderr, "Failed to set CPU affinity to core {}: {}\n", 
                   cpu_core, strerror(result));
        return false;
    }
    
    fmt::print("Thread pinned to CPU core {}\n", cpu_core);
    return true;
    
#elif defined(__APPLE__)
    // macOS doesn't support hard CPU affinity, but we can use thread affinity hints
    thread_affinity_policy_data_t policy = { cpu_core };
    kern_return_t result = thread_policy_set(
        pthread_mach_thread_np(pthread_self()),
        THREAD_AFFINITY_POLICY,
        (thread_policy_t)&policy,
        THREAD_AFFINITY_POLICY_COUNT
    );
    
    if (result != KERN_SUCCESS) {
        fmt::print(stderr, "Failed to set thread affinity hint to core {} (macOS)\n", cpu_core);
        return false;
    }
    
    fmt::print("Thread affinity hint set to CPU core {} (macOS)\n", cpu_core);
    return true;
    
#else
    fmt::print(stderr, "CPU affinity not supported on this platform\n");
    return false;
#endif
}

bool set_thread_affinity(std::uint64_t thread_id, int cpu_core) {
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_core, &cpuset);
    
    int result = pthread_setaffinity_np(static_cast<pthread_t>(thread_id), 
                                        sizeof(cpu_set_t), &cpuset);
    return result == 0;
#else
    (void)thread_id;
    (void)cpu_core;
    return false;
#endif
}

int get_thread_affinity() {
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    
    int result = pthread_getaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (result != 0) {
        return -1;
    }
    
    // Find first set CPU
    for (int i = 0; i < CPU_SETSIZE; ++i) {
        if (CPU_ISSET(i, &cpuset)) {
            return i;
        }
    }
    return -1;
#else
    return -1;
#endif
}

bool set_realtime_priority(int priority) {
#ifdef __linux__
    struct sched_param param;
    param.sched_priority = priority;
    
    int result = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
    if (result != 0) {
        fmt::print(stderr, "Failed to set real-time priority {}: {} "
                   "(try running with CAP_SYS_NICE or as root)\n",
                   priority, strerror(result));
        return false;
    }
    
    fmt::print("Real-time priority set to {} (SCHED_FIFO)\n", priority);
    return true;
    
#elif defined(__APPLE__)
    // macOS uses different priority mechanism
    struct sched_param param;
    param.sched_priority = priority;
    
    int result = pthread_setschedparam(pthread_self(), SCHED_RR, &param);
    if (result != 0) {
        fmt::print(stderr, "Failed to set thread priority (macOS)\n");
        return false;
    }
    
    fmt::print("Thread priority set to {} (macOS SCHED_RR)\n", priority);
    return true;
#else
    (void)priority;
    return false;
#endif
}

int get_cpu_count() {
    return static_cast<int>(std::thread::hardware_concurrency());
}

int get_current_cpu() {
#ifdef __linux__
    return sched_getcpu();
#elif defined(__APPLE__)
    // macOS doesn't have sched_getcpu, return -1
    return -1;
#else
    return -1;
#endif
}

int get_numa_node(int cpu_core) {
#ifdef __linux__
    // Read from /sys/devices/system/cpu/cpu{N}/node{X}
    char path[128];
    std::snprintf(path, sizeof(path), 
                  "/sys/devices/system/cpu/cpu%d/topology/physical_package_id", 
                  cpu_core);
    
    FILE* f = fopen(path, "r");
    if (!f) {
        return 0;  // Default to node 0 if can't determine
    }
    
    int node = 0;
    if (fscanf(f, "%d", &node) != 1) {
        node = 0;
    }
    fclose(f);
    return node;
#else
    (void)cpu_core;
    return 0;
#endif
}

void* allocate_numa(std::size_t size, int numa_node) {
    // For simplicity, we use regular allocation here
    // In production, you'd use libnuma: numa_alloc_onnode()
    (void)numa_node;
    void* ptr = aligned_alloc(config::CACHE_LINE_SIZE, size);
    return ptr;
}

void free_numa(void* ptr, std::size_t size) {
    (void)size;
    free(ptr);
}

bool disable_frequency_scaling(int cpu_core) {
#ifdef __linux__
    char path[128];
    std::snprintf(path, sizeof(path),
                  "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_governor",
                  cpu_core);
    
    FILE* f = fopen(path, "w");
    if (!f) {
        fmt::print(stderr, "Cannot set CPU governor (requires root)\n");
        return false;
    }
    
    fprintf(f, "performance");
    fclose(f);
    
    fmt::print("CPU {} governor set to 'performance'\n", cpu_core);
    return true;
#else
    (void)cpu_core;
    fmt::print("Frequency scaling control not available on this platform\n");
    return false;
#endif
}

void print_cpu_topology() {
    int cpu_count = get_cpu_count();
    fmt::print("=== CPU Topology ===\n");
    fmt::print("CPU cores: {}\n", cpu_count);
    
#ifdef __linux__
    fmt::print("NUMA nodes:\n");
    for (int i = 0; i < cpu_count; ++i) {
        int node = get_numa_node(i);
        fmt::print("  Core {}: NUMA node {}\n", i, node);
    }
#endif
    
    fmt::print("Cache line size: {} bytes\n", config::CACHE_LINE_SIZE);
    fmt::print("====================\n");
}

// ScopedAffinity implementation
ScopedAffinity::ScopedAffinity(int cpu_core)
    : original_affinity_(get_thread_affinity())
    , was_set_(false) {
    was_set_ = set_thread_affinity(cpu_core);
}

ScopedAffinity::~ScopedAffinity() {
    if (was_set_ && original_affinity_ >= 0) {
        set_thread_affinity(original_affinity_);
    }
}

}  // namespace hft
