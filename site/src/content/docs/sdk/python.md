---
title: "Python SDK"
description: "alpha-sdk-client — sync + async REST clients with Pydantic models"
---

`alpha-sdk-client` is the official Python client for the Alpha Camera REST API. It ships both sync (`AlphaSDKClient`) and async (`AsyncAlphaSDKClient`) variants and uses `httpx` for transport.

- **PyPI:** [`alpha-sdk-client`](https://pypi.org/project/alpha-sdk-client/)
- **Latest:** `0.3.0`
- **Python:** 3.8+
- **Example app / notebook helper:** [`alpha-sdk-python`](https://github.com/crsdk/alpha-sdk-python)

---

## Install

```bash
pip install alpha-sdk-client
```

You also need a running server on `localhost:8080`. The Python client does **not** ship a `ServerManager` class — start the server using the Node-based CLI from the [server package](/alpha-sdk-api/web-api/server/), or spawn the binary as a subprocess from Python ([recipe](/alpha-sdk-api/sdk/recipes/server-subprocess/)).

```bash
# One-time install of the server package (Node)
npm install -g @alpha-sdk/api
camera-server start
```

---

## Initialize

```python
from alpha_sdk_client import AlphaSDKClient

client = AlphaSDKClient(base_url="http://localhost:8080")
```

### Constructor options

```python
AlphaSDKClient(
    base_url="http://localhost:8080",   # base URL — defaults to localhost:8080
    environment=None,                   # alternative — pass an Environment
    headers={"X-Trace-Id": "abc123"},   # applied to every request
    timeout=30.0,                       # seconds
    httpx_client=None,                  # bring your own httpx.Client
    follow_redirects=True,
)
```

### Async variant

```python
import asyncio
from alpha_sdk_client import AsyncAlphaSDKClient

async def main():
    client = AsyncAlphaSDKClient(base_url="http://localhost:8080")
    cameras = await client.cameras.list()
    print(cameras)

asyncio.run(main())
```

The async client mirrors the sync API method-for-method — every call is `await`-able.

---

## Common operations

### List + connect a camera

```python
res = client.cameras.list()
camera_id = res.cameras[0].id

client.cameras.connect(
    camera_id=camera_id,
    mode="remote-transfer",
    reconnecting="on",
)

# Required for the host PC to drive most settings:
client.properties.set_priority_key(
    camera_id=camera_id,
    setting="pc-remote",
)
```

### Read a single property

```python
aperture = client.properties.get(camera_id=camera_id, property_name="aperture")
print(aperture.data.value, aperture.data.formatted)
```

### Read every property at once

```python
res = client.properties.get_all(camera_id=camera_id)
print(res.data.properties["iso"].current_formatted)
```

### Set a property

```python
client.properties.set(
    camera_id=camera_id,
    property_name="shutter-speed",
    value="1/250",
)
```

### Trigger the shutter

```python
client.actions.shutter(camera_id=camera_id)
# In `remote` mode the server auto-transfers; poll SSE or check the save
# directory for the new file.
# In `remote-transfer` mode call client.sd_card.download(...) explicitly.
```

### Pull a single live-view JPEG

```python
from PIL import Image
from io import BytesIO

jpeg_bytes = b"".join(client.live_view.get_frame(camera_id=camera_id))
img = Image.open(BytesIO(jpeg_bytes))
img.show()
```

For OSD-overlaid frames after `client.live_view.enable_osd(camera_id=...)`:

```python
jpeg_bytes = client.live_view.get_osd_frame(camera_id=camera_id)
```

See the [live-view polling recipe](/alpha-sdk-api/sdk/recipes/live-view-polling/) for a render loop.

### Disconnect

```python
client.cameras.disconnect(camera_id=camera_id)
```

---

## Error handling

Every method raises a typed subclass of `ApiError`:

```python
from alpha_sdk_client import (
    BadRequestError,
    NotFoundError,
    InternalServerError,
)
from alpha_sdk_client.core.api_error import ApiError

try:
    client.properties.set(
        camera_id=camera_id,
        property_name="aperture",
        value="F1.0",  # not a valid value on this lens
    )
except BadRequestError as e:
    print("server rejected the value:", e.body)
except NotFoundError:
    print("camera no longer connected")
except ApiError as e:
    print("server error", e.status_code, e.body)
```

Each error exposes `status_code`, `body` (parsed dict), and `headers`. Network failures and timeouts raise `httpx.HTTPError` subclasses (not `ApiError`).

---

## Advanced

### Custom request options per call

```python
client.cameras.list(
    request_options={
        "additional_headers": {"X-Trace-Id": "request-42"},
        "timeout_in_seconds": 60,
    }
)
```

### Bring your own httpx.Client

Useful for proxying, retries, or testing:

```python
import httpx
from alpha_sdk_client import AlphaSDKClient

httpx_client = httpx.Client(
    proxies="http://corp-proxy:8080",
    transport=httpx.HTTPTransport(retries=3),
)
client = AlphaSDKClient(base_url="http://localhost:8080", httpx_client=httpx_client)
```

### Pydantic models

Every response body is a Pydantic v2 model — use `.model_dump()` to serialize, `.model_validate()` to construct from a dict:

```python
res = client.cameras.list()
print(res.model_dump_json(indent=2))
```

### Use in a Jupyter notebook

The sync client works seamlessly in notebooks. For long-running async patterns inside Jupyter, use `await` at the top level (Jupyter supports it natively):

```python
client = AsyncAlphaSDKClient(base_url="http://localhost:8080")
res = await client.cameras.list()
```

For a minimal notebook-oriented helper layer, see:

- [`alpha-sdk-python`](https://github.com/crsdk/alpha-sdk-python)
- [`notebook_data_collection.py`](https://github.com/crsdk/alpha-sdk-python/blob/main/notebook_data_collection.py)

---

## What's NOT in the client

By design, the following live in [recipes](/alpha-sdk-api/sdk/overview/#recipes) rather than the generated SDK:

- **SSE events** — use `httpx.stream()` against `/api/events` and parse `text/event-stream` line-by-line.
- **Discovery + reconnect orchestration** — copy from the [discovery + reconnect recipe](/alpha-sdk-api/sdk/recipes/discovery-reconnect/).

---

## Versioning

`alpha-sdk-client` follows SemVer. Every release is documented in the package CHANGELOG, available on PyPI.
