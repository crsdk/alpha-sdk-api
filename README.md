# Alpha Camera REST API

Local REST, SSE, and WebSocket control for Sony cameras, built on top of Sony's official Camera Remote SDK.

This MIT-licensed open-source repository lets customers add REST API coverage for additional Sony Camera Remote SDK features.

## Repository Hygiene

Local SDK/vendor artifacts are intentionally excluded:

- Sony Camera Remote SDK files and binaries are not redistributed. Contributors must download the SDK from Sony separately.
- The `shared/core/` SDK integration wrapper (`CameraDevice`, `PropertyValueTable`, etc.) is Sony sample-derived and is also not redistributed. It ships in the SDK download and is placed locally by the extraction helper; only its `CMakeLists.txt` and `README.md` are tracked here.
- OpenCV headers and runtime files copied from the SDK sample archive are local setup artifacts and are ignored.
- Deprecated/generated client package artifacts are excluded. Current SDK, npm package, and example-app details live in the hosted [SDK overview](https://crsdk.app/sdk/overview).

## Project Layout

- `api/server/` - C++ REST API server (MIT-licensed, in this repo).
- `shared/core/` - SDK integration wrapper. Sony sample-derived, obtained from the SDK download (see [docs/SDK_SETUP.md](docs/SDK_SETUP.md)); only `CMakeLists.txt` and `README.md` are tracked here.
- `api/openapi.yaml` - REST API contract.
- `docs/` - Public setup and contributor documentation.
- `tests/unit/` - deterministic unit tests that do not require camera hardware.
- `skills/` - Agent skills for contributors adding API coverage.

Use the hosted docs for detailed REST API, SDK package, npm install, and example-app guidance:

- [Alpha Camera REST API docs](https://crsdk.app/)
- [SDK overview and published packages](https://crsdk.app/sdk/overview)
- [MCP server setup](https://crsdk.app/MCP-Server/overview)

## SDK Setup

Sony SDK files are not distributed in this repository. See [docs/SDK_SETUP.md](docs/SDK_SETUP.md) for the expected folder layout and extraction instructions.

## Binaries

Prebuilt binaries ship with the npm package — see the [SDK overview](https://crsdk.app/sdk/overview). To build your own for an unpublished platform, a different SDK version, or local changes, see [docs/BUILDING_BINARIES.md](docs/BUILDING_BINARIES.md).

## Contributing

Start with [CONTRIBUTING.md](CONTRIBUTING.md). For adding missing SDK features, use [docs/ADDING_SDK_APIS.md](docs/ADDING_SDK_APIS.md).

## Testing

See [docs/TESTING.md](docs/TESTING.md) for unit/static checks, build checks, and camera-backed e2e curl flows.

## License

The open-source project code is intended to be released under the MIT License. Sony Camera Remote SDK files remain subject to Sony's SDK license and must be obtained separately by each contributor.
