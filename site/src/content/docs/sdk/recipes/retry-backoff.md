---
title: "Retry + backoff"
description: "Hand-roll exponential backoff for flaky calls"
---

A small utility for retrying REST calls that fail transiently. Useful around:

- `connect()` immediately after plugging in a camera (takes a few seconds to be ready)
- SSE reconnection (already baked into Recipe 1, but reusable elsewhere)
- Network blips between the client and server
- The 500ms server-side settle race after connect

## When to use

- Any code path where "try a few times with delay" is the right recovery
- Wrapping `connect()` so you don't fail immediately on a half-ready camera
- CI tests that need to wait for server readiness

## When NOT to use

- Hard errors that won't get better with retry (400 Bad Request, 404 Not Found) — retry these and you just burn time
- Long-running ops (downloads, live view) — retry the kickoff, not every iteration

## Key insight: retry on the *right* errors

Not every error should retry. A `400 "Invalid property value"` will fail identically on retry; a `400 "Camera not ready"` probably won't. The recipes below let you supply a predicate.

---

## TypeScript

### Complete recipe

```typescript
// retry.ts — retry with exponential backoff.

export interface RetryOptions {
  /** Maximum number of attempts (including the first). Default 5. */
  maxAttempts?: number;
  /** Initial delay in ms. Default 500. */
  initialDelayMs?: number;
  /** Max delay between attempts, in ms. Default 10000. */
  maxDelayMs?: number;
  /** Multiplier for backoff. Default 2 (doubles each attempt). */
  factor?: number;
  /** Return `true` to retry this error, `false` to throw immediately. */
  shouldRetry?: (err: unknown, attempt: number) => boolean;
  /** Fired before each retry delay. */
  onRetry?: (err: unknown, attempt: number, delayMs: number) => void;
  /** AbortSignal to stop retrying. */
  signal?: AbortSignal;
}

export async function retry<T>(
  fn: () => Promise<T>,
  opts: RetryOptions = {},
): Promise<T> {
  const maxAttempts = opts.maxAttempts ?? 5;
  const initialDelay = opts.initialDelayMs ?? 500;
  const maxDelay = opts.maxDelayMs ?? 10_000;
  const factor = opts.factor ?? 2;
  const shouldRetry = opts.shouldRetry ?? (() => true);

  let lastErr: unknown;

  for (let attempt = 1; attempt <= maxAttempts; attempt++) {
    if (opts.signal?.aborted) throw new Error("retry aborted");

    try {
      return await fn();
    } catch (err) {
      lastErr = err;
      if (attempt === maxAttempts) break;
      if (!shouldRetry(err, attempt)) throw err;

      const delay = Math.min(
        initialDelay * Math.pow(factor, attempt - 1),
        maxDelay,
      );
      opts.onRetry?.(err, attempt, delay);
      await new Promise((resolve) => setTimeout(resolve, delay));
    }
  }

  throw lastErr;
}
```

### Usage — retry camera connect with a "camera not ready" predicate

```typescript
import { retry } from "./retry";
import { AlphaSDKClient, BadRequestError } from "@alpha-sdk/client";

const client = new AlphaSDKClient({ baseUrl: "http://localhost:8080" });

const result = await retry(
  () => client.cameras.connect({ cameraId, mode: "remote" }),
  {
    maxAttempts: 5,
    initialDelayMs: 1000,
    // Only retry on "camera not ready"; fail fast on other errors
    shouldRetry: (err) => {
      if (err instanceof BadRequestError) {
        const body = err.body as { message?: string };
        return body?.message?.includes("not ready") ?? false;
      }
      return false;
    },
    onRetry: (err, attempt, delay) => {
      console.log(`Attempt ${attempt} failed; retrying in ${delay}ms`);
    },
  },
);
```

### Usage — wait for the server to be ready

```typescript
// Useful after server.start() if you're not using the built-in health check
await retry(
  async () => {
    const res = await fetch(`${baseUrl}/api/server/status`);
    if (!res.ok) throw new Error(`status ${res.status}`);
    return res.json();
  },
  { maxAttempts: 30, initialDelayMs: 500, maxDelayMs: 2000, factor: 1.2 },
);
```

---

## Python

### Complete recipe

```python
# retry.py — async retry with exponential backoff.

import asyncio
from typing import Awaitable, Callable, Optional, TypeVar

T = TypeVar("T")


async def retry(
    fn: Callable[[], Awaitable[T]],
    *,
    max_attempts: int = 5,
    initial_delay_s: float = 0.5,
    max_delay_s: float = 10.0,
    factor: float = 2.0,
    should_retry: Optional[Callable[[BaseException, int], bool]] = None,
    on_retry: Optional[Callable[[BaseException, int, float], None]] = None,
) -> T:
    """
    Call `fn` up to `max_attempts` times. Wait `initial_delay_s` after the
    first failure, doubling each time up to `max_delay_s`. Fails fast if
    `should_retry(err, attempt)` returns False.
    """
    last_err: Optional[BaseException] = None

    for attempt in range(1, max_attempts + 1):
        try:
            return await fn()
        except BaseException as err:
            last_err = err
            if attempt == max_attempts:
                break
            if should_retry and not should_retry(err, attempt):
                raise
            delay = min(initial_delay_s * (factor ** (attempt - 1)), max_delay_s)
            if on_retry:
                on_retry(err, attempt, delay)
            await asyncio.sleep(delay)

    assert last_err is not None
    raise last_err
```

