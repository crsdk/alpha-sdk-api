#!/bin/bash
# ==============================================================================
# Extract Sony Camera Remote SDK zip into project directory structure
# Usage: ./scripts/extract-sdk.sh <zip-path> <platform>
# Platform: macos, linux-x64, linux-arm64, linux-arm, win32-x64
# ==============================================================================

set -e

if [ $# -lt 2 ]; then
    echo "Usage: $0 <zip-path> <platform>"
    echo "Platforms: macos, linux-x64, linux-arm64, linux-arm, win32-x64"
    exit 1
fi

ZIP_PATH="$1"
PLATFORM="$2"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
TEMP_DIR=$(mktemp -d)

echo "=== Extracting SDK for $PLATFORM ==="
echo "Zip: $ZIP_PATH"
echo "Project: $PROJECT_ROOT"

# Extract to temp directory
echo "Extracting..."
unzip -q "$ZIP_PATH" -d "$TEMP_DIR"

# V2.01+ SDK zips contain RemoteCli.zip (nested) with external/crsdk inside.
# V1.14 had external/crsdk at the top level. Handle both layouts.
EXTRACT_ROOT="$TEMP_DIR"
if [ -d "$TEMP_DIR/external" ]; then
    EXTRACT_ROOT="$TEMP_DIR"
elif [ "$(ls -d "$TEMP_DIR"/*/external 2>/dev/null | head -1)" ]; then
    EXTRACT_ROOT="$(ls -d "$TEMP_DIR"/*/ | head -1)"
elif [ -f "$TEMP_DIR/RemoteCli.zip" ]; then
    echo "V2.01+ layout detected — extracting inner RemoteCli.zip..."
    unzip -q "$TEMP_DIR/RemoteCli.zip" -d "$TEMP_DIR/RemoteCli"
    EXTRACT_ROOT="$TEMP_DIR/RemoteCli"
fi

SDK_SRC="$EXTRACT_ROOT/external/crsdk"
OPENCV_SRC="$EXTRACT_ROOT/external/opencv"
SDK_HEADER_SRC="$EXTRACT_ROOT/app/CrSDK"
if [ ! -d "$SDK_HEADER_SRC" ]; then
    SDK_HEADER_SRC="$EXTRACT_ROOT/app/CRSDK"
fi

if [ ! -d "$SDK_SRC" ]; then
    echo "ERROR: Could not find external/crsdk in zip"
    echo "See docs/SDK_SETUP.md for the expected SDK archive layout and local placement."
    echo "Contents of extract dir:"
    ls -la "$EXTRACT_ROOT"
    rm -rf "$TEMP_DIR"
    exit 1
fi

if [ ! -d "$SDK_HEADER_SRC" ]; then
    echo "ERROR: Could not find SDK headers in app/CrSDK or app/CRSDK"
    echo "See docs/SDK_SETUP.md for the expected SDK archive layout and local placement."
    echo "Contents of extract dir:"
    ls -la "$EXTRACT_ROOT"
    rm -rf "$TEMP_DIR"
    exit 1
fi

# Determine target directories based on platform
case "$PLATFORM" in
    macos)
        # Put libs directly in shared/sdk/lib/ (where CMake find_library expects them).
        # Adapter dylibs stay in shared/sdk/lib/CrAdapter/ for runtime copying.
        SDK_LIB_DIR="$PROJECT_ROOT/shared/sdk/lib"
        OPENCV_LIB_DIR="$PROJECT_ROOT/shared/opencv/Darwin/Release/macos/bin"

        mkdir -p "$SDK_LIB_DIR" "$SDK_LIB_DIR/CrAdapter"
        mkdir -p "$OPENCV_LIB_DIR"

        echo "Copying SDK libraries..."
        cp "$SDK_SRC/libCr_Core.dylib" "$SDK_LIB_DIR/"
        cp "$SDK_SRC/libmonitor_protocol.dylib" "$SDK_LIB_DIR/"
        cp "$SDK_SRC/libmonitor_protocol_pf.dylib" "$SDK_LIB_DIR/"
        cp "$SDK_SRC/CrAdapter/libCr_PTP_IP.dylib" "$SDK_LIB_DIR/CrAdapter/"
        cp "$SDK_SRC/CrAdapter/libCr_PTP_USB.dylib" "$SDK_LIB_DIR/CrAdapter/"
        cp "$SDK_SRC/CrAdapter/libssh2.dylib" "$SDK_LIB_DIR/CrAdapter/"
        cp "$SDK_SRC/CrAdapter/libusb-1.0.0.dylib" "$SDK_LIB_DIR/CrAdapter/"

        echo "Copying OpenCV libraries..."
        cp "$OPENCV_SRC/Darwin/Release/macos/bin/"libopencv_*.dylib "$OPENCV_LIB_DIR/"
        ;;

    linux-x64|linux-arm64|linux-arm)
        # Put libs directly in shared/sdk/lib/ (where CMake find_library expects them)
        SDK_LIB_DIR="$PROJECT_ROOT/shared/sdk/lib"
        OPENCV_LIB_DIR="$PROJECT_ROOT/shared/opencv/Linux"

        mkdir -p "$SDK_LIB_DIR" "$SDK_LIB_DIR/CrAdapter"
        mkdir -p "$OPENCV_LIB_DIR"

        echo "Copying SDK libraries..."
        cp "$SDK_SRC/libCr_Core.so" "$SDK_LIB_DIR/"
        cp "$SDK_SRC/libmonitor_protocol.so" "$SDK_LIB_DIR/"
        cp "$SDK_SRC/libmonitor_protocol_pf.so" "$SDK_LIB_DIR/"
        cp "$SDK_SRC/CrAdapter/libCr_PTP_IP.so" "$SDK_LIB_DIR/CrAdapter/"
        cp "$SDK_SRC/CrAdapter/libCr_PTP_USB.so" "$SDK_LIB_DIR/CrAdapter/"
        cp "$SDK_SRC/CrAdapter/libssh2.so" "$SDK_LIB_DIR/CrAdapter/"
        cp "$SDK_SRC/CrAdapter/libusb-1.0.so" "$SDK_LIB_DIR/CrAdapter/"

        echo "Copying OpenCV libraries..."
        cp "$OPENCV_SRC/Linux/"libopencv_*.so.408 "$OPENCV_LIB_DIR/"
        ;;

    win32-x64)
        # Put libs directly in shared/sdk/lib/ (where CMake expects them)
        SDK_LIB_DIR="$PROJECT_ROOT/shared/sdk/lib"
        OPENCV_DIR="$PROJECT_ROOT/shared/opencv/Windows/x86_64"

        mkdir -p "$SDK_LIB_DIR" "$SDK_LIB_DIR/CrAdapter"
        mkdir -p "$OPENCV_DIR/Release/bin" "$OPENCV_DIR/Release/lib"

        echo "Copying SDK libraries..."
        cp "$SDK_SRC/Cr_Core.dll" "$SDK_LIB_DIR/"
        cp "$SDK_SRC/Cr_Core.lib" "$SDK_LIB_DIR/"
        cp "$SDK_SRC/monitor_protocol.dll" "$SDK_LIB_DIR/"
        cp "$SDK_SRC/monitor_protocol_pf.dll" "$SDK_LIB_DIR/"
        cp "$SDK_SRC/CrAdapter/Cr_PTP_IP.dll" "$SDK_LIB_DIR/CrAdapter/"
        cp "$SDK_SRC/CrAdapter/Cr_PTP_USB.dll" "$SDK_LIB_DIR/CrAdapter/"
        cp "$SDK_SRC/CrAdapter/libssh2.dll" "$SDK_LIB_DIR/CrAdapter/"
        cp "$SDK_SRC/CrAdapter/libusb-1.0.dll" "$SDK_LIB_DIR/CrAdapter/"

        echo "Copying OpenCV libraries..."
        cp "$OPENCV_SRC/Windows/x86_64/Release/bin/"opencv_*.dll "$OPENCV_DIR/Release/bin/"
        cp "$OPENCV_SRC/Windows/x86_64/Release/lib/"opencv_*.lib "$OPENCV_DIR/Release/lib/"
        ;;

    *)
        echo "ERROR: Unknown platform: $PLATFORM"
        rm -rf "$TEMP_DIR"
        exit 1
        ;;
