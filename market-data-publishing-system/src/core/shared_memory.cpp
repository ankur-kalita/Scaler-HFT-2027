#include "core/shared_memory.hpp"
#include <fmt/core.h>
#include <fmt/format.h>

#include <cerrno>
#include <cstring>
#include <new>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace hft {

SharedMemory::SharedMemory(bool create_new, const char* name)
    : shm_name_(name)
    , shm_fd_(-1)
    , mapped_addr_(nullptr)
    , shm_size_(0)
    , ring_buffer_(nullptr)
    , is_locked_(false) {
    
    shm_size_ = sizeof(SharedRingBuffer);
    
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size > 0) {
        shm_size_ = ((shm_size_ + page_size - 1) / page_size) * page_size;
    }
    
    fmt::print("Shared memory size: {} bytes ({} KB)\n", 
               shm_size_, shm_size_ / 1024);
    
    if (create_new) {
        shm_unlink(shm_name_);
        
        shm_fd_ = shm_open(shm_name_, O_CREAT | O_RDWR | O_EXCL, config::SHM_MODE);
        if (shm_fd_ == -1) {
            fmt::print(stderr, "Failed to create shared memory '{}': {}\n",
                       shm_name_, strerror(errno));
            return;
        }
        
        if (ftruncate(shm_fd_, static_cast<off_t>(shm_size_)) == -1) {
            fmt::print(stderr, "Failed to set shared memory size: {}\n",
                       strerror(errno));
            close(shm_fd_);
            shm_unlink(shm_name_);
            shm_fd_ = -1;
            return;
        }
        
        int mmap_flags = MAP_SHARED;
#ifdef __linux__
        mmap_flags |= MAP_POPULATE;
#endif
        
        mapped_addr_ = mmap(nullptr, shm_size_, 
                           PROT_READ | PROT_WRITE,
                           mmap_flags,
                           shm_fd_, 0);
        
        if (mapped_addr_ == MAP_FAILED) {
            fmt::print(stderr, "Failed to mmap shared memory: {}\n",
                       strerror(errno));
            close(shm_fd_);
            shm_unlink(shm_name_);
            shm_fd_ = -1;
            mapped_addr_ = nullptr;
            return;
        }
        
        ring_buffer_ = new (mapped_addr_) SharedRingBuffer();
        
        fmt::print("Created shared memory '{}' at address {:p}\n",
                   shm_name_, mapped_addr_);
        
    } else {
        
        shm_fd_ = shm_open(shm_name_, O_RDWR, 0);
        if (shm_fd_ == -1) {
            fmt::print(stderr, "Failed to open shared memory '{}': {}\n"
                       "Make sure the publisher is running first.\n",
                       shm_name_, strerror(errno));
            return;
        }
        
        struct stat sb;
        if (fstat(shm_fd_, &sb) == -1) {
            fmt::print(stderr, "Failed to get shared memory size: {}\n",
                       strerror(errno));
            close(shm_fd_);
            shm_fd_ = -1;
            return;
        }
        shm_size_ = static_cast<std::size_t>(sb.st_size);
        
        mapped_addr_ = mmap(nullptr, shm_size_,
                           PROT_READ | PROT_WRITE,
                           MAP_SHARED,
                           shm_fd_, 0);
        
        if (mapped_addr_ == MAP_FAILED) {
            fmt::print(stderr, "Failed to mmap shared memory: {}\n",
                       strerror(errno));
            close(shm_fd_);
            shm_fd_ = -1;
            mapped_addr_ = nullptr;
            return;
        }
        
        ring_buffer_ = reinterpret_cast<SharedRingBuffer*>(mapped_addr_);
        
        fmt::print("Attached to shared memory '{}' at address {:p}\n",
                   shm_name_, mapped_addr_);
    }
}

SharedMemory::~SharedMemory() {
    if (mapped_addr_ != nullptr && mapped_addr_ != MAP_FAILED) {
        if (is_locked_) {
            munlock(mapped_addr_, shm_size_);
        }
        
        if (munmap(mapped_addr_, shm_size_) == -1) {
            fmt::print(stderr, "Warning: Failed to unmap shared memory: {}\n",
                       strerror(errno));
        }
        mapped_addr_ = nullptr;
        ring_buffer_ = nullptr;
    }
    
    if (shm_fd_ != -1) {
        close(shm_fd_);
        shm_fd_ = -1;
    }
}

void SharedMemory::unlink() {
    if (shm_name_ != nullptr) {
        if (shm_unlink(shm_name_) == -1) {
            fmt::print(stderr, "Warning: Failed to unlink shared memory '{}': {}\n",
                       shm_name_, strerror(errno));
        } else {
            fmt::print("Unlinked shared memory '{}'\n", shm_name_);
        }
    }
}

void SharedMemory::unlink(const char* name) {
    if (shm_unlink(name) == -1) {
        fmt::print(stderr, "Warning: Failed to unlink shared memory '{}': {}\n",
                   name, strerror(errno));
    } else {
        fmt::print("Unlinked shared memory '{}'\n", name);
    }
}

bool SharedMemory::lock_memory() {
    if (mapped_addr_ == nullptr) {
        return false;
    }
    
    if (mlock(mapped_addr_, shm_size_) == -1) {
        fmt::print(stderr, "Failed to lock shared memory: {} "
                   "(try increasing RLIMIT_MEMLOCK or run as root)\n",
                   strerror(errno));
        return false;
    }
    
    is_locked_ = true;
    fmt::print("Locked {} KB of shared memory (prevents swapping)\n",
               shm_size_ / 1024);
    return true;
}

void SharedMemory::prefault_pages() {
    if (mapped_addr_ == nullptr) {
        return;
    }
    
    volatile char* ptr = static_cast<volatile char*>(mapped_addr_);
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        page_size = 4096;
    }
    
    for (std::size_t i = 0; i < shm_size_; i += static_cast<std::size_t>(page_size)) {
        char temp = ptr[i];
        ptr[i] = temp;
        (void)temp;
    }
    
    fmt::print("Pre-faulted {} pages\n", shm_size_ / page_size);
}

}  // namespace hft
