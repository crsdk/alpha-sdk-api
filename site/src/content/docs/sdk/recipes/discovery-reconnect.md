---
title: "Discovery + reconnect"
description: "Track connected cameras across plug/unplug cycles"
---

Poll `GET /api/cameras` to detect USB hot-plug events, auto-connect new cameras, and reconnect if one drops. The Alpha Camera REST server enumerates cameras every time you call `/api/cameras`, so regular polling is the idiomatic way to track physical state.

## When to use

- Your app should handle USB cable unplug/replug gracefully
- You want to auto-connect the first camera that appears
- You're running 24/7 and need resilience against transient disconnects
- You want to track multiple cameras and react when one drops

## When NOT to use

- One-shot scripts — just call `list()` once, grab the first camera, proceed
- Apps where the user explicitly picks the camera from a list — polling only helps if state is changing

---

## TypeScript

The real Next.js example collapses polling + SSE + reconnect into one small lifecycle class:

- `example_app/src/lib/camera-manager.ts`

### Complete recipe

```typescript
import { CameraManager } from "./camera-manager";

const manager = new CameraManager({
  baseUrl: "http://localhost:8080",
  pollInterval: 5000,
  autoConnect: true,
  connectionMode: "remote-transfer",
  autoReconnect: true,
  maxReconnectAttempts: 5,
});

manager.on("camera-found", ({ camera }) => {
  console.log("found", camera.model, camera.id);
});

manager.on("camera-ready", ({ cameraId, camera }) => {
  console.log("ready", camera.model, cameraId);
});

manager.on("camera-disconnected", ({ cameraId, error }) => {
  console.log("disconnected", cameraId, error);
});

manager.on("connection-failed", ({ cameraId, error, attempt }) => {
  console.error("connect failed", cameraId, attempt, error);
});

await manager.start();
```

### Why this is the right TypeScript shape

- polling alone is too slow for good connect/disconnect UX
- SSE alone is not enough to recover newly reappeared cameras
- reconnect timing belongs in one place, not spread across React components

The real manager does all of the following:

- polls `GET /api/cameras`
- keeps diffed camera state
- opens a global SSE stream for connect/disconnect edges
- opens per-camera event streams on demand
- waits for SSE-connected confirmation after `connect()`
- retries reconnect with capped backoff

If you are building a React app, use this lifecycle layer under your components instead of duplicating polling logic in the page.

---

## Python

### Complete recipe

```python
# discovery.py — async polling with diff events.

import asyncio
from dataclasses import dataclass
from typing import Awaitable, Callable, Optional


@dataclass
class CameraInfo:
    id: str
    model: str
    connectionType: str
    connected: bool


OnAppear = Callable[[CameraInfo], Awaitable[None]]
OnDisappear = Callable[[str], Awaitable[None]]
OnError = Callable[[BaseException], Awaitable[None]]


async def watch_cameras(
    client,  # AsyncAlphaSDKClient
    *,
    interval_s: float = 3.0,
    on_appear: Optional[OnAppear] = None,
    on_disappear: Optional[OnDisappear] = None,
    on_error: Optional[OnError] = None,
    cancel: Optional[asyncio.Event] = None,
) -> None:
    """
    Poll client.cameras.list() every interval_s seconds.
    Fires on_appear/on_disappear callbacks when cameras change.
    """
    previous: dict[str, CameraInfo] = {}

    while not (cancel and cancel.is_set()):
        try:
            listing = await client.cameras.list()
            current = {
                cam.id: CameraInfo(
                    id=cam.id,
                    model=cam.model,
                    connectionType=getattr(cam, "connectionType", "USB"),
                    connected=cam.connected,
                )
                for cam in (listing.cameras or [])
                if cam.id
            }

            # Appearances
            for cam_id, cam in current.items():
                if cam_id not in previous and on_appear:
                    await on_appear(cam)

            # Disappearances
            for cam_id in previous.keys():
                if cam_id not in current and on_disappear:
                    await on_disappear(cam_id)

            previous = current

        except BaseException as err:
            if on_error:
                await on_error(err)

        # Interruptible sleep
        try:
            await asyncio.wait_for(
                (cancel.wait() if cancel else asyncio.Future()),
                timeout=interval_s,
            )
            return  # cancel set during sleep
        except asyncio.TimeoutError:
            continue
```

