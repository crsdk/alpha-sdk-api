---
layout: "default"
title: "Camera Remote SDK Docs"
description: "AI-powered search for Sony's official C++ and C# Camera Remote SDK documentation"
nav_exclude: true
search_exclude: false
---

The Camera Remote SDK Docs MCP server gives your AI assistant access to documentation for Sony's **Camera Remote SDK** — the official native C++ and C# SDK for programmatic camera control.

## What's Indexed

| Content Type | Count | Description |
|-------------|-------|-------------|
| **Documentation** | Guides, tutorials, and explanatory content | SDK architecture, workflows, and concepts |
| **API Functions** | Function definitions and signatures | Parameters, return types, and usage notes |
| **Code Examples** | Working C++ and C# implementations | Connection, shooting, settings, live view, file transfer |
| **Compatibility Tables** | Camera-feature matrices | Which cameras support which SDK features |
| **Error Codes** | Error and warning code definitions | Descriptions, causes, and handling examples |

Total indexed content: **13,000+** pieces across SDK V2.01.00 and V2.00.00.

## What You Can Ask

- "How do I connect to a Sony camera and capture a photo in C++?"
- "What cameras support remote shooting via USB?"
- "Show me C# code for setting ISO and shutter speed"
- "What does error code CrError_Connect_TimeOut mean?"
- "How do I start live view streaming with the Camera Remote SDK?"
- "What's the difference between PTP-2 and PTP-3?"

## Server Details

| | |
|---|---|
| **Server Name** | `CameraRemoteSDK` |
| **URL** | `https://sdk-rag-agent-production.up.railway.app/mcp` |
| **Content** | Camera Remote SDK (C++/C#) documentation, API references, code examples, compatibility tables |
| **Versions** | V2.01.00 (latest), V2.00.00 |
| **Tools** | 14 tools — `search_sdk`, `search_code_examples`, `search_api_functions`, `search_compatibility`, `search_error_codes`, and more |


## Setup

See the [Setup Guide]({{ site.baseurl }}/mcp-server/setup) for installation instructions across Claude, Claude Code, and Cursor.
