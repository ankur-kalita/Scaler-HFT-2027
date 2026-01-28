#include "core/market_data.hpp"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace hft {

std::size_t serialize_to_json(const MarketData& data, char* buffer) noexcept {
    char* ptr = buffer;
    
    std::memcpy(ptr, "{\"instrument\":\"", 15);
    ptr += 15;
    
    std::size_t sym_len = std::strlen(data.instrument);
    std::memcpy(ptr, data.instrument, sym_len);
    ptr += sym_len;
    
    std::memcpy(ptr, "\",\"bid\":", 8);
    ptr += 8;
    ptr += std::snprintf(ptr, 32, "%.2f", data.bid);
    
    std::memcpy(ptr, ",\"ask\":", 7);
    ptr += 7;
    ptr += std::snprintf(ptr, 32, "%.2f", data.ask);
    
    std::memcpy(ptr, ",\"timestamp_ns\":", 16);
    ptr += 16;
    ptr += std::snprintf(ptr, 24, "%lu", static_cast<unsigned long>(data.timestamp_ns));
    
    std::memcpy(ptr, "}\n", 2);
    ptr += 2;
    
    return static_cast<std::size_t>(ptr - buffer);
}

bool deserialize_from_json(const char* json, MarketData& data) noexcept {
    const char* inst_start = std::strstr(json, "\"instrument\":\"");
    if (!inst_start) return false;
    inst_start += 14;
    
    const char* inst_end = std::strchr(inst_start, '"');
    if (!inst_end) return false;
    
    std::size_t inst_len = std::min(static_cast<std::size_t>(inst_end - inst_start), 
                                     config::MAX_SYMBOL_LENGTH - 1);
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

}  // namespace hft
