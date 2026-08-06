---
layout: "default"
title: "Actions"
description: "Shooting commands, video recording, zoom, focus control, AF targeting, and remote button presses"
parent: "REST API"
nav_order: 6
---

Shooting, focus, zoom, and movie-record commands. All actions require the camera to be connected and `priority-key` set to `pc-remote`.

See the auto-generated [API reference]({{ site.baseurl }}/web-api/overview) for full request/response schemas.

## Shutter

`POST /api/cameras/{cameraId}/actions/shutter` — take a single photo, or control continuous shooting with `{"action": "down"}` / `{"action": "up"}`.

**curl (Single Shot)**


```bash
curl -X POST http://localhost:8080/api/cameras/D10F60149B0C/actions/shutter
```

**curl (Continuous Start)**


```bash
curl -X POST http://localhost:8080/api/cameras/D10F60149B0C/actions/shutter \
  -H "Content-Type: application/json" \
  -d '{"action": "down"}'
```

**curl (Continuous Stop)**


```bash
curl -X POST http://localhost:8080/api/cameras/D10F60149B0C/actions/shutter \
  -H "Content-Type: application/json" \
  -d '{"action": "up"}'
```


{: .note }
> For continuous shooting, set drive mode to a continuous mode first (e.g. `Continuous Hi`).


---

## Half-Press (Focus Lock)

`POST /api/cameras/{cameraId}/actions/half-press` — half-press the shutter button to lock autofocus (S1 press). No body required.

**curl**


```bash
curl -X POST http://localhost:8080/api/cameras/D10F60149B0C/actions/half-press
```


---

## AF + Shutter

`POST /api/cameras/{cameraId}/actions/af-shutter` — autofocus then immediately capture in one operation. No body required.

**curl**


```bash
curl -X POST http://localhost:8080/api/cameras/D10F60149B0C/actions/af-shutter
```


---

## Zoom

`POST /api/cameras/{cameraId}/actions/zoom` — control power zoom lenses. Requires a power zoom lens to be attached.

Body: `direction` (`in` or `out`), `speed` (`normal` or `fast`).

**curl**


```bash
curl -X POST http://localhost:8080/api/cameras/D10F60149B0C/actions/zoom \
  -H "Content-Type: application/json" \
  -d '{"direction": "in", "speed": "normal"}'
```


---

## Focus Near/Far

`POST /api/cameras/{cameraId}/actions/focus-near-far` — move focus in discrete steps. Range: `-7` (near, max speed) to `+7` (far, max speed). Magnitude controls speed. `0` is not valid.

**curl (Focus Near)**


```bash
curl -X POST http://localhost:8080/api/cameras/D10F60149B0C/actions/focus-near-far \
  -H "Content-Type: application/json" \
  -d '{"step": -1}'
```

**curl (Focus Far)**


```bash
curl -X POST http://localhost:8080/api/cameras/D10F60149B0C/actions/focus-near-far \
  -H "Content-Type: application/json" \
  -d '{"step": 7}'
```


