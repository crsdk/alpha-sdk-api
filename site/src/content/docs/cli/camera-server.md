---
title: "CameraWebApp"
description: "Running the camera server binary directly from a terminal"
---

`CameraWebApp` is the server binary. Running it from a terminal is the simplest
way to bring the REST API up — useful for local testing, debugging, and
language-agnostic integrations where you just want the HTTP interface available.

It is built from source; see `api/distribution/INSTALL_MACOS.md` in the
repository for build steps and prerequisites.

## Usage

```bash
CameraWebApp
```

Starts the server on `http://localhost:8080` and runs in the foreground, logging
to stdout. Stop it with <kbd>Ctrl</kbd>+<kbd>C</kbd> — it traps `SIGINT` and
`SIGTERM` and shuts down gracefully, disconnecting cameras and closing SSE
streams first.

### Options

| Flag | Default | Description |
| --- | --- | --- |
| `--port`, `-p` | `8080` | Port to listen on. Must be 1–65535; anything outside that range exits with an error. |

```bash
CameraWebApp --port 9000
```

Those are the only flags. There are no subcommands — the binary starts a server
and runs until stopped.

## Checking it is up

```bash
curl http://localhost:8080/api/server/status
```

## Stopping it from elsewhere

If something other than your terminal needs to stop the server, prefer the HTTP
endpoint over a signal so cameras disconnect cleanly:

```bash
curl -X POST http://localhost:8080/api/server/shutdown
```

## Running it under another process

To start and supervise the server from an application — picking a free port,
waiting for readiness, shutting it down on exit — see the
[server subprocess recipe](/alpha-sdk-api/sdk/recipes/server-subprocess/).
