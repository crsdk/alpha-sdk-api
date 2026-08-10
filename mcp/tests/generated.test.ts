// Registration guard: every tool module registers exactly once, no name collides,
// and the full expected tool set is present. Catches a forgotten deletion after
// the flat tools were moved into the generated module.
import assert from "node:assert/strict";
import { describe, it } from "node:test";
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import type { Ctx } from "../src/context.js";
import * as session from "../src/tools/session.js";
import * as perception from "../src/tools/perception.js";
import * as action from "../src/tools/action.js";
import * as events from "../src/tools/events.js";
import * as files from "../src/tools/files.js";
import * as generated from "../src/tools/generated.js";

// The 12 flat tools now owned by the generated module.
const GENERATED = [
  "get_property", "half_press", "focus_step", "zoom", "toggle_movie_rec",
  "get_save_config", "set_save_config", "snapshot_camera_settings",
  "restore_camera_settings", "list_settings_files", "import_lut", "list_captures",
];

// The hand-written composites / SSE / session tools.
const HANDWRITTEN = [
  "list_cameras", "connect_camera", "disconnect_camera", "get_connection_status",
  "stop_camera_server", "get_server_diagnostics",
  "get_camera_state", "get_live_frame", "get_last_capture",
  "set_property", "configure", "capture", "capture_and_review",
  "poll_events", "get_capture_preview", "download_capture",
];

function collectToolNames(): string[] {
  const names: string[] = [];
  const stub = { registerTool: (name: string) => names.push(name) } as unknown as McpServer;
  const ctx = {} as Ctx; // handlers aren't invoked at registration time
  for (const mod of [session, perception, action, events, files, generated]) {
    mod.register(stub, ctx);
  }
  return names;
}

describe("tool registration", () => {
  it("registers no duplicate tool names", () => {
    const names = collectToolNames();
    const dupes = names.filter((n, i) => names.indexOf(n) !== i);
    assert.deepEqual(dupes, [], `duplicate tool registrations: ${dupes.join(", ")}`);
  });

  it("registers exactly the expected tool set", () => {
    const names = new Set(collectToolNames());
    const expected = [...GENERATED, ...HANDWRITTEN];
    assert.equal(names.size, expected.length, `expected ${expected.length} tools, got ${names.size}`);
    for (const name of expected) {
      assert.ok(names.has(name), `missing tool: ${name}`);
    }
  });

  it("the generated module owns exactly the flat tools", () => {
    const names: string[] = [];
    const stub = { registerTool: (name: string) => names.push(name) } as unknown as McpServer;
    generated.register(stub, {} as Ctx);
    assert.deepEqual(names.sort(), [...GENERATED].sort());
  });
});
