---
name: alpha-sdk-test
description: Plan and run unit and e2e tests for the Alpha Camera REST API. Use when adding tests, validating REST behavior, checking non-hardware logic coverage, or preparing camera-backed manual/e2e evidence before publishing or merging.
---

# Alpha SDK Test

## Overview

Use this workflow to validate Alpha Camera REST API changes without mixing deterministic unit tests with hardware-dependent camera checks.

## Testing Policy

- Unit tests should target 100% coverage for non-hardware logic where feasible.
- Prioritize deterministic unit coverage for parsers, formatters, value conversion, request validation, response shaping, OpenAPI/client helpers, retry/backoff utilities, and error mapping.
- Do not require camera hardware for unit tests. Abstract or fake SDK inputs when testing code that is logically independent from a physical camera.
- Treat hardware-dependent behavior as e2e or manual validation. Record camera model, firmware when known, SDK version, OS, connection type, connection mode, command run, expected result, and actual result.
- If 100% unit coverage is not feasible, state the uncovered paths and why they require hardware, SDK callbacks, OS facilities, or future harness work.

## Unit Test Workflow

1. Identify the changed non-hardware logic and the existing test entry point.
2. Add focused tests close to the code under test. Prefer small deterministic fixtures over real camera state.
3. Cover success, invalid input, unsupported value, timeout/error mapping, and edge formatting paths.
4. Run the narrow test target first, then the broader package or server checks.
5. Report coverage gaps explicitly. Do not count hardware e2e evidence as unit coverage.

## E2E Camera Workflow

Run e2e checks only after the server builds and a physical camera is available.

Ask the user before the hardware portion:

> Please connect a supported Sony camera, power it on, choose the required USB or network connection mode, and confirm when it is ready for e2e testing.

Use this command sequence as the baseline and adapt paths only when the repo layout changes:

```bash
cd api/server
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release
./CameraWebApp
```

In another terminal, verify the server and camera with curl:

```bash
API_BASE=http://localhost:8080/api
curl -sS "$API_BASE/server/status"
curl -sS "$API_BASE/cameras"
```

After `GET /api/cameras` returns at least one camera, set the first camera id and run a minimal connection smoke test:

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

Run the scripted API suite when the camera is connected and the server is still running:

```bash
cd api/tests
./api_tests.sh --verbose
```

For feature-specific e2e checks, add curl commands for the exact endpoint changed, including request body, expected HTTP status, expected response fields, and cleanup. Avoid destructive camera/media actions unless the user explicitly agrees.

## Reporting

Summarize:

- Unit commands run and result.
- E2E/manual commands run and result.
- Camera model, firmware when known, SDK version, OS, connection mode, and server build path.
- Any skipped coverage or hardware limitations.
