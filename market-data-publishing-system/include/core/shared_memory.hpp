#pragma once

#include "config.hpp"
#include "spsc_ring_buffer.hpp"
#include "market_data.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace hft {

using SharedRingBuffer = SPSCRingBuffer<MarketData, config::RING_BUFFER_CAPACITY>;

class SharedMemory {
public:
    explicit SharedMemory(bool create_new, 
                         const char* name = config::SHM_NAME);
    ~SharedMemory();
    
    SharedMemory(const SharedMemory&) = delete;
    SharedMemory& operator=(const SharedMemory&) = delete;
    SharedMemory(SharedMemory&&) = delete;
    SharedMemory& operator=(SharedMemory&&) = delete;
    
    SharedRingBuffer* get_buffer() noexcept {
        return ring_buffer_;
    }
    
    const SharedRingBuffer* get_buffer() const noexcept {
        return ring_buffer_;
    }
    
    bool is_valid() const noexcept {
        return ring_buffer_ != nullptr;
    }
    
    std::size_t size() const noexcept {
        return shm_size_;
    }
    
    void unlink();
    static void unlink(const char* name);
    bool lock_memory();
    void prefault_pages();

private:
    const char* shm_name_;
    int shm_fd_;
    void* mapped_addr_;
    std::size_t shm_size_;
    SharedRingBuffer* ring_buffer_;
    bool is_locked_;
};

}  // namespace hft
