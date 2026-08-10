// AlphaApi request construction, checked against a recording fetch.
//
// The tool layer was written against the retired generated client, so what
// matters here is that each facade method still lands on the URL, method and
// body the spec defines — a silent mismatch would only surface with hardware.
import assert from "node:assert/strict";
import { afterEach, beforeEach, describe, it } from "node:test";
import { AlphaApi, ApiError, slot } from "../src/api/client.js";
import { isPropertyName, PROPERTY_NAMES } from "../src/api/property-names.js";

const BASE = "http://localhost:8080";

interface Call {
  url: string;
  method: string;
  body: unknown;
}

let calls: Call[];
let respond: () => { status: number; body: unknown };
let realFetch: typeof globalThis.fetch;

beforeEach(() => {
  calls = [];
  respond = () => ({ status: 200, body: { success: true } });
  realFetch = globalThis.fetch;
  globalThis.fetch = (async (input: RequestInfo | URL, init?: RequestInit) => {
    const req = new Request(input, init);
    const raw = await req.text();
    calls.push({
      url: req.url,
      method: req.method,
      body: raw ? JSON.parse(raw) : undefined,
    });
    const { status, body } = respond();
    return new Response(JSON.stringify(body), {
      status,
      headers: { "Content-Type": "application/json" },
    });
  }) as typeof fetch;
});

afterEach(() => {
  globalThis.fetch = realFetch;
});

const api = () => new AlphaApi(BASE);
const only = () => {
  assert.equal(calls.length, 1, `expected exactly one request, got ${calls.length}`);
  return calls[0];
};

describe("connection", () => {
  it("lists cameras", async () => {
    respond = () => ({ status: 200, body: { success: true, cameras: [] } });
    await api().cameras.list();
    assert.deepEqual(only(), { url: `${BASE}/api/cameras`, method: "GET", body: undefined });
  });

  it("connects with mode and reconnecting in the body", async () => {
    await api().cameras.connect({ cameraId: "CAM1", mode: "remote-transfer", reconnecting: "on" });
    const call = only();
    assert.equal(call.url, `${BASE}/api/cameras/CAM1/connection`);
    assert.equal(call.method, "POST");
    assert.deepEqual(call.body, { mode: "remote-transfer", reconnecting: "on" });
  });

  it("disconnects with DELETE on the same path", async () => {
    await api().cameras.disconnect({ cameraId: "CAM1" });
    assert.deepEqual(only(), {
      url: `${BASE}/api/cameras/CAM1/connection`,
      method: "DELETE",
      body: undefined,
    });
  });
});

describe("properties", () => {
  it("gets a property by slug", async () => {
    respond = () => ({ status: 200, body: { success: true, data: { value: "0xc80" } } });
    await api().properties.get({ cameraId: "CAM1", propertyName: "iso" });
    assert.equal(only().url, `${BASE}/api/cameras/CAM1/properties/iso`);
  });

  it("sets a property with PUT and a {value} body", async () => {
    await api().properties.set({ cameraId: "CAM1", propertyName: "shutter-speed", value: "1/250" });
    const call = only();
    assert.equal(call.url, `${BASE}/api/cameras/CAM1/properties/shutter-speed`);
    assert.equal(call.method, "PUT");
    assert.deepEqual(call.body, { value: "1/250" });
  });

  it("claims the priority key with the setting in the body", async () => {
    await api().properties.setPriorityKey({ cameraId: "CAM1", setting: "pc-remote" });
    const call = only();
    assert.equal(call.url, `${BASE}/api/cameras/CAM1/priority-key`);
    assert.equal(call.method, "PUT");
    assert.deepEqual(call.body, { setting: "pc-remote" });
  });

  it("reads all properties from the bulk endpoint", async () => {
    respond = () => ({ status: 200, body: { success: true, data: { properties: {} } } });
    await api().properties.getAll({ cameraId: "CAM1" });
    assert.equal(only().url, `${BASE}/api/cameras/CAM1/properties/all`);
  });
});

describe("actions", () => {
  it("fires a plain shutter with no action field", async () => {
    await api().actions.shutter({ cameraId: "CAM1" });
    const call = only();
    assert.equal(call.url, `${BASE}/api/cameras/CAM1/actions/shutter`);
    assert.equal(call.method, "POST");
    assert.deepEqual(call.body, {});
  });

  it("passes the continuous-shooting phase through", async () => {
    await api().actions.shutter({ cameraId: "CAM1", action: "down" });
    assert.deepEqual(only().body, { action: "down" });
  });

  it("sends focus steps as a signed step", async () => {
    await api().actions.focusNearFar({ cameraId: "CAM1", step: -3 });
    const call = only();
    assert.equal(call.url, `${BASE}/api/cameras/CAM1/actions/focus-near-far`);
    assert.deepEqual(call.body, { step: -3 });
  });

  it("sends zoom as a signed speed", async () => {
    await api().actions.zoom({ cameraId: "CAM1", body: { speed: -3 } });
    const call = only();
    assert.equal(call.url, `${BASE}/api/cameras/CAM1/actions/zoom`);
    assert.deepEqual(call.body, { speed: -3 });
  });

  it("routes af-shutter, half-press and movie-rec to their own paths", async () => {
    const client = api();
    await client.actions.afShutter({ cameraId: "CAM1" });
    await client.actions.halfPress({ cameraId: "CAM1" });
    await client.actions.movieRec({ cameraId: "CAM1" });
    assert.deepEqual(
      calls.map((c) => c.url.replace(`${BASE}/api/cameras/CAM1/actions/`, "")),
      ["af-shutter", "half-press", "movie-rec"],
    );
  });
});

