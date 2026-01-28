#pragma once

#include <cstdint>
#include <cstddef>

namespace hft {
namespace config {

constexpr std::size_t CACHE_LINE_SIZE = 64;
constexpr std::size_t RING_BUFFER_CAPACITY = 65536;
constexpr std::size_t RING_BUFFER_MASK = RING_BUFFER_CAPACITY - 1;

static_assert((RING_BUFFER_CAPACITY & (RING_BUFFER_CAPACITY - 1)) == 0,
              "RING_BUFFER_CAPACITY must be power of 2");

constexpr const char* TCP_HOST = "127.0.0.1";
constexpr uint16_t TCP_PORT = 9876;
constexpr std::size_t TCP_SEND_BUFFER_SIZE = 1024 * 1024;
constexpr std::size_t TCP_RECV_BUFFER_SIZE = 1024 * 1024;
constexpr std::size_t TCP_READ_BUFFER_SIZE = 4096;

constexpr const char* SHM_NAME = "/market_data_ring_buffer";
constexpr int SHM_MODE = 0666;

constexpr std::size_t MAX_SYMBOL_LENGTH = 16;
constexpr std::size_t NUM_INSTRUMENTS = 10;
constexpr std::size_t PUBLISH_RATE_HZ = 100000;

constexpr std::size_t SPIN_COUNT = 10000;
constexpr bool USE_BUSY_WAIT = true;

constexpr int PUBLISHER_CPU_CORE = 0;
constexpr int SHM_CONSUMER_CPU_CORE = 1;
constexpr int TCP_CONSUMER_CPU_CORE = 2;

}  // namespace config
}  // namespace hft
