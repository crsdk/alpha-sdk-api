---
title: "REST API CLI"
description: "Start, stop, and inspect the Alpha Camera REST API server binary from the terminal."
---

The `camera-server` CLI ships with the [`@alpha-sdk/api`](https://www.npmjs.com/package/@alpha-sdk/api) npm package. It wraps the native server binary so you can run the REST API without writing any application code — useful for local testing, debugging, and language-agnostic integrations.

## Install

```bash
npm install -g @alpha-sdk/api
```

The post-install step downloads the platform-specific native binary (`darwin-arm64`, `linux-arm64`, `linux-x64`, `win32-x64`).

## Commands

### `camera-server start`

Starts the server in the foreground on port `8080`.

```bash
camera-server start
```

Options:

| Flag | Description | Default |
|------|-------------|---------|
| `--port <n>` | Port to bind | `8080` |
| `--host <addr>` | Host to bind | `127.0.0.1` |
| `--detach` | Run in the background (PID file + log file) | off |

Examples:

```bash
camera-server start --port 9000
camera-server start --detach
```

### `camera-server stop`

Stops a server started with `--detach`.

```bash
camera-server stop
```

### `camera-server status`

Reports whether a detached server is running, its PID, and its HTTP health.

```bash
camera-server status
```

Exit codes: `0` if running and healthy, `1` otherwise — suitable for shell scripts and health probes.

## Use cases

- **Local development** — run the REST API without spinning up the TypeScript client.
- **Language-agnostic workflows** — control the camera from Python, Go, Rust, or a shell script by calling the REST endpoints directly.
- **Remote server setups** — run `camera-server start --host 0.0.0.0` on a host connected to the camera (e.g. a Raspberry Pi) and point other machines at it.

## Programmatic equivalent

The TypeScript SDK's [`ServerManager`](/alpha-sdk-api/web-api/server/) provides the same lifecycle control programmatically. Use the CLI for scripts and one-off tasks; use `ServerManager` when the server's lifecycle is tied to your application's.
