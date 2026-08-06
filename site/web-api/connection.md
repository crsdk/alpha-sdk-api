---
layout: "default"
title: "Connection"
description: "Discover, connect, and disconnect cameras"
parent: "REST API"
nav_order: 4
---

Enumerate cameras connected via USB and network, then connect in the desired mode. Priority key **must** be set to `pc-remote` after connecting before any property changes or shooting commands will be accepted.

See the auto-generated [API reference]({{ site.baseurl }}/web-api/overview) for full request/response schemas.

## List Cameras

`GET /api/cameras` — enumerate all cameras visible via USB and network.

**curl**


```bash
curl http://localhost:8080/api/cameras
```

**Response**


```json
{
  "success": true,
  "message": "Camera discovery completed",
  "cameras": [
    {
      "id": "D10F60149B0C",
      "model": "ILCE-9M3",
      "connectionType": "USB",
      "connected": false
    }
  ]
}
```


---

## Connect Camera

`POST /api/cameras/{cameraId}/connection` — establish a connection in the specified [mode]({{ site.baseurl }}/sdk/typescript#connection-modes).

Body fields:

| Field | Default | Description |
|-------|---------|-------------|
| `mode` | `remote` | `remote`, `remote-transfer`, or `contents` |
| `reconnecting` | — | `on` or `off` — auto-reconnect on disconnection |
| `username` | — | For network cameras |
| `password` | — | For network cameras |

**curl**


```bash
curl -X POST http://localhost:8080/api/cameras/D10F60149B0C/connection \
  -H "Content-Type: application/json" \
  -d '{"mode": "remote"}'
```

**Response**


```json
{
  "success": true,
  "message": "Camera connected successfully in remote mode",
  "camera": {
    "connected": true,
    "model": "ILCE-9M3",
    "id": "D10F60149B0C"
  }
}
```


{: .note }
> After `POST /api/cameras/{cameraId}/connection` returns success, use `GET /api/cameras/{cameraId}/connection` if you want to confirm the active mode from `data.mode`.


---

## Connection Status

`GET /api/cameras/{cameraId}/connection` — check whether a camera is currently connected.

**curl**


```bash
curl http://localhost:8080/api/cameras/D10F60149B0C/connection
```


---

## Disconnect Camera

`DELETE /api/cameras/{cameraId}/connection` — disconnect a camera. Clears event callbacks and waits for clean disconnection.

**curl**


```bash
curl -X DELETE http://localhost:8080/api/cameras/D10F60149B0C/connection
```


{: .note }
> For typed client calls, use the language SDK pages: [TypeScript]({{ site.baseurl }}/sdk/typescript), [Python]({{ site.baseurl }}/sdk/python), or [Swift]({{ site.baseurl }}/sdk/swift).
