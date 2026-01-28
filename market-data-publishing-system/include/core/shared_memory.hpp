#pragma once

/**
 * @file shared_memory.hpp
 * @brief POSIX shared memory management for IPC
 * 
 * DESIGN DECISIONS:
 * 
 * 1. shm_open() + mmap() (vs System V shmget)
 *    - POSIX API is more portable and modern
 *    - File-based naming is easier to manage
 *    - Integrates with filesystem permissions
 * 
 * 2. MAP_SHARED | MAP_POPULATE
 *    - MAP_SHARED: Changes visible to all processes
 *    - MAP_POPULATE: Pre-fault pages to avoid page faults in hot path
 * 
 * 3. mlock() for pinned memory
 *    - Prevents pages from being swapped out
 *    - Eliminates swap-related latency spikes
 * 
 * 4. Huge Pages support (optional)
 *    - Reduces TLB misses
 *    - Fewer page table entries
 *    - Better for large buffers
 * 
 * 5. RAII wrapper
 *    - Automatic cleanup on destruction
 *    - Exception-safe (well, we disable exceptions, but still safe)
 */

#include "config.hpp"
#include "spsc_ring_buffer.hpp"
#include "market_data.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace hft {

/**
 * @brief Shared memory ring buffer type for market data.
 */
using SharedRingBuffer = SPSCRingBuffer<MarketData, config::RING_BUFFER_CAPACITY>;

/**
 * @brief Shared memory manager for creating/attaching to ring buffer.
 * 
 * Usage:
 *   Producer: SharedMemory shm(true);   // Creates shared memory
 *   Consumer: SharedMemory shm(false);  // Attaches to existing
 */
class SharedMemory {
public:
    /**
     * @brief Construct shared memory manager.
     * 
     * @param create_new If true, creates new shared memory segment.
     *                   If false, attaches to existing segment.
     * @param name Shared memory name (default from config)
     */
    explicit SharedMemory(bool create_new, 
                         const char* name = config::SHM_NAME);
    
    /**
     * @brief Destructor - unmaps memory (optionally unlinks).
     */
    ~SharedMemory();
    
    /// Deleted copy/move operations
    SharedMemory(const SharedMemory&) = delete;
    SharedMemory& operator=(const SharedMemory&) = delete;
    SharedMemory(SharedMemory&&) = delete;
    SharedMemory& operator=(SharedMemory&&) = delete;
    
    /**
     * @brief Get pointer to the ring buffer.
     * @return Pointer to shared ring buffer
     */
    SharedRingBuffer* get_buffer() noexcept {
        return ring_buffer_;
    }
    
    const SharedRingBuffer* get_buffer() const noexcept {
        return ring_buffer_;
    }
    
    /**
     * @brief Check if shared memory is valid.
     */
    bool is_valid() const noexcept {
        return ring_buffer_ != nullptr;
    }
    
    /**
     * @brief Get the size of shared memory region.
     */
    std::size_t size() const noexcept {
        return shm_size_;
    }
    
    /**
     * @brief Unlink (delete) the shared memory segment.
     * 
     * Call this to clean up after all processes are done.
     * Only the creator should call this.
     */
    void unlink();
    
    /**
     * @brief Static method to unlink shared memory by name.
     * 
     * Useful for cleanup scripts.
     */
    static void unlink(const char* name);
    
    /**
     * @brief Lock memory pages to prevent swapping.
     * 
     * Requires appropriate permissions (CAP_IPC_LOCK or sufficient memlock limit).
     * @return true if successful, false otherwise
     */
    bool lock_memory();
    
    /**
     * @brief Pre-fault all pages to avoid page faults during operation.
     */
    void prefault_pages();

private:
    const char* shm_name_;      ///< Shared memory name
    int shm_fd_;                ///< File descriptor
    void* mapped_addr_;         ///< mmap'd address
    std::size_t shm_size_;
    SharedRingBuffer* ring_buffer_;
    bool is_locked_;
};

}  // namespace hft
