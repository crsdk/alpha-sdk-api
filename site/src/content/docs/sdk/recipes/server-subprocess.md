---
title: "Server subprocess"
description: "Spawn the native server from Python or any non-Node runtime"
---

Launch the native camera server binary from your app, wait for it to be ready, and shut it down cleanly on exit. The Alpha Camera REST API server is a C++ binary that must be running somewhere on the network — usually on the same machine as the camera. This recipe is for apps that want to own the server's lifecycle.

## When to use

- Desktop apps (Electron, Tauri, PyQt, SwiftUI macOS) that should be self-contained — the user shouldn't have to start a separate server
- CI or test environments that need a camera server per run
- Dev harnesses where you're iterating on server + client together

## When NOT to use

- Anything on iOS — iOS apps can't spawn subprocesses. Use a server running elsewhere (laptop, Raspberry Pi).
- Browser-only UIs — point at an already-running server
- Server is deployed separately from your app (e.g. always-on Pi)

## Where does the binary come from?

The camera server binary ships with `@alpha-sdk/api` on npm via platform-specific optional dependencies. For self-contained apps, the easiest path:

- **Node/Electron:** `npm install @alpha-sdk/api` — the `camera-server` CLI in `node_modules/.bin/` is the binary launcher
- **Python:** Install Node + `@alpha-sdk/api`, or download the binary from GitHub releases
- **Swift macOS:** Ship the binary inside your `.app` bundle's Resources, or download on first run

For this recipe, we assume the binary is at a path your app controls. The current macOS example app resolves it from a local npm install first (`node_modules/@alpha-sdk/darwin-arm64/CameraWebApp`), then falls back to `CAMERA_SERVER_BINARY`.

---

## TypeScript / Node.js

### Complete recipe

```typescript
// route.ts — mirrors example_app/src/app/api/server/route.ts

import { NextResponse } from "next/server";
import { ServerManager } from "@alpha-sdk/api";

let server: ServerManager | null = null;
let startPromise: Promise<number> | null = null;

async function isHealthyServer(port: number): Promise<boolean> {
  try {
    const response = await fetch(`http://127.0.0.1:${port}/api/server/status`, {
      signal: AbortSignal.timeout(1500),
    });
    return response.ok;
  } catch {
    return false;
  }
}

export async function POST() {
  if (startPromise) {
    const port = await startPromise;
    return NextResponse.json({ status: "already_running", port });
  }

  if (server) {
    return NextResponse.json({ status: "already_running", port: server.getPort() });
  }

  if (await isHealthyServer(8080)) {
    return NextResponse.json({ status: "already_running", port: 8080 });
  }

  const instance = new ServerManager({ port: 8080, autoPort: true });

  try {
    startPromise = (async () => {
      await instance.start();
      server = instance;
      return instance.getPort();
    })();

    const port = await startPromise;
    startPromise = null;
    return NextResponse.json({ status: "started", port });
  } catch (error) {
    startPromise = null;
    server = null;
    return NextResponse.json(
      { status: "error", message: error instanceof Error ? error.message : String(error) },
      { status: 500 }
    );
  }
}

export async function DELETE() {
  if (!server) {
    return NextResponse.json({ status: "not_running" });
  }

  const instance = server;
  server = null;
  await instance.stop();
  return NextResponse.json({ status: "stopped" });
}
```

### Usage

```typescript
// Browser / React side
const response = await fetch("/api/server", { method: "POST" });
const payload = await response.json();

if (payload.status !== "started" && payload.status !== "already_running") {
  throw new Error(payload.message ?? "Failed to start server");
}

const baseUrl = `http://localhost:${payload.port}`;
```

This is the boundary the real Next.js example app uses:

- route owns `ServerManager`
- browser code only talks to `/api/server`
- the camera lifecycle layer then points `@alpha-sdk/client` at the returned port

---

## Python

### Complete recipe

```python
# server_manager.py — spawn, health-check, shutdown.

import asyncio
import os
import signal
import subprocess
from typing import Optional, Callable

import httpx