### Usage — auto-connect first camera

```python
import asyncio
from alpha_sdk_client import AsyncAlphaSDKClient
from discovery import watch_cameras, CameraInfo

async def main():
    client = AsyncAlphaSDKClient(base_url="http://localhost:8080")

    async def on_appear(cam: CameraInfo):
        print(f"Camera appeared: {cam.model} ({cam.id})")
        if not cam.connected:
            await client.cameras.connect(cam.id, mode="remote")
            await asyncio.sleep(0.5)  # settle
            await client.properties.set_priority_key(cam.id, setting="pc-remote")
            print(f"Auto-connected {cam.id}")

    async def on_disappear(cam_id: str):
        print(f"Camera {cam_id} gone")

    async def on_error(err):
        print(f"Discovery retry after error: {err}")

    cancel = asyncio.Event()
    await watch_cameras(
        client,
        interval_s=3.0,
        on_appear=on_appear,
        on_disappear=on_disappear,
        on_error=on_error,
        cancel=cancel,
    )

asyncio.run(main())
```

---

## Swift

### Complete recipe

```swift
// DiscoveryController.swift — mirrors the real app's poll + SSE hybrid.

import Foundation
import AlphaSDK

@MainActor
final class DiscoveryController: ObservableObject {
    @Published private(set) var cameras: [CameraInfo] = []
    @Published private(set) var connectedCameraId: String?

    private let client: AlphaSDKClient
    private let baseURL: String

    private var discoveryTask: Task<Void, Never>?
    private var disconnectListenerTask: Task<Void, Never>?
    private var connecting: Set<String> = []
    private var missedConnectedTicks = 0
    private var consecutiveListErrors = 0

    // Match the real app: 5s polls, require two consecutive negative
    // ticks before declaring a previously-connected camera gone.
    private static let pollIntervalNs: UInt64 = 5_000_000_000
    private static let connectionDropThreshold = 2

    // Example app also suppresses discovery while downloads are active.
    // Keep this flag here so call sites can gate polling if they have
    // long-running transfer work.
    var shouldSkipDiscoveryTick = false

    init(client: AlphaSDKClient, baseURL: String) {
        self.client = client
        self.baseURL = baseURL
    }

    func start() {
        discoveryTask?.cancel()
        discoveryTask = Task { [weak self] in
            while !Task.isCancelled {
                guard let self else { return }
                if !shouldSkipDiscoveryTick {
                    await refreshCameras()
                }
                try? await Task.sleep(nanoseconds: Self.pollIntervalNs)
            }
        }
    }

    func stop() {
        discoveryTask?.cancel()
        disconnectListenerTask?.cancel()
        discoveryTask = nil
        disconnectListenerTask = nil
    }

    func refreshCameras() async {
        do {
            let response = try await client.cameras.list(
                requestOptions: RequestOptions(timeout: 10)
            )
            cameras = response.cameras

            let serverConnected = response.cameras.first(where: { $0.connected })

            if let cam = serverConnected {
                missedConnectedTicks = 0
                if connectedCameraId != cam.id {
                    connectedCameraId = cam.id
                    await postConnectSetup(cameraId: cam.id)
                }
            } else if let first = response.cameras.first,
                      !connecting.contains(first.id),
                      connectedCameraId == nil {
                await connect(first.id)
            }

            if let id = connectedCameraId {
                let stillListed = response.cameras.contains(where: { $0.id == id })
                if !stillListed {
                    handleDisconnect()
                    missedConnectedTicks = 0
                } else if serverConnected?.id != id {
                    missedConnectedTicks += 1
                    if missedConnectedTicks >= Self.connectionDropThreshold {
                        handleDisconnect()
                        missedConnectedTicks = 0
                    }
                }
            }
            consecutiveListErrors = 0
        } catch {
            consecutiveListErrors += 1
            let backoff = min(8, 2 * (consecutiveListErrors - 1))
            if backoff > 0 {
                try? await Task.sleep(nanoseconds: UInt64(backoff) * 1_000_000_000)
            }
        }
    }

    func connect(_ cameraId: String) async {
        guard !connecting.contains(cameraId) else { return }
        connecting.insert(cameraId)
        defer { connecting.remove(cameraId) }

        // Real app pattern: subscribe BEFORE connect so the SDK's
        // OnConnected callback can win the race against the next 5s poll.
        let sse = SSEClient(baseURL: baseURL, cameraId: cameraId, onEvent: nil)
        let connectedTask = Task<SSEClient.ConnectedPayload?, Never> {
            await sse.waitForConnected(timeoutSeconds: 60)
        }
        try? await Task.sleep(nanoseconds: 200_000_000)

        do {
            _ = try await client.cameras.connect(
                cameraId: cameraId,
                request: Requests.ConnectionRequest(mode: .remoteTransfer, reconnecting: .off)
            )

            if let payload = await connectedTask.value {
                connectedCameraId = payload.id
                cameras = cameras.map { cam in
                    cam.id == payload.id
                        ? CameraInfo(
                            id: cam.id,
                            model: cam.model,
                            connectionType: cam.connectionType,
                            connected: true,
                            additionalProperties: cam.additionalProperties
                        )
                        : cam
                }
                await postConnectSetup(cameraId: payload.id)
            } else {
                // Fallback path if the SSE listener missed the event.
                try? await Task.sleep(nanoseconds: 1_000_000_000)
                await refreshCameras()
            }
        } catch {
            connectedTask.cancel()
        }
    }

    private func postConnectSetup(cameraId: String) async {
        startDisconnectListener(cameraId: cameraId)
        do {
            _ = try await client.properties.setPriorityKey(
                cameraId: cameraId,
                request: Requests.SetPriorityKeyRequest(setting: .pcRemote)
            )
        } catch {}
        // The real app also enables/starts live view and begins
        // property/live-view polling here.
    }

    private func startDisconnectListener(cameraId: String) {
        disconnectListenerTask?.cancel()
        disconnectListenerTask = Task { [weak self] in
            guard let self else { return }
            let sse = SSEClient(baseURL: baseURL, cameraId: cameraId, onEvent: nil)
            let fired = await sse.waitForDisconnected()
            await MainActor.run {
                guard !Task.isCancelled else { return }
                guard self.connectedCameraId == cameraId else { return }
                if fired { self.handleDisconnect() }
            }
        }
    }

    private func handleDisconnect() {
        let priorId = connectedCameraId
        connectedCameraId = nil
        disconnectListenerTask?.cancel()
        disconnectListenerTask = nil
        if let priorId {
            cameras = cameras.map { cam in
                cam.id == priorId
                    ? CameraInfo(
                        id: cam.id,
                        model: cam.model,
                        connectionType: cam.connectionType,
                        connected: false,
                        additionalProperties: cam.additionalProperties
                    )
                    : cam
            }
        }
    }
}
```

