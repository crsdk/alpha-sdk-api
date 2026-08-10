# Alpha Camera MCP Server

**Control your Sony Alpha camera by chatting with Claude.**

This connects a Sony camera (plugged into your computer over USB) to
[Claude Desktop](https://claude.ai/download). Once set up, you can say things like
*"take a photo and show me how it looks"* or *"the shot's too dark, open up a stop and
try again"* — and Claude operates the camera for you: changing settings, taking pictures,
and pulling them back so it can see the result and adjust.

It works by wrapping the community
[**Alpha Camera REST API**](https://github.com/crsdk/alpha-sdk-api) as an
[MCP server](https://modelcontextprotocol.io) (the standard way to give Claude new tools).

> **Is my camera supported?** Any Sony body supported by the Camera Remote SDK — most
> modern Alpha mirrorless cameras (a7 / a1 / a9 / FX series and similar). Built and tested
> against a **Sony a7 V (ILCE-7M5)** over USB.

---

## What you'll need

| | |
|---|---|
| A Sony Alpha camera | Connected to your computer with a **USB cable** |
| A computer | **macOS** (Apple Silicon or Intel), Linux, or Windows |
| [Claude Desktop](https://claude.ai/download) | The desktop app (this does **not** work in the browser) |
| [Node.js](https://nodejs.org) | Version 20 or newer — runs this server |
| The `CameraWebApp` binary | The camera driver. **You build it yourself** — see below |

> **Heads up: this is not a one-command install.** The camera driver links against
> Sony's Camera Remote SDK, which Sony does not allow anyone to redistribute — so
> there is no npm package, no Homebrew formula, and no download link that can ship
> it to you. You download the SDK from Sony under their licence and compile the
> driver once. Everything after that is just talking to Claude.

---

## Set up your camera first

1. Turn the camera on.
2. In the camera menu, set the USB connection mode to **PC Remote** (sometimes under
   *Setup → USB → USB Connection Mode* or *Network → PC Remote Function*). This is what
   lets the computer control it.
3. Plug the camera into your computer with the USB cable.

---

## Installation (one time)

### 1. Build the camera driver

Follow the build instructions in the
[alpha-sdk-api](https://github.com/crsdk/alpha-sdk-api) repository — clone it, download
Sony's Camera Remote SDK and place its files per that repo's `docs/SDK_SETUP.md`, then
build. The result is a binary called `CameraWebApp`.

Note where it lands (typically `api/server/build/CameraWebApp`). Either put it on your
`PATH`, or point `CAMERA_SERVER_BINARY` at it in step 4.

Check it runs:

```bash
/path/to/CameraWebApp --port 8080
```

It should stay in the foreground and serve `http://localhost:8080/api/server/status`.
Stop it with Ctrl-C — this server starts and stops it for you from here on.

### 2. Download this project

```bash
git clone https://github.com/crsdk/alpha-mcp-server.git
cd alpha-mcp-server
```

### 3. Install dependencies and build

```bash
npm install
npm run build
```

### 4. Note this folder's full path

```bash
pwd
```

Copy the path it prints (e.g. `/Users/you/alpha-mcp-server`). Keep it handy.

---

## Connect it to Claude Desktop

Because this runs as a local program (not a website), you register it in Claude Desktop's
**config file** — not the Connectors settings page.

1. Open **Claude Desktop**.
2. Go to **Settings → Developer → Edit Config**. This opens a file called
   `claude_desktop_config.json` in your text editor. (If it's blank, that's fine.)
3. Paste in the following, replacing `<PATH TO THIS FOLDER>` with the path from step 4
   and `<PATH TO CameraWebApp>` with the binary you built:

```json
{
  "mcpServers": {
    "alpha-camera": {
      "command": "node",
      "args": ["<PATH TO THIS FOLDER>/dist/server.js"],
      "cwd": "<PATH TO THIS FOLDER>",
      "env": {
        "CAMERA_SERVER_BINARY": "<PATH TO CameraWebApp>"
      }
    }
  }
}
```

> If `CameraWebApp` is already on your `PATH`, you can drop the `"env"` block —
> the server finds it there.

> If the file already had other servers in `"mcpServers"`, just add the `"alpha-camera"`
> block alongside them (mind the commas).

4. **Save the file, then fully quit and reopen Claude Desktop** (it only reads the config
   when it starts).

You'll know it worked when you see a tools/plug icon in the chat box and `alpha-camera`
listed among the available tools.

**Where is the config file?** (in case you'd rather edit it directly)
- **macOS:** `~/Library/Application Support/Claude/claude_desktop_config.json`
- **Windows:** `%APPDATA%\Claude\claude_desktop_config.json`

---

## Using it

You don't run any commands or call tools by name — you just talk to Claude. With the camera
plugged in and turned on, try:

- *"Connect to my camera and tell me its current settings."*
- *"Set me up for a portrait: f/2.8, 1/200 shutter, and auto ISO."*
- *"Take a photo and show me the result."*
- *"That's underexposed — raise the ISO and reshoot."*
- *"Give me a live view so I can see what the camera sees."*

Claude figures out the sequence of steps (connect → adjust settings → shoot → look at the
photo → refine) on its own. Your job is to describe what you want.

The camera driver isn't started until you actually connect, so nothing holds your camera
while it sits idle. When you're done, *"disconnect the camera"* releases it, and *"shut down
the camera server"* stops the driver entirely (a later *"connect"* starts it again
automatically). Everything also shuts down cleanly when you quit Claude Desktop.

> **A note on seeing photos:** Claude can't paste a picture into its own reply — no AI model
> can; they produce text. The actual image shows up in the **tool-result block** (the
> expandable row above Claude's message), while Claude's message gives you its *read* of the
> shot ("slightly underexposed, let's open up a stop"). Full-resolution keepers are saved to
> `~/.alpha-mcp/captures` so you can open them normally.

---

## Troubleshooting

**"The tools don't show up in Claude Desktop."**
Make sure you fully quit and reopened the app. Double-check the `args` path points at
`<this folder>/dist/server.js` (run `npm run build` if `dist/` is missing), and that `cwd`
points at this project folder.

**"No CameraWebApp binary found."**
The server could not locate the camera driver. Set `CAMERA_SERVER_BINARY` to its full
path in the Claude Desktop config (see above), or put the binary on your `PATH`. If you
haven't built it yet, start at step 1.

**"camera-server did not become ready" / "exited during startup".**
The binary was found but didn't come up. Run it by hand — `CameraWebApp --port 8080` —
and read the output; that's the same message the server reports back. A port clash is
the usual cause; either free port 8080 or set `ALPHA_BASE_URL` to a different port.

**"No cameras discovered."**
Confirm the camera is on, the USB cable is a data cable (not charge-only), and the camera's
USB mode is set to **PC Remote**. Try a different USB port.

**Autofocus won't fire a shot.**
If the scene is too dark or the lens can't lock focus, autofocus capture fails by design.
Ask Claude to *"take the photo without autofocus"* (or switch to manual focus first).

---

## What can it do? (tool reference)

The server exposes 28 tools; Claude picks the right ones automatically.

| Area | What Claude can do |
|---|---|
| **Session** | Find, connect to, and disconnect cameras; check status; start/stop the camera driver on demand |
| **See** | Read every camera setting (with the valid options for *your* body), pull a live-view frame, review the last shot |
| **Act** | Change one or many settings (and confirm they took), take photos, half-press for focus, nudge manual focus, power-zoom, start/stop video |
| **Files** | Browse the SD card, fetch quick previews, download full-resolution keepers |
| **Presets** | Save/restore full camera configurations, set where photos are saved, import LUTs |

---

## Optional: the camera Skill

This repo ships a Claude **Skill** at [`skills/sony-camera-control/`](./skills/sony-camera-control/SKILL.md)
that teaches Claude *how* to be a good camera operator — the connect → discover → configure →
shoot → review loop, focus/AF gotchas, and guided setups (astro, portrait, low-light, action)
plus exposure bracketing and focus stacking. The MCP server gives Claude the *tools*; the
Skill gives it the *technique*.

It's optional — the tools work without it — but it makes multi-step photographic requests much
smoother. Add it wherever your Claude reads skills (e.g. copy the `sony-camera-control` folder
into your Claude skills directory), then just talk to the camera as usual.

## For developers

Built in **TypeScript** with the [MCP SDK](https://github.com/modelcontextprotocol/typescript-sdk).

The API contract is the repo's single OpenAPI source at [`../api/openapi.yaml`](../api/openapi.yaml).
`src/api/schema.d.ts` and `src/api/property-names.ts` are
generated from it and committed, so a plain `npm install && npm run build` needs no codegen
step; CI regenerates them and fails on drift. `src/api/client.ts` is a thin typed wrapper
over those types, and `src/server-manager.ts` owns the `CameraWebApp` process lifecycle.
`src/events.ts` hand-rolls the Server-Sent-Events consumer, because the event stream is
outside what the spec's generators can produce.

Photographic *procedures* (bracketing, focus stacking, guided setups) are intentionally left
to a Claude **Skill**, so the server stays a faithful, un-opinionated hardware interface.

```bash
npm run dev        # run from source via tsx
npm run build      # compile src/ -> dist/
npm run typecheck  # types only, including tests
npm test           # unit tests (no camera or binary needed)
npm run codegen    # regenerate types + tools from ../api/openapi.yaml
```

### Configuration (optional environment variables)

| Variable | Default | Purpose |
|---|---|---|
| `ALPHA_BASE_URL` | `http://localhost:8080` | Where the camera driver listens |
| `ALPHA_MANAGE_BINARY` | `1` | Auto-start the camera driver (`0` = you run it yourself) |
| `CAMERA_SERVER_BINARY` | looked up on `PATH` | Explicit path to the `CameraWebApp` binary |
| `ALPHA_WORK_DIR` | `~/.alpha-mcp` | Where photos/previews are saved |
| `ALPHA_EVENT_BUFFER` | `512` | Event ring-buffer capacity |

### Running the driver separately (optional)

By default this server starts the camera driver for you (lazily, on the first connect). If
you'd rather run it yourself:

```bash
CameraWebApp --port 8080
```

Then set `ALPHA_MANAGE_BINARY=0` so the MCP server adopts your instance instead of managing
its own. The binary takes a single `--port` flag; stop it with Ctrl-C.

---

## License

MIT — see [`LICENSE`](./LICENSE) and [`NOTICE`](./NOTICE). The `CameraWebApp` binary this
server drives links against Sony's Camera Remote SDK, which is covered by Sony's own licence.