class ServerManager:
    def __init__(
        self,
        binary_path: str,
        port: int = 8080,
        cwd: Optional[str] = None,
        on_log: Optional[Callable[[str], None]] = None,
        ready_timeout_s: float = 15.0,
    ):
        self.binary_path = binary_path
        self.port = port
        self.cwd = cwd
        self.on_log = on_log
        self.ready_timeout_s = ready_timeout_s
        self.process: Optional[subprocess.Popen] = None
        self.base_url = f"http://localhost:{port}"
        self._log_task: Optional[asyncio.Task] = None

    async def start(self) -> None:
        if self.process:
            raise RuntimeError("ServerManager: already started")

        env = {**os.environ, "PORT": str(self.port)}
        self.process = subprocess.Popen(
            [self.binary_path],
            cwd=self.cwd,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )

        # Drain stdout in background so the buffer doesn't fill
        if self.on_log and self.process.stdout:
            loop = asyncio.get_event_loop()
            self._log_task = loop.create_task(self._drain_logs())

        # Poll until ready
        async with httpx.AsyncClient(timeout=1.0) as client:
            deadline = asyncio.get_event_loop().time() + self.ready_timeout_s
            while asyncio.get_event_loop().time() < deadline:
                if self.process.poll() is not None:
                    raise RuntimeError("Server process exited before becoming ready")
                try:
                    response = await client.get(f"{self.base_url}/api/server/status")
                    if response.status_code == 200:
                        return
                except httpx.HTTPError:
                    pass
                await asyncio.sleep(0.25)

        # Timeout
        self.process.terminate()
        raise RuntimeError("Server did not become ready within timeout")

    async def _drain_logs(self):
        loop = asyncio.get_event_loop()
        while self.process and self.process.stdout:
            line = await loop.run_in_executor(None, self.process.stdout.readline)
            if not line:
                break
            if self.on_log:
                self.on_log(line.rstrip())

    async def stop(self) -> None:
        if not self.process:
            return

        # Graceful shutdown via HTTP first
        try:
            async with httpx.AsyncClient(timeout=2.0) as client:
                await client.post(f"{self.base_url}/api/server/shutdown")
        except httpx.HTTPError:
            pass

        # Wait for clean exit
        try:
            await asyncio.wait_for(
                asyncio.get_event_loop().run_in_executor(None, self.process.wait),
                timeout=3.0,
            )
        except asyncio.TimeoutError:
            self.process.terminate()
            try:
                await asyncio.wait_for(
                    asyncio.get_event_loop().run_in_executor(None, self.process.wait),
                    timeout=1.0,
                )
            except asyncio.TimeoutError:
                self.process.kill()

        if self._log_task:
            self._log_task.cancel()
        self.process = None

    async def __aenter__(self):
        await self.start()
        return self

    async def __aexit__(self, exc_type, exc, tb):
        await self.stop()
```

### Usage

```python
import asyncio
from alpha_sdk_client import AsyncAlphaSDKClient
from server_manager import ServerManager

async def main():
    async with ServerManager(
        binary_path="/path/to/CameraWebApp",
        port=8080,
        on_log=lambda line: print("[server]", line),
    ) as server:
        client = AsyncAlphaSDKClient(base_url=server.base_url)
        listing = await client.cameras.list()
        print(f"Found {len(listing.cameras)} cameras")
        # ... work ...
    # server automatically stopped on context exit

asyncio.run(main())
```

---

## Swift (macOS only)

iOS cannot spawn processes. This recipe applies only to macOS (`#if os(macOS)`).

### Complete recipe