### Usage

```swift
let client = AlphaSDKClient(baseURL: "http://localhost:8080", timeout: 60)
let controller = DiscoveryController(client: client, baseURL: "http://localhost:8080")
controller.start()

// Stop later:
controller.stop()
```

---

## Combining with SSE for best UX

Polling catches hot-plug events within the poll interval (5s in the real macOS app). SSE reports disconnects instantly. Ideal pattern:

- **Polling** — detects new cameras and performs eventual consistency / recovery
- **SSE `connected`** — fast-paths connect completion so the UI does not wait for the next 5s poll
- **SSE `disconnected`** — instant teardown when a camera drops mid-session

You don't need both if your use case is simple. The real app uses both because 5-second polling alone feels too slow for connect/disconnect edges.

## Common pitfalls

- **Polling alone is not enough for good UX.** The real app combines 5s discovery polling with short-lived SSE listeners around connect and a long-lived `disconnected` listener.
- **Callbacks can throw.** Wrap your `onAppear` logic in try/catch — otherwise one bad camera kills the discovery loop.
- **Don't poll too fast.** The real app uses 5s. `/api/cameras` can itself take several seconds because the server performs a fresh discovery scan.
- **Do not assume one negative `connected:false` means dropped.** The real app requires two consecutive negative polls because the camera may transiently report not-connected during post-connect setup.
- **Skip discovery during transfers if your app can.** The example app suppresses camera-list polling while SD downloads are active because the SDK serializes those operations per camera.

## Verified against

The Swift section mirrors the connection/discovery lifecycle in `example_swift_app/Sources/AlphaCameraExample/AppCoordinator.swift`.
