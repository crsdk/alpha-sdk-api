# Adding SDK APIs

Use this guide when exposing a Sony Camera Remote SDK feature through the REST API.

## Before Coding

1. Identify the camera being used: model, firmware when known, connection type, connection mode, lens/media dependencies, and priority-key state.
2. Identify the use case in customer language. Separate the desired camera behavior from the REST/client API shape.
3. Research the feature with `CameraRemoteSDK` MCP.
4. Check camera/model behavior with `CameraHelp` MCP when the feature is model-specific.
5. Check current REST naming with `CameraWebAPI` MCP and `api/openapi.yaml`.
6. Identify whether the feature is a property, action, operation, callback/event, file-transfer flow, or live-view flow.
7. Record dependent SDK APIs, required camera state, required connection mode, expected callbacks, and known unsupported models.

## Repo Map

- `api/server/src/device/CameraDeviceRest.*` is the MIT device layer. Add non-interactive SDK calls (multi-step sequences, bounded waits, callback flows) here.
- `api/server/src/device/RestPropertyParsers.*` holds REST value parsers/formatters (string <-> SDK value). Add new value helpers here.
- `shared/core/*` are Sony's **stock** SDK sample helpers (`PropertyValueTable`, `CrDebugString`, `OpenCVWrapper`, etc.), fetched from the SDK download and not tracked in this repo. Do not edit them; call them from the device layer.
- `api/server/src/CameraWebController.*` is the server-side camera orchestration layer. It owns `PROPERTY_MAP`, `ACTION_MAP`, generic property/action handlers, callback correlation, and per-camera workers.
- `api/server/src/CameraWebServer.*` is the raw HTTP/SSE/WebSocket layer. Add route matching and `handleApi*` HTTP wrappers here when generic endpoints are not enough.
- `api/openapi.yaml` is the public REST contract.
- `tests/unit/` contains deterministic unit tests for non-hardware helper logic.
- `api/tests/api_tests.sh` is the camera-backed API smoke test entry point.

There is no `CameraManager.cpp` in this repository. When a contribution guide says "camera manager" for the HTTP server, use `CameraWebController.*`.

## Implementation Path

Prefer the generic property and action endpoints before adding a new route.
Add resource-specific routes only when the request body, response shape, async
behavior, or SDK state machine cannot be represented by the generic handlers.

### Properties

For SDK properties such as `CrDeviceProperty_*`:

1. Add the REST property name and SDK code to `PROPERTY_MAP` in `api/server/src/CameraWebController.cpp`.
2. Add or confirm parse/format support in `shared/core/PropertyValueTable.*`.
3. Use stable kebab-case REST names, for example `priority-key`, `focus-mode`, or `white-balance`.
4. Prefer generic `GET/PUT /api/cameras/{cameraId}/properties/{propertyName}` handling through `getPropertyGeneric` and `setPropertyGeneric` unless the SDK requires a custom flow.
5. Confirm `getAllProperties()` includes the new mapped property if the UI or clients rely on bulk initialization.
6. Update `api/openapi.yaml` with accepted values, request schema, and response examples.

### Actions

For SDK commands such as shutter, movie record, zoom, or focus drive:

1. Add the action name and command/custom method to `ACTION_MAP` in `api/server/src/CameraWebController.cpp` when the generic action endpoint is sufficient.
2. Add a non-interactive `CameraDevice` method if the existing method prompts on stdin or assumes CLI use.
3. Wire the method through `CameraWebController`.
4. Expose it under `POST /api/cameras/{cameraId}/actions/{actionName}` unless a resource-specific route is clearer.
5. Use bounded waits for actions that depend on callbacks or camera state changes.
6. Document required camera state, connection mode, and priority-key requirements.

### Operations and Callbacks

For asynchronous SDK operations:

1. Include callback correlation if the result arrives through `IDeviceCallback`.
2. Emit SSE events when customers need progress or completion notifications.
3. Avoid blocking HTTP requests indefinitely. Use bounded waits and return actionable timeout errors.
4. Unregister callback listeners on success, timeout, and all error paths.
5. Add event schemas to OpenAPI and docs.

## Required Updates

Every public API addition should update:

- `api/server/src/device/CameraDeviceRest.*` when a new non-interactive SDK sequence is needed
- `api/server/src/device/RestPropertyParsers.*` when property value parsing/formatting is involved
- `api/server/src/CameraWebController.*`
- `api/server/src/CameraWebServer.*` when adding routes
- `api/openapi.yaml`
- Hosted REST docs source for `crsdk.app` when the public docs need to change
- Supported client source in the relevant SDK/client repository when generated client behavior changes
- Hosted example or recipe docs on `crsdk.app` if usage is not obvious

Do not add generated client packages or bundled example apps to this repository.

When adding routes in `CameraWebServer.cpp`, keep exact REST regex routes above
legacy substring routes. The legacy block is intentionally broad for backward
compatibility and can accidentally match newer endpoints if ordered first.

## Tests

Add or update tests at the right level:

- API routes and error handling: deterministic helpers belong in `tests/unit/`; camera-backed route behavior belongs in `api/tests/api_tests.sh`.
- Client behavior: the test suite in the relevant SDK/client repository.
- Pure parsing/formatting helpers: a focused C++ unit test target if available, or add one if the logic is not hardware-dependent.
- Hardware-dependent flows: manual evidence is acceptable, but include the details listed below.

## Testing Notes

Hardware-dependent PRs should include:

- Camera model and firmware.
- SDK version.
- OS and architecture.
- USB or network connection.
- Connection mode: `remote`, `remote-transfer`, or `contents-transfer`.
- Exact curl/client command used.
- Expected camera behavior and actual result.
