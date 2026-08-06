---
layout: "default"
title: "Events (SSE)"
description: "Real-time camera callbacks via Server-Sent Events"
parent: "REST API"
nav_order: 8
---

Real-time event stream from the camera SDK. Subscribe to events from all cameras or a specific camera.

See the auto-generated [API reference]({{ site.baseurl }}/web-api/overview) for full request/response schemas.

## Subscribe to All Camera Events

`GET /api/events`

**curl**


```bash
curl -N http://localhost:8080/api/events
```


## Subscribe to Select Camera Events

`GET /api/cameras/{cameraId}/events` — same event types, filtered to a single camera.

**curl**


```bash
curl -N http://localhost:8080/api/cameras/D10F60149B0C/events
```


---

## Connection & Lifecycle Events

Events related to camera connection state and SDK errors.

{: .info }
> When using the Client SDK, connection and lifecycle events can also be accessed via `manager.on()`. See the language SDK pages for typed event helpers and stream wrappers.


| Event | SDK Callback | Description |
|-------|-------------|-------------|
| `connected` | `OnConnected` | Camera connection established |
| `disconnected` | `OnDisconnected` | Camera disconnected |
| `error` | `OnError` | SDK error occurred |

### connected
```json
{"cameraId": "D10F60149B0C"}
```

### disconnected
```json
{"error": "0x00000000"}
```

### error
```json
{"code": "0x00008001", "message": "Generic error"}
```

---

## Property & Action Events

Real-time notifications from camera activity — settings changes, shutter events, autofocus, and file transfers.

| Event | SDK Callback | Description |
|-------|-------------|-------------|
| `propertyChanged` | `OnPropertyChangedCodes` | Property values changed |
| `warning` | `OnWarning` | Camera warning/notification |
| `afStatus` | `OnWarningExt` | Autofocus state change |
| `downloadComplete` | `OnCompleteDownload` | Auto-transferred image saved |
| `transferProgress` | `OnNotifyRemoteTransferResult` | File pull completed |
| `lutImportResult` | `OnWarning` | LUT import completed or failed |

### propertyChanged
```json
{"codes": ["0x104", "0x105"], "count": 2}
```

### warning
```json
{"code": "0x20011", "message": "Captured_Event"}
```
Code `0x20011` = image captured (shutter fired).

### afStatus
```json
{"state": "focused"}
```
States: `focused`, `unlocked`, `tracking`

### downloadComplete
```json
{"filename": "/path/to/DSC09596.JPG", "type": "image"}
```
Only fires in `remote` connection mode with `still-image-store-destination` set to Host PC or Both.

### transferProgress
```json
{"percent": 100, "notify": "0x20093", "filename": "/path/to/DSC09598.JPG"}
```
Only fires in `remote-transfer` mode after explicit file pull.

### lutImportResult
```json
{"slot": 1, "status": "success"}
```

```json
{"slot": 1, "status": "error", "error": "invalid_file"}
```
Fires after a [LUT import]({{ site.baseurl }}/web-api/advanced-topics) operation completes. Error codes: `general_failure`, `invalid_filename`, `device_busy`, `storage_full`, `invalid_parameter`, `invalid_file`, `invalid`.

---

## Event Sequence Examples

### Capture with auto-transfer (`remote` mode)

```
event: afStatus
data: {"state":"focused"}

event: warning
data: {"code":"0x20011","message":"Captured_Event"}

event: downloadComplete
data: {"filename":"/path/to/DSC09596.JPG","type":"image"}

event: afStatus
data: {"state":"unlocked"}
```

### SD card file pull (`remote-transfer` mode)

```
event: transferProgress
data: {"percent":100,"notify":"0x20093","filename":"/path/to/DSC09598.JPG"}
```

---

## File Transfer by Mode

| Mode | Auto-transfer? | SSE Event | Mechanism |
|------|---------------|-----------|-----------|
| `remote` | Yes (if StoreDestination = HostPC/Both) | `downloadComplete` | SDK pushes via `OnCompleteDownload` |
| `remote-transfer` | No — explicit pull | `transferProgress` | Pull via `POST .../download` |
| `contents` | No — pull only | `contentsTransfer` | Pull-only, no shooting |

## Keepalive

The server sends a `: keepalive` comment every 30 seconds to detect dead connections.


{: .note }
> For typed SSE consumers, use the examples under [/sdk/recipes/sse-events]({{ site.baseurl }}/sdk/recipes/sse-events).
