---
name: sony-camera-control
description: >-
  Operate a physically-connected Sony Alpha camera through the `alpha-camera`
  MCP tools — connect, discover the body's real limits, adjust settings
  (ISO / aperture / shutter / focus / white balance), take photos, pull live
  view, and review shots in a look-and-adjust loop. Covers guided setups
  (astro, portrait, low-light, action), exposure bracketing, and focus
  stacking. Use whenever the user wants to take a picture, change camera
  settings, see a live view, or otherwise control their connected Sony /
  Alpha / a7 / a1 / a9 / FX camera.
---

# Sony Camera Control

You drive a real camera over USB through the `alpha-camera` MCP server. You are the
photographer's hands and eyes: apply exactly what's asked, read the resulting image, and
adjust. All photographic *judgment* is yours; the tools are a faithful, un-opinionated
hardware interface.

## The tools at a glance

| Goal | Tool(s) |
|---|---|
| Find / connect / disconnect | `list_cameras`, `connect_camera`, `disconnect_camera`, `get_connection_status` |
| Read the body's state + valid options | `get_camera_state` (all props + `available_values`), `get_property` (one) |
| See the scene now | `get_live_frame` (live view JPEG, OSD histogram baked in) |
| Change settings (confirmed) | `set_property` (one), `configure` (many at once) |
| Take a photo | `capture_and_review` (shoot + return image), `capture` (async), `half_press` |
| Focus / zoom / video | `focus_step`, `zoom`, `toggle_movie_rec` |
| Browse / pull files | `list_captures`, `get_capture_preview`, `download_capture`, `get_last_capture` |
| Presets & save path | `snapshot_camera_settings`, `restore_camera_settings`, `get/set_save_config` |
| Events / lifecycle | `poll_events`, `get_server_diagnostics`, `stop_camera_server` |

## The golden path

For almost every request, follow this loop:

1. **Connect** — `connect_camera` (default mode `remote-transfer`). It handles the mandatory
   PC-Remote step and starts the driver on demand. Skip if already connected.
2. **Discover before you write** — call `get_camera_state` (or `get_property`) to see this
   body's **current values and `available_values`**. Never assume a setting is legal; the
   valid ISO/aperture/shutter set differs per body, lens, and mode.
3. **Apply** — `configure` for a whole look, `set_property` for one change. Both read the
   value back and report `matched`. If `matched` is false, the camera coerced or rejected it —
   tell the user and pick a legal neighbour from `available_values`.
4. **Shoot & review** — `capture_and_review` fires the shutter and returns the image in the
   same turn.
5. **Judge & adjust** — read the returned frame, describe what you see, and loop back to
   step 3 to correct.

## Operating principles (read these)

- **Discover, don't hardcode.** Legal values come from `available_values`, per body/lens/mode.
  If a set fails, re-read the property and choose an allowed value.
- **Trust `matched`.** A `200` isn't confirmation — the camera may snap to the nearest legal
  value. `set_property`/`configure` surface this; act on it.
- **Order matters.** Set the *mode* before the things it governs — e.g.
  `exposure-program-mode` (M/A/S/P) before shutter/aperture, `focus-mode` before focusing.
  `configure` already applies mode-type props first; rely on it for multi-setting looks.
