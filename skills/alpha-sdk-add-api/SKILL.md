---
name: alpha-sdk-add-api
description: Add missing Sony Camera Remote SDK features to the Alpha Camera REST API. Use when implementing or reviewing new REST endpoints, camera properties, camera actions, operations, SSE events, OpenAPI changes, or TypeScript client support for `@alpha-sdk/api` and `@alpha-sdk/client`.
---

# Alpha SDK Add API

## Overview

Use this workflow to expose a Sony Camera Remote SDK feature through the local C++ REST API and supported Alpha SDK client packages.

## Workflow

Use this sequence for an unsupported SDK feature request:

1. Identify the camera being used: model, firmware when known, connection type, connection mode, lens/media dependencies, and current priority-key state.
2. Identify the customer use case in plain language. Separate desired camera behavior from the HTTP/client API shape.
3. Research API compatibility:
   - Use `CameraRemoteSDK` MCP for exact SDK symbols, enums, operation codes, callbacks, error codes, and sample code.
   - Use `CameraHelp` MCP for model-specific behavior and menu terminology.
   - Read `api/openapi.yaml` (the source of truth) and the [hosted docs](https://crsdk.github.io/alpha-sdk-api/) for existing REST naming.
4. Choose the SDK API or SDK property to add. Record dependent APIs, required camera state, required connection mode, and expected callbacks.
5. Classify the feature as a property, action, operation, callback/event, file-transfer flow, live-view flow, or client-only helper.
6. Inspect current implementation points before editing:
   - `api/server/src/device/CameraDeviceRest.*`
   - `api/server/src/device/RestPropertyParsers.*`
   - `api/server/src/CameraWebController.*`
   - `api/server/src/CameraWebServer.*`
   - `api/openapi.yaml`
   - `site/src/content/docs/` (the docs site, in this repo)
7. Implement the smallest API surface consistent with existing naming. Use kebab-case REST names.
8. Update OpenAPI in the same change. If public docs or examples need to change, update `site/src/content/docs/` in the same PR — the docs live in this repo.
9. Update supported public clients in their owning SDK/client repositories when generated client behavior changes. Do not add generated client packages or example apps to this repository.
10. Add tests or hardware/manual test notes when automation is not possible.
11. Before finishing, check that new callbacks, worker-thread work, and SDK handles have bounded lifetimes and shutdown cleanup.

## Repo Map

- `api/server/src/device/CameraDeviceRest.h/.cpp` are the MIT device layer. Add non-interactive SDK calls (multi-step sequences, bounded waits, callback flows) here.
- `api/server/src/device/RestPropertyParsers.h/.cpp` hold REST value parsers/formatters (string <-> SDK value). Add new value helpers here.
- `shared/core/*` are Sony's stock SDK sample helpers (`PropertyValueTable`, `CrDebugString`, `OpenCVWrapper`, etc.), fetched from the SDK download and not tracked in this repo. Do not edit them; call them from the device layer.
- `api/server/src/CameraWebController.cpp` is the camera orchestration layer. It owns `PROPERTY_MAP`, `ACTION_MAP`, `getPropertyGeneric`, `setPropertyGeneric`, `executeActionGeneric`, callback correlation, and per-camera workers.
- `api/server/src/CameraWebController.h` declares controller methods, mapping structs, response structs, and worker interfaces.
- `api/server/src/CameraWebServer.cpp` is the raw HTTP/SSE/WebSocket layer. Add route matching in `handleRequest`, JSON body parsing, and `handleApi*` methods here when generic property/action endpoints are not enough.
- `api/server/src/CameraWebServer.h` declares the HTTP handlers.
- `api/openapi.yaml` is the source of public REST contract.
- `tests/unit/` contains deterministic unit tests for non-hardware helper logic.
- `api/tests/api_tests.sh` is the camera-backed API smoke test entry point.

There is no `CameraManager.cpp` in this repo. Treat `CameraWebController.*` as the server-side camera manager/orchestration layer.

## Properties

For `CrDeviceProperty_*` features:

- Add the REST name and SDK property code to `PROPERTY_MAP` in `CameraWebController.cpp`.
- Add parse and format support in `PropertyValueTable.*` when values need display names, possible values, or conversions.
- Prefer generic `GET/PUT /api/cameras/{cameraId}/properties/{propertyName}` support through `getPropertyGeneric` and `setPropertyGeneric`.
- Add custom `CameraDevice` methods only when the SDK call is not covered by generic property setting.
- Include `writable`, current value, formatted value, and available values in responses when the camera exposes them.
- Confirm bulk initialization paths such as `getAllProperties()` include the new public property when clients need it on startup.
- Update `components.parameters.propertyName`, schemas, and examples in `api/openapi.yaml` when adding public names or value shapes.

## Actions

For commands such as shutter, focus drive, movie recording, zoom, or custom white balance:

- Add the REST action name and SDK command/custom method to `ACTION_MAP` in `CameraWebController.cpp` when the generic action endpoint can handle it.
- Add non-interactive `CameraDevice` methods. Avoid methods that prompt on stdin or assume CLI use.
- Route through `CameraWebController`.
- Expose as `POST /api/cameras/{cameraId}/actions/{actionName}` unless the feature is clearly resource-specific.
- Return bounded, actionable errors for unsupported state, timeout, missing priority key, or incompatible connection mode.
- Keep REST action handlers off the HTTP thread when they can block on camera I/O; use existing controller workers where practical.

## Async Operations

When the SDK reports completion through callbacks:

- Correlate requests to callbacks where possible.
- Use SSE events for progress and completion if callers need to observe work after the HTTP response.
- Do not hold HTTP requests indefinitely. Use bounded waits and return timeout details.
- Unregister callback listeners on success, timeout, and every early-return error path.

## Tests

- For route/schema changes, add deterministic helper coverage in `tests/unit/` where possible and camera-backed API coverage in `api/tests/api_tests.sh` when hardware is required.
- For SDK-dependent behavior, document manual test evidence with camera model, OS, SDK version, connection type, connection mode, endpoint/client call, expected result, and actual result.
- For parsing/formatting helpers, prefer deterministic unit tests if a test harness exists. If no harness exists yet, add a small focused test target rather than only relying on camera hardware.

## Required Docs

Read `docs/ADDING_SDK_APIS.md` for the repository-level checklist before making broad changes. Read `docs/MCP_SERVERS.md` if MCP setup or research server names are unclear.

If exact file locations change, use `rg` to find current equivalents rather than assuming stale paths.
