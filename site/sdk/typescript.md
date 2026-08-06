---
layout: "default"
title: "TypeScript SDK"
description: "@alpha-sdk/client — typed REST client for Node, Electron, Tauri, and browsers"
parent: "SDK Reference"
nav_order: 2
---

`@alpha-sdk/client` is the official TypeScript client for the Alpha Camera REST API. It ships ESM + CJS bundles and has zero runtime dependencies beyond its own resource clients.

- **npm:** [`@alpha-sdk/client`](https://www.npmjs.com/package/@alpha-sdk/client)
- **Latest:** `0.3.0`
- **Example app:** [`alpha-sdk-typescript`](https://github.com/jordlee/alpha-sdk-typescript)

---

## Install

```bash
npm install @alpha-sdk/client
```

You also need a running server on `localhost:8080` (or wherever you point the client). Install the [server package]({{ site.baseurl }}/web-api/server) and start it:

```bash
npm install -g @alpha-sdk/api
camera-server start
```

For embedded server lifecycle inside a Node, Electron, or Tauri app, see the [`ServerManager` class]({{ site.baseurl }}/web-api/server#servermanager-nodejs).

---

## Initialize

```ts
import { AlphaSDKClient } from "@alpha-sdk/client";

const client = new AlphaSDKClient({
  environment: "http://localhost:8080",
});
```

### Constructor options

```ts
new AlphaSDKClient({
  environment: "http://localhost:8080",   // base URL — defaults to localhost:8080
  baseUrl:     "http://192.168.1.50:8080", // alternative to `environment`
  headers:     { "X-Trace-Id": "abc123" }, // applied to every request
  timeoutInSeconds: 30,
  maxRetries:  3,
  fetch:       customFetch,                // override the fetch implementation
  logging:     { level: "debug" },         // wire-level request/response logging
});
```

---

## Common operations

### List + connect a camera

```ts
const { cameras } = await client.cameras.list();
const cameraId = cameras[0].id;

await client.cameras.connect({
  cameraId,
  mode: "remote-transfer",
  reconnecting: "on",
});

// Required for the host PC to drive most settings:
await client.properties.setPriorityKey({ cameraId, setting: "pc-remote" });
```

### Read a single property

```ts
const aperture = await client.properties.get({
  cameraId,
  propertyName: "aperture",
});
console.log(aperture.data.value, aperture.data.formatted);
```

### Read every property at once

```ts
const all = await client.properties.getAll({ cameraId });
console.log(all.data.properties.iso.currentFormatted);
```

### Set a property

```ts
await client.properties.set({
  cameraId,
  propertyName: "shutter-speed",
  value: "1/250",
});
```

### Trigger the shutter

```ts
await client.actions.shutter({ cameraId });
// In `remote` mode the server auto-transfers; watch SSE for `downloadComplete`.
// In `remote-transfer` mode call client.sdCard.download(...) explicitly.
```

### Pull a single live-view JPEG

```ts
const res = await client.liveView.getFrame({ cameraId });
const jpeg = await res.arrayBuffer();   // also: .blob(), .bytes(), .stream()
```

For OSD-overlaid frames after `liveView.enableOsd({ cameraId })`:

```ts
const res = await client.liveView.getOsdFrame({ cameraId });
const blob = await res.blob();
```

See the [live-view polling recipe]({{ site.baseurl }}/sdk/recipes/live-view-polling) for a render loop.

### Disconnect

```ts
await client.cameras.disconnect({ cameraId });
```

---

## Error handling

Every method rejects with a typed subclass of `AlphaSDKError`:

```ts
import {
  AlphaSDKError,
  BadRequestError,
  NotFoundError,
  InternalServerError,
} from "@alpha-sdk/client";
try {
  await client.properties.set({
    cameraId,
    propertyName: "aperture",
    value: "F1.0", // not a valid value on this lens
  });
} catch (err) {
  if (err instanceof BadRequestError) {
    console.error("server rejected the value:", err.message);
  } else if (err instanceof NotFoundError) {
    console.error("camera no longer connected");
  } else if (err instanceof InternalServerError) {
    console.error("server error:", err.statusCode, err.message);
  } else if (err instanceof AlphaSDKError) {
    console.error("transport/client error:", err.message);
  }
}
```

Each error exposes `statusCode`, `body` (raw response), and `message`. Network failures and timeouts throw the base `AlphaSDKError` with `statusCode: undefined`.

---

## Advanced

### Custom request headers per call

```ts
await client.cameras.list({
  headers: { "X-Trace-Id": "request-42" },
});
```

### Override the underlying fetch

Useful for proxying, caching, or test harnesses:

```ts
const client = new AlphaSDKClient({
  environment: "http://localhost:8080",
  fetch: (url, init) => myCustomFetch(url, init),
});
```

### Wire-level logging

```ts
const client = new AlphaSDKClient({
  environment: "http://localhost:8080",
  logging: { level: "debug" }, // logs every request + response body
});
```

### Cancellation

Pass an `AbortSignal` via the per-call request options:

```ts
const ac = new AbortController();
setTimeout(() => ac.abort(), 5000);

await client.properties.getAll({
  cameraId,
}, { abortSignal: ac.signal });
```

---

## What's NOT in the client

By design, the following live in [recipes]({{ site.baseurl }}/sdk/overview#recipes) rather than the generated SDK:

- **SSE events** (`/api/events`, `/api/cameras/{id}/events`) — use native `EventSource` in the browser, or `fetch` + `ReadableStream` in Node 18+.
- **Discovery + reconnect orchestration** — copy from the [discovery + reconnect recipe]({{ site.baseurl }}/sdk/recipes/discovery-reconnect).

---

## Versioning

`@alpha-sdk/client` follows SemVer. Bumps:

- **Patch** — additive non-breaking changes (new optional fields).
- **Minor** — new endpoints or method signatures.
- **Major** — breaking changes.

Every release is documented in the package CHANGELOG, available on the npm page.

---

## Migrating from `camera-remote-web-api`

`camera-remote-web-api` is deprecated and will not receive updates after `1.3.7`. Migration table:

| Old | New |
|---|---|
| `import { CameraClient } from "camera-remote-web-api"` | `import { AlphaSDKClient } from "@alpha-sdk/client"` |
| `import { ServerManager } from "camera-remote-web-api/server"` | `import { ServerManager } from "@alpha-sdk/api"` |
| `import { EventStream } from "camera-remote-web-api/server"` | See [SSE events recipe]({{ site.baseurl }}/sdk/recipes/sse-events) |
| `import { LiveViewStream } from "camera-remote-web-api/server"` | See [live-view polling recipe]({{ site.baseurl }}/sdk/recipes/live-view-polling) |
| `import { CameraManager } from "camera-remote-web-api/server"` | See [discovery + reconnect recipe]({{ site.baseurl }}/sdk/recipes/discovery-reconnect) |
| `client.list()` | `client.cameras.list()` |
| `client.connect(...)` | `client.cameras.connect({ cameraId, ... })` |
| `client.getProperty(camId, name)` | `client.properties.get({ cameraId, propertyName })` |
