---
title: "Live-view JPEG polling"
description: "Pull JPEG frames at ~15fps and render them"
---

Pull JPEG frames from the camera's live-view stream at a configurable rate. The server exposes `GET /api/cameras/{id}/live-view/frame` returning `image/jpeg` — binary data that sits outside the typed SDK surface. This recipe is the "get frames" pattern.

## When to use

- Showing a preview in your UI (a `<canvas>` or `<img>` that updates ~15fps)
- Feeding frames to ML inference
- Recording a timelapse from the live view feed
- Any case where you want the camera's viewfinder, not a captured photo

## Prerequisites

The camera must be connected in `remote` or `remote-transfer` mode, and live view must be enabled + started:

```typescript
await client.cameras.connect({ cameraId, mode: "remote" });
await client.properties.setPriorityKey({ cameraId, setting: "pc-remote" });
await client.liveView.enable({ cameraId });
await client.liveView.start({ cameraId });
// Now you can pull frames.
```

Frames are ~70-80 KB each on the A7 IV. At 15fps that's ~1.2 MB/s — tolerable over USB, not great over WiFi.

---

## TypeScript

### Complete recipe

```typescript
// live-view.ts — async generator of JPEG frames.

/**
 * Async generator yielding JPEG frames as Uint8Array.
 *
 * Stops when the signal is aborted. Swallows transient errors (404 "no
 * frame yet", network blips) so consumers don't have to handle them.
 */
export async function* pollLiveViewFrames(
  baseUrl: string,
  cameraId: string,
  options: {
    intervalMs?: number;
    signal?: AbortSignal;
  } = {},
): AsyncGenerator<Uint8Array> {
  const interval = options.intervalMs ?? 66; // ~15fps
  const signal = options.signal;
  const url = `${baseUrl}/api/cameras/${cameraId}/live-view/frame`;

  while (!signal?.aborted) {
    const iterStart = Date.now();

    try {
      const res = await fetch(url, { signal });
      if (res.ok) {
        const buf = new Uint8Array(await res.arrayBuffer());
        // Basic sanity check: valid JPEG starts with FF D8 FF
        if (buf.length >= 3 && buf[0] === 0xff && buf[1] === 0xd8 && buf[2] === 0xff) {
          yield buf;
        }
      }
      // 404 = no frame yet (e.g. live view just started); ignore & retry
    } catch (err) {
      if (signal?.aborted) return;
      // Network blip — backoff slightly so we don't hammer the server
      await new Promise((r) => setTimeout(r, 500));
      continue;
    }

    // Pace the loop. If the fetch took longer than the interval, skip the sleep.
    const elapsed = Date.now() - iterStart;
    const sleep = Math.max(0, interval - elapsed);
    if (sleep > 0) await new Promise((r) => setTimeout(r, sleep));
  }
}
```

### Usage — write frames to disk

```typescript
import { writeFileSync } from "fs";
import { pollLiveViewFrames } from "./live-view";

const controller = new AbortController();
let i = 0;

(async () => {
  for await (const frame of pollLiveViewFrames("http://localhost:8080", camId, { signal: controller.signal })) {
    writeFileSync(`frame-${i++}.jpg`, frame);
    if (i >= 100) controller.abort(); // capture 100 frames then stop
  }
})();
```

### Usage — render to a `<canvas>` in the browser

```typescript
const canvas = document.querySelector<HTMLCanvasElement>("#preview")!;
const ctx = canvas.getContext("2d")!;

(async () => {
  for await (const frame of pollLiveViewFrames("http://localhost:8080", camId)) {
    const blob = new Blob([frame], { type: "image/jpeg" });
    const bitmap = await createImageBitmap(blob);
    canvas.width = bitmap.width;
    canvas.height = bitmap.height;
    ctx.drawImage(bitmap, 0, 0);
    bitmap.close();
  }
})();
```

### Usage — render to an `<img>` (simpler, no ImageBitmap)

```typescript
const img = document.querySelector<HTMLImageElement>("#preview")!;
let url: string | null = null;

(async () => {
  for await (const frame of pollLiveViewFrames("http://localhost:8080", camId)) {
    if (url) URL.revokeObjectURL(url); // free the previous blob
    url = URL.createObjectURL(new Blob([frame], { type: "image/jpeg" }));
    img.src = url;
  }
})();
```

---

## Python

### Complete recipe

```python
# live_view.py — async generator of JPEG frame bytes.

import asyncio
from typing import AsyncGenerator, Optional

import httpx


async def poll_live_view_frames(
    base_url: str,
    camera_id: str,
    interval_s: float = 0.066,  # ~15fps
    cancel: Optional[asyncio.Event] = None,
) -> AsyncGenerator[bytes, None]:
    """
    Yields JPEG frame bytes. Swallows transient 404s and network blips.
    Set the `cancel` event to stop cleanly.
    """
    url = f"{base_url}/api/cameras/{camera_id}/live-view/frame"

    async with httpx.AsyncClient(timeout=5.0) as client:
        while not (cancel and cancel.is_set()):
            iter_start = asyncio.get_event_loop().time()
            try:
                response = await client.get(url)
                if response.status_code == 200:
                    frame = response.content
                    # Valid JPEG starts with FF D8 FF
                    if len(frame) >= 3 and frame[:3] == b"\xff\xd8\xff":
                        yield frame
                # 404 = no frame yet; ignore and retry
            except httpx.HTTPError:
                if cancel and cancel.is_set():
                    return
                await asyncio.sleep(0.5)
                continue

            # Pace the loop
            elapsed = asyncio.get_event_loop().time() - iter_start
            sleep = max(0.0, interval_s - elapsed)
            if sleep > 0:
                await asyncio.sleep(sleep)
```

