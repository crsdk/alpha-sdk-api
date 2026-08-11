# Testing

Use deterministic unit/type checks for non-hardware logic and e2e checks for camera-backed behavior.

## Unit and Static Checks

Run deterministic unit tests for non-hardware helper logic:

```bash
cd tests/unit
npm test
```

Run the strict coverage gate for the testable helper target:

```bash
cd tests/unit
npm run coverage
```

This coverage command is intentionally scoped to `tests/unit/src/lib/request-helpers.mjs`, which contains pure URL/path/query/body/value helpers. It enforces 100% line, branch, and function coverage for that introduced unit. Full-repo 100% coverage is not currently feasible because the C++ server depends on Sony SDK binaries, OS networking, WebSocket/EventSource APIs, and camera-backed behavior.

Run shell syntax checks for the API scripts:

```bash
bash -n api/tests/api_tests.sh
```

Run diff hygiene before opening a PR:

```bash
git diff --check
```

Unit tests should target 100% coverage for non-hardware logic where feasible: parsers, formatters, value conversion, request validation, response shaping, OpenAPI/client helpers, retry/backoff utilities, and error mapping. Do not require a physical camera for unit tests.

## Build Check

After placing Sony SDK files as described in `docs/SDK_SETUP.md`, build the C++ server:

```bash
cd api/server
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release
```

If SDK files are missing, CMake should fail with an error pointing to `docs/SDK_SETUP.md`.

## E2E Camera Smoke Test

Before running hardware tests, ask the user to connect a supported Sony camera, power it on, choose the required USB or network mode, and confirm it is ready.

Start the server:

```bash
cd api/server/build
./CameraWebApp
```

In another terminal:

```bash
API_BASE=http://localhost:8080/api
curl -sS "$API_BASE/server/status"
curl -sS "$API_BASE/cameras"
```

After `GET /api/cameras` returns a camera:

```bash
API_BASE=http://localhost:8080/api
CAMERA_ID="<camera id from /api/cameras>"

curl -sS -X POST "$API_BASE/cameras/$CAMERA_ID/connection" \
  -H "Content-Type: application/json" \
  -d '{"mode":"remote"}'

curl -sS "$API_BASE/cameras/$CAMERA_ID/connection"
curl -sS "$API_BASE/cameras/$CAMERA_ID/properties/all"
curl -sS -X DELETE "$API_BASE/cameras/$CAMERA_ID/connection"
```

Run the API test suite while the camera server is running:

```bash
cd api/tests
./api_tests.sh --verbose
```

## Hardware Evidence

For hardware-dependent PRs, include:

- Camera model and firmware when known.
- SDK version.
- OS and architecture.
- USB or network connection.
- Connection mode.
- Exact curl/client commands.
- Expected behavior and actual result.
