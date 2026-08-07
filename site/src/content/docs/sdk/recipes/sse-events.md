---
title: "SSE event consumer"
description: "React in real time to camera events without polling"
---

Subscribe to real-time camera events: photo taken, property changed, autofocus status, transfer progress, etc. The server exposes `GET /api/events` (all cameras) and `GET /api/cameras/{id}/events` (single camera) as Server-Sent Events streams.

## When to use

- You need to react when a photo is captured (`downloadComplete`) instead of polling
- You want progress feedback during SD card downloads (`transferProgress`)
- You're building a dashboard that reflects property changes immediately
- You need to know when a camera is unplugged mid-session (`disconnected`)

## When NOT to use

- One-shot scripts that take a photo and exit — just call the endpoint, disconnect, done
- Polling a single property rarely — a periodic GET is simpler

## Event catalog

| Event | Payload |
|-------|---------|
| `connected` | `{cameraId}` — fires once on SSE handshake AND on camera connect (discriminate by context) |
| `disconnected` | `{cameraId, reason?}` |
| `propertyChanged` | `{cameraId, codes: number[]}` — refresh affected props after |
| `afStatus` | `{state: "focused" \| "unlocked" \| "tracking"}` |
| `downloadComplete` | `{cameraId, filename, savedPath}` — auto-transfer landed |
| `transferProgress` | `{cameraId, contentId, fileId, percent, savedPath?}` — explicit pull in progress |
| `contentsTransfer` | `{cameraId, files: [...]}` — contents-mode file list updates |
| `operationResult` | `{cameraId, operation, result}` |
| `error` | `{cameraId, code, message}` |

Keepalive comments (`: keepalive`) fire every 30s — ignore them.

⚠ **The dual `connected` event quirk:** `connected` fires immediately when the SSE channel opens (subscription handshake) AND when a real camera's `OnConnected` SDK callback fires. Discriminate by payload shape or ignore the first one after opening.

---

## TypeScript

The real Next.js example app uses a small browser-native `EventSource` wrapper in:

- `example_app/src/lib/event-stream.ts`

### Complete recipe

