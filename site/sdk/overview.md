---
layout: "default"
title: "SDK Reference"
description: "Three language clients + a Node.js server manager — pick what matches your stack"
parent: "SDK Reference"
nav_order: 1
---

The Alpha Camera SDK family is a set of official REST clients for the Alpha Camera REST API. Each is published independently and tracks the same versioned API surface.

| Language | Package | Registry | Reference |
|---|---|---|---|
| TypeScript / JavaScript | [`@alpha-sdk/client`](https://www.npmjs.com/package/@alpha-sdk/client) | npm | [TypeScript SDK]({{ site.baseurl }}/sdk/typescript) |
| Python | [`alpha-sdk-client`](https://pypi.org/project/alpha-sdk-client/) | PyPI | [Python SDK]({{ site.baseurl }}/sdk/python) |
| Swift | [`AlphaSDK`](https://github.com/jordlee/alpha-sdk-swift) | SwiftPM | [Swift SDK]({{ site.baseurl }}/sdk/swift) |

## Example apps

Reference apps that use the published SDKs and server package:

- **Next.js / TypeScript:** [`alpha-sdk-typescript`](https://github.com/jordlee/alpha-sdk-typescript)
- **Python notebook / data collection:** [`alpha-sdk-python`](https://github.com/jordlee/alpha-sdk-python)
- **Swift SDK + macOS sample app:** [`alpha-sdk-swift`](https://github.com/jordlee/alpha-sdk-swift)

{: .note }
> Every client targets the same REST server. To run that server locally, install [`@alpha-sdk/api`]({{ site.baseurl }}/web-api/server) — it ships the native binary, the `camera-server` CLI, and (for Node.js applications) a `ServerManager` class for embedding the server lifecycle. Browser-only or mobile applications connect to a server running elsewhere on the network.


---

## Quick orientation

Every language client follows the same shape:

```
client
  ├── server          health, status, logs
  ├── cameras         list, connect, disconnect, getConnectionStatus
  ├── properties      get, set, getAll, getPriorityKey, setPriorityKey
  ├── actions         shutter, halfPress, afShutter, zoom, focusNearFar, movieRec
  ├── liveView        enable, disable, getStatus, start, stop, getFrame
  ├── sdCard          list, download, downloadThumbnail, downloadScreennail
  └── settings        download, upload, list, importLut
```

The following are intentionally outside the generated client surface — see [recipes](#recipes) for the recommended patterns:

- **Server-Sent Events** — consume with native `EventSource` (browser), `URLSession.bytes(for:)` (Swift), or `httpx.stream` (Python).
- **Discovery, reconnect, and lifecycle helpers** — copy the recipe that fits your application.

---

## Pick your path

  - [**TypeScript**]({{ site.baseurl }}/sdk/typescript) — `npm install @alpha-sdk/client` — Node, Electron, Tauri, browsers.
  - [**Python**]({{ site.baseurl }}/sdk/python) — `pip install alpha-sdk-client` — sync + async clients, Pydantic models.
  - [**Swift**]({{ site.baseurl }}/sdk/swift) — SwiftPM — iOS 15+, macOS 12+, native async/await.

- [**Need to run the server?**]({{ site.baseurl }}/web-api/server) — `@alpha-sdk/api` — installs the REST API server, the `camera-server` CLI, and a Node.js `ServerManager` class.

---

## Recipes

Patterns that live outside the generated SDK surface. Copy and adapt — every recipe shows TypeScript, Python, and Swift side-by-side.

  - [**SSE event consumer**]({{ site.baseurl }}/sdk/recipes/sse-events) — React in real time to `propertyChanged`, `downloadComplete`, `transferProgress`, etc.
  - [**Live-view JPEG polling**]({{ site.baseurl }}/sdk/recipes/live-view-polling) — Pull a frame every ~66ms and render it.
  - [**Server subprocess**]({{ site.baseurl }}/sdk/recipes/server-subprocess) — Spawn the native server from Python or any non-Node runtime.
  - [**Discovery + reconnect**]({{ site.baseurl }}/sdk/recipes/discovery-reconnect) — Poll `/api/cameras`, track which is connected, hot-plug handling.
  - [**Retry + backoff**]({{ site.baseurl }}/sdk/recipes/retry-backoff) — Hand-roll exponential backoff for flaky connect / SD card calls.
  - [**React hook**]({{ site.baseurl }}/sdk/recipes/react-hook) — `useCamera()` — bind one camera's connection + property state.
