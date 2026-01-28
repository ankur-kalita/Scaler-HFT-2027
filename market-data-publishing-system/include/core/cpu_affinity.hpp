#pragma once

#include "config.hpp"
#include <cstdint>

namespace hft {

bool set_thread_affinity(int cpu_core);
bool set_thread_affinity(std::uint64_t thread_id, int cpu_core);
int get_thread_affinity();
bool set_realtime_priority(int priority);
int get_cpu_count();
int get_current_cpu();
int get_numa_node(int cpu_core);
void* allocate_numa(std::size_t size, int numa_node);
void free_numa(void* ptr, std::size_t size);
bool disable_frequency_scaling(int cpu_core);
void print_cpu_topology();

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