{: .tip }
> For absolute focus positioning, use `PUT /api/cameras/{id}/properties/focus-position` with a value from 0 (infinity) to 65535 (closest). To move the AF *frame* rather than the focus distance, see [AF Frame Position](#af-frame-position).


---

## AF Frame Position

`GET /api/cameras/{cameraId}/af-area-position` — read the camera's current autofocus frame(s), for drawing a focus box over live view.

Each frame reports its centre three ways: `normalized` (0–1, use this for overlays at any render size), `position` (the coordinate space `PUT` accepts, so a read feeds straight back), and `raw` (the camera's own numerator/denominator pair). `size.normalized_width` / `normalized_height` give the box dimensions as a fraction of the frame. Drive box colour from `state` — `focused`, `not-focused`, `moving`, `selection`, and others.

**curl**


```bash
curl http://localhost:8080/api/cameras/D10F60149B0C/af-area-position
```

**Response**


```json
{
  "success": true,
  "message": "AF area position retrieved",
  "data": {
    "available": true,
    "frame_count": 1,
    "frames": [
      {
        "type": "contrast-flexible-main",
        "state": "focused",
        "priority": 0,
        "normalized": { "x": 0.5, "y": 0.5 },
        "position": { "x": 320, "y": 240 },
        "size": {
          "width": 38,
          "height": 38,
          "normalized_width": 0.059,
          "normalized_height": 0.079
        }
      }
    ]
  }
}
```


{: .note }
> **More than one frame can be returned.** Expanding and tracking areas report a main box plus an assist box — an ILCE-7M4 set to Tracking Expand Flexible Spot returns a 38×38 `contrast-flexible-main` alongside a 76×76 `contrast-flexible-assist`. Render every entry in `frames`.


{: .warning }
> The camera only reports a frame for focus areas that actually draw one — Flexible Spot, Expand Flexible Spot, and the Tracking variants. In Wide or a face/eye-priority mode there is no movable frame and the request returns `400`. Live view must be streaming, since this reads a live-view property.


### Move the AF Frame

`PUT /api/cameras/{cameraId}/af-area-position` — move the centre of the autofocus frame.

| Field | Default | Description |
|-------|---------|-------------|
| `x` | — | Frame centre X, `0`–`639` |
| `y` | — | Frame centre Y, `0`–`479` |
| `normalized` | — | `{ "x": 0–1, "y": 0–1 }` as fractions of the live view — wins if both forms are sent |

Send `normalized` when translating a tap or click on a live view; it saves converting into the SDK's coordinate space.

**curl (Absolute)**


```bash
curl -X PUT http://localhost:8080/api/cameras/D10F60149B0C/af-area-position \
  -H "Content-Type: application/json" \
  -d '{"x": 320, "y": 240}'
```

**curl (Normalized)**


```bash
curl -X PUT http://localhost:8080/api/cameras/D10F60149B0C/af-area-position \
  -H "Content-Type: application/json" \
  -d '{"normalized": {"x": 0.5, "y": 0.5}}'
```


{: .warning }
> **The camera clamps silently and still returns `200`.** The usable area is inset from the edges of the coordinate space and varies by model, aspect ratio, and AF setting. Always follow a write with a `GET` rather than assuming the requested value took.


Measured on an ILCE-7M4 in Tracking Expand Flexible Spot — every corner was requested, and every one was pulled inward. Treat these numbers as illustrative, not a contract: a different body, aspect ratio, or AF area will inset differently.

| Requested | Landed |
|---|---|
| `0, 0` | `65, 61` |
| `639, 0` | `574, 61` |
| `639, 479` | `574, 418` |
| `0, 479` | `65, 418` |
| `320, 240` | `320, 240` |

That is a usable area of x `65`–`574`, y `61`–`418` — the middle 80% × 74% of the coordinate space. A write is also ignored outright when the focus area has no movable box.

---

## Remote Touch and Tracking

Tap a point in the live-view frame to seed the camera's own subject tracking, then read the tracked box back.

| Method | Path | Description |
|--------|------|-------------|
| `POST` | `/api/cameras/{id}/actions/touch` | Tap a point — seeds subject tracking |
| `GET` | `/api/cameras/{id}/tracking-frame` | The box the camera is tracking a subject in |
| `POST` | `/api/cameras/{id}/actions/touch-cancel` | Release a tracked subject |

### Moving the box vs. tracking a subject

These are not interchangeable.

`PUT /af-area-position` **drags the focus box and leaves it there.** The frame stays where you put it until you move it again.

`POST /actions/touch` **seeds the camera's subject tracking.** The camera locks onto whatever is at that point and follows it on its own as it moves — you cannot reproduce that by rewriting positions from a client, because the tracking runs on the camera. A touch also works in focus areas that have no movable box, such as Wide or face priority, where there is nothing for `PUT /af-area-position` to move.

Both endpoints return the same frame shape, so a single overlay renderer draws either.

### Touch a Point

`POST /api/cameras/{cameraId}/actions/touch` — tap a point, as if someone touched the camera's own screen there. Same body shape as [Move the AF Frame](#move-the-af-frame): `x` / `y`, or `normalized`.

**curl**


```bash
curl -X POST http://localhost:8080/api/cameras/D10F60149B0C/actions/touch \
  -H "Content-Type: application/json" \
  -d '{"normalized": {"x": 0.5, "y": 0.5}}'
```


{: .warning }
> **The camera decides what a touch does, and this API cannot change it.** The SDK property that would select the behaviour is unsupported on Alpha bodies including the ILCE-7M4 — only broadcast bodies can choose. A touch does whatever the body's own **Touch Func. in Shooting** setting is, and that setting is not exposed over this API.


Set it on the body under `MENU > Setup > Touch Operation > Touch Panel Settings > Shooting Screen`. Both rows below were measured on an ILCE-7M4.

| Touch Func. in Shooting | What a touch does | `tracking-frame` |
|---|---|---|
| `Touch Focus` (ILCE-7M4 default) | Moves the AF frame, like `PUT /af-area-position` | Stays empty |
| `Touch Tracking` | Starts subject tracking at that point | Reports the box |

{: .note }
> A touch is accepted even when the body's **Touch Operation** is `Off` — that setting governs the physical screen, not the remote property.


{: .warning }
> **A `200` does not mean tracking started.** The camera accepts the write and then decides. With `Touch Tracking` set, a touch on a point with nothing trackable — measured on a flat wall and on blown-out window areas — returns `200` and produces no tracking frame at all. Always read `GET /tracking-frame` to find out what actually happened.


Touches also land close to where they are asked for, but not exactly — the camera snaps to the nearest feature it can hold. Measured on an ILCE-7M4, `320,240` landed at `320,239` and `200,300` at `209,297`, while points near the edges pulled further in: `0,479` landed at `35,445`.

{: .warning }
> Leave a beat between touches. Firing several within a second or so, or one immediately after a cancel, made the camera drop tracking entirely until it was left alone.


A `400` means the camera reports remote touch as unavailable right now. On the ILCE-7SM3 and ILCE-7C it is movie-mode only; other bodies gate it on focus mode or on live view running.

### Get the Tracking Frame

`GET /api/cameras/{cameraId}/tracking-frame` — read the box the camera is tracking a subject in. Poll it to follow a moving subject; the camera updates the frame on its own once tracking has been seeded.

`type` is `target-af` for the subject being tracked, or `non-target-af` for a candidate the camera is showing but not acting on.

**curl**


```bash
curl http://localhost:8080/api/cameras/D10F60149B0C/tracking-frame
```

**Response**


```json
{
  "success": true,
  "message": "Tracking frame retrieved",
  "data": {
    "available": true,
    "frame_count": 1,
    "frames": [
      {
        "type": "target-af",
        "state": "focused",
        "normalized": { "x": 0.42, "y": 0.61 },
        "position": { "x": 269, "y": 293 }
      }
    ]
  }
}
```


{: .note }
> **An empty result is normal.** `200` with `"frames": []` and `"available": true` means the property was readable and there was simply no subject. A `400` means the property could not be read at all, usually because live view is not running.


Two ways to get a frame here, both measured on an ILCE-7M4:

- A touch, with the body's **Touch Func. in Shooting** set to `Touch Tracking`. Tracking then holds on its own with no button held.
- The camera tracking for its own reasons — a tracking focus area with AF engaged. Holding `af-on` in Tracking: Expand Flexible Spot reports a `target-af` frame that disappears on release.

With the ILCE-7M4 default of `Touch Focus`, a touch moves the AF frame instead and nothing appears here — read [AF Frame Position](#af-frame-position) in that case.

### Cancel a Touch

`POST /api/cameras/{cameraId}/actions/touch-cancel` — release the subject a touch locked onto. No body required.

**curl**


```bash
curl -X POST http://localhost:8080/api/cameras/D10F60149B0C/actions/touch-cancel
```


{: .note }
> A `400` here is a **safe no-op, not a failure to retry** — it means nothing is being tracked. The gate is specifically about a *remote touch* being in progress: tracking the camera started itself, such as a tracking focus area with `af-on` held, reports nothing to cancel because there is no remote touch operation to release.


{: .warning }
> The camera's ready flag lags a successful cancel by a beat, so an immediate second call can still report success. Treat `GET /tracking-frame` as the truth.


---

## Button Press

`POST /api/cameras/{cameraId}/actions/button` — press a physical button on the camera body, as if someone pressed it by hand.

Combined with [live view and the OSD overlay]({{ site.baseurl }}/web-api/live-view#osd-overlay), this is enough to drive the camera's own menu system remotely and read the result back off the screen — reaching settings that have no property of their own.

| Field | Default | Description |
|-------|---------|-------------|
| `button` | — | **Required.** Which button to press |
| `action` | `press` | `press` for a full down-then-up, or `down` / `up` to hold and release |

Buttons cover the D-pad and multi-selector, `enter`, `menu`, `fn`, `playback`, `c1`–`c7`, `movie`, `ael`, `af-on`, `mode`, `delete`, `display`, `home`, `clips`, `slot-select`, `cancel-back`, and `thumbnail`. See the API reference for the authoritative list.

Holding a button uses the same `down` / `up` contract as [Shutter](#shutter).

**curl (Full Press)**


```bash
curl -X POST http://localhost:8080/api/cameras/D10F60149B0C/actions/button \
  -H "Content-Type: application/json" \
  -d '{"button": "menu"}'
```

**curl (Hold)**


```bash
curl -X POST http://localhost:8080/api/cameras/D10F60149B0C/actions/button \
  -H "Content-Type: application/json" \
  -d '{"button": "af-on", "action": "down"}'
```

**curl (Release)**


```bash
curl -X POST http://localhost:8080/api/cameras/D10F60149B0C/actions/button \
  -H "Content-Type: application/json" \
  -d '{"button": "af-on", "action": "up"}'
```


{: .warning }
> **Not every body exposes every button.** An ILCE-7M4 reports 24 of them — the D-pad and multi-selector, `enter`, `menu`, `fn`, `playback`, `c1`–`c4`, `movie`, `ael`, and `af-on` — but not `delete`, `mode`, `c5`–`c7`, `display`, `home`, `clips`, `slot-select`, `cancel-back`, or `thumbnail`.


A rejected press returns `400` with the camera's own operable list in `data.supported_buttons`, so the real set is discoverable at runtime — send a probe press and read the list back rather than hardcoding one.

```json Response
{
  "success": false,
  "message": "Button not supported on this camera",
  "data": {
    "supported_buttons": ["up", "down", "left", "right", "enter", "menu", "fn", "playback", "c1", "c2", "c3", "c4", "movie", "ael", "af-on"]
  }
}
```

{: .warning }
> **Held keys block new presses.** The SDK only starts a press while the camera reports itself idle, so a button already held — by an earlier `down`, or by a hand on the body — makes the next press fail. A release is never blocked: an `up` always goes through, so a held key can always be let go.


{: .tip }
> When driving menus, pace the loop: one press, one [OSD frame]({{ site.baseurl }}/web-api/live-view#osd-overlay) read to see where you landed, then the next press.


---

## Movie Recording

`POST /api/cameras/{cameraId}/actions/movie-rec` — start or stop video recording. This is a toggle action — call once to start recording, call again to stop. Uses the SDK's hold-style command (Down to start, Up to stop).

Monitor recording state via the [`recording-state`]({{ site.baseurl }}/web-api/properties#video-recording) property or [`propertyChanged`]({{ site.baseurl }}/web-api/events#propertychanged) SSE events.

**curl**


```bash
curl -X POST http://localhost:8080/api/cameras/D10F60149B0C/actions/movie-rec
```


{: .note }
> Ensure the camera is in a video-capable exposure mode (e.g. Movie mode `0x8050`–`0x8055`) before triggering. Check `recording-state` to confirm the current state.


{: .note }
> For typed action helpers, use the SDK pages: [TypeScript]({{ site.baseurl }}/sdk/typescript), [Python]({{ site.baseurl }}/sdk/python), or [Swift]({{ site.baseurl }}/sdk/swift).