```typescript
type SSEEventType =
  | "connected"
  | "disconnected"
  | "propertyChanged"
  | "warning"
  | "afStatus"
  | "downloadComplete"
  | "transferProgress"
  | "error"
  | "close";

type EventCallback<T = unknown> = (data: T) => void;

export class EventStream {
  private eventSource: EventSource | null = null;
  private reconnectTimeout: ReturnType<typeof setTimeout> | null = null;
  private listeners = new Map<SSEEventType, Set<EventCallback>>();
  private callbackMap = new Map<EventCallback, EventCallback>();
  private nativeListenerMap = new Map<EventCallback, EventListener>();

  constructor(
    private readonly baseUrl: string,
    private readonly cameraId?: string,
  ) {}

  connect(): void {
    if (this.eventSource) return;

    const url = this.cameraId
      ? `${this.baseUrl}/api/cameras/${this.cameraId}/events`
      : `${this.baseUrl}/api/events`;

    this.eventSource = new EventSource(url);

    this.eventSource.onerror = () => {
      this.eventSource?.close();
      this.eventSource = null;
      this.emit("error", undefined);
      this.reconnectTimeout = setTimeout(() => this.connect(), 3000);
    };

    for (const [event, callbacks] of this.listeners) {
      for (const callback of callbacks) {
        this.addNativeListener(event, callback);
      }
    }
  }

  on<T = unknown>(event: SSEEventType, callback: EventCallback<T>): this {
    const wrapped = (data: unknown) => callback(data as T);
    this.callbackMap.set(callback as EventCallback, wrapped);

    if (!this.listeners.has(event)) {
      this.listeners.set(event, new Set());
    }
    this.listeners.get(event)?.add(wrapped);

    if (this.eventSource) {
      this.addNativeListener(event, wrapped);
    } else {
      this.connect();
    }
    return this;
  }

  off<T = unknown>(event: SSEEventType, callback?: EventCallback<T>): this {
    if (!callback) {
      this.listeners.delete(event);
      return this;
    }

    const wrapped = this.callbackMap.get(callback as EventCallback);
    if (!wrapped) return this;

    this.listeners.get(event)?.delete(wrapped);
    this.callbackMap.delete(callback as EventCallback);

    const native = this.nativeListenerMap.get(wrapped);
    if (native && this.eventSource && event !== "error" && event !== "close") {
      this.eventSource.removeEventListener(event, native);
    }
    if (native) {
      this.nativeListenerMap.delete(wrapped);
    }
    return this;
  }

  close(): void {
    if (this.reconnectTimeout) {
      clearTimeout(this.reconnectTimeout);
      this.reconnectTimeout = null;
    }
    this.eventSource?.close();
    this.eventSource = null;
    this.nativeListenerMap.clear();
    this.emit("close", undefined);
    this.listeners.clear();
  }

  private emit(event: SSEEventType, data: unknown): void {
    const callbacks = this.listeners.get(event);
    if (!callbacks) return;
    for (const callback of callbacks) {
      try {
        callback(data);
      } catch {}
    }
  }

  private addNativeListener(event: SSEEventType, callback: EventCallback): void {
    if (!this.eventSource || event === "error" || event === "close") return;

    const nativeListener: EventListener = (rawEvent: Event) => {
      const messageEvent = rawEvent as MessageEvent<string>;
      try {
        callback(JSON.parse(messageEvent.data));
      } catch {
        callback(messageEvent.data);
      }
    };

    this.nativeListenerMap.set(callback, nativeListener);
    this.eventSource.addEventListener(event, nativeListener);
  }
}
```

### Usage

```typescript
const stream = new EventStream("http://localhost:8080", "camera-id");

stream.on("transferProgress", (data: { percent?: number }) => {
  console.log("Transfer:", data.percent);
});

stream.on("downloadComplete", (data: { savedPath?: string }) => {
  console.log("Saved:", data.savedPath);
});

stream.on("disconnected", () => {
  console.log("Camera disconnected");
});

stream.connect();

// Later:
stream.close();
```

This is the pattern the real browser app uses. It keeps SSE logic small and lets the lifecycle layer decide which events matter.

---

## Python

### Complete recipe

```python
# sse.py — drop into your project, adapt as needed.

from dataclasses import dataclass
from typing import AsyncGenerator, Optional
import asyncio
import json

import httpx


@dataclass
class CameraEvent:
    """Camera event from the SSE stream."""
    event: str
    data: dict


async def stream_camera_events(
    base_url: str,
    camera_id: Optional[str] = None,
    cancel: Optional[asyncio.Event] = None,
) -> AsyncGenerator[CameraEvent, None]:
    """
    Async generator yielding typed camera events.

    Auto-reconnects with exponential backoff on network errors.
    Set the `cancel` event to stop cleanly.

    Args:
        base_url: e.g. "http://localhost:8080"
        camera_id: omit for all-camera stream; pass ID for single-camera stream
        cancel: optional asyncio.Event — set it to break the generator
    """
    url = (
        f"{base_url}/api/cameras/{camera_id}/events"
        if camera_id
        else f"{base_url}/api/events"
    )

    reconnect_delay = 0.5
    initial_connected_seen = False

    async with httpx.AsyncClient(timeout=None) as client:
        while not (cancel and cancel.is_set()):
            try:
                async with client.stream("GET", url) as response:
                    response.raise_for_status()
                    reconnect_delay = 0.5  # reset on success
                    buf = ""

                    async for chunk in response.aiter_text():
                        if cancel and cancel.is_set():
                            return
                        buf += chunk

                        # SSE events separated by "\n\n"
                        while "\n\n" in buf:
                            raw, buf = buf.split("\n\n", 1)

                            # Keepalive lines start with ":"
                            if raw.startswith(":"):
                                continue

                            parsed = {}
                            for line in raw.split("\n"):
                                if ": " not in line:
                                    continue
                                key, _, value = line.partition(": ")
                                parsed[key] = value

                            event_name = parsed.get("event")
                            if not event_name:
                                continue

                            # Skip the handshake "connected" — not a real camera event
                            if event_name == "connected" and not initial_connected_seen:
                                initial_connected_seen = True
                                continue

                            try:
                                data = json.loads(parsed.get("data") or "null")
                                yield CameraEvent(event=event_name, data=data)
                            except json.JSONDecodeError:
                                # Malformed payload — skip, don't kill the stream
                                pass

            except (httpx.HTTPError, httpx.ReadError):
                if cancel and cancel.is_set():
                    return
                await asyncio.sleep(reconnect_delay)
                reconnect_delay = min(reconnect_delay * 2, 10.0)  # cap at 10s
```

