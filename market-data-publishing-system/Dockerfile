# =============================================================================
# HFT Market Data Publishing System - Docker Build
# =============================================================================
# 
# Multi-stage build for minimal final image size and maximum security.
# 
# Build: docker build -t hft-market-data .
# Run:   docker-compose up (see docker-compose.yml)
#
# =============================================================================

# -----------------------------------------------------------------------------
# Stage 1: Builder
# -----------------------------------------------------------------------------
FROM ubuntu:22.04 AS builder

# Avoid timezone prompts
ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    libboost-system-dev \
    libboost-thread-dev \
    libfmt-dev \
    git \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# Copy source files
COPY CMakeLists.txt ./
COPY include/ ./include/
COPY src/ ./src/

# Build with optimizations
RUN mkdir build && cd build && \
    cmake .. \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_FLAGS_RELEASE="-O3 -march=x86-64 -mtune=generic -DNDEBUG" && \
    ninja

# -----------------------------------------------------------------------------
# Stage 2: Runtime
# -----------------------------------------------------------------------------
FROM ubuntu:22.04 AS runtime

# Install runtime dependencies only
RUN apt-get update && apt-get install -y --no-install-recommends \
    libboost-system1.74.0 \
    libfmt8 \
    && rm -rf /var/lib/apt/lists/*

# Create non-root user for security
RUN useradd -m -s /bin/bash hft

# Copy binaries from builder
WORKDIR /app
COPY --from=builder /app/build/publisher /app/
COPY --from=builder /app/build/shm_consumer /app/
COPY --from=builder /app/build/tcp_consumer /app/
COPY --from=builder /app/build/benchmark /app/

# Change ownership
RUN chown -R hft:hft /app

# Switch to non-root user
USER hft

# Expose TCP port
EXPOSE 9876

# Default command (can be overridden)
CMD ["./publisher"]

# -----------------------------------------------------------------------------
# Labels
# -----------------------------------------------------------------------------
LABEL maintainer="HFT Student"
LABEL description="High-Frequency Trading Market Data Publishing System"
LABEL version="1.0.0"