### Usage — write frames to disk

```python
import asyncio
from pathlib import Path
from live_view import poll_live_view_frames

async def main():
    out = Path("./frames")
    out.mkdir(exist_ok=True)
    i = 0
    async for frame in poll_live_view_frames("http://localhost:8080", "D06CE00004C4"):
        (out / f"frame-{i:04d}.jpg").write_bytes(frame)
        i += 1
        if i >= 100:
            break

asyncio.run(main())
```

### Usage — stream to ML inference

```python
async def main():
    async for frame in poll_live_view_frames("http://localhost:8080", cam_id):
        # e.g. feed into opencv / PIL / torchvision
        import numpy as np
        import cv2
        arr = cv2.imdecode(np.frombuffer(frame, np.uint8), cv2.IMREAD_COLOR)
        # run inference, display, whatever
```

---

## Swift

### Complete recipe

```swift
// LiveView.swift — AsyncSequence of JPEG frame Data.

import Foundation

public struct LiveViewStream {
    public let baseURL: String
    public let cameraId: String
    public let intervalMs: UInt64

    public init(baseURL: String, cameraId: String, intervalMs: UInt64 = 66) {
        self.baseURL = baseURL
        self.cameraId = cameraId
        self.intervalMs = intervalMs
    }

    public var frames: AsyncStream<Data> {
        AsyncStream { continuation in
            let task = Task {
                guard let url = URL(string: "\(baseURL)/api/cameras/\(cameraId)/live-view/frame") else {
                    continuation.finish()
                    return
                }

                while !Task.isCancelled {
                    let iterStart = Date()

                    do {
                        let (data, response) = try await URLSession.shared.data(from: url)
                        if let http = response as? HTTPURLResponse, http.statusCode == 200 {
                            // Valid JPEG starts with FF D8 FF
                            if data.count >= 3 && data[0] == 0xff && data[1] == 0xd8 && data[2] == 0xff {
                                continuation.yield(data)
                            }
                        }
                        // 404 = no frame yet, keep polling
                    } catch {
                        if Task.isCancelled { break }
                        try? await Task.sleep(nanoseconds: 500 * 1_000_000)
                        continue
                    }

                    // Pace the loop
                    let elapsedMs = UInt64(Date().timeIntervalSince(iterStart) * 1000)
                    if elapsedMs < intervalMs {
                        try? await Task.sleep(nanoseconds: (intervalMs - elapsedMs) * 1_000_000)
                    }
                }
                continuation.finish()
            }
            continuation.onTermination = { _ in task.cancel() }
        }
    }
}
```

### Usage — SwiftUI preview

```swift
import SwiftUI
import AlphaCameraRestAPI

struct CameraPreview: View {
    let cameraId: String
    @State private var currentImage: UIImage?  // use NSImage on macOS

    var body: some View {
        Group {
            if let image = currentImage {
                Image(uiImage: image).resizable().aspectRatio(contentMode: .fit)
            } else {
                ProgressView()
            }
        }
        .task {
            let stream = LiveViewStream(baseURL: "http://localhost:8080", cameraId: cameraId)
            for await frameData in stream.frames {
                currentImage = UIImage(data: frameData)
            }
        }
    }
}
```

---

## Performance notes

- **Don't go below ~50ms interval.** The server encodes JPEGs on every request; hammering it harder than camera can produce frames wastes CPU.
- **15fps (66ms) is a good default.** The camera's live view is typically capped around 15-30fps internally.
- **Back-pressure is free.** Each recipe is a pull-based generator — if your consumer is slow, frames simply aren't fetched, they don't pile up in memory.
- **Keep-alive matters.** All three recipes reuse a single HTTP client (`fetch` pool, `httpx.AsyncClient`, `URLSession.shared`) so TCP connections are reused — significantly faster than a fresh connection per frame.

## Starting live view before polling

Live view must be enabled + started on the camera first. Easiest full flow:

```typescript
await client.cameras.connect({ cameraId, mode: "remote" });
await new Promise((r) => setTimeout(r, 500)); // server settle
await client.properties.setPriorityKey({ cameraId, setting: "pc-remote" });
await client.liveView.enable({ cameraId });
await client.liveView.start({ cameraId });
// frames are now available at /api/cameras/{id}/live-view/frame
```

`start` auto-enables live view if it's off, but being explicit reads better.

## Verified against

The same validation pattern was used during live-camera test runs — poll a few frames, verify JPEG magic bytes, and save one to disk. Confirmed working against the live ILCE-7M4 at ~70-80 KB/frame.