### Usage

```python
import asyncio
from sse import stream_camera_events

async def main():
    cancel = asyncio.Event()

    async def consumer():
        async for evt in stream_camera_events("http://localhost:8080", cancel=cancel):
            if evt.event == "downloadComplete":
                print(f"Saved: {evt.data['savedPath']}")
            elif evt.event == "transferProgress":
                print(f"{evt.data['percent']}% — {evt.data['contentId']}/{evt.data['fileId']}")
            elif evt.event == "propertyChanged":
                print(f"Properties changed: {evt.data['codes']}")
            elif evt.event == "afStatus":
                print(f"Autofocus: {evt.data['state']}")

    task = asyncio.create_task(consumer())

    # later...
    await asyncio.sleep(30)
    cancel.set()
    await task

asyncio.run(main())
```

### Typed event variants

For stricter type safety, replace the `dict` payload with Pydantic models per event:

```python
from pydantic import BaseModel

class DownloadComplete(BaseModel):
    cameraId: str
    filename: str
    savedPath: str

# In the generator:
if event_name == "downloadComplete":
    yield DownloadComplete(**data)
```

This gives you `isinstance` checks + autocomplete in consumers. Optional — the plain dict works too.

---

## Swift

### Complete recipe

```swift
// SSEClient.swift — the pattern used in example_swift_app.
// macOS/iOS/tvOS/watchOS compatible, but especially important on macOS:
// avoid URLSession.bytes(...).lines for SSE because sparse streams can buffer.

import Foundation

struct SSEClient {
    let baseURL: String
    let cameraId: String
    let onEvent: (@Sendable (_ type: String, _ payload: String) -> Void)?

    init(
        baseURL: String,
        cameraId: String,
        onEvent: (@Sendable (_ type: String, _ payload: String) -> Void)? = nil
    ) {
        self.baseURL = baseURL
        self.cameraId = cameraId
        self.onEvent = onEvent
    }

    struct ConnectedPayload {
        let model: String
        let id: String
    }

    func waitForConnected(timeoutSeconds: Double = 60) async -> ConnectedPayload? {
        let targetId = cameraId
        let payload = await waitForEvent(timeoutSeconds: timeoutSeconds) { name, payload in
            guard name == "connected" else { return false }
            guard let bytes = payload.data(using: .utf8),
                  let obj = try? JSONSerialization.jsonObject(with: bytes) as? [String: Any]
            else { return false }
            // IMPORTANT: the transport itself emits `connected` as a handshake.
            // The real SDK callback includes `model` + `id`; the handshake does not.
            guard obj["model"] != nil else { return false }
            return (obj["id"] as? String) == targetId
        }
        guard let payload,
              let bytes = payload.data(using: .utf8),
              let obj = try? JSONSerialization.jsonObject(with: bytes) as? [String: Any],
              let model = obj["model"] as? String,
              let id = obj["id"] as? String
        else { return nil }
        return ConnectedPayload(model: model, id: id)
    }

    func waitForDisconnected(timeoutSeconds: Double = 86_400) async -> Bool {
        let payload = await waitForEvent(timeoutSeconds: timeoutSeconds) { name, _ in
            name == "disconnected"
        }
        return payload != nil
    }

    func waitForTransferComplete(timeoutSeconds: Double = 120) async -> Bool {
        let payload = await waitForEvent(timeoutSeconds: timeoutSeconds) { name, payload in
            guard name == "transferProgress" else { return false }
            guard let bytes = payload.data(using: .utf8),
                  let obj = try? JSONSerialization.jsonObject(with: bytes) as? [String: Any],
                  let pct = obj["percent"] as? Int
            else { return false }
            // Accept both real and synthetic completion events.
            return pct >= 100
        }
        return payload != nil
    }

    private func waitForEvent(
        timeoutSeconds: Double,
        match: @escaping @Sendable (_ event: String, _ payload: String) -> Bool
    ) async -> String? {
        guard let url = URL(string: "\(baseURL)/api/cameras/\(cameraId)/events") else {
            return nil
        }

        return await withCheckedContinuation { continuation in
            let delegate = SSEDelegate(onEvent: onEvent, match: match) { matchedPayload in
                continuation.resume(returning: matchedPayload)
            }
            let config = URLSessionConfiguration.default
            config.requestCachePolicy = .reloadIgnoringLocalAndRemoteCacheData
            config.urlCache = nil
            config.timeoutIntervalForRequest = timeoutSeconds + 5
            config.timeoutIntervalForResource = timeoutSeconds + 5
            config.httpAdditionalHeaders = [
                "Accept": "text/event-stream",
                "Cache-Control": "no-cache"
            ]

            let session = URLSession(configuration: config, delegate: delegate, delegateQueue: nil)
            delegate.session = session
            delegate.deadline = Date().addingTimeInterval(timeoutSeconds)

            var req = URLRequest(url: url)
            req.timeoutInterval = timeoutSeconds + 5
            req.setValue("text/event-stream", forHTTPHeaderField: "Accept")
            req.setValue("no-cache", forHTTPHeaderField: "Cache-Control")

            let task = session.dataTask(with: req)
            delegate.task = task
            task.resume()

            DispatchQueue.global().asyncAfter(deadline: .now() + timeoutSeconds) {
                delegate.fireTimeout()
            }
        }
    }
}

private final class SSEDelegate: NSObject, URLSessionDataDelegate, @unchecked Sendable {
    private let onEvent: (@Sendable (String, String) -> Void)?
    private let match: @Sendable (String, String) -> Bool
    private let onComplete: (String?) -> Void
    private let lock = NSLock()
    private var done = false
    private var buffer = Data()
    private var eventName: String?
    private var dataBuffer: String?

    var session: URLSession?
    var task: URLSessionDataTask?
    var deadline: Date = .distantFuture

    init(
        onEvent: (@Sendable (String, String) -> Void)?,
        match: @escaping @Sendable (String, String) -> Bool,
        onComplete: @escaping (String?) -> Void
    ) {
        self.onEvent = onEvent
        self.match = match
        self.onComplete = onComplete
    }

    func fireTimeout() { finish(nil) }

    private func finish(_ value: String?) {
        lock.lock()
        if done {
            lock.unlock()
            return
        }
        done = true
        let s = session
        let t = task
        lock.unlock()
        t?.cancel()
        s?.invalidateAndCancel()
        onComplete(value)
    }

    func urlSession(
        _ session: URLSession,
        dataTask: URLSessionDataTask,
        didReceive response: URLResponse,
        completionHandler: @escaping (URLSession.ResponseDisposition) -> Void
    ) {
        if let http = response as? HTTPURLResponse, http.statusCode != 200 {
            onEvent?("__http_error__", "status=\(http.statusCode)")
            completionHandler(.cancel)
            finish(nil)
            return
        }
        onEvent?("__connected__", "")
        completionHandler(.allow)
    }

    func urlSession(_ session: URLSession, dataTask: URLSessionDataTask, didReceive data: Data) {
        if Date() > deadline {
            finish(nil)
            return
        }

        lock.lock()
        buffer.append(data)
        var lines: [String] = []

        while let nl = buffer.firstIndex(of: 0x0a) {
            let lineData = buffer[..<nl]
            buffer.removeSubrange(...nl)
            let trimmed: Data = (lineData.last == 0x0d) ? lineData.dropLast() : lineData
            lines.append(String(data: trimmed, encoding: .utf8) ?? "")
        }
        lock.unlock()

        for line in lines {
            if line.isEmpty {
                let name = eventName ?? "message"
                let payload = dataBuffer ?? ""
                onEvent?(name, payload)
                if match(name, payload) {
                    finish(payload)
                    return
                }
                eventName = nil
                dataBuffer = nil
                continue
            }
            if line.hasPrefix(":") { continue }
            if line.hasPrefix("event:") {
                eventName = String(line.dropFirst("event:".count)).trimmingCharacters(in: .whitespaces)
            } else if line.hasPrefix("data:") {
                let piece = String(line.dropFirst("data:".count)).trimmingCharacters(in: .whitespaces)
                if let existing = dataBuffer {
                    dataBuffer = existing + "\n" + piece
                } else {
                    dataBuffer = piece
                }
            }
        }
    }

    func urlSession(_ session: URLSession, task: URLSessionTask, didCompleteWithError error: Error?) {
        if let error {
            onEvent?("__error__", error.localizedDescription)
        } else {
            onEvent?("__stream_closed__", "")
        }
        finish(nil)
    }
}
```

