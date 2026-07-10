#!/bin/bash

# API Server Build Script

set -e  # Exit on any error

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
API_DIR="$( cd "$SCRIPT_DIR/.." &> /dev/null && pwd )"
PROJECT_ROOT="$( cd "$API_DIR/.." &> /dev/null && pwd )"
BUILD_DIR="$API_DIR/build"
SERVER_DIR="$API_DIR/server"

echo "===================="
echo "Building API Server"
echo "===================="
echo "Project Root: $PROJECT_ROOT"
echo "API Directory: $API_DIR"
echo "Build Directory: $BUILD_DIR"
echo ""

# Clean and create build directory
if [ -d "$BUILD_DIR" ]; then
    echo "Cleaning existing build directory..."
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Run CMake configuration
echo "Configuring API server build with CMake..."
cmake "$SERVER_DIR" -DCMAKE_BUILD_TYPE=Release

# Build the project
echo "Building API server..."
cmake --build . --target CameraWebApp --config Release

# Check if build was successful
if [ -f "CameraWebApp" ] || [ -f "CameraWebApp.exe" ]; then
    echo ""
    echo "✅ API server build completed successfully!"
    echo "Executable location: $BUILD_DIR/"
else
    echo ""
    echo "❌ API server build failed!"
    exit 1
fi

echo ""
echo "To run the API server:"
if [[ "$OSTYPE" == "msys" ]] || [[ "$OSTYPE" == "win32" ]]; then
    echo "  cd $BUILD_DIR && ./CameraWebApp.exe"
else
    echo "  cd $BUILD_DIR && ./CameraWebApp"
fi
echo ""
echo "Then open your browser to: http://localhost:8080"
