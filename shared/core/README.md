# shared/core — stock Sony SDK sample helpers (not in this repo)

This folder holds Sony's **stock** Camera Remote SDK sample helper sources
(`CameraDevice`, `PropertyValueTable`, `CrDebugString`, `MessageDefine`,
`ConnectionInfo`, `Text`, `OpenCVWrapper`) — the files from the SDK download's
`app/` folder, unmodified.

**These source files are part of Sony's Camera Remote SDK and are not
redistributed in this open-source repository.** Only `CMakeLists.txt` (build
configuration) and this `README.md` are tracked here.

The REST server does **not** modify these files. The server's own non-interactive
device layer lives in [`../../api/server/src/device/`](../../api/server/src/device/)
(MIT-licensed) and links against these stock helpers.

## How the sources get here

They ship inside the Camera Remote SDK download (`app/` folder) and are placed
automatically by `crsdk install`:

```bash
./crsdk install --zip /path/to/CrSDK.zip
```

See [../../docs/SDK_SETUP.md](../../docs/SDK_SETUP.md) for the full setup,
expected file list, and troubleshooting.

## Why it lives here

The SDK ships these helpers as source; keeping them out of the public repo (and
keeping the REST server's own logic in `api/server/`) lets the project ship under
the MIT license without redistributing Sony's SDK source. Do not commit the
`.cpp` / `.h` files back into this repository.
