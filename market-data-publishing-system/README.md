# HFT Market Data Publishing System in C++

A high-performance, low-latency market data publishing system implemented in modern C++17. This system simulates how an exchange publishes real-time bid/ask prices and distributes them using both TCP loopback networking and shared memory with a lock-free ring buffer.

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Design Decisions](#design-decisions)
3. [Performance Characteristics](#performance-characteristics)
4. [Building the Project](#building-the-project)
5. [Running the System](#running-the-system)
6. [Docker Deployment](#docker-deployment)
7. [Configuration](#configuration)
8. [Benchmarks](#benchmarks)
9. [Code Structure](#code-structure)

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                        HFT Market Data System                               │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ┌─────────────────────┐                                                    │
│  │   Process A         │                                                    │
│  │   (Publisher)       │                                                    │
│  │                     │                                                    │
│  │  ┌───────────────┐  │    Shared Memory (mmap)     ┌─────────────────┐   │
│  │  │ Market Data   │──┼────────────────────────────▶│   Process B     │   │
│  │  │ Generator     │  │    Lock-Free Ring Buffer    │ (SHM Consumer)  │   │
│  │  └───────┬───────┘  │                             └─────────────────┘   │
│  │          │          │                                                    │
│  │          ▼          │                                                    │
│  │  ┌───────────────┐  │    TCP Loopback (JSON)      ┌─────────────────┐   │
│  │  │ TCP Server    │──┼────────────────────────────▶│   Process C     │   │
│  │  │ (Boost.Asio)  │  │    127.0.0.1:9876           │ (TCP Consumer)  │   │
│  │  └───────────────┘  │                             └─────────────────┘   │
│  └─────────────────────┘                                                    │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Components

| Process | Description | Transport |
|---------|-------------|-----------|
| **Process A** | Market Data Publisher - generates dummy market data and distributes via both channels | TCP + SHM |
| **Process B** | SHM Consumer - reads from shared memory ring buffer | Shared Memory |
| **Process C** | TCP Consumer - reads from TCP loopback socket | TCP/IP |

---

## Design Decisions

### 1. Lock-Free SPSC Ring Buffer

**Why Lock-Free?**
- Mutexes involve syscalls (futex) which add ~1000ns latency
- Lock-free allows sub-100ns push/pop operations
- No context switches or kernel involvement in hot path

**Implementation Details:**
```cpp
// Memory ordering for correctness AND performance
// Producer: relaxed load (own), acquire load (other), release store
// Consumer: relaxed load (own), acquire load (other), release store

auto push(const T& value) {
    const index_type write = write_idx_.load(std::memory_order_relaxed);
    const index_type read = read_idx_.load(std::memory_order_acquire);
    // ... write data ...
    write_idx_.store(write + 1, std::memory_order_release);
}
```

**Time Complexity:**
- Push: O(1) amortized
- Pop: O(1) amortized

### 2. Power-of-2 Buffer Capacity

**Why Power of 2?**
```cpp
// Standard modulo (slow - involves division):
index = counter % capacity;  // ~20-30 CPU cycles

// Power-of-2 optimization (fast - single AND instruction):
index = counter & (capacity - 1);  // ~1 CPU cycle
```

**Our choice:** 65536 (2^16) entries = ~4MB buffer for 64-byte messages

### 3. Cache-Line Aligned Data Structures

**Why 64-byte alignment?**
- x86-64 cache line is 64 bytes
- Prevents **false sharing** between CPU cores
- Single cache line load per message

```cpp
// MarketData is exactly 64 bytes (1 cache line)
struct alignas(64) MarketData {
    char instrument[16];  // 16 bytes
    double bid;           //  8 bytes
    double ask;           //  8 bytes
    uint64_t timestamp;   //  8 bytes
    uint64_t sequence;    //  8 bytes
    char _padding[16];    // 16 bytes (explicit padding)
};  // Total: 64 bytes
```

**Ring buffer index alignment:**
```cpp
// Each index on separate cache line prevents false sharing
alignas(64) std::atomic<index_type> write_idx_;  // Cache line 0
alignas(64) std::atomic<index_type> read_idx_;   // Cache line 1
```

### 4. Fixed-Size Message Structure

**Why not std::string or dynamic allocation?**
- `malloc()`/`new` involve syscalls and global locks
- Variable-size messages require length prefixing
- Unpredictable memory layout hurts cache performance

**Our approach:**
- Fixed 16-char instrument symbol
- No heap allocation in hot path
- Predictable memory access patterns

### 5. TCP Optimizations

**Socket Options Applied:**

| Option | Purpose | Impact |
|--------|---------|--------|
| `TCP_NODELAY` | Disable Nagle's algorithm | Immediate transmission (critical for latency) |
| `TCP_QUICKACK` | Disable delayed ACK | Faster acknowledgments |
| `SO_SNDBUF` | 1MB send buffer | Fewer syscalls |
| `SO_RCVBUF` | 1MB receive buffer | Fewer syscalls |
| `SO_REUSEADDR` | Quick restart | Development convenience |

**Why Boost.Asio?**
- Mature async I/O framework
- Uses OS-native APIs (epoll on Linux, kqueue on macOS)
- Better than raw sockets for maintainability

### 6. Nanosecond Timestamps

**Implementation:**
```cpp
inline uint64_t get_timestamp_ns() noexcept {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}
```

**Why `clock_gettime()`?**
- VDSO-accelerated on Linux (no syscall!)
- Kernel maps time directly into user space
- ~20-30ns overhead on modern systems

**Alternative (x86 only):**
```cpp
// RDTSCP for ultra-low latency (~10 cycles)
inline uint64_t rdtscp() noexcept {
    uint32_t lo, hi, aux;
    __asm__ __volatile__ ("rdtscp" : "=a"(lo), "=d"(hi), "=c"(aux));
    return (uint64_t(hi) << 32) | lo;
}
```

### 7. CPU Affinity (NUMA Awareness)

**Why pin threads to cores?**
- L1/L2 cache stays warm
- No scheduler migration overhead
- Predictable latency

```cpp
// Pin publisher to core 0
set_thread_affinity(0);

// Pin SHM consumer to core 1 (same NUMA node)
set_thread_affinity(1);

// Pin TCP consumer to core 2
set_thread_affinity(2);
```

### 8. Memory Locking

**Why `mlock()`?**
- Prevents pages from being swapped to disk
- Eliminates swap-related latency spikes
- Critical for consistent latency

```cpp
mlock(mapped_addr_, shm_size_);  // Lock pages in RAM
```

### 9. Shared Memory Implementation

**Why `shm_open()` + `mmap()` (POSIX)?**
- More portable than System V `shmget()`
- Filesystem-based naming (easy management)
- Integrates with standard permissions

**Flags Used:**
- `MAP_SHARED`: Changes visible across processes
- `MAP_POPULATE` (Linux): Pre-fault pages to avoid page faults later

---

## Performance Characteristics

### Expected Latencies

| Operation | Expected Latency | Notes |
|-----------|-----------------|-------|
| Ring buffer push | 20-50 ns | Lock-free, cache-aligned |
| Ring buffer pop | 20-50 ns | Lock-free, cache-aligned |
| Timestamp read | 20-30 ns | VDSO-accelerated |
| JSON serialization | 100-200 ns | Hand-rolled, no allocation |
| SHM end-to-end | 100-500 ns | Sub-microsecond IPC |
| TCP end-to-end | 10-100 μs | Loopback, kernel overhead |

### Throughput

- Target: 100,000+ messages/second
- Shared memory: ~1M+ messages/second possible
- TCP: Limited by kernel network stack

---

## Building the Project

### Prerequisites

**Ubuntu/Debian:**
```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    libboost-system-dev \
    libboost-thread-dev \
    libfmt-dev
```

**macOS:**
```bash
brew install cmake ninja boost fmt
```

**Arch Linux:**
```bash
sudo pacman -S cmake ninja boost fmt
```

### Build Commands

```bash
# Clone/navigate to project directory
cd market-data-publishing-system

# Create build directory
mkdir build && cd build

# Configure with CMake (Release for performance)
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release

# Build
ninja

# Or with standard make:
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Build Outputs

After successful build:
```
build/
├── publisher      # Process A - Market Data Publisher
├── shm_consumer   # Process B - Shared Memory Consumer
├── tcp_consumer   # Process C - TCP Consumer
└── benchmark      # Latency benchmark tool
```

---

## Running the System

### Quick Start

```bash
# Terminal 1: Start Publisher (Process A)
./build/publisher

# Terminal 2: Start SHM Consumer (Process B)
./build/shm_consumer

# Terminal 3: Start TCP Consumer (Process C)
./build/tcp_consumer
```

### Expected Output

**Publisher (Process A):**
```
╔═══════════════════════════════════════════════════════════════╗
║     HFT Market Data Publisher - Process A                     ║
╚═══════════════════════════════════════════════════════════════╝

=== CPU Configuration ===
CPU cores: 8
Thread pinned to CPU core 0

=== Shared Memory Setup ===
Created shared memory '/market_data_ring_buffer' at address 0x7f...
Ring buffer capacity: 65536 messages

=== TCP Server Setup ===
TCP server listening on 127.0.0.1:9876

[Stats] Published:     100000 | SHM:     100000 | TCP:     100000 | Rate:   99823 msg/s
```

**SHM Consumer (Process B):**
```
╔═══════════════════════════════════════════════════════════════╗
║     HFT Market Data Consumer - Process B (Shared Memory)      ║
╚═══════════════════════════════════════════════════════════════╝

[12:34:56.123456789] RELIANCE BID=2850.25 ASK=2850.75 SEQ=1 LAT=450ns
[Stats] Msgs:     100000 | Rate:   99500/s | Lat(avg/min/max): 380/120/2500 ns
```

**TCP Consumer (Process C):**
```
╔═══════════════════════════════════════════════════════════════╗
║     HFT Market Data Consumer - Process C (TCP)                ║
╚═══════════════════════════════════════════════════════════════╝

[12:34:56.123456789] RELIANCE BID=2850.25 ASK=2850.75 LAT=45000ns
[Stats] Msgs:     100000 | Rate:   98000/s | Lat(avg/min/max): 45000/12000/120000 ns
```

### Cleanup

```bash
# Remove shared memory segment
rm -f /dev/shm/market_data_ring_buffer

# Or on macOS:
# Shared memory is automatically cleaned on reboot
```

---

## Docker Deployment

### Using Docker

```bash
# Build image
docker build -t hft-market-data .

# Run publisher
docker run --rm -it \
    --cap-add IPC_LOCK \
    --cap-add SYS_NICE \
    -p 9876:9876 \
    hft-market-data ./publisher
```

### Using Docker Compose

```bash
# Build and start all services
docker-compose up --build

# Start only specific services
docker-compose up publisher shm_consumer

# View logs
docker-compose logs -f

# Stop all services
docker-compose down

# Run benchmark
docker-compose --profile benchmark up benchmark
```

### Docker Compose Services

| Service | Container Name | Description |
|---------|---------------|-------------|
| `publisher` | hft-publisher | Market Data Publisher |
| `shm_consumer` | hft-shm-consumer | Shared Memory Consumer |
| `tcp_consumer` | hft-tcp-consumer | TCP Consumer |
| `benchmark` | hft-benchmark | Benchmark Tool (manual) |

---

## Configuration

Configuration is centralized in `include/core/config.hpp`:

```cpp
namespace hft::config {
    // Cache
    constexpr size_t CACHE_LINE_SIZE = 64;
    
    // Ring Buffer
    constexpr size_t RING_BUFFER_CAPACITY = 65536;  // 2^16
    
    // Network
    constexpr const char* TCP_HOST = "127.0.0.1";
    constexpr uint16_t TCP_PORT = 9876;
    constexpr size_t TCP_SEND_BUFFER_SIZE = 1024 * 1024;  // 1MB
    
    // Publishing
    constexpr size_t PUBLISH_RATE_HZ = 100000;  // 100K msg/sec
    
    // CPU Affinity
    constexpr int PUBLISHER_CPU_CORE = 0;
    constexpr int SHM_CONSUMER_CPU_CORE = 1;
    constexpr int TCP_CONSUMER_CPU_CORE = 2;
}
```

### Kernel Tuning (Linux)

For optimal TCP performance, apply these sysctl settings:

```bash
# Add to /etc/sysctl.conf or run with sudo:

# Increase socket buffer sizes
sysctl -w net.core.rmem_max=16777216
sysctl -w net.core.wmem_max=16777216
sysctl -w net.core.rmem_default=1048576
sysctl -w net.core.wmem_default=1048576

# Increase TCP buffer sizes
sysctl -w net.ipv4.tcp_rmem='4096 87380 16777216'
sysctl -w net.ipv4.tcp_wmem='4096 65536 16777216'

# Enable low latency mode
sysctl -w net.ipv4.tcp_low_latency=1

# Increase connection backlog
sysctl -w net.core.somaxconn=65535
sysctl -w net.ipv4.tcp_max_syn_backlog=65535
```

---

## Benchmarks

Run the benchmark tool:

```bash
./build/benchmark
```

### Sample Output

```
=== Timestamp Benchmark ===
clock_gettime(CLOCK_REALTIME)
  Min:          18 ns (0.02 us)
  Max:        1250 ns (1.25 us)
  Avg:          25 ns (0.03 us)
  P99:          45 ns (0.05 us)

=== Ring Buffer Benchmark ===
Ring Buffer Push
  Min:          22 ns (0.02 us)
  Max:         890 ns (0.89 us)
  Avg:          35 ns (0.04 us)
  P99:          68 ns (0.07 us)

Ring Buffer Pop
  Min:          20 ns (0.02 us)
  Max:         750 ns (0.75 us)
  Avg:          32 ns (0.03 us)
  P99:          55 ns (0.06 us)

=== JSON Serialization Benchmark ===
JSON Serialization
  Min:          85 ns (0.09 us)
  Max:        2100 ns (2.10 us)
  Avg:         125 ns (0.13 us)
  P99:         210 ns (0.21 us)
```

---

## Code Structure

```
market-data-publishing-system/
├── CMakeLists.txt              # Build configuration
├── Dockerfile                  # Docker build file
├── docker-compose.yml          # Multi-container orchestration
├── README.md                   # This file
│
├── include/
│   ├── core/
│   │   ├── config.hpp          # Global configuration constants
│   │   ├── timestamp.hpp       # Nanosecond timestamp utilities
│   │   ├── cpu_affinity.hpp    # CPU pinning and NUMA
│   │   ├── market_data.hpp     # Market data structure (64 bytes)
│   │   ├── spsc_ring_buffer.hpp # Lock-free ring buffer
│   │   └── shared_memory.hpp   # POSIX shared memory wrapper
│   │
│   └── publisher/
│       ├── tcp_server.hpp      # Boost.Asio TCP server
│       └── market_data_generator.hpp  # Price simulation
│
└── src/
    ├── core/
    │   ├── timestamp.cpp
    │   ├── cpu_affinity.cpp
    │   └── shared_memory.cpp
    │
    ├── publisher/
    │   ├── main.cpp            # Process A entry point
    │   ├── tcp_server.cpp
    │   └── market_data_generator.cpp
    │
    ├── consumer_shm/
    │   └── main.cpp            # Process B entry point
    │
    ├── consumer_tcp/
    │   └── main.cpp            # Process C entry point
    │
    └── benchmark/
        └── main.cpp            # Latency benchmark tool
```

---

## Technologies Used

| Component | Technology | Rationale |
|-----------|------------|-----------|
| Language | C++17 | Modern features, zero-cost abstractions |
| Build System | CMake + Ninja | Fast, cross-platform builds |
| Networking | Boost.Asio | Mature async I/O, uses OS-native APIs |
| Logging | fmt library | Fast, type-safe formatting |
| IPC | POSIX shm_open + mmap | Portable, efficient shared memory |
| Containers | Docker | Cross-platform deployment |

---

## Author

Built for the Scaler HFT Systems Programming course.

## License

MIT License - See LICENSE file for details.
