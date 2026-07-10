#!/bin/bash

# API Server Clean Script

set -e  # Exit on any error

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
API_DIR="$( cd "$SCRIPT_DIR/.." &> /dev/null && pwd )"
BUILD_DIR="$API_DIR/build"

echo "===================="
echo "Cleaning API Server"
echo "===================="
echo "API Directory: $API_DIR"
echo "Build Directory: $BUILD_DIR"
echo ""

# Remove build directory
if [ -d "$BUILD_DIR" ]; then
    echo "Removing build directory..."
    rm -rf "$BUILD_DIR"
    echo "✅ API server build directory cleaned!"
else
    echo "📁 Build directory doesn't exist - nothing to clean."
fi

echo ""
echo "API server clean completed."
