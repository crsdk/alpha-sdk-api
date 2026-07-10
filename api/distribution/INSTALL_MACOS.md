# macOS Installation Guide

## Quick Start (For Testing)

### Option 1: Run Directly from Build

```bash
cd api/server/build
./CameraWebApp
```

Server starts on `http://localhost:8080`

### Option 2: Install to System Path

```bash
# From api directory
sudo cp server/build/CameraWebApp /usr/local/bin/
CameraWebApp  # Run from anywhere
```

## Homebrew Distribution

Once published, users will install via:

```bash
brew tap jordlee/alpha-sdk-api
brew install sony-camera-webserver
CameraWebApp
```

## Testing Installation

### 1. Start the Server

```bash
./CameraWebApp
```

You should see:
```
Camera Web Server starting on port 8080...
Server running at http://localhost:8080
```

### 2. Test with curl

```bash
# List cameras
curl http://localhost:8080/api/cameras

# Expected response:
# {"success":true,"cameras":[...]}
```

### 3. Install Client Libraries

Published SDK package details, npm install commands, and sample apps are documented at `https://crsdk.app/sdk/overview`.

### 4. Test with Client Library

Use the hosted SDK docs and example apps at `https://crsdk.app/sdk/overview`.

## Creating Release Package

To create a distributable package:

```bash
cd api/distribution
./package-macos.sh
```

This creates `package/CameraWebApp-macos.tar.gz` with SHA256 hash for Homebrew.

## Publishing to Homebrew

### Step 1: Create GitHub Release

1. Push code to GitHub
2. Create a new release (v2.1.0)
3. Upload `package/CameraWebApp-macos.tar.gz` as release asset

### Step 2: Create Homebrew Tap

```bash
# Create a new repo: homebrew-sony-camera
# Add the formula from api/distribution/homebrew/sony-camera-webserver.rb
# Update SHA256 hash from package-macos.sh output
# Update GitHub URLs
```

### Step 3: Users Install

```bash
brew tap jordlee/alpha-sdk-api
brew install sony-camera-webserver
```

## Troubleshooting

### Port Already in Use

```bash
# Find process using port 8080
lsof -i :8080

# Kill existing CameraWebApp
killall CameraWebApp
```

### Library Not Found Errors

The app uses dylibs from the Sony SDK. If you get library errors:

```bash
# Check linked libraries
otool -L server/build/CameraWebApp

# Install dependencies (if needed)
brew install opencv
```

### Camera Not Detected

1. Ensure camera is in PC Remote mode
2. Check USB connection
3. Restart CameraWebApp

## Next Steps

- **API Documentation**: See [API_DOCUMENTATION.md](API_DOCUMENTATION.md)
- **OpenAPI Spec**: See [openapi.yaml](openapi.yaml)
- **Client Examples**: See `clients/typescript/` and `clients/python/`