### Usage

```python
import asyncio
import httpx
from alpha_sdk_client import AsyncAlphaSDKClient
from alpha_sdk_client.core.api_error import ApiError
from retry import retry

async def main():
    client = AsyncAlphaSDKClient(base_url="http://localhost:8080")

    # Retry only on "not ready" errors
    def retry_pred(err, attempt):
        if isinstance(err, ApiError) and err.status_code == 400:
            msg = str(err).lower()
            return "not ready" in msg or "not connected" in msg
        return False

    camera_id = "D06CE00004C4"
    result = await retry(
        lambda: client.cameras.connect(camera_id, mode="remote"),
        max_attempts=5,
        initial_delay_s=1.0,
        should_retry=retry_pred,
        on_retry=lambda err, attempt, delay: print(f"Attempt {attempt} failed; retrying in {delay}s"),
    )

asyncio.run(main())
```

---

## Swift

### Complete recipe

```swift
// Retry.swift — async retry with exponential backoff.

import Foundation

public struct RetryOptions: Sendable {
    public var maxAttempts: Int = 5
    public var initialDelaySec: TimeInterval = 0.5
    public var maxDelaySec: TimeInterval = 10.0
    public var factor: Double = 2.0
    public init(
        maxAttempts: Int = 5,
        initialDelaySec: TimeInterval = 0.5,
        maxDelaySec: TimeInterval = 10.0,
        factor: Double = 2.0
    ) {
        self.maxAttempts = maxAttempts
        self.initialDelaySec = initialDelaySec
        self.maxDelaySec = maxDelaySec
        self.factor = factor
    }
}

/// Retry `body` up to `options.maxAttempts` times with exponential backoff.
/// Pass a `shouldRetry` closure to skip retries for certain errors.
public func retry<T>(
    options: RetryOptions = RetryOptions(),
    shouldRetry: (@Sendable (Error, Int) -> Bool)? = nil,
    onRetry: (@Sendable (Error, Int, TimeInterval) -> Void)? = nil,
    body: @Sendable () async throws -> T
) async throws -> T {
    var lastError: Error?

    for attempt in 1...options.maxAttempts {
        do {
            return try await body()
        } catch {
            lastError = error
            if attempt == options.maxAttempts { break }
            if let shouldRetry = shouldRetry, !shouldRetry(error, attempt) {
                throw error
            }
            let delay = min(
                options.initialDelaySec * pow(options.factor, Double(attempt - 1)),
                options.maxDelaySec
            )
            onRetry?(error, attempt, delay)
            try await Task.sleep(nanoseconds: UInt64(delay * 1_000_000_000))
        }
    }

    throw lastError ?? URLError(.unknown)
}
```

### Usage

```swift
import AlphaCameraRestAPI

let client = AlphaSDKClient(baseURL: "http://localhost:8080")
let cameraId = "D06CE00004C4"

let result = try await retry(
    options: RetryOptions(maxAttempts: 5, initialDelaySec: 1.0),
    shouldRetry: { error, _ in
        // The SDK error exposes statusCode + body; inspect here
        if let apiErr = error as? AlphaSDKError,
           apiErr.statusCode == 400 {
            let msg = "\(error)".lowercased()
            return msg.contains("not ready") || msg.contains("not connected")
        }
        return false
    },
    onRetry: { err, attempt, delay in
        print("Attempt \(attempt) failed; retrying in \(delay)s: \(err)")
    }
) {
    try await client.cameras.connect(
        cameraId: cameraId,
        request: Requests.ConnectionRequest(mode: .remote)
    )
}
```

---

## When to retry vs. give up

A good `shouldRetry` predicate dramatically reduces wasted time:

| Error pattern | Retry? | Why |
|---------------|--------|-----|
| `400 "Camera not ready"` | ✅ Yes | Server state is settling; will succeed soon |
| `400 "Camera not connected"` | ✅ Yes | Usually the 500ms post-connect race |
| `400 "Invalid property value"` | ❌ No | Will never succeed; wasted time |
| `404 "Camera not found"` | ❌ No | Wrong ID; user must fix |
| `503`, `502`, timeout, ECONNREFUSED | ✅ Yes | Server restart / transient network |
| `500 Internal Server Error` | 🟡 Maybe | Depends on cause; retry 1-2x max |

If in doubt: **retry network-layer errors, don't retry validation errors.** The recipes above default to retrying everything, which is safe but sometimes wasteful; supplying a predicate is the tuned path.

## Combined with the connect→priority-key race

A common use is wrapping the connect + priority-key sequence so both settle races are handled automatically:

```typescript
async function connectAndReady(
  client: AlphaSDKClient,
  cameraId: string,
  mode: "remote" | "remote-transfer" | "contents",
) {
  await retry(() => client.cameras.connect({ cameraId, mode }));
  // 500ms settle still helpful even with retry, because the server says "success"
  // before the camera-side state is fully reconciled
  await new Promise((r) => setTimeout(r, 500));
  if (mode !== "contents") {
    await retry(() =>
      client.properties.setPriorityKey({ cameraId, setting: "pc-remote" }),
    );
  }
}
```

This single helper eliminates 90% of "camera not ready" flakiness in real apps.

## Verified against

The retry pattern was validated during the multi-mode test runs — the 500ms `sleep` in `connectWithPriority` is the shortcut version of this recipe. A full retry wrapper is better when conditions are less predictable.