```swift
// ServerManager.swift — mirrors example_swift_app.

#if os(macOS)
import Foundation

public actor ServerManager {
    public private(set) var baseURL: String

    private let binaryPath: String
    private let preferredPort: Int
    private let portSearchRange: Int
    private(set) var port: Int
    private let cwd: URL?
    private let onLog: (@Sendable (String) -> Void)?
    private let readyTimeoutSec: Double
    private var process: Process?

    public init(
        binaryPath: String,
        port: Int = 8080,
        portSearchRange: Int = 10,
        cwd: URL? = nil,
        readyTimeoutSec: Double = 15.0,
        onLog: (@Sendable (String) -> Void)? = nil
    ) {
        self.binaryPath = binaryPath
        self.preferredPort = port
        self.port = port
        self.portSearchRange = portSearchRange
        self.cwd = cwd
        self.onLog = onLog
        self.readyTimeoutSec = readyTimeoutSec
        self.baseURL = "http://localhost:\(port)"
    }

    public func start() async throws {
        if process != nil {
            throw ServerManagerError.alreadyStarted
        }

        // Reuse an already-healthy server on the preferred port.
        if await isHealthy() {
            onLog?("[server] already running on port \(port); reusing")
            return
        }

        // If the preferred port is occupied by an orphan CameraWebApp, reap it.
        // Otherwise, walk forward to the next free port instead of killing
        // unrelated processes.
        if let orphanPids = pidsBoundToPort(port),
           !orphanPids.isEmpty,
           orphanPids.allSatisfy({ processName(forPid: $0) == "CameraWebApp" }) {
            onLog?("[server] reaping stale CameraWebApp(s) on port \(port): \(orphanPids)")
            for pid in orphanPids { kill(pid, SIGKILL) }
            try? await Task.sleep(nanoseconds: 500_000_000)
        } else if isPortInUse(port) {
            var found: Int? = nil
            for candidate in port..<(port + portSearchRange) {
                if !isPortInUse(candidate) {
                    found = candidate
                    break
                }
            }
            guard let chosen = found else {
                throw ServerManagerError.noFreePort(start: port, range: portSearchRange)
            }
            if chosen != port {
                onLog?("[server] port \(port) in use; falling back to \(chosen)")
                self.port = chosen
                self.baseURL = "http://localhost:\(chosen)"
            }
        }

        let p = Process()
        p.executableURL = URL(fileURLWithPath: binaryPath)
        p.arguments = []
        if let cwd = cwd { p.currentDirectoryURL = cwd }
        p.environment = ProcessInfo.processInfo.environment.merging(
            ["PORT": String(port)],
            uniquingKeysWith: { _, new in new }
        )

        let pipe = Pipe()
        p.standardOutput = pipe
        p.standardError = pipe

        if let onLog = onLog {
            pipe.fileHandleForReading.readabilityHandler = { handle in
                let data = handle.availableData
                if !data.isEmpty, let text = String(data: data, encoding: .utf8) {
                    for line in text.split(separator: "\n") {
                        onLog(String(line))
                    }
                }
            }
        }

        try p.run()
        process = p

        let deadline = Date().addingTimeInterval(readyTimeoutSec)
        while Date() < deadline {
            if !p.isRunning {
                process = nil
                throw ServerManagerError.processExitedEarly
            }
            if await isHealthy() { return }
            try? await Task.sleep(nanoseconds: 250_000_000)
        }

        p.terminate()
        process = nil
        throw ServerManagerError.startTimeout
    }

    public func stop() async {
        guard let p = process else { return }

        if let url = URL(string: "\(baseURL)/api/server/shutdown") {
            var req = URLRequest(url: url)
            req.httpMethod = "POST"
            req.timeoutInterval = 2.0
            _ = try? await URLSession.shared.data(for: req)
        }

        let waitStart = Date()
        while p.isRunning && Date().timeIntervalSince(waitStart) < 3.0 {
            try? await Task.sleep(nanoseconds: 100_000_000)
        }

        if p.isRunning {
            p.terminate()
            try? await Task.sleep(nanoseconds: 1_000_000_000)
            if p.isRunning { p.interrupt() }
        }

        process = nil
    }

    private func isHealthy() async -> Bool {
        guard let url = URL(string: "\(baseURL)/api/server/status") else { return false }
        var req = URLRequest(url: url)
        // Fresh server boot can block on camera discovery for several seconds.
        req.timeoutInterval = 30.0
        do {
            let (_, response) = try await URLSession.shared.data(for: req)
            if let http = response as? HTTPURLResponse, http.statusCode == 200 {
                return true
            }
        } catch {}
        return false
    }

    private func pidsBoundToPort(_ port: Int) -> [pid_t]? {
        let task = Process()
        task.executableURL = URL(fileURLWithPath: "/usr/sbin/lsof")
        task.arguments = ["-ti", ":\(port)"]
        let pipe = Pipe()
        task.standardOutput = pipe
        task.standardError = Pipe()
        do {
            try task.run()
            task.waitUntilExit()
        } catch {
            return nil
        }
        let data = pipe.fileHandleForReading.readDataToEndOfFile()
        guard let out = String(data: data, encoding: .utf8) else { return nil }
        return out
            .split(separator: "\n")
            .compactMap { pid_t($0.trimmingCharacters(in: .whitespaces)) }
    }

    private func isPortInUse(_ port: Int) -> Bool {
        let sock = socket(AF_INET, SOCK_STREAM, 0)
        guard sock >= 0 else { return true }
        defer { close(sock) }

        var noReuse: Int32 = 0
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &noReuse, socklen_t(MemoryLayout<Int32>.size))

        var addr = sockaddr_in()
        addr.sin_family = sa_family_t(AF_INET)
        addr.sin_port = UInt16(port).bigEndian
        addr.sin_addr.s_addr = INADDR_ANY.bigEndian

        let result = withUnsafePointer(to: &addr) { ptr -> Int32 in
            ptr.withMemoryRebound(to: sockaddr.self, capacity: 1) { sa in
                bind(sock, sa, socklen_t(MemoryLayout<sockaddr_in>.size))
            }
        }
        return result != 0
    }

    private func processName(forPid pid: pid_t) -> String? {
        let task = Process()
        task.executableURL = URL(fileURLWithPath: "/bin/ps")
        task.arguments = ["-p", String(pid), "-o", "comm="]
        let pipe = Pipe()
        task.standardOutput = pipe
        task.standardError = Pipe()
        do {
            try task.run()
            task.waitUntilExit()
        } catch {
            return nil
        }
        let data = pipe.fileHandleForReading.readDataToEndOfFile()
        guard let out = String(data: data, encoding: .utf8) else { return nil }
        return (out.trimmingCharacters(in: .whitespacesAndNewlines) as NSString).lastPathComponent
    }
}

public enum ServerManagerError: Error {
    case alreadyStarted
    case processExitedEarly
    case startTimeout
    case noFreePort(start: Int, range: Int)
}
#endif
```

