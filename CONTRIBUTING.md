# Contributing

Thanks for helping improve the Alpha Camera REST API.

## Supported Packages

Package details and install commands are documented on the hosted SDK docs:

- [SDK overview and published packages](https://crsdk.app/sdk/overview)
- [Alpha Camera REST API docs](https://crsdk.app/)

Do not add generated client packages, example apps, or deprecated package artifacts to this repository.

## Local Setup

1. Clone the public repository.
2. Download the Sony Camera Remote SDK separately from Sony.
3. Place SDK files using [docs/SDK_SETUP.md](docs/SDK_SETUP.md).
4. Install platform dependencies for CMake and Node.js as needed. (JSON is vendored in `api/server/third_party/jsoncpp`; there is no OpenSSL or external jsoncpp dependency.)
5. Build the C++ server from `api/server`.

## Adding API Coverage

For new camera features, follow [docs/ADDING_SDK_APIS.md](docs/ADDING_SDK_APIS.md).

All contribution happens in this repo. `shared/core/` holds Sony's **stock** SDK
sample helpers (fetched from the SDK download, not redistributed here) and is
never edited; the REST server's own device logic is MIT code in
`api/server/src/device/`.

- **Most features — the generic endpoints:** add an entry to `PROPERTY_MAP` or
  `ACTION_MAP` in `api/server/src/CameraWebController.cpp`, confirm value
  parsing/formatting (add helpers to `api/server/src/device/RestPropertyParsers.*`
  if needed), and update `api/openapi.yaml`.
- **Features needing a new device method** (a multi-step SDK sequence, bounded
  wait, or callback flow): add a non-interactive method to
  `api/server/src/device/CameraDeviceRest.*` and wire it through the controller.
  This is MIT code in this repo — open a normal PR.

Every feature should update:

- Device method in `api/server/src/device/CameraDeviceRest.*` when a new SDK sequence is needed.
- Value helpers in `api/server/src/device/RestPropertyParsers.*` when new value parsing/formatting is needed.
- REST handling in `api/server/src/CameraWebController.*`.
- OpenAPI contract in `api/openapi.yaml`.
- Supported client package changes in the relevant public SDK/client repository.
- Hosted docs or examples on `crsdk.app` when customer usage is not obvious.

Do not edit `shared/core/` — those are Sony's stock SDK sample sources, fetched
locally via `scripts/extract-sdk.sh` and not tracked in this repository.

## Agent-Assisted Contributions

This repo includes agent skills under `skills/`:

- `alpha-sdk-doc-research` - research SDK, camera help, and Web API docs through MCP before implementation.
- `alpha-sdk-add-api` - implement a missing REST API endpoint or property/action mapping.
- `alpha-sdk-test` - plan and run unit/e2e validation, including camera-backed curl checks.
- `alpha-sdk-publish` - check CI workflows and public GitHub publishing readiness.

Use [docs/MCP_SERVERS.md](docs/MCP_SERVERS.md) to configure the documentation MCP servers.

## Pull Request Checklist

- The change does not add Sony SDK files, SDK binaries, generated build folders, camera media, or local credentials.
- The API contract and implementation match.
- Public package names and install guidance match `https://crsdk.app/sdk/overview`.
- Unit/static checks and any relevant e2e checks from `docs/TESTING.md` were run, or skipped with a clear reason.
- The feature has at least manual test notes. Hardware-dependent behavior should list camera model, firmware, connection mode, and SDK version.
- Any model-specific claim is backed by SDK docs or camera help docs.
