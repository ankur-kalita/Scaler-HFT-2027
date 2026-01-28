/**
 * @file main.cpp
 * @brief Process B - Market Data Consumer over Shared Memory
 * 
 * This consumer reads market data from the shared memory ring buffer
 * and logs received messages with nanosecond timestamps.
 * 
 * PERFORMANCE CHARACTERISTICS:
 * - Lock-free reading from SPSC ring buffer
 * - Zero system calls in hot path
 * - Nanosecond timestamp logging
 * - CPU affinity for separate core
 * - Busy-wait polling for lowest latency
 */

#include "core/config.hpp"
#include "core/timestamp.hpp"
#include "core/cpu_affinity.hpp"
#include "core/shared_memory.hpp"
#include "core/market_data.hpp"

#include <fmt/core.h>
#include <fmt/format.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <thread>

namespace {

std::atomic<bool> g_running{true};

void signal_handler(int signum) {
    fmt::print("\nReceived signal {}, shutting down...\n", signum);
    g_running.store(false, std::memory_order_release);
}

void print_banner() {
    fmt::print("\n");
    fmt::print("╔═══════════════════════════════════════════════════════════════╗\n");
    fmt::print("║     HFT Market Data Consumer - Process B (Shared Memory)      ║\n");
    fmt::print("║     Low-Latency IPC via Lock-Free Ring Buffer                 ║\n");
    fmt::print("╚═══════════════════════════════════════════════════════════════╝\n");
    fmt::print("\n");
}

/**
 * @brief Log market data with formatted timestamp.
 * 
 * Format: [HH:MM:SS.nnnnnnnnn] SYMBOL BID=X.XX ASK=X.XX SEQ=N LAT=Xns
 */
void log_market_data(const hft::MarketData& data, int64_t latency_ns) {
    char ts_buffer[32];
    hft::format_timestamp(data.timestamp_ns, ts_buffer);
    
    // Use fmt library for logging (as required by assignment)
    fmt::print("{} {} BID={:.2f} ASK={:.2f} SEQ={} LAT={}ns\n",
               ts_buffer,
               data.instrument,
               data.bid,
               data.ask,
               data.sequence_number,
               latency_ns);
}

}  // anonymous namespace

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    print_banner();
    
    // =========================================================================
    // STEP 1: CPU Affinity
    // =========================================================================
    fmt::print("=== CPU Configuration ===\n");
    
    if (hft::set_thread_affinity(hft::config::SHM_CONSUMER_CPU_CORE)) {
        fmt::print("SHM Consumer pinned to CPU core {}\n", 
                   hft::config::SHM_CONSUMER_CPU_CORE);
    }
    
    hft::set_realtime_priority(50);
    
    // =========================================================================
    // STEP 2: Attach to Shared Memory
    // =========================================================================
    fmt::print("\n=== Shared Memory Setup ===\n");
    
    hft::SharedMemory shm(false);  // false = attach to existing
    if (!shm.is_valid()) {
        fmt::print(stderr, "FATAL: Failed to attach to shared memory.\n");
        fmt::print(stderr, "Make sure the publisher (Process A) is running first.\n");
        return 1;
    }
    
    // Lock memory for consistent performance
    shm.lock_memory();
    shm.prefault_pages();
    
    auto* ring_buffer = shm.get_buffer();
    fmt::print("Attached to ring buffer at {:p}\n", static_cast<void*>(ring_buffer));
    fmt::print("Ring buffer capacity: {} messages\n", ring_buffer->capacity());
    
    // =========================================================================
    // STEP 3: Main Consumer Loop (HOT PATH)
    // =========================================================================
    fmt::print("\n=== Starting Consumer ===\n");
    fmt::print("Press Ctrl+C to stop\n\n");
    
    auto start_time = std::chrono::steady_clock::now();
    uint64_t messages_received = 0;
    uint64_t total_latency_ns = 0;
    int64_t min_latency_ns = INT64_MAX;
    int64_t max_latency_ns = 0;
    uint64_t last_sequence = 0;
    uint64_t gaps_detected = 0;
    
    // Statistics tracking
    uint64_t last_stats_time = hft::get_monotonic_ns();
    constexpr uint64_t STATS_INTERVAL_NS = 1000000000ULL;  // 1 second
    uint64_t messages_this_second = 0;
    
    hft::MarketData data;
    
    while (g_running.load(std::memory_order_acquire)) {
        // =====================================================================
        // Try to pop from ring buffer (non-blocking)
        // =====================================================================
        if (ring_buffer->pop(data)) {
            // Calculate end-to-end latency
            uint64_t receive_time = hft::get_timestamp_ns();
            int64_t latency = hft::calculate_latency_ns(data.timestamp_ns, receive_time);
            
            // Update statistics
            ++messages_received;
            ++messages_this_second;
            total_latency_ns += static_cast<uint64_t>(latency);
            
            if (latency < min_latency_ns) min_latency_ns = latency;
            if (latency > max_latency_ns) max_latency_ns = latency;
            
            // Check for sequence gaps
            if (last_sequence > 0 && data.sequence_number != last_sequence + 1) {
                ++gaps_detected;
            }
            last_sequence = data.sequence_number;
            
            // Log the message (every N messages to avoid overwhelming output)
            if (messages_received % 10000 == 0 || messages_received <= 10) {
                log_market_data(data, latency);
            }
        } else {
            // Buffer empty - busy wait with pause instruction
            #if defined(__x86_64__)
            __asm__ __volatile__("pause" ::: "memory");
            #elif defined(__aarch64__)
            __asm__ __volatile__("yield" ::: "memory");
            #endif
        }
        
        // Print statistics periodically
        uint64_t now = hft::get_monotonic_ns();
        if (now - last_stats_time >= STATS_INTERVAL_NS) {
            double avg_latency = messages_received > 0 
                ? static_cast<double>(total_latency_ns) / messages_received 
                : 0.0;
            
            fmt::print("[Stats] Msgs: {:>10} | Rate: {:>8}/s | "
                       "Lat(avg/min/max): {:.0f}/{}/{} ns | Gaps: {}\n",
                       messages_received,
                       messages_this_second,
                       avg_latency,
                       min_latency_ns,
                       max_latency_ns,
                       gaps_detected);
            
            messages_this_second = 0;
            last_stats_time = now;
        }
    }
    
    // =========================================================================
    // FINAL STATISTICS
    // =========================================================================
    fmt::print("\n=== Final Statistics ===\n");
    
    auto end_time = std::chrono::steady_clock::now();
    auto total_seconds = std::chrono::duration_cast<std::chrono::seconds>(
        end_time - start_time).count();
    
    double avg_latency_ns = messages_received > 0 
        ? static_cast<double>(total_latency_ns) / messages_received 
        : 0.0;
    
    fmt::print("Total messages received: {}\n", messages_received);
    fmt::print("Runtime: {} seconds\n", total_seconds);
    
    if (total_seconds > 0) {
        fmt::print("Average throughput: {:.0f} msg/sec\n",
                  static_cast<double>(messages_received) / total_seconds);
    }
    
    fmt::print("\nLatency Statistics (nanoseconds):\n");
    fmt::print("  Average: {:.0f} ns ({:.2f} us)\n", 
               avg_latency_ns, avg_latency_ns / 1000.0);
    fmt::print("  Minimum: {} ns ({:.2f} us)\n", 
               min_latency_ns, static_cast<double>(min_latency_ns) / 1000.0);
    fmt::print("  Maximum: {} ns ({:.2f} us)\n", 
               max_latency_ns, static_cast<double>(max_latency_ns) / 1000.0);
    
    fmt::print("\nSequence Analysis:\n");
    fmt::print("  Last sequence number: {}\n", last_sequence);
    fmt::print("  Gaps detected: {}\n", gaps_detected);
    
    fmt::print("\nRing Buffer Status:\n");
    fmt::print("  Total published: {}\n", ring_buffer->total_published());
    fmt::print("  Total consumed: {}\n", ring_buffer->total_consumed());
    fmt::print("  Messages remaining: {}\n", ring_buffer->size());
    
    return 0;
}
