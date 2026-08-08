# @alpha-sdk/crsdk

Developer CLI for the [Alpha Camera REST API](https://crsdk.app) — the
build-from-source companion. It checks your toolchain, builds the native REST
server from this repo, runs it, and wires up docs MCP servers.

The server is compiled from source (it links Sony's Camera Remote SDK, which you
download separately — see [docs/SDK_SETUP.md](../docs/SDK_SETUP.md)). This CLI does
**not** fetch prebuilt binaries.

## Usage

```bash
crsdk doctor            # check toolchain, SDK, and build state
crsdk build [--clean]   # compile the native REST server
crsdk serve [--port N]  # run the built server (default :8080)
crsdk status            # is the server running?
crsdk stop              # stop a running server
crsdk mcp <sub>         # manage docs MCP servers (status | install)
```

Run it from inside a clone of `alpha-sdk-api`, or point it at a built binary with
`CRSDK_BINARY=/path/to/CameraWebApp`.

## Typical first run

```bash
git clone https://github.com/crsdk/alpha-sdk-api && cd alpha-sdk-api
# place Sony's SDK per docs/SDK_SETUP.md
crsdk doctor            # confirm the toolchain is ready
crsdk build             # compile CameraWebApp
crsdk serve             # http://localhost:8080
```

Then drive it from the generated clients — [`@alpha-sdk/client`](https://www.npmjs.com/package/@alpha-sdk/client)
(npm) or [`alpha-sdk-client`](https://pypi.org/project/alpha-sdk-client/) (PyPI).

## Roadmap

SDK-lifecycle commands (`install --zip` to accept the EULA and extract the SDK,
plus `versions`/`use` to archive and swap SDK releases) are being ported from the
standalone `crsdk` tool. Until then, follow `docs/SDK_SETUP.md` to place the SDK.
