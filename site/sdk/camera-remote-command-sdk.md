---
layout: "default"
title: "Camera Remote Command SDK"
description: "Sony's PTP-based SDK for direct camera commands"
nav_exclude: true
search_exclude: false
---

The Camera Remote Command SDK provides direct camera control using PTP (Picture Transfer Protocol) commands over USB. It supports both PTP-2 and PTP-3 protocols for sending operational commands to compatible Sony cameras.

## How It Differs from Camera Remote SDK

| | Camera Remote SDK | Camera Remote Command SDK |
|---|---|---|
| **Protocol** | Proprietary HTTP/USB | PTP-2 / PTP-3 |
| **Languages** | C++, C# | C++ |
| **Abstraction** | High-level API | Low-level PTP commands |
| **Use Case** | Application development | Custom protocol integration |
| **Camera Discovery** | Built-in | Manual PTP enumeration |

The Camera Remote SDK is recommended for most application development. The Camera Remote Command SDK is intended for developers who need direct PTP-level control or are integrating with existing PTP-based workflows.

## Key Capabilities

- **PTP-2 Commands** — Standard Picture Transfer Protocol operations
- **PTP-3 Commands** — Extended protocol with additional camera control features
- **Direct USB Control** — Low-level USB communication without abstraction layers
- **Custom Workflows** — Build specialized capture pipelines with precise timing control

## Download

- [**Camera Remote Command SDK**](https://support.d-imaging.sony.co.jp/app/cameraremotecommand/en/index.html) — Download from Sony's developer portal

## Related Resources

  - [**Camera Remote SDK**]({{ site.baseurl }}/sdk/camera-remote-sdk) — Sony's higher-level native SDK — recommended for most projects
  - [**CameraRemoteSDK MCP Server**]({{ site.baseurl }}/mcp-server/overview) — Search SDK documentation with AI-powered natural language queries
