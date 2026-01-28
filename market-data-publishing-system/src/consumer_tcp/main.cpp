#include "core/config.hpp"
#include "core/timestamp.hpp"
#include "core/cpu_affinity.hpp"
#include "core/market_data.hpp"

#include <fmt/core.h>
#include <fmt/format.h>

#include <boost/asio.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <string>

namespace {

std::atomic<bool> g_running{true};

void signal_handler(int signum) {
    fmt::print("\nReceived signal {}, shutting down...\n", signum);
    g_running.store(false, std::memory_order_release);
}

void print_banner() {
    fmt::print("\n");
    fmt::print("╔═══════════════════════════════════════════════════════════════╗\n");
    fmt::print("║     HFT Market Data Consumer - Process C (TCP)                ║\n");
    fmt::print("║     Low-Latency Network Consumer via Loopback                 ║\n");
    fmt::print("╚═══════════════════════════════════════════════════════════════╝\n");
    fmt::print("\n");
}

bool parse_json(const char* json, hft::MarketData& data) {
    const char* inst_start = std::strstr(json, "\"instrument\":\"");
    if (!inst_start) return false;
    inst_start += 14;
    
    const char* inst_end = std::strchr(inst_start, '"');
    if (!inst_end) return false;
    
    std::size_t inst_len = std::min(static_cast<std::size_t>(inst_end - inst_start), 
                                     hft::config::MAX_SYMBOL_LENGTH - 1);
    std::memset(data.instrument, 0, sizeof(data.instrument));
    std::memcpy(data.instrument, inst_start, inst_len);
    
    const char* bid_start = std::strstr(json, "\"bid\":");
    if (!bid_start) return false;
    bid_start += 6;
    data.bid = std::strtod(bid_start, nullptr);
    
    const char* ask_start = std::strstr(json, "\"ask\":");
    if (!ask_start) return false;
    ask_start += 6;
    data.ask = std::strtod(ask_start, nullptr);
    
    const char* ts_start = std::strstr(json, "\"timestamp_ns\":");
    if (!ts_start) return false;
    ts_start += 15;
    data.timestamp_ns = std::strtoull(ts_start, nullptr, 10);
    
    return true;
}

void log_market_data(const hft::MarketData& data, int64_t latency_ns) {
    char ts_buffer[32];
    hft::format_timestamp(data.timestamp_ns, ts_buffer);
    
    fmt::print("{} {} BID={:.2f} ASK={:.2f} LAT={}ns\n",
               ts_buffer,
               data.instrument,
               data.bid,
               data.ask,
               latency_ns);
}

}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    print_banner();
    
    fmt::print("=== CPU Configuration ===\n");
    
    if (hft::set_thread_affinity(hft::config::TCP_CONSUMER_CPU_CORE)) {
        fmt::print("TCP Consumer pinned to CPU core {}\n", 
                   hft::config::TCP_CONSUMER_CPU_CORE);
    }
    
    hft::set_realtime_priority(50);
    
    fmt::print("\n=== TCP Connection Setup ===\n");
    
    boost::asio::io_context io_context;
    boost::asio::ip::tcp::socket socket(io_context);
    
    try {
        boost::asio::ip::tcp::endpoint endpoint(
            boost::asio::ip::make_address(hft::config::TCP_HOST),
            hft::config::TCP_PORT);
        
        fmt::print("Connecting to {}:{}...\n", 
                   hft::config::TCP_HOST, hft::config::TCP_PORT);
        
        socket.connect(endpoint);
        
        boost::system::error_code ec;
        
        socket.set_option(boost::asio::ip::tcp::no_delay(true), ec);
        if (ec) {
            fmt::print(stderr, "Warning: Failed to set TCP_NODELAY\n");
        }
        
        socket.set_option(boost::asio::socket_base::receive_buffer_size(
            static_cast<int>(hft::config::TCP_RECV_BUFFER_SIZE)), ec);
        
        socket.set_option(boost::asio::socket_base::send_buffer_size(
            static_cast<int>(hft::config::TCP_SEND_BUFFER_SIZE)), ec);
        
#ifdef __linux__
        int quickack = 1;
        setsockopt(socket.native_handle(), IPPROTO_TCP, TCP_QUICKACK,
                   &quickack, sizeof(quickack));
#endif
        
        fmt::print("Connected successfully!\n");
        
    } catch (const boost::system::system_error& e) {
        fmt::print(stderr, "FATAL: Failed to connect to server: {}\n", e.what());
        fmt::print(stderr, "Make sure the publisher (Process A) is running.\n");
        return 1;
    }
    
    fmt::print("\n=== Starting Consumer ===\n");
    fmt::print("Press Ctrl+C to stop\n\n");
    
    auto start_time = std::chrono::steady_clock::now();
    uint64_t messages_received = 0;
    uint64_t total_latency_ns = 0;
    int64_t min_latency_ns = INT64_MAX;
    int64_t max_latency_ns = 0;
    uint64_t parse_errors = 0;
    
    char read_buffer[hft::config::TCP_READ_BUFFER_SIZE];
    std::string line_buffer;
    line_buffer.reserve(512);
    
    uint64_t last_stats_time = hft::get_monotonic_ns();
    constexpr uint64_t STATS_INTERVAL_NS = 1000000000ULL;
    uint64_t messages_this_second = 0;
    
    hft::MarketData data;
    
    while (g_running.load(std::memory_order_acquire)) {
        boost::system::error_code ec;
        
        std::size_t bytes_read = socket.read_some(
            boost::asio::buffer(read_buffer, sizeof(read_buffer) - 1), ec);
        
        if (ec) {
            if (ec == boost::asio::error::eof) {
                fmt::print("Server disconnected\n");
                break;
            }
            if (ec != boost::asio::error::would_block) {
                fmt::print(stderr, "Read error: {}\n", ec.message());
                break;
            }
            continue;
        }
        
        uint64_t receive_time = hft::get_timestamp_ns();
        
        read_buffer[bytes_read] = '\0';
        
        line_buffer.append(read_buffer, bytes_read);
        
        std::size_t pos = 0;
        std::size_t newline_pos;
        
        while ((newline_pos = line_buffer.find('\n', pos)) != std::string::npos) {
            std::string json_line = line_buffer.substr(pos, newline_pos - pos);
            pos = newline_pos + 1;
            
            if (parse_json(json_line.c_str(), data)) {
                int64_t latency = hft::calculate_latency_ns(data.timestamp_ns, receive_time);
                
                ++messages_received;
                ++messages_this_second;
                total_latency_ns += static_cast<uint64_t>(latency);
                
                if (latency < min_latency_ns) min_latency_ns = latency;
                if (latency > max_latency_ns) max_latency_ns = latency;
                
                if (messages_received % 10000 == 0 || messages_received <= 10) {
                    log_market_data(data, latency);
                }
            } else {
                ++parse_errors;
            }
        }
        
        if (pos > 0) {
            line_buffer.erase(0, pos);
        }
        
        uint64_t now = hft::get_monotonic_ns();
        if (now - last_stats_time >= STATS_INTERVAL_NS) {
            double avg_latency = messages_received > 0 
                ? static_cast<double>(total_latency_ns) / messages_received 
                : 0.0;
            
            fmt::print("[Stats] Msgs: {:>10} | Rate: {:>8}/s | "
                       "Lat(avg/min/max): {:.0f}/{}/{} ns | Errors: {}\n",
                       messages_received,
                       messages_this_second,
                       avg_latency,
                       min_latency_ns,
                       max_latency_ns,
                       parse_errors);
            
            messages_this_second = 0;
            last_stats_time = now;
        }
    }
    
    fmt::print("\n=== Final Statistics ===\n");
    
    auto end_time = std::chrono::steady_clock::now();
    auto total_seconds = std::chrono::duration_cast<std::chrono::seconds>(
        end_time - start_time).count();
    
    double avg_latency_ns = messages_received > 0 
        ? static_cast<double>(total_latency_ns) / messages_received 
        : 0.0;
    
    fmt::print("Total messages received: {}\n", messages_received);
    fmt::print("Parse errors: {}\n", parse_errors);
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
    
    boost::system::error_code ec;
    socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    socket.close(ec);
    
    return 0;
}