esac

# Copy OpenCV headers if not already present
if [ ! -d "$PROJECT_ROOT/shared/opencv/include/opencv2" ]; then
    echo "Copying OpenCV headers..."
    mkdir -p "$PROJECT_ROOT/shared/opencv/include"
    cp -r "$OPENCV_SRC/include/opencv2" "$PROJECT_ROOT/shared/opencv/include/"
fi

# Copy SDK headers
echo "Copying SDK headers..."
mkdir -p "$PROJECT_ROOT/shared/sdk/include"
cp "$SDK_HEADER_SRC/"*.h "$PROJECT_ROOT/shared/sdk/include/"

# Copy the stock SDK sample helper sources (shared/core).
# These are Sony's sample sources from the SDK download's `app/` folder and are
# NOT redistributed in the public repo. The REST server's own device layer lives
# in api/server/src/device/ (MIT); it links against these stock helpers. Only
# shared/core/CMakeLists.txt and README.md are tracked in the repo; the .cpp/.h
# sources are placed here from the SDK archive.
CORE_DST="$PROJECT_ROOT/shared/core"
CORE_FILES=(
    CameraDevice.cpp CameraDevice.h
    PropertyValueTable.cpp PropertyValueTable.h
    ConnectionInfo.cpp ConnectionInfo.h
    Text.cpp Text.h
    MessageDefine.cpp MessageDefine.h
    OpenCVWrapper.cpp OpenCVWrapper.h
    CrDebugString.cpp CrDebugString.h
)

# The stock helper sources live in the sample app root (`app/`) of the SDK
# archive, next to the CRSDK headers folder. (RemoteCli.cpp — the CLI sample's
# main — is intentionally NOT copied; the REST server has its own entry point.)
CORE_SRC="$EXTRACT_ROOT/app"

if [ -f "$CORE_SRC/CameraDevice.cpp" ]; then
    echo "Copying stock SDK sample helper sources into shared/core..."
    mkdir -p "$CORE_DST"
    for CORE_FILE in "${CORE_FILES[@]}"; do
        if [ -f "$CORE_SRC/$CORE_FILE" ]; then
            cp "$CORE_SRC/$CORE_FILE" "$CORE_DST/"
        else
            echo "  WARNING: $CORE_FILE not found in $CORE_SRC"
        fi
    done
else
    echo "WARNING: stock SDK sample helper sources (CameraDevice.cpp, ...) not found at $CORE_SRC."
    echo "         The C++ server cannot build without shared/core sources."
    echo "         See docs/SDK_SETUP.md for the expected archive layout and placement."
fi

# Cleanup
rm -rf "$TEMP_DIR"

echo "=== SDK extracted for $PLATFORM ==="
echo "SDK libs: $SDK_LIB_DIR"
if [ -n "${OPENCV_LIB_DIR:-}" ]; then
    echo "OpenCV libs: $OPENCV_LIB_DIR"
else
    echo "OpenCV libs: $OPENCV_DIR"
fi
echo "SDK headers: $PROJECT_ROOT/shared/sdk/include"
