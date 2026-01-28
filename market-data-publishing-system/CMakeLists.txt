cmake_minimum_required(VERSION 3.16)
project(MarketDataPublisher VERSION 1.0.0 LANGUAGES CXX)

# =============================================================================
# C++ STANDARD AND COMPILER FLAGS
# =============================================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# =============================================================================
# HFT-OPTIMIZED COMPILER FLAGS
# =============================================================================
# Release build with aggressive optimizations for HFT
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -mtune=native -funroll-loops -ffast-math -flto")

# Debug build with sanitizers for development
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer")

# Additional HFT-specific flags
add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Wno-unused-parameter
    # Note: We keep exceptions enabled because Boost.Asio requires them
    # In a real HFT system, you'd use ASIO_NO_EXCEPTIONS with custom handlers
)

# Default to Release build for maximum performance
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "CXX Flags: ${CMAKE_CXX_FLAGS}")
message(STATUS "CXX Flags Release: ${CMAKE_CXX_FLAGS_RELEASE}")

# =============================================================================
# DEPENDENCIES
# =============================================================================

# Find Boost (for Asio)
# Note: Since Boost 1.69, boost::system is header-only, so we only need headers
cmake_policy(SET CMP0167 NEW)  # Use Config mode for FindBoost
find_package(Boost 1.70 REQUIRED)

# Find fmt library
find_package(fmt REQUIRED)

# Find threads
find_package(Threads REQUIRED)

# =============================================================================
# INCLUDE DIRECTORIES
# =============================================================================
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${Boost_INCLUDE_DIRS}
)

# =============================================================================
# LIBRARY: Core HFT Components
# =============================================================================
add_library(hft_core STATIC
    src/core/timestamp.cpp
    src/core/cpu_affinity.cpp
    src/core/shared_memory.cpp
    src/core/market_data.cpp
)

target_link_libraries(hft_core
    PUBLIC
        fmt::fmt
        Threads::Threads
        ${CMAKE_DL_LIBS}
)

# Platform-specific libraries
if(UNIX AND NOT APPLE)
    target_link_libraries(hft_core PUBLIC rt)  # For shm_open on Linux
endif()

# =============================================================================
# EXECUTABLE: Process A - Market Data Publisher
# =============================================================================
add_executable(publisher
    src/publisher/main.cpp
    src/publisher/tcp_server.cpp
    src/publisher/market_data_generator.cpp
)

target_link_libraries(publisher
    PRIVATE
        hft_core
        Boost::headers
        fmt::fmt
        Threads::Threads
)

# =============================================================================
# EXECUTABLE: Process B - SHM Consumer
# =============================================================================
add_executable(shm_consumer
    src/consumer_shm/main.cpp
)

target_link_libraries(shm_consumer
    PRIVATE
        hft_core
        fmt::fmt
        Threads::Threads
)

# =============================================================================
# EXECUTABLE: Process C - TCP Consumer
# =============================================================================
add_executable(tcp_consumer
    src/consumer_tcp/main.cpp
)

target_link_libraries(tcp_consumer
    PRIVATE
        hft_core
        Boost::headers
        fmt::fmt
        Threads::Threads
)

# =============================================================================
# EXECUTABLE: Benchmark Tool
# =============================================================================
add_executable(benchmark
    src/benchmark/main.cpp
)

target_link_libraries(benchmark
    PRIVATE
        hft_core
        fmt::fmt
        Threads::Threads
)

# =============================================================================
# INSTALLATION
# =============================================================================
install(TARGETS publisher shm_consumer tcp_consumer benchmark
    RUNTIME DESTINATION bin
)

# =============================================================================
# CUSTOM TARGETS
# =============================================================================
# Clean shared memory (useful for development)
add_custom_target(clean_shm
    COMMAND rm -f /dev/shm/market_data_*
    COMMENT "Cleaning shared memory segments"
)

# Run all components
add_custom_target(run_all
    COMMAND ${CMAKE_COMMAND} -E echo "Starting Market Data System..."
    DEPENDS publisher shm_consumer tcp_consumer
)

message(STATUS "===========================================")
message(STATUS "HFT Market Data Publishing System")
message(STATUS "===========================================")
message(STATUS "Executables:")
message(STATUS "  - publisher     : Market data generator (TCP + SHM)")
message(STATUS "  - shm_consumer  : Shared memory consumer")
message(STATUS "  - tcp_consumer  : TCP socket consumer")
message(STATUS "  - benchmark     : Latency benchmark tool")
message(STATUS "===========================================")
