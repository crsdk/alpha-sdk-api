# Alpha Camera REST API

Local REST, SSE, and WebSocket control for Sony cameras, built on top of Sony's official Camera Remote SDK.

This MIT-licensed open-source repository lets customers add REST API coverage for additional Sony Camera Remote SDK features.

## Repository Hygiene

Local SDK/vendor artifacts are intentionally excluded:

- Sony Camera Remote SDK files and binaries are not redistributed. Download the SDK yourself from the [official Sony download page](https://support.d-imaging.sony.co.jp/app/sdk/en/index.html).
- The `shared/core/` SDK integration wrapper (`CameraDevice`, `PropertyValueTable`, etc.) is Sony sample-derived and is also not redistributed. It ships in the SDK download and is placed locally by the extraction helper; only its `CMakeLists.txt` and `README.md` are tracked here.
- OpenCV headers and runtime files copied from the SDK sample archive are local setup artifacts and are ignored.
- Generated client-SDK output and build folders are excluded — the clients are regenerated from `api/openapi.yaml`, not committed. See the hosted [SDK overview](https://crsdk.github.io/alpha-sdk-api/sdk/overview/).

## Project Layout

- `api/server/` - C++ REST API server (MIT-licensed, in this repo).
- `api/openapi.yaml` - REST API contract (the single source of truth).
- `shared/core/` - SDK integration wrapper. Sony sample-derived, obtained from the SDK download (see [docs/SDK_SETUP.md](docs/SDK_SETUP.md)); only `CMakeLists.txt` and `README.md` are tracked here.
- `cli/` - the `crsdk` developer CLI (build the server, manage the SDK, build the MCP bundle). Run it from the repo root with `./crsdk`.
- `mcp/` - the Alpha Camera **MCP server** for AI camera control, generated + hand-written from the same spec.
- `fern/` - [Fern](https://buildwithfern.com) config that generates the TypeScript/Python client SDKs from `api/openapi.yaml`.
- `site/` - the documentation site (Astro Starlight), published to GitHub Pages.
- `docs/` - contributor documentation (this folder).
- `skills/` - agent skills for contributors adding API coverage.
- `tests/unit/` - deterministic unit tests that do not require camera hardware.

Hosted docs (GitHub Pages — the site has no custom domain):

- [Alpha Camera REST API docs](https://crsdk.github.io/alpha-sdk-api/)
- [SDK overview and published packages](https://crsdk.github.io/alpha-sdk-api/sdk/overview/)
- [MCP servers](https://crsdk.github.io/alpha-sdk-api/mcp-server/overview/)

## SDK Setup

Sony SDK files are not distributed in this repository. Download the SDK from the [official Sony download page](https://support.d-imaging.sony.co.jp/app/sdk/en/index.html), then place it with `crsdk install --zip <sony-sdk.zip>` (see [docs/SDK_SETUP.md](docs/SDK_SETUP.md) for the expected folder layout).

## Build

The server is **built from source** — it links Sony's SDK, which cannot be redistributed, so there is no prebuilt binary to install. The quickest path:

```bash
./crsdk install --zip <sony-camera-remote-sdk.zip>   # accept the EULA + place the SDK
./crsdk build                                        # compile the CameraWebApp server
./crsdk start                                        # run it on :8080
```

For the manual CMake recipe, a specific platform, or a different SDK version, see [docs/BUILDING_BINARIES.md](docs/BUILDING_BINARIES.md).

## Contributing

Start with [CONTRIBUTING.md](CONTRIBUTING.md). For adding missing SDK features, use [docs/ADDING_SDK_APIS.md](docs/ADDING_SDK_APIS.md).

## Testing

See [docs/TESTING.md](docs/TESTING.md) for unit/static checks, build checks, and camera-backed e2e curl flows.

## License

The open-source project code is intended to be released under the MIT License. Sony Camera Remote SDK files remain subject to Sony's SDK license and must be obtained separately by each contributor.
