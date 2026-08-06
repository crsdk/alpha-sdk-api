---
layout: "default"
title: "Overview"
description: "RESTful control for Sony cameras — shooting, settings, live view, and file transfer"
parent: "REST API"
nav_order: 1
---

{: .note }
> The Alpha Camera REST API is a community-built developer tool and is **not officially supported by Sony**. It is built on Sony's native Camera Remote SDK, but this site focuses on the REST API, client SDKs, and tools.


The Alpha Camera REST API provides HTTP endpoints to control Sony cameras connected via USB or network. It runs as a local server and supports multi-camera simultaneous operation.

## Connection Modes

| Mode | Capabilities |
|------|-------------|
| `remote` | Full camera control — shooting, settings, live view. Auto-transfers images to the host PC. |
| `remote-transfer` | Full camera control + explicit SD card file access. Most capable mode. |
| `contents` | SD card file access only. No shooting or settings control. |

## Architecture

The Alpha Camera REST API is an HTTP + SSE server built on Sony's native Camera Remote SDK. It translates RESTful requests into native SDK calls, enabling camera control from any language or tool that can make HTTP requests.

If you'd rather call typed methods than build URLs by hand, three [language clients]({{ site.baseurl }}/sdk/overview) wrap this API:

- **TypeScript / Node** — [`@alpha-sdk/client`]({{ site.baseurl }}/sdk/typescript)
- **Python** — [`alpha-sdk-client`]({{ site.baseurl }}/sdk/python)
- **Swift (iOS / macOS)** — [`AlphaSDK`]({{ site.baseurl }}/sdk/swift)

To run the server itself: `npm install -g @alpha-sdk/api && camera-server start`. Or embed it in your Node app via the [`ServerManager`]({{ site.baseurl }}/web-api/server) class.

## Quick Start

- [**Alpha Camera REST API overview**]({{ site.baseurl }}/web-api/overview) — Discover, connect, and shoot with curl in under 5 minutes

**1. Discover cameras**

```bash
curl http://localhost:8080/api/cameras
```

**2. Connect**

```bash
curl -X POST http://localhost:8080/api/cameras/{id}/connection \
  -H "Content-Type: application/json" \
  -d '{"mode": "remote"}'
```

**3. Set Priority Key**

```bash
curl -X PUT http://localhost:8080/api/cameras/{id}/priority-key \
  -H "Content-Type: application/json" \
  -d '{"setting": "pc-remote"}'
```
{: .warning }
> Priority key **must** be set to `pc-remote` before the camera accepts remote commands.


**4. Configure settings**

```bash
curl -X PUT http://localhost:8080/api/cameras/{id}/properties/iso \
  -H "Content-Type: application/json" \
  -d '{"value": "400"}'
```

**5. Shoot**

```bash
curl -X POST http://localhost:8080/api/cameras/{id}/actions/af-shutter
```

**6. Disconnect**

```bash
curl -X DELETE http://localhost:8080/api/cameras/{id}/connection
```


## Base URL

```
http://localhost:8080
```

The server runs locally. The port is configurable via `--port` flag or the TypeScript `CameraServer({ port })` option.

## Response Format

All endpoints return JSON with a consistent shape:

```json
{
  "success": true,
  "message": "Description of what happened",
  "camera": {
    "connected": true,
    "model": "ILCE-9M3",
    "id": "D10F60149B0C"
  },
  "data": { ... }
}
```

Errors return `"success": false` with an HTTP status of `400`, `404`, or `500`.

## Compatibility

Not all APIs are supported on every camera model. See the [Compatibility]({{ site.baseurl }}/web-api/compatibility) page for per-camera API support, connection mode requirements, and OS compatibility.
