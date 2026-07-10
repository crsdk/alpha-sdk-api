# Camera Remote API Test Suite

Comprehensive unit and integration tests for the Camera Remote SDK REST API.

## Test Coverage

### Unit Tests
- **Camera Discovery & Connection**
  - GET /api/cameras (list cameras)
  - POST /api/cameras/{id}/connection (connect)
  - GET /api/cameras/{id}/connection (status)

- **Property Getters**
  - Individual property getters (ISO, aperture, shutter-speed, etc.)
  - GET /api/cameras/{id}/properties/all (getAllProperties)

- **Property Setters - Hex Values**
  - ISO setter with hex values (0x64)
  - Aperture setter with hex values (0x320)
  - Shutter-speed setter with hex values (0x10064)

- **Property Setters - Friendly Names**
  - ISO with decimal (200)
  - Aperture with f-stop (f/5.6) and decimal (4.5)
  - Shutter-speed with fraction (1/250)
  - Focus-area with strings (wide, center, etc.)
  - Raw-compression with strings (lossless, compressed, etc.)

- **Camera Actions**
  - POST /api/cameras/{id}/actions/shutter
  - POST /api/cameras/{id}/actions/af-shutter

- **SD Card Operations**
  - GET /api/cameras/{id}/sd-card/slot/{slot}/files

### Integration Tests
- **Full Shooting Workflow**
  - Connect → Set ISO → Set Aperture → Set Shutter → Set Focus → Verify → Shoot

- **Property Value Format Compatibility**
  - Tests all value formats for each property (hex, decimal, friendly names, SDK values)

## Running Tests

### Prerequisites
1. Sony camera connected via USB
2. CameraWebApp server running on http://localhost:8080

```bash
# Start the server first
cd api/server/build
./CameraWebApp
```

### Run All Tests
```bash
cd api/tests
./api_tests.sh
```

### Run with Verbose Output
```bash
./api_tests.sh --verbose
```

## Test Results

Tests will output:
- ✅ **PASS** - Test succeeded
- ❌ **FAIL** - Test failed with details
- ℹ️  **INFO** - Informational message

### Example Output
```
╔═══════════════════════════════════════════════════════════╗
║         Camera Remote API Test Suite                      ║
║         Comprehensive Unit & Integration Tests            ║
╚═══════════════════════════════════════════════════════════╝

ℹ️  INFO: Server is running

========================================
Camera Discovery & Connection Tests
========================================

TEST: GET /cameras - List available cameras
✅ PASS: Camera list endpoint works
ℹ️  INFO: Found camera: D06CE05ECA8D

...

========================================
Test Summary
========================================
Total Tests: 45
Passed: 43
Failed: 2

🎉 All tests passed!
```

## What's Being Tested

### Session 1-3 Fixes Verification
All fixes from Sessions 1-3 are tested:

1. **Hex Value Support** (Session 3)
   - ISO: 0x64, 0xc8, 0x190
   - Aperture: 0x320, 0x230
   - Shutter: 0x10064, 0x100fa

2. **String Enum Support** (Session 2)
   - Focus-area: "wide", "center", "flexible-spot-s"
   - Raw-compression: "lossless", "compressed", "uncompressed"

3. **Friendly Format Support**
   - Aperture: f/5.6, f/8, 4.5
   - Shutter: 1/250, 1/500, 2s
   - ISO: 200, 400, 800

4. **AF+Shutter Non-Interactive** (Session 2)
   - Ensures no stdin prompts block API

## Adding New Tests

To add a new test:

1. Create a test function following the naming pattern `test_*`
2. Use helper functions:
   - `api_request METHOD ENDPOINT DATA EXPECTED_STATUS`
   - `check_json_key JSON KEY`
   - `check_json_value JSON KEY EXPECTED_VALUE`
   - `print_test`, `print_pass`, `print_fail`, `print_info`

3. Add to `run_all_tests()` function

### Example Test
```bash
test_my_new_feature() {
    print_section "My New Feature Tests"

    print_test "GET /api/my-endpoint"
    response=$(api_request "GET" "/my-endpoint")

    if check_json_key "$response" "success"; then
        print_pass "My feature works"
    else
        print_fail "My feature failed"
    fi
}
```

## Continuous Integration

These tests can be integrated into CI/CD pipelines:

```bash
# In GitHub Actions, GitLab CI, etc.
- name: Run API Tests
  run: |
    ./CameraWebApp &
    sleep 5
    ./tests/api_tests.sh
```

## Troubleshooting

### Server Not Running
```
❌ FAIL: Server is not running on http://localhost:8080
Please start the server with: ./CameraWebApp
```
**Solution**: Start the CameraWebApp server before running tests

### No Camera Detected
```
❌ FAIL: No cameras detected
```
**Solution**: Ensure Sony camera is connected via USB and powered on

### Permission Errors
```bash
chmod +x api_tests.sh
```

## Test File Structure

```
api/tests/
├── README.md           # This file
├── api_tests.sh        # Main test suite
└── (future test files)
```

## Future Enhancements

- [ ] Add Python-based tests using pytest
- [ ] Add performance/load tests
- [ ] Add WebSocket/LiveView tests
- [ ] Add camera settings save/load tests
- [ ] Add multi-camera tests
- [ ] Generate test coverage reports
- [ ] Add CI/CD integration examples
