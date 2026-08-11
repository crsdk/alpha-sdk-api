# MCP Documentation Servers

Contributors can use MCP documentation servers to research SDK APIs, camera behavior, and existing REST API docs before implementing features.

These documentation servers are community-built tools. Sony's official product is the Camera Remote SDK itself.

> **Not the camera-control MCP.** The servers below are hosted, read-only knowledge bases for *research*. The **camera-control** MCP server — which actually operates a connected camera — is a separate, local server that lives in [`mcp/`](../mcp/) and is built with `crsdk pack:mcp`. See its guide at [mcp-server/camera-control](https://crsdk.github.io/alpha-sdk-api/mcp-server/camera-control/).

## Servers

| Server | Use For | URL |
| --- | --- | --- |
| `CameraRemoteSDK` | C++/C# SDK APIs, code examples, compatibility, error codes | `https://sdk-rag-agent-production.up.railway.app/mcp` |
| `CameraHelp` | Camera model help guides, settings, feature behavior | `https://camera-rag-agent-production.up.railway.app/mcp` |

For the REST API's own request/response shapes, read `api/openapi.yaml` directly — it is the single source of truth in this repo.

## Project Config

This repository includes a project-scoped `.mcp.json` with both servers. For MCP-compatible development agents, install `mcp-remote` through `npx` and restart the agent after adding the config.

```json
{
  "mcpServers": {
    "CameraRemoteSDK": {
      "type": "stdio",
      "command": "npx",
      "args": ["mcp-remote", "https://sdk-rag-agent-production.up.railway.app/mcp"]
    },
    "CameraHelp": {
      "type": "stdio",
      "command": "npx",
      "args": ["mcp-remote", "https://camera-rag-agent-production.up.railway.app/mcp"]
    }
  }
}
```

## Research Pattern

1. Use `CameraRemoteSDK` first for exact SDK symbols such as `CrDeviceProperty_*`, `CrCommandId_*`, callback names, error codes, and sample code.
2. Use `CameraHelp` when behavior depends on camera model, firmware, shooting mode, storage mode, or menu terminology.
3. Read `api/openapi.yaml` to compare against the currently documented REST API and avoid inventing inconsistent endpoint names.
4. Record the exact SDK symbols and camera models used to justify the implementation in the PR description.
