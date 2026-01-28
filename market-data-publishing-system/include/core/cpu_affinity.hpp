#pragma once

/**
 * @file cpu_affinity.hpp
 * @brief CPU affinity and NUMA optimization utilities
 * 
 * DESIGN DECISIONS:
 * 
 * 1. CPU PINNING (Affinity)
 *    - Binds thread/process to specific CPU core
 *    - Prevents OS scheduler from moving threads between cores
 *    - Benefits:
 *      * Warm L1/L2 cache stays with the thread
 *      * Predictable latency (no migration overhead)
 *      * Better cache locality
 * 
 * 2. NUMA AWARENESS
 *    - Non-Uniform Memory Access architecture
 *    - Memory access time depends on which NUMA node owns the memory
 *    - We pin threads to cores and allocate memory on same NUMA node
 *    - Critical for multi-socket servers
 * 
 * 3. ISOLATED CORES
 *    - For ultra-low latency, use isolcpus kernel parameter
 *    - Prevents kernel from scheduling other tasks on our cores
 *    - Example: isolcpus=2,3 in kernel boot parameters
 * 
 * 4. THREAD PRIORITY
 *    - SCHED_FIFO for real-time scheduling
 *    - Higher priority than regular processes
 *    - Requires CAP_SYS_NICE capability
 * 
 * USAGE:
 *   // Pin current thread to core 2
 *   hft::set_thread_affinity(2);
 *   
 *   // Set real-time priority
 *   hft::set_realtime_priority(99);
 */

#include "config.hpp"
#include <cstdint>

namespace hft {

/**
 * @brief Set CPU affinity for current thread.
 * 
 * Pins the calling thread to a specific CPU core.
 * 
 * @param cpu_core CPU core number (0-indexed)
 * @return true if successful, false otherwise
 */
bool set_thread_affinity(int cpu_core);

/**
 * @brief Set CPU affinity for a specific thread.
 * 
 * @param thread_id Native thread handle (pthread_t on POSIX)
 * @param cpu_core CPU core number
 * @return true if successful, false otherwise
 */
bool set_thread_affinity(std::uint64_t thread_id, int cpu_core);

/**
 * @brief Get current thread's CPU affinity.
 * 
 * @return CPU core number, or -1 if affinity spans multiple cores
 */
int get_thread_affinity();

/**
 * @brief Set real-time scheduling priority.
 * 
 * Uses SCHED_FIFO for deterministic scheduling.
 * Requires CAP_SYS_NICE capability or root.
 * 
 * @param priority Priority level (1-99, higher = more priority)
 * @return true if successful, false otherwise
 */
bool set_realtime_priority(int priority);

/**
 * @brief Get number of available CPU cores.
 */
int get_cpu_count();

/**
 * @brief Get current CPU core where thread is running.
 */
int get_current_cpu();

/**
 * @brief Get NUMA node for a given CPU core.
 * 
 * @param cpu_core CPU core number
 * @return NUMA node ID, or -1 if NUMA not available
 */
int get_numa_node(int cpu_core);

/**
 * @brief Allocate memory on a specific NUMA node.
 * 
 * @param size Bytes to allocate
 * @param numa_node NUMA node ID
 * @return Pointer to allocated memory, or nullptr on failure
 */
void* allocate_numa(std::size_t size, int numa_node);

/**
 * @brief Free NUMA-allocated memory.
 */
void free_numa(void* ptr, std::size_t size);

/**
 * @brief Disable CPU frequency scaling for consistent performance.
 * 
 * Sets CPU governor to "performance" mode.
 * Requires root or appropriate permissions.
 * 
 * @param cpu_core CPU core to configure
 * @return true if successful
 */
bool disable_frequency_scaling(int cpu_core);

/**
 * @brief Log CPU topology information.
 * 
 * Prints core count, NUMA topology, cache sizes, etc.
 */
void print_cpu_topology();

/**
 * @brief RAII helper to set and restore thread affinity.
 */
class ScopedAffinity {
public:
    explicit ScopedAffinity(int cpu_core);
    ~ScopedAffinity();
    
    ScopedAffinity(const ScopedAffinity&) = delete;
    ScopedAffinity& operator=(const ScopedAffinity&) = delete;
    
    bool is_set() const noexcept { return was_set_; }
    
private:
    int original_affinity_;
    bool was_set_;
};

}  // namespace hft
