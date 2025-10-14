#!/bin/bash
#
# build.sh - Build LWIR compression tool
#
# Usage:
#   ./build.sh              # Release build
#   ./build.sh debug        # Debug build
#   ./build.sh clean        # Clean build directory
#

set -e  # Exit on error

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BUILD_TYPE="${1:-Release}"

if [ "$BUILD_TYPE" = "clean" ]; then
    echo "🧹 Cleaning build directory..."
    rm -rf build
    echo "✅ Clean complete"
    exit 0
fi

# Normalize build type
if [ "$BUILD_TYPE" = "debug" ] || [ "$BUILD_TYPE" = "Debug" ]; then
    BUILD_TYPE="Debug"
else
    BUILD_TYPE="Release"
fi

echo "🔨 Building LWIR compression tool ($BUILD_TYPE)..."
echo ""

# Create build directory
mkdir -p build
cd build

# Configure with CMake
echo "📝 Configuring CMake..."
cmake .. -DCMAKE_BUILD_TYPE=$BUILD_TYPE

echo ""
echo "⚙️  Compiling..."
make -j$(nproc)

echo ""
echo "✅ Build complete!"
echo ""
echo "Binary: $SCRIPT_DIR/build/lwir_compress"
echo ""
echo "Run with:"
echo "  ./build/lwir_compress --help"
