#!/bin/bash

# =============================================================================
# HFT Market Data System - Dependency Installation Script
# =============================================================================
# 
# This script installs all required dependencies for building the project.
#
# Supported platforms:
#   - Ubuntu/Debian
#   - macOS (Homebrew)
#   - Arch Linux
#   - Fedora/RHEL
#
# Usage:
#   ./scripts/install_deps.sh
#
# =============================================================================

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}╔═══════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║     HFT Market Data System - Dependency Installer             ║${NC}"
echo -e "${GREEN}╚═══════════════════════════════════════════════════════════════╝${NC}"
echo ""

# Detect OS
detect_os() {
    if [[ "$OSTYPE" == "darwin"* ]]; then
        echo "macos"
    elif [[ -f /etc/debian_version ]]; then
        echo "debian"
    elif [[ -f /etc/arch-release ]]; then
        echo "arch"
    elif [[ -f /etc/fedora-release ]]; then
        echo "fedora"
    elif [[ -f /etc/redhat-release ]]; then
        echo "rhel"
    else
        echo "unknown"
    fi
}

OS=$(detect_os)
echo -e "${YELLOW}Detected OS: $OS${NC}"
echo ""

case $OS in
    macos)
        echo -e "${YELLOW}Installing dependencies via Homebrew...${NC}"
        
        # Check if Homebrew is installed
        if ! command -v brew &> /dev/null; then
            echo -e "${RED}Homebrew not found. Installing...${NC}"
            /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
        fi
        
        brew update
        brew install cmake ninja boost fmt
        
        echo -e "${GREEN}Dependencies installed successfully!${NC}"
        ;;
        
    debian)
        echo -e "${YELLOW}Installing dependencies via apt...${NC}"
        
        sudo apt-get update
        sudo apt-get install -y \
            build-essential \
            cmake \
            ninja-build \
            libboost-system-dev \
            libboost-thread-dev \
            libfmt-dev \
            git
        
        echo -e "${GREEN}Dependencies installed successfully!${NC}"
        ;;
        
    arch)
        echo -e "${YELLOW}Installing dependencies via pacman...${NC}"
        
        sudo pacman -Syu --noconfirm
        sudo pacman -S --noconfirm \
            base-devel \
            cmake \
            ninja \
            boost \
            fmt \
            git
        
        echo -e "${GREEN}Dependencies installed successfully!${NC}"
        ;;
        
    fedora)
        echo -e "${YELLOW}Installing dependencies via dnf...${NC}"
        
        sudo dnf update -y
        sudo dnf install -y \
            gcc-c++ \
            cmake \
            ninja-build \
            boost-devel \
            fmt-devel \
            git
        
        echo -e "${GREEN}Dependencies installed successfully!${NC}"
        ;;
        
    rhel)
        echo -e "${YELLOW}Installing dependencies via yum...${NC}"
        
        sudo yum update -y
        sudo yum install -y epel-release
        sudo yum install -y \
            gcc-c++ \
            cmake \
            ninja-build \
            boost-devel \
            fmt-devel \
            git
        
        echo -e "${GREEN}Dependencies installed successfully!${NC}"
        ;;
        
    *)
        echo -e "${RED}Unsupported OS. Please install manually:${NC}"
        echo ""
        echo "Required packages:"
        echo "  - C++17 compiler (g++ >= 7 or clang++ >= 5)"
        echo "  - CMake >= 3.16"
        echo "  - Ninja (optional, but faster)"
        echo "  - Boost >= 1.70 (system, thread components)"
        echo "  - fmt library"
        echo ""
        exit 1
        ;;
esac

echo ""
echo -e "${GREEN}All dependencies installed!${NC}"
echo ""
echo "Next steps:"
echo "  1. Build the project:"
echo "     mkdir build && cd build"
echo "     cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release"
echo "     ninja"
echo ""
echo "  2. Run the system:"
echo "     ./publisher      # Terminal 1"
echo "     ./shm_consumer   # Terminal 2"
echo "     ./tcp_consumer   # Terminal 3"
echo ""
