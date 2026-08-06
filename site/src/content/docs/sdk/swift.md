---
title: "Swift SDK"
description: "AlphaSDK — native async/await client for iOS, macOS, tvOS, watchOS"
---

`AlphaSDK` is the official Swift client for the Alpha Camera REST API. It uses `URLSession` only (zero third-party dependencies) and exposes Swift concurrency throughout.

- **SwiftPM:** [`https://github.com/jordlee/alpha-sdk-swift.git`](https://github.com/jordlee/alpha-sdk-swift)
- **Latest:** `0.3.1`
- **Platforms:** iOS 15+, macOS 12+, tvOS 15+, watchOS 8+
- **Swift:** 5.7+
- **SDK repo + macOS sample app:** [`alpha-sdk-swift`](https://github.com/jordlee/alpha-sdk-swift)

---

## Install

Add to your `Package.swift` `dependencies`:

```swift
.package(url: "https://github.com/jordlee/alpha-sdk-swift.git", from: "0.3.1"),
```

Then add `AlphaSDK` to your target's dependencies:

```swift
.target(
    name: "MyApp",
    dependencies: [
        .product(name: "AlphaSDK", package: "alpha-sdk-swift"),
    ]
)
```

In Xcode: **File → Add Package Dependencies…**, paste the URL, choose `from 0.3.1`.

You also need a server reachable from the device. The Swift client does **not** ship a `ServerManager` class — start the server using the Node-based CLI from the [server package](/alpha-sdk-api/web-api/server/), or, on macOS, spawn the bundled binary from your application using `Process` ([recipe](/alpha-sdk-api/sdk/recipes/server-subprocess/#macos)). iOS applications typically connect over the local network to a server running on a Mac, Raspberry Pi, or similar host.

---

## Initialize

```swift
import AlphaSDK

let client = AlphaSDKClient(baseURL: "http://localhost:8080")
```

### Constructor options

```swift
let client = AlphaSDKClient(
    baseURL: "http://192.168.1.50:8080",      // base URL — defaults to localhost:8080
    headers: ["X-Trace-Id": "abc123"],         // applied to every request
    timeout: 30,                               // seconds
    maxRetries: 3,
    urlSession: .shared                        // bring your own URLSession
)
```

`AlphaSDKClient` is `Sendable` and safe to share across actors.

---

## Common operations

### List + connect a camera

```swift
let cameras = try await client.cameras.list()
guard let cameraId = cameras.cameras.first?.id else { return }

try await client.cameras.connect(
    cameraId: cameraId,
    request: .init(mode: .remoteTransfer, reconnecting: .on)
)

// Required for the host PC to drive most settings:
try await client.properties.setPriorityKey(
    cameraId: cameraId,
    request: .init(setting: .pcRemote)
)
```

### Read a single property

```swift
let aperture = try await client.properties.get(
    cameraId: cameraId,
    propertyName: "aperture"
)
print(aperture.data.value, aperture.data.formatted)
```

### Read every property at once

```swift
let res = try await client.properties.getAll(cameraId: cameraId)
for (name, entry) in res.data.properties {
    print(name, entry.currentFormatted, "writable=\(entry.writable)")
}
// `properties` is a [String: BulkPropertyEntry]; entries use the
// `current_*` field shape (NOT the same as single-property GET).
```

### Set a property

```swift
try await client.properties.set(
    cameraId: cameraId,
    propertyName: "shutter-speed",
    request: .init(value: "1/250")
)
```

### Trigger the shutter

```swift
// Single shot — pass an empty request body.
try await client.actions.shutter(cameraId: cameraId, request: .init())

// Continuous shooting (set drive mode to a continuous mode first):
try await client.actions.shutter(cameraId: cameraId, request: .init(action: .down))
// ... hold ...
try await client.actions.shutter(cameraId: cameraId, request: .init(action: .up))
```

### Pull a single live-view JPEG

```swift
let data: Data = try await client.liveView.getFrame(cameraId: cameraId)
#if canImport(UIKit)
let image = UIImage(data: data)
#elseif canImport(AppKit)
let image = NSImage(data: data)
#endif
```

For OSD-overlaid frames after `try await client.liveView.enableOsd(cameraId: cameraId)`:

```swift
let data = try await client.liveView.getOsdFrame(cameraId: cameraId)
```

See the [live-view polling recipe](/alpha-sdk-api/sdk/recipes/live-view-polling/) for a render loop.

### Disconnect

```swift
try await client.cameras.disconnect(cameraId: cameraId)
```

---

## Error handling

Every method throws `AlphaSDKError`, an enum with cases for transport
failures and HTTP responses. Pattern-match the case (or, for HTTP responses,
classify with `HTTPError.kind`):

```swift
import AlphaSDK

do {
    try await client.properties.set(
        cameraId: cameraId,
        propertyName: "aperture",
        request: .init(value: "F1.0")  // not a valid value on this lens
    )
} catch let AlphaSDKError.httpError(http) {
    switch http.kind {
    case .validation, .client:
        print("server rejected the value (\(http.statusCode)):", http.body?.message ?? "")
    case .notFound:
        print("camera no longer connected")
    default:
        print("server error \(http.statusCode):", http.body?.message ?? "")
    }
} catch AlphaSDKError.timeout {
    print("request timed out")
} catch let AlphaSDKError.networkError(underlying) {
    print("network error:", underlying)
} catch {
    print("unexpected error:", error)
}
```

`HTTPError` exposes `statusCode`, parsed `body` (with `message` / `code` / `type`),
and `kind` (`.notFound`, `.validation`, `.client`, `.server`, ...). All other
failure modes are dedicated cases on `AlphaSDKError` itself
(`.timeout`, `.networkError`, `.decodingError`, ...).

---

## Advanced

### Custom request headers per call

```swift
try await client.cameras.list(
    requestOptions: .init(additionalHeaders: ["X-Trace-Id": "request-42"])
)
```

### Cancellation

Swift's structured concurrency does the right thing — wrap the call in a `Task` and cancel:

```swift
let task = Task {
    try await client.properties.getAll(cameraId: cameraId)
}
// elsewhere:
task.cancel()
```

### Bring your own URLSession

Useful for proxying, intercepting, or testing:

```swift
let config = URLSessionConfiguration.default
config.protocolClasses = [MyMockURLProtocol.self]
let urlSession = URLSession(configuration: config)

let client = AlphaSDKClient(
    baseURL: "http://localhost:8080",
    urlSession: urlSession
)
```

---

## What's NOT in the client

By design, the following live in [recipes](/alpha-sdk-api/sdk/overview/#recipes) rather than the generated SDK:

- **SSE events** — use `URLSession.bytes(for:)` to stream `/api/events` and parse `text/event-stream` line-by-line. See the [SSE events recipe](/alpha-sdk-api/sdk/recipes/sse-events/#swift).
- **Discovery + reconnect orchestration** — copy from the [discovery + reconnect recipe](/alpha-sdk-api/sdk/recipes/discovery-reconnect/).

---

## Versioning

`AlphaSDK` follows SemVer. Every release is git-tagged on the SwiftPM repository — see the CHANGELOG there for release notes.
