---
layout: "default"
title: "Camera Remote SDK"
description: "Sony's official native C++ and C# SDK for camera control"
nav_exclude: true
search_exclude: false
---

The Camera Remote SDK is Sony's official native SDK for controlling cameras programmatically. It provides C++ and C# APIs for camera connection, shooting, settings control, live view, and file transfer. The Alpha Camera REST API server (and the [TypeScript / Python / Swift clients]({{ site.baseurl }}/sdk/overview) that talk to it) is built on top of this SDK.

## Variants

  - **C++ SDK** — Cross-platform native library for macOS, Linux, and Windows. Supports USB and network connections with full camera control capabilities. [Download C++ SDK](https://support.d-imaging.sony.co.jp/app/sdk/en/index.html)
  - **C# SDK (Windows)** — Windows-only .NET wrapper providing the same camera control capabilities with a managed API surface. [Download C# SDK](https://sonyipsamarketing.cimediacloud.com/r/YvFlmszMaZMr)

## Supported Platforms

| Variant | Windows | macOS | Linux |
|---------|---------|-------|-------|
| C++ | x64 | ARM64 | x64, ARM64 |
| C# | x64 | -- | -- |

## Key Capabilities

- **Camera Discovery** — Enumerate cameras connected via USB or network
- **Connection Management** — Connect/disconnect with mode selection (remote, contents transfer)
- **Shooting Control** — Trigger shutter, bulb shooting, continuous shooting, interval capture
- **Property Control** — Read/write ISO, shutter speed, aperture, white balance, and 50+ other properties
- **Live View** — Stream real-time camera preview frames
- **File Transfer** — Download images and videos from the camera's SD card
- **Event Handling** — Subscribe to property changes, connection state, and download notifications

## When to Use This SDK

Use the Camera Remote SDK directly when you need:
- **Native performance** — Direct C++ or C# integration without HTTP overhead
- **Custom platform support** — Embedding camera control in existing native applications
- **Full SDK access** — Features not yet exposed through the Alpha Camera REST API

For most use cases, the [language clients]({{ site.baseurl }}/sdk/overview) (TypeScript / Python / Swift) or the [Alpha Camera REST API]({{ site.baseurl }}/web-api/overview) provide a faster development path with the same core capabilities.

## AI-Assisted Documentation

Use the [CameraRemoteSDK MCP server]({{ site.baseurl }}/mcp-server/overview) to search through the Camera Remote SDK documentation, find API functions, look up error codes, and get working code examples — all through natural language queries in Claude or Cursor.

- [**Set Up the CameraRemoteSDK MCP Server**]({{ site.baseurl }}/mcp-server/setup) — Connect your AI tool to search 13,000+ pieces of SDK documentation instantly
