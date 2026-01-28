#pragma once

#include "config.hpp"
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <new>

namespace hft {
template<typename T, std::size_t Capacity = config::RING_BUFFER_CAPACITY>
class SPSCRingBuffer {
public:
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
    static_assert(Capacity > 0, "Capacity must be positive");
    
    using value_type = T;
    using size_type = std::size_t;
    using index_type = uint64_t;
    
    static constexpr index_type MASK = Capacity - 1;
    static constexpr std::size_t CACHE_LINE = config::CACHE_LINE_SIZE;
    
    SPSCRingBuffer() noexcept
        : write_idx_{0}
        , read_idx_{0} {
        for (std::size_t i = 0; i < Capacity; ++i) {
            new (&buffer_[i]) T();
        }
    }
    
    SPSCRingBuffer(const SPSCRingBuffer&) = delete;
    SPSCRingBuffer& operator=(const SPSCRingBuffer&) = delete;
    SPSCRingBuffer(SPSCRingBuffer&&) = delete;
    SPSCRingBuffer& operator=(SPSCRingBuffer&&) = delete;
    
    ~SPSCRingBuffer() {
        // Call destructors for any remaining elements
        while (!empty()) {
            T temp;
            pop(temp);
        }
    }
    
    bool push(const T& value) noexcept {
        const index_type write = write_idx_.load(std::memory_order_relaxed);
        const index_type read = read_idx_.load(std::memory_order_acquire);
        
        if (write - read >= Capacity) {
            return false;
        }
        
        buffer_[write & MASK] = value;
        write_idx_.store(write + 1, std::memory_order_release);
        
        return true;
    }
    
    void push_blocking(const T& value) noexcept {
        while (!push(value)) {
            #if defined(__x86_64__)
            __asm__ __volatile__("pause" ::: "memory");
            #endif
        }
    }
    
    bool pop(T& value) noexcept {
        const index_type read = read_idx_.load(std::memory_order_relaxed);
        const index_type write = write_idx_.load(std::memory_order_acquire);
        
        if (read >= write) {
            return false;
        }
        
        value = buffer_[read & MASK];
        read_idx_.store(read + 1, std::memory_order_release);
        
        return true;
    }
    
    void pop_blocking(T& value) noexcept {
        while (!pop(value)) {
            #if defined(__x86_64__)
            __asm__ __volatile__("pause" ::: "memory");
            #endif
        }
    }
    
    bool peek(T& value) const noexcept {
        const index_type read = read_idx_.load(std::memory_order_relaxed);
        const index_type write = write_idx_.load(std::memory_order_acquire);
        
        if (read >= write) {
            return false;
        }
        
        value = buffer_[read & MASK];
        return true;
    }
    
    bool empty() const noexcept {
        const index_type read = read_idx_.load(std::memory_order_acquire);
        const index_type write = write_idx_.load(std::memory_order_acquire);
        return read >= write;
    }
    
    bool full() const noexcept {
        const index_type read = read_idx_.load(std::memory_order_acquire);
        const index_type write = write_idx_.load(std::memory_order_acquire);
        return (write - read) >= Capacity;
    }
    
    size_type size() const noexcept {
        const index_type read = read_idx_.load(std::memory_order_acquire);
        const index_type write = write_idx_.load(std::memory_order_acquire);
        return static_cast<size_type>(write - read);
    }
    
    static constexpr size_type capacity() noexcept {
        return Capacity;
    }
    
    index_type total_published() const noexcept {
        return write_idx_.load(std::memory_order_acquire);
    }
    
    index_type total_consumed() const noexcept {
        return read_idx_.load(std::memory_order_acquire);
    }
    
    void reset() noexcept {
        write_idx_.store(0, std::memory_order_relaxed);
        read_idx_.store(0, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }

private:
    alignas(CACHE_LINE) std::atomic<index_type> write_idx_;
    alignas(CACHE_LINE) std::atomic<index_type> read_idx_;
    alignas(CACHE_LINE) T buffer_[Capacity];
};

}  // namespace hft
