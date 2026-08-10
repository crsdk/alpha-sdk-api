# crsdk — Alpha Camera REST API developer CLI

The [Alpha Camera REST API](https://crsdk.app) build-from-source companion. It
accepts Sony's SDK license and unpacks the SDK, checks your toolchain, builds the
native REST server, runs it, and wires up docs MCP servers.

**Bundled, not published.** This CLI ships *in the repo* — it is not an npm
package, because every command needs the repo present. Run it with the
repo-root wrapper, which builds the CLI on first use:

```bash
git clone https://github.com/crsdk/alpha-sdk-api && cd alpha-sdk-api
./crsdk doctor
```

## First run

```bash
./crsdk doctor                                   # is my toolchain ready?
./crsdk install --zip ~/Downloads/CrSDK_*.zip    # accept EULA + unpack the SDK
./crsdk build                                    # compile the native server
./crsdk start                                    # http://localhost:8080
```

Then drive the camera from the generated clients —
[`@alpha-sdk/client`](https://www.npmjs.com/package/@alpha-sdk/client) (npm) or
[`alpha-sdk-client`](https://pypi.org/project/alpha-sdk-client/) (PyPI).

## Commands

```
# SDK
crsdk install --zip <p>    accept Sony's EULA and extract the SDK into shared/
crsdk update --zip <p>     archive the current SDK, then install a new one
crsdk versions             list active + archived SDK versions
crsdk use <name>           swap an archived SDK version back in

# Server
crsdk doctor               check toolchain, SDK, and build state
crsdk build [--clean]      compile the native REST server
crsdk start [--port N]     run the built server (default :8080)   (alias: serve)
crsdk status | stop        check / stop a running server

# Agents
crsdk mcp <sub>            manage docs MCP servers (status | install)
```

Point the server commands at a binary elsewhere with
`CRSDK_BINARY=/path/to/CameraWebApp`. Building the server needs Sony's SDK —
see [docs/SDK_SETUP.md](../docs/SDK_SETUP.md).