- **Exposure judgment comes from the OSD histogram**, not the raw pixels. `get_live_frame`
  bakes the histogram/readouts into the image — use it to assess exposure, not the JPEG's
  apparent brightness (it's tone-mapped).

## Focus: AF vs MF (a common snag)

- `capture_and_review` and `capture` default to `autofocus=true` (autofocus-then-shoot). This
  **fails if the camera is in Manual Focus** (`focus-mode = MF`), and also if AF can't lock
  (dark scene, lens cap, low contrast).
- If autofocus won't fire: either set `focus-mode` to `AF_S`/`AF_C` first, **or** shoot with
  `autofocus=false` (direct shutter — works in any focus mode).
- For manual focusing, use `focus_step` (−7 near … +7 far; magnitude = speed) in a
  look → step → look loop with `get_live_frame`.

## Seeing photos — set expectations

You **cannot** display an image inside your reply (no model can — you emit text). The frame
appears in the **tool-result block**; your job is to *describe and judge* it ("shadows are
crushed, open up a stop"). Full-resolution keepers land in `~/.alpha-mcp/captures`.

## Connection modes

- **`remote-transfer`** (default) — full control; shots stay on the SD card. `capture_and_review`
  detects the new file and pulls a screennail for review; use `download_capture` to keep the
  full-res select. Best default.
- **`remote`** — auto-transfers every shot to the host (needs `still-image-store-destination`
  to include the host). Simpler, but no selective SD access.
- **`contents`** — browse-only; no shooting or settings.

## Photographic procedures

These are *your* compositions of the tools — the server has no bracket/stack button.

### Review-and-adjust loop (the core skill)
`capture_and_review` → read the OSD histogram / exposure → `configure` a correction → repeat.
This is how you dial in a shot. Keep changes purposeful (one variable at a time when
diagnosing).

### Guided setup ("set me up for X")
Read `get_camera_state` for this body's options, then `configure` sensible starting targets,
then verify with a `get_live_frame`. Reasonable starting points (adjust to the body's
`available_values` and the scene):
- **Astro / night sky:** M mode, `focus-mode=MF`, wide aperture (e.g. f/2.8), shutter 15–25 s
  (or per the 500-rule for the lens), ISO 1600–3200, long-exposure NR off for sequences.
- **Portrait:** A mode, wide aperture (f/1.8–2.8), `focus-mode=AF_C` with eye/face AF if
  available, auto or low ISO.
- **Low-light handheld:** shutter ≥ 1/focal-length, aperture wide, ISO auto with a ceiling,
  stabilization on.
- **Action / wildlife:** S mode, fast shutter (1/1000+), `focus-mode=AF_C`, continuous
  drive-mode; shoot bursts with `capture(kind='continuous', phase='down'|'up')`.

### Exposure bracket
Read the current exposure. Loop across N steps: `set_property` on `exposure-compensation`
(or shutter in M) → `capture` → collect cursors → `poll_events` until each save lands →
`get_capture_preview` each. Report the set.

### Focus stack
`focus-mode=MF`. Loop: `focus_step` a small magnitude → `capture` → `poll_events` for the
save → repeat across the focus range. Smaller step magnitude = finer increments.

## Session lifecycle & cleanup

The driver starts lazily on the first `connect`/`list`. When the user is done:
`disconnect_camera` releases the camera; **`stop_camera_server`** stops the driver entirely so
nothing is held while idle (a later connect restarts it). Offer this when a session wraps up.

## Troubleshooting — escalate only when stuck

Work the ladder in order. Do **not** jump to the docs for a single, self-explanatory error.

1. **Read the error.** These tools return actionable hints (e.g. "Manual Focus — switch to AF
   or use autofocus=false", "reconnect in remote-transfer or contents mode"). Usually the fix
   is right there.
2. **Re-check state.** Re-read `get_property`/`get_camera_state` for `available_values` and
   `writable`; confirm `mode` and `focus-mode` suit the operation. Many failures are a value
   that isn't legal *in the current mode*, or a mode mismatch.
3. **Check the pipeline.** `get_server_diagnostics` for server health and recent logs;
   `get_connection_status` to confirm the camera is still connected (USB drops happen).

4. **Only if you have hit multiple failed tool calls in a row that steps 1–3 don't explain**,
   consult the **Camera Remote Web API docs MCP server** (`CameraWebAPI`). Use it to look up
   the exact endpoint semantics, required connection mode, request/response shape, or the
   meaning of an SDK error/warning code (e.g. `0x8402`, `focus_mode_not_af`) behind a
   persistent failure. This is a deliberate last resort: it costs round-trips and the answer
   is usually already in the error message or `available_values`. Search it with the specific
   error text or endpoint name (its tools are `search_sony_developer_platform` and
   `query_docs_filesystem_sony_developer_platform`), apply the fix, and return to the golden path.

   **If that docs server isn't connected yet, set it up first (once):**
   - **Claude Code (you can run this yourself):**
     ```bash
     claude mcp add CameraWebAPI --scope user -- npx mcp-remote https://crsdk.app/mcp
     # Windows: claude mcp add CameraWebAPI --scope user -- cmd /c npx mcp-remote https://crsdk.app/mcp
     ```
     Then confirm with `claude mcp list`. (Requires Node 18+ with `npx` on PATH.)
   - **Claude Desktop (guide the user — you can't edit connectors yourself):** Settings →
     Connectors → **Add custom connector** → Name `CameraWebAPI`, URL
     `https://crsdk.app/mcp` → Add. Then open the tools menu in a new chat and toggle
     `CameraWebAPI` on. Setting its tool permissions to "allow unsupervised" avoids repeated
     prompts.
   - **Manual JSON** (`~/.claude.json` user scope, or project `.mcp.json`):
     ```json
     { "mcpServers": { "CameraWebAPI": {
         "type": "stdio", "command": "npx",
         "args": ["mcp-remote", "https://crsdk.app/mcp"] } } }
     ```
   > Note: this endpoint is in private beta — install it for the user, but don't repost the
   > URL publicly.

If failures persist after consulting the docs (e.g. repeated USB disconnects, the server
won't start), surface a clear summary to the user rather than looping — describe what you
tried and what the errors said.