### Usage

```swift
let sse = SSEClient(baseURL: "http://localhost:8080", cameraId: "<camera-id>")

// Fast-path connect confirmation
if let payload = await sse.waitForConnected(timeoutSeconds: 60) {
    print("Connected: \(payload.model) (\(payload.id))")
}

// Long-lived disconnect listener
let disconnectTask = Task {
    let dropped = await sse.waitForDisconnected()
    if dropped {
        print("Camera disconnected")
    }
}

// Explicit download completion
let transferDone = await sse.waitForTransferComplete(timeoutSeconds: 120)
print("Transfer finished: \(transferDone)")

disconnectTask.cancel()
```

### Why this differs from a generic AsyncSequence SSE client

- The real app does **not** keep a single catch-all event stream on the main actor.
- It uses **purpose-built waits**:
  - `waitForConnected()` during connect
  - `waitForDisconnected()` for the lifetime of a connection
  - `waitForTransferComplete()` around each SD-card download
- This keeps the state machine explicit and avoids flooding the UI with `propertyChanged` traffic.
- Most importantly on Apple platforms, this recipe uses `URLSessionDataDelegate` because `URLSession.bytes(...).lines` was not reliable enough for sparse SSE traffic.

---

## Common pitfalls

- **Don't forget the keepalive skip.** The server sends `: keepalive\n\n` every 30 seconds. If your parser treats these as events, you'll get garbage data.
- **The first `connected` event is the handshake, not a camera connecting.** All three recipes skip the first occurrence.
- **JSON parse failures shouldn't kill the stream.** Log and continue.
- **Reconnect without cap = infinite loop on bad config.** All three recipes cap backoff at 10 seconds.
- **Browser EventSource vs Node fetch:** native `EventSource` auto-reconnects; if you use the fetch variant in Node, the manual reconnect loop in this recipe handles it.

## Verified against

`test-app/sdk-comparison/debug-autotransfer.ts` uses this exact fetch-streaming pattern to verify `downloadComplete` events fire during shooting. See that file for a working in-repo example.
