---
layout: "default"
title: "Overview"
description: "AI-powered documentation search for Sony's camera development tools"
parent: "MCP Server"
nav_order: 1
---

{: .note }
> The MCP Servers are community-built developer tools and are **not officially supported by Sony**. Only the [Camera Remote SDK]({{ site.baseurl }}/sdk/camera-remote-sdk) (C++/C#) is an official Sony product.


- [**Quick Setup**]({{ site.baseurl }}/mcp-server/setup) — Get MCP servers running in under 5 minutes

## What Are MCP Servers?

MCP (Model Context Protocol) servers connect AI assistants like Claude and Cursor to specialized knowledge bases. Instead of manually searching through hundreds of pages of documentation, you ask questions in plain English and get instant answers with code examples.

We provide **three MCP endpoints**, each covering a different part of the camera development ecosystem:

| Server | Name | What it covers | URL |
|--------|------|---------|-----|
| Camera Remote SDK Docs | `CameraRemoteSDK` | C++/C# SDK — APIs, code examples, compatibility, error codes. 13,000+ indexed pieces | `https://sdk-rag-agent-production.up.railway.app/mcp` |
| Camera Help Guides | `CameraHelp` | 24 Sony camera models — settings, features, shooting modes, troubleshooting. 55,000+ entries | `https://camera-rag-agent-production.up.railway.app/mcp` |
| Camera Remote Web API Docs | `CameraWebAPI` | Hosted REST API docs, endpoint reference, request/response formats, and client SDK guidance for the Alpha Camera REST API | `https://crsdk.app/mcp` |

You can install any combination of them — they run independently and can be used simultaneously. The Web API docs endpoint is hosted on Mintlify and is the preferred MCP target for questions about the Alpha Camera REST API.

## How It Works

1. **You Ask a Question** — Ask your AI assistant about any Sony camera development topic
2. **The MCP Server Searches** — Your AI automatically selects the right server and searches through indexed documentation
3. **You Get Instant Results** — Receive relevant documentation, code examples, and explanations without leaving your conversation

{: .warning }
> **ChatGPT Not Recommended**: Due to OpenAI's unofficial support for MCP servers, ChatGPT has significant limitations with code generation and tool functionality. We strongly recommend using Claude for SDK development. Only use ChatGPT if you already have an active subscription and cannot access Claude.


## The Technology (Simplified)

- **RAG (Retrieval-Augmented Generation)**: Combines AI understanding with database search to find accurate answers
- **Vector Embeddings**: Converts documentation into a format AI can search semantically
- **MCP (Model Context Protocol)**: The standard that lets AI assistants connect to these servers
- **Cloud Hosted**: Runs on Railway and Cloudflare for reliable 24/7 availability

You don't need to understand these technologies — just connect the servers to your AI assistant and start asking questions.
