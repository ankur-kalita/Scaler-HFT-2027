#!/bin/bash

# =============================================================================
# HFT Market Data System - Launch Script
# =============================================================================
# 
# This script builds and runs all three processes in separate terminals.
#
# Usage:
#   ./scripts/run_all.sh         # Build and run
#   ./scripts/run_all.sh --skip-build  # Run without building
#
# =============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}╔═══════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║     HFT Market Data Publishing System - Launch Script         ║${NC}"
echo -e "${GREEN}╚═══════════════════════════════════════════════════════════════╝${NC}"
echo ""

# Build unless --skip-build is passed
if [[ "$1" != "--skip-build" ]]; then
    echo -e "${YELLOW}Building project...${NC}"
    
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    # Configure
    if command -v ninja &> /dev/null; then
        cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
        ninja
    else
        cmake .. -DCMAKE_BUILD_TYPE=Release
        make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)
    fi
    
    echo -e "${GREEN}Build complete!${NC}"
    echo ""
fi

# Check if executables exist
if [[ ! -f "$BUILD_DIR/publisher" ]]; then
    echo -e "${RED}Error: Executables not found. Please build first.${NC}"
    exit 1
fi

# Clean up old shared memory
echo -e "${YELLOW}Cleaning up old shared memory...${NC}"
rm -f /dev/shm/market_data_ring_buffer 2>/dev/null || true

# Function to open new terminal based on OS
open_terminal() {
    local title="$1"
    local command="$2"
    
    if [[ "$OSTYPE" == "darwin"* ]]; then
        # macOS
        osascript -e "tell application \"Terminal\" to do script \"cd '$BUILD_DIR' && echo '$title' && $command\""
    elif [[ -n "$DISPLAY" ]]; then
        # Linux with X11
        if command -v gnome-terminal &> /dev/null; then
            gnome-terminal --title="$title" -- bash -c "cd '$BUILD_DIR' && echo '$title' && $command; exec bash"
        elif command -v xterm &> /dev/null; then
            xterm -T "$title" -e "cd '$BUILD_DIR' && echo '$title' && $command; bash" &
        elif command -v konsole &> /dev/null; then
            konsole --title "$title" -e "cd '$BUILD_DIR' && echo '$title' && $command; bash" &
        else
            echo -e "${RED}No supported terminal emulator found.${NC}"
            echo "Please run manually:"
            echo "  cd $BUILD_DIR && $command"
        fi
    else
        echo -e "${YELLOW}No display detected. Run these commands in separate terminals:${NC}"
        echo "  cd $BUILD_DIR && ./publisher"
        echo "  cd $BUILD_DIR && ./shm_consumer"
        echo "  cd $BUILD_DIR && ./tcp_consumer"
        exit 0
    fi
}

echo -e "${GREEN}Starting processes...${NC}"
echo ""

# Start publisher first
echo -e "${YELLOW}Starting Publisher (Process A)...${NC}"
open_terminal "HFT Publisher" "./publisher"

# Wait for publisher to initialize
sleep 2

# Start consumers
echo -e "${YELLOW}Starting SHM Consumer (Process B)...${NC}"
open_terminal "HFT SHM Consumer" "./shm_consumer"

sleep 1

echo -e "${YELLOW}Starting TCP Consumer (Process C)...${NC}"
open_terminal "HFT TCP Consumer" "./tcp_consumer"

echo ""
echo -e "${GREEN}All processes started!${NC}"
echo ""
echo "Press Ctrl+C in each terminal to stop."
echo ""
echo -e "To run benchmark: ${YELLOW}$BUILD_DIR/benchmark${NC}"
echo -e "To clean shared memory: ${YELLOW}rm -f /dev/shm/market_data_ring_buffer${NC}"