### Usage

```swift
#if os(macOS)
import AlphaSDK

let server = ServerManager(
    binaryPath: "/path/to/node_modules/@alpha-sdk/darwin-arm64/CameraWebApp",
    port: 8080,
    onLog: { line in print("[server]", line) }
)

try await server.start()
print("Server ready at \(server.baseURL)")

let client = AlphaSDKClient(baseURL: server.baseURL, timeout: 60)
let cameras = try await client.cameras.list()
print("Found \(cameras.cameras.count) cameras")

// ... work ...
await server.stop()
#endif
```

---

## Common pitfalls

- **Handle Ctrl-C.** If your app gets SIGINT, you need to call `stop()` — otherwise the server keeps running. Wire a signal handler or use the context-manager variants (Python `async with`, Swift `defer`).
- **Port conflicts.** The real macOS app does not assume 8080 is free. It first reuses a healthy server, then reaps stale `CameraWebApp` orphans, then walks to the next free port.
- **Stdout buffering.** Node's `spawn` doesn't buffer if you drain `stdout`. Python needs `bufsize=1` + explicit read loop. Swift needs a `readabilityHandler`. Forgetting these silently stalls the server when its output buffer fills (~64KB).
- **Don't kill the server on minor errors.** Only call `stop()` on app exit or when you actually want it gone. Transient HTTP errors from camera commands aren't server problems.
- **Main-actor teardown can deadlock on app quit.** The real app snapshots `serverURL` + `connectedCameraId` and performs shutdown with plain `URLSession` off-actor so AppKit termination does not hang.

## Health check before starting

If you want to avoid a port conflict, probe first:

```typescript
async function isServerAlreadyRunning(port: number): Promise<boolean> {
  try {
    const res = await fetch(`http://localhost:${port}/api/server/status`, {
      signal: AbortSignal.timeout(500),
    });
    return res.ok;
  } catch { return false; }
}

if (await isServerAlreadyRunning(8080)) {
  console.log("Server already running; skipping spawn");
} else {
  await server.start();
}
```

## Verified against

The Swift section mirrors `example_swift_app/Sources/AlphaCameraExample/ServerManager.swift` and its shutdown path in `example_swift_app/Sources/AlphaCameraExample/AppCoordinator.swift`.
