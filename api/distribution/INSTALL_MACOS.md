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

- **OpenAPI specification**: [../openapi.yaml](../openapi.yaml)
- **API reference and request console**: <https://crsdk.github.io/alpha-sdk-api/api-reference/>
- **Generating a client**: <https://crsdk.github.io/alpha-sdk-api/sdk/overview/>
