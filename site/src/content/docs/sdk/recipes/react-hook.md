---
title: "React hook"
description: "Bind one camera's connection + property state to a component"
---

React apps usually need three pieces at once:

- a local route that starts or adopts the camera server
- a lifecycle layer that handles polling + SSE + reconnect
- component state that renders camera/session data cleanly

The real TypeScript example app in this repo does **not** hide all of that behind one giant library call. It keeps the moving pieces explicit in:

- `example_app/src/app/api/server/route.ts`
- `example_app/src/lib/event-stream.ts`
- `example_app/src/lib/camera-manager.ts`
- `example_app/src/app/page.tsx`

This recipe shows the same pattern as a reusable hook.

## When to use

- Next.js / React apps that want to embed the server lifecycle
- Apps that should auto-discover and auto-connect cameras
- Apps that want typed action helpers without owning raw SSE parsing everywhere

## When NOT to use

- Scripts or CLIs — call the SDK client directly
- Non-React apps — use the discovery/SSE recipes directly

## Hook pattern

```typescript
// use-camera-session.ts

import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { CameraManager, type ManagedCamera } from "./camera-manager";

type ServerState = "starting" | "running" | "error";

export interface CameraSessionState {
  serverState: ServerState;
  serverError: string | null;
  cameras: ManagedCamera[];
  selectedCameraId: string | null;
}

export interface CameraSessionActions {
  selectCamera: (cameraId: string) => void;
  refreshCameras: () => void;
  disconnectCamera: () => Promise<void>;
}

export function useCameraSession(): CameraSessionState & CameraSessionActions {
  const [serverState, setServerState] = useState<ServerState>("starting");
  const [serverError, setServerError] = useState<string | null>(null);
  const [cameras, setCameras] = useState<ManagedCamera[]>([]);
  const [selectedCameraId, setSelectedCameraId] = useState<string | null>(null);

  const managerRef = useRef<CameraManager | null>(null);
  const selectedCameraIdRef = useRef<string | null>(null);
  selectedCameraIdRef.current = selectedCameraId;

  const syncCameras = useCallback(() => {
    const manager = managerRef.current;
    if (!manager) return;
    const next = manager.getCameras();
    setCameras(next);
  }, []);

  useEffect(() => {
    let cancelled = false;

    (async () => {
      try {
        const response = await fetch("/api/server", { method: "POST" });
        const payload = await response.json();

        if (cancelled) return;
        if (payload.status !== "started" && payload.status !== "already_running") {
          setServerState("error");
          setServerError(payload.message ?? "Failed to start server");
          return;
        }

        const manager = new CameraManager({
          baseUrl: `http://localhost:${payload.port}`,
          autoConnect: true,
          connectionMode: "remote-transfer",
          pollInterval: 5000,
          autoReconnect: true,
        });

        managerRef.current = manager;

        manager.on("camera-found", ({ camera }) => {
          syncCameras();
          if (!selectedCameraIdRef.current) {
            setSelectedCameraId(camera.id);
          }
        });

        manager.on("camera-lost", () => {
          syncCameras();
        });

        manager.on("camera-ready", ({ cameraId }) => {
          syncCameras();
          if (!selectedCameraIdRef.current) {
            setSelectedCameraId(cameraId);
          }
        });

        manager.on("camera-disconnected", () => {
          syncCameras();
        });

        manager.on("connection-failed", () => {
          syncCameras();
        });

        manager.on("error", ({ message }) => {
          setServerError(message);
        });

        await manager.start();
        if (cancelled) return;

        syncCameras();
        setServerState("running");
      } catch (error) {
        if (cancelled) return;
        setServerState("error");
        setServerError(error instanceof Error ? error.message : String(error));
      }
    })();

    return () => {
      cancelled = true;
      managerRef.current?.close();
      managerRef.current = null;
    };
  }, [syncCameras]);

  const disconnectCamera = useCallback(async () => {
    const manager = managerRef.current;
    const cameraId = selectedCameraIdRef.current;
    if (!manager || !cameraId) return;
    await manager.disconnect(cameraId);
    syncCameras();
  }, [syncCameras]);

  return useMemo(
    () => ({
      serverState,
      serverError,
      cameras,
      selectedCameraId,
      selectCamera: setSelectedCameraId,
      refreshCameras: syncCameras,
      disconnectCamera,
    }),
    [serverState, serverError, cameras, selectedCameraId, syncCameras, disconnectCamera]
  );
}
```

## Usage

```tsx
import { useCameraSession } from "./use-camera-session";

export function CameraSidebar() {
  const {
    serverState,
    serverError,
    cameras,
    selectedCameraId,
    selectCamera,
    refreshCameras,
  } = useCameraSession();

  if (serverState === "starting") return <div>Starting camera server…</div>;
  if (serverState === "error") return <div>Error: {serverError}</div>;

  return (
    <div>
      <button onClick={refreshCameras}>Refresh</button>
      {cameras.map((camera) => (
        <button
          key={camera.info.id}
          onClick={() => selectCamera(camera.info.id)}
          style={{
            display: "block",
            fontWeight: selectedCameraId === camera.info.id ? "bold" : "normal",
          }}
        >
          {camera.info.model} — {camera.state}
        </button>
      ))}
    </div>
  );
}
```

## Why this matches the real app

- The browser layer starts the server through `POST /api/server`, not by spawning a binary directly in client code.
- `CameraManager` owns polling, SSE, connect timing, reconnect, and post-connect setup.
- React components stay focused on state + rendering, not transport details.

That is the same boundary the repo’s actual Next.js example uses.
