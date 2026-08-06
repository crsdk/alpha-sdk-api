---
layout: "default"
title: "Server"
description: "Install the server package, run the camera-server CLI, embed ServerManager, and use health/logs/shutdown endpoints"
parent: "REST API"
nav_order: 2
---

`@alpha-sdk/api` is the npm distribution of the Alpha Camera REST API. A single install gives you three things:

1. The **native REST API server binary** (`CameraWebApp`) for macOS, Linux, and Windows.
2. The **`camera-server` CLI** for installing, starting, stopping, and inspecting the server.
3. A small **`ServerManager` Node.js class** for embedding the server lifecycle inside a Node application.

The matching platform binary (`@alpha-sdk/darwin-arm64`, `@alpha-sdk/linux-arm64`, `@alpha-sdk/linux-x64`, `@alpha-sdk/win32-x64`) is pulled in automatically as an `optionalDependency`.

{: .note }
> `ServerManager` is a Node.js convenience class, not a separate SDK. Python and Swift clients do not ship a `ServerManager` equivalent. From those languages, run `camera-server start`, spawn the binary as a subprocess, or connect to a server already running elsewhere on the network.


## Install

```bash
# Globally — CLI / scripting
npm install -g @alpha-sdk/api

# Per-project — when embedding ServerManager
npm install @alpha-sdk/api
```

After install, the [Sony Camera Remote SDK License Agreement](https://support.d-imaging.sony.co.jp/app/sdk/licenseagreement_d/en.html) prompt appears once. Accept it to unlock the binary.

## CLI

```bash
camera-server start             # spawn the server on :8080
camera-server stop
camera-server status            # check what's running

camera-server info              # version, platform, binary path
camera-server doctor            # verify prerequisites
camera-server platforms         # list installed/available platforms

camera-server install-sdk ts        # install @alpha-sdk/client in current project
camera-server install-sdk python    # install alpha-sdk-client
camera-server install-sdk swift     # print SwiftPM snippet
camera-server install-sdk all       # install all three

camera-server playground start      # browser-based interactive playground
camera-server mcp install <agent>   # install Camera Help Guides MCP server
camera-server docs                  # open https://crsdk.app
```

Run `camera-server` with no arguments to launch the interactive TUI.

## ServerManager (Node.js)

A small class for embedding the server lifecycle inside a Node, Electron, or Tauri application.

```ts
import { ServerManager } from "@alpha-sdk/api";

const server = new ServerManager();
await server.start();
console.log(`server on :${server.getPort()}`);

// ... do work against http://localhost:${server.getPort()}/api ...

await server.stop();
```

### Constructor options

```ts
new ServerManager({
  port: 8080,
  binaryPath: "...",
  autoPort: true,
  detached: false,
});
```

### Methods

| Method | Description |
|---|---|
| `start()` → `Promise<void>` | Spawn the binary. Resolves when `/api/server/status` responds 200. |
| `stop()` → `Promise<void>` | Graceful shutdown. Falls back to SIGTERM → SIGKILL if the binary does not exit. |
| `kill()` → `void` | Synchronous no-fallback kill. Useful inside signal handlers. |
| `isRunning()` → `Promise<boolean>` | Pings `/api/server/status`. |
| `getPort()` → `number` | Currently bound port. |
| `getPid()` → `number \| undefined` | Child process PID or detached PID. |
| `getStdout()` → `string[]` | Captured stdout lines. |
| `getStderr()` → `string[]` | Captured stderr lines. |

## End-to-end example

Pair `ServerManager` with the [TypeScript Client SDK]({{ site.baseurl }}/sdk/typescript) — start the server, connect to a camera, change a setting, fire the shutter, list and download the resulting photo, then disconnect:

```ts
import { ServerManager } from "@alpha-sdk/api";
import { AlphaSDKClient } from "@alpha-sdk/client";

const server = new ServerManager();
await server.start();

const client = new AlphaSDKClient({
  environment: `http://localhost:${server.getPort()}`,
});

const { cameras } = await client.cameras.list();
const cameraId = cameras[0].id;

await client.cameras.connect({
  cameraId,
  mode: "remote-transfer",
  reconnecting: "on",
});