describe("live view", () => {
  it("returns the raw Response for frames so callers can read bytes", async () => {
    globalThis.fetch = (async () =>
      new Response(new Uint8Array([0xff, 0xd8, 0xff]), {
        status: 200,
        headers: { "Content-Type": "image/jpeg" },
      })) as typeof fetch;
    const res = await api().liveView.getFrame({ cameraId: "CAM1" });
    assert.equal(new Uint8Array(await res.arrayBuffer()).length, 3);
  });

  it("throws ApiError when no frame is ready yet", async () => {
    globalThis.fetch = (async () =>
      new Response(JSON.stringify({ success: false, message: "NoFrameAvailable" }), {
        status: 404,
        headers: { "Content-Type": "application/json" },
      })) as typeof fetch;
    await assert.rejects(
      () => api().liveView.getFrame({ cameraId: "CAM1" }),
      (err: unknown) => err instanceof ApiError && err.statusCode === 404,
    );
  });

  it("uses the osd-frame path for the composited frame", async () => {
    globalThis.fetch = (async (input: RequestInfo | URL) => {
      calls.push({ url: new Request(input).url, method: "GET", body: undefined });
      return new Response(new Uint8Array([0xff]), { status: 200 });
    }) as typeof fetch;
    await api().liveView.getOsdFrame({ cameraId: "CAM1" });
    assert.equal(only().url, `${BASE}/api/cameras/CAM1/live-view/osd-frame`);
  });
});

describe("sd card", () => {
  it("lists files for a slot", async () => {
    respond = () => ({ status: 200, body: { success: true, slot: 1, file_count: 0, files: [] } });
    await api().sdCard.list({ cameraId: "CAM1", slotNumber: 1 });
    assert.equal(only().url, `${BASE}/api/cameras/CAM1/sd-card/slot/1/files`);
  });

  it("builds the nested download path from content and file ids", async () => {
    respond = () => ({ status: 200, body: { success: true, message: "accepted" } });
    await api().sdCard.download({ cameraId: "CAM1", slotNumber: 2, contentId: 12, fileId: 34 });
    const call = only();
    assert.equal(call.url, `${BASE}/api/cameras/CAM1/sd-card/slot/2/files/12/34/download`);
    assert.equal(call.method, "POST");
    assert.deepEqual(call.body, {});
  });

  it("passes save_path through when given", async () => {
    respond = () => ({ status: 200, body: { success: true, message: "ok" } });
    await api().sdCard.downloadScreennail({
      cameraId: "CAM1",
      slotNumber: 1,
      contentId: 1,
      fileId: 2,
      body: { save_path: "/tmp/shots" },
    });
    const call = only();
    assert.ok(call.url.endsWith("/files/1/2/screennail"));
    assert.deepEqual(call.body, { save_path: "/tmp/shots" });
  });

  it("rejects a slot the spec does not define, without a round trip", () => {
    assert.throws(() => slot(3), (err: unknown) => err instanceof ApiError);
    assert.equal(calls.length, 0);
  });
});

describe("settings", () => {
  it("sets the host save path", async () => {
    await api().settings.setSaveInfo({ cameraId: "CAM1", path: "/tmp/captures" });
    const call = only();
    assert.equal(call.url, `${BASE}/api/cameras/CAM1/settings/save-info`);
    assert.equal(call.method, "PUT");
    assert.deepEqual(call.body, { path: "/tmp/captures" });
  });

  it("defaults the LUT slot to 1", async () => {
    respond = () => ({ status: 200, body: { success: true, message: "ok" } });
    await api().settings.importLut({ cameraId: "CAM1", filePath: "/tmp/a.cube" });
    assert.deepEqual(only().body, { filePath: "/tmp/a.cube", slot: 1 });
  });
});

describe("server", () => {
  it("passes log query parameters", async () => {
    respond = () => ({ status: 200, body: { success: true, logs: [] } });
    await api().server.logs({ lines: 50, level: "warn" });
    const url = new URL(only().url);
    assert.equal(url.pathname, "/api/server/logs");
    assert.equal(url.searchParams.get("lines"), "50");
    assert.equal(url.searchParams.get("level"), "warn");
  });
});

describe("error handling", () => {
  it("throws ApiError carrying the status and parsed body", async () => {
    respond = () => ({ status: 400, body: { success: false, message: "Camera not connected" } });
    await assert.rejects(
      () => api().properties.get({ cameraId: "CAM1", propertyName: "iso" }),
      (err: unknown) => {
        assert.ok(err instanceof ApiError);
        assert.equal(err.statusCode, 400);
        assert.equal(err.message, "Camera not connected");
        return true;
      },
    );
  });
});

describe("property names", () => {
  it("carries the spec's slugs", () => {
    assert.ok(PROPERTY_NAMES.length > 50, "expected the full generated slug list");
    for (const slug of ["iso", "aperture", "shutter-speed", "focus-mode", "priority-key"]) {
      assert.ok(isPropertyName(slug), `${slug} should be a known property`);
    }
  });

  it("rejects slugs the spec does not define", () => {
    assert.equal(isPropertyName("iso-invalid"), false);
    assert.equal(isPropertyName("ISO"), false);
    assert.equal(isPropertyName(""), false);
  });
});
