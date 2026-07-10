#!/bin/bash
# Package CameraWebApp for macOS Homebrew distribution

set -e

echo "Packaging CameraWebApp for macOS..."

# Configuration
VERSION="2.1.0"
BUILD_DIR="../server/build"
PROJECT_ROOT="$(cd ../.. && pwd)"
PACKAGE_DIR="package"
PACKAGE_NAME="CameraWebApp-macos"

# Clean previous package
rm -rf "$PACKAGE_DIR"
mkdir -p "$PACKAGE_DIR/$PACKAGE_NAME"

# Copy the full self-contained build output.
#
# The CMake build places CameraWebApp alongside its runtime libraries — the Sony
# SDK core dylibs and OpenCV next to the binary, and the SDK adapters under
# Contents/Frameworks/CrAdapter — all referenced via @rpath / @executable_path.
# JSON is compiled in from third_party/jsoncpp and the WebSocket handshake is
# self-contained, so there are no Homebrew/absolute-path dependencies and no
# relocation step is required.

if [ ! -f "$BUILD_DIR/CameraWebApp" ]; then
    echo "❌ $BUILD_DIR/CameraWebApp not found. Build first: api/scripts/build.sh"
    exit 1
fi

echo "Copying binary and bundled libraries..."
cp "$BUILD_DIR/CameraWebApp" "$PACKAGE_DIR/$PACKAGE_NAME/"
[ -d "$BUILD_DIR/Contents" ] && cp -R "$BUILD_DIR/Contents" "$PACKAGE_DIR/$PACKAGE_NAME/Contents"
# SDK core + OpenCV dylibs the build placed next to the binary.
find "$BUILD_DIR" -maxdepth 1 -name "*.dylib" -exec cp {} "$PACKAGE_DIR/$PACKAGE_NAME/" \;

# Create README
cat > "$PACKAGE_DIR/$PACKAGE_NAME/README.txt" << 'EOF'
Sony Camera WebServer v2.1.0
=============================

Installation:
  Copy this whole folder to a location of your choice. The CameraWebApp binary
  loads its bundled libraries from alongside itself, so keep them together.

Usage:
  ./CameraWebApp

The server will run on http://localhost:8080

Client Libraries:
  See https://crsdk.app/sdk/overview

Documentation:
  https://crsdk.app/
EOF

# Create tarball
echo "Creating tarball..."
cd "$PACKAGE_DIR"
tar -czf "$PACKAGE_NAME.tar.gz" "$PACKAGE_NAME"
cd ..

# Calculate SHA256
echo "Calculating SHA256..."
SHA256=$(shasum -a 256 "$PACKAGE_DIR/$PACKAGE_NAME.tar.gz" | awk '{print $1}')

echo ""
echo "✅ Package created: $PACKAGE_DIR/$PACKAGE_NAME.tar.gz"
echo "📦 SHA256: $SHA256"
echo ""
echo "Update homebrew/sony-camera-webserver.rb with this SHA256"
echo ""