await client.properties.setPriorityKey({ cameraId, setting: "pc-remote" });
await client.properties.set({ cameraId, propertyName: "shutter-speed", value: "1/250" });
await client.actions.shutter({ cameraId });

await new Promise(r => setTimeout(r, 1500));
const { files } = await client.sdCard.list({ cameraId, slotNumber: 1 });
const latest = files[files.length - 1];

await client.sdCard.download({
  cameraId,
  slotNumber: 1,
  contentId: latest.contentId,
  fileId: latest.fileId,
  body: {},
});

await client.cameras.disconnect({ cameraId });
await server.stop();
```

## Integration patterns

| Application type | Pattern | Bundling |
|---|---|---|
| Node CLI / script | `ServerManager.start()` | Binary auto-installed via npm. |
| Electron | `ServerManager` + `extraResources` | Binary copied into the app bundle. |
| Tauri | Sidecar | Binary in `src-tauri/binaries/`. |
| Browser-only SPA | Talk to a server running elsewhere | Nothing bundled. |
| iOS / Android | Talk to a server running elsewhere | Nothing bundled. |
| Python / Swift / other languages | Spawn the binary as a subprocess, or run `camera-server start` separately | See the [server subprocess example]({{ site.baseurl }}/sdk/recipes/server-subprocess). |

## Health and control endpoints

{: .info }
> For TypeScript projects, use [`ServerManager`]({{ site.baseurl }}/sdk/typescript) plus the published client and example patterns instead of calling these endpoints directly in application code.


See the auto-generated [API reference]({{ site.baseurl }}/web-api/overview) for full request and response schemas.

### Server status

`GET /api/server/status` returns server version, uptime, platform, and camera counts. `ServerManager` uses this endpoint to confirm the server is ready.

**curl**


```bash
curl http://localhost:8080/api/server/status
```

**Response**


```json
{
  "success": true,
  "server": {
    "version": "3.0.0",
    "sdkVersion": "V2.01.00",
    "uptime": 42,
    "platform": "darwin-arm64"
  },
  "cameras": {
    "connected": 1,
    "discovered": 2
  }
}
```


### Server logs

`GET /api/server/logs` returns buffered server log output. Query params: `lines` (default 100), `level` (default `info`).

**curl**


```bash
curl http://localhost:8080/api/server/logs
```

**Response**


```json
{
  "success": true,
  "logs": [
    {
      "timestamp": "2026-03-02T12:18:45",
      "level": "info",
      "message": "GET /api/server/status"
    }
  ],
  "total": 47
}
```


### Shutdown

`POST /api/server/shutdown` initiates graceful shutdown: disconnects all cameras, closes open SSE streams, then exits. `ServerManager.stop()` calls this before falling back to SIGTERM.

```bash
curl -X POST http://localhost:8080/api/server/shutdown
```

## Embedding in Electron or Tauri

The bundled binary lives at `node_modules/@alpha-sdk/<platform>/CameraWebApp` (or `CameraWebApp.exe` on Windows).

### Electron-builder

```json
{
  "build": {
    "extraResources": [
      {
        "from": "node_modules/@alpha-sdk/${platform}-${arch}",
        "to": "alpha-sdk-binary",
        "filter": ["**/*"]
      }
    ]
  }
}
```

Then in your main process:

```ts
import path from "node:path";
import { ServerManager } from "@alpha-sdk/api";

const server = new ServerManager({
  binaryPath: path.join(
    process.resourcesPath,
    "alpha-sdk-binary",
    process.platform === "win32" ? "CameraWebApp.exe" : "CameraWebApp"
  ),
});
await server.start();
```

### Tauri

Drop the binary into `src-tauri/binaries/CameraWebApp-<triple>` and declare it as a sidecar in `tauri.conf.json`:

```json
{
  "tauri": {
    "bundle": {
      "externalBin": ["binaries/CameraWebApp"]
    }
  }
}
```

## Versioning

`@alpha-sdk/api` follows SemVer.

- **Patch** — bug fixes, bundled-binary updates, playground bundle refreshes.
- **Minor** — new CLI commands, new `ServerManager` methods.
- **Major** — breaking changes to the CLI surface or `ServerManager` API.
