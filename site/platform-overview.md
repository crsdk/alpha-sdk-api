---
layout: "default"
title: "Platform overview"
description: "A map of the developer stack — HTTP server, client SDKs, and MCP tooling."
nav_order: 2
---

The Sony Developer Platform (unofficial) consists of the **Alpha Camera REST API** and supporting tooling, including the CLI, API playground, and documentation MCP servers. Most developers should use the community-developed REST API with one of the supported Client SDKs for the most modern developer experience; Sony's official native SDKs are best if you need direct C\+\+ / C# integration to acheive best device performance. 

## The stack

| Layer | What it is | When to use it |
| --- | --- | --- |
| **Alpha Camera REST API** | A local HTTP and SSE server that wraps the native C\+\+ SDK. Distributed via npm (`@alpha-sdk/api`) with the `camera-server` CLI and a Node.js `ServerManager` class | Cross-platform apps; multi-client setups; embedding the server in a Node / Electron / Tauri app |
| **Language client SDKs** | REST clients in TypeScript, Python, and Swift | Modern UI-centric app frameworks (React, SwiftUI) or headless server applications (Express.js, FastAPI) |
| **MCP Servers** | AI-assistant–consumable indexes of the SDK and Help Guide | Building with Claude, Cursor, or other AI assisted development tools |

## Picking a layer

  - [**I'm shipping a TypeScript / Node / Electron app**]({{ site.baseurl }}/sdk/typescript) — Install the [server package]({{ site.baseurl }}/web-api/server) (`@alpha-sdk/api`) and pair it with `@alpha-sdk/client`.

  - [**I'm shipping a Python app or Jupyter notebook**]({{ site.baseurl }}/sdk/python) — `pip install alpha-sdk-client` — sync \+ async clients with Pydantic models.

  - [**I'm shipping an iOS or macOS app**]({{ site.baseurl }}/sdk/swift) — Add `AlphaSDK` via SwiftPM — native async/await, zero third-party deps.

  - [**I'm using a language without an SDK**]({{ site.baseurl }}/web-api/overview) — Call the **Alpha Camera REST API** directly or generate an SDK yourself from the OpenAPI contract.

  - [**I'm building with AI assistants**]({{ site.baseurl }}/mcp-server/overview) — Configure the **MCP Servers** with your coding agent for SDK-accurate code and spec lookups.

## Native Sony SDKs

If you explicitly need direct native integration rather than the REST API, Sony also provides:

- [Camera Remote SDK]({{ site.baseurl }}/sdk/camera-remote-sdk) — official C\+\+ / C# camera control SDK
- [Camera Remote Command SDK]({{ site.baseurl }}/sdk/camera-remote-command-sdk) — lower-level PTP command SDK for specialized workflows

## Connection modes

Whichever SDK/API you use, the underlying camera connection uses one of three modes:

| Mode | Capabilities |
| --- | --- |
| `remote` | Full shooting \+ settings control. Auto-transfers captured images to the host PC. |
| `remote-transfer` | Full shooting \+ settings control. Explicit SD card file access (pull specific files on demand). |
| `contents` | SD card browsing only — no shooting or settings. |

See [Connection]({{ site.baseurl }}/web-api/connection) for the full workflow.

## Priority key requirement

Before the camera accepts any remote command, its **priority key** must be set to `pc-remote`. Sending a shoot or property change before this will silently fail on most bodies. Set it right after connecting:

```bash
curl -X PUT http://localhost:8080/api/cameras/{id}/priority-key \
  -H "Content-Type: application/json" \
  -d '{"setting": "pc-remote"}'
```

All three language clients expose this as `setPriorityKey({ cameraId, setting: "pc-remote" })` — call it right after `cameras.connect(...)` succeeds.
