#pragma once

#include "config.hpp"
#include <cstdint>
#include <cstring>
#include <atomic>

namespace hft {

struct alignas(config::CACHE_LINE_SIZE) MarketData {
    char instrument[config::MAX_SYMBOL_LENGTH];
    double bid;
    double ask;
    uint64_t timestamp_ns;
    uint64_t sequence_number;
    char _padding[16];
    
    MarketData() noexcept 
        : bid(0.0)
        , ask(0.0)
        , timestamp_ns(0)
        , sequence_number(0) {
        std::memset(instrument, 0, sizeof(instrument));
        std::memset(_padding, 0, sizeof(_padding));
    }
    
    MarketData(const char* symbol, double bid_price, double ask_price,
               uint64_t ts, uint64_t seq) noexcept
        : bid(bid_price)
        , ask(ask_price)
        , timestamp_ns(ts)
        , sequence_number(seq) {
        std::memset(instrument, 0, sizeof(instrument));
        std::strncpy(instrument, symbol, config::MAX_SYMBOL_LENGTH - 1);
        std::memset(_padding, 0, sizeof(_padding));
    }
    
    void set_instrument(const char* symbol) noexcept {
        std::memset(instrument, 0, sizeof(instrument));
        std::strncpy(instrument, symbol, config::MAX_SYMBOL_LENGTH - 1);
    }
    
    double spread() const noexcept {
        return ask - bid;
    }
    
    double mid_price() const noexcept {
        return (bid + ask) / 2.0;
    }
};

static_assert(sizeof(MarketData) == config::CACHE_LINE_SIZE,
              "MarketData must be exactly one cache line");
static_assert(alignof(MarketData) == config::CACHE_LINE_SIZE,
              "MarketData must be aligned to cache line boundary");

constexpr std::size_t JSON_BUFFER_SIZE = 256;

std::size_t serialize_to_json(const MarketData& data, char* buffer) noexcept;
bool deserialize_from_json(const char* json, MarketData& data) noexcept;

}  // namespace hft
