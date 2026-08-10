// Emit the flat 1:1 MCP tools from the OpenAPI spec.
//
// The "flat" tools are thin wrappers: validate input -> one ctx.client.<group>.<method>()
// -> return the raw response. They carry no orchestration, so they are generated
// here instead of hand-written. Composites (connect, capture, live-view, events)
// stay hand-written in src/tools/*.ts — they own sequence, quirks, state, and SSE.
//
// The manifest below is the source of truth for each flat tool's curated name,
// input schema, client call, arg mapping, guard, and error label. Output is the
// raw API response (standard shape) via textResult(res).
//
// A coverage gate cross-checks every annotated spec operation against three
// buckets (generated here / hand-written in composites / deliberately un-exposed)
// plus x-fern-ignore, so a newly-added endpoint can't ship unclassified.
import { readFileSync, writeFileSync } from "node:fs";

const SPEC = "../api/openapi.yaml";
const OUT = "src/tools/generated.ts";

// ---------------------------------------------------------------------------
// Manifest — one entry per generated flat tool.
//   name    tool name (agent-facing; may differ from operationId)
//   op      operationId (coverage cross-check)
//   call    ctx.client.<call>(...)
//   input   Zod raw shape (agent-facing param names, snake_case)
//   args    the client-call argument expression (maps snake_case -> camelCase)
//   pre?    a statement emitted before the call (e.g. compute a derived value)
//   guard?  a validation statement that throws ToolError
//   errLabel  label used in the catch message: `<errLabel> failed: ...`
//   errHint?  extra expression appended inside the catch template literal
// ---------------------------------------------------------------------------
const TOOLS = [
  {
    name: "get_property",
    op: "getProperty",
    desc: "Read one property with available_values + writability. name is a slug discovered from get_camera_state (e.g. iso, aperture, shutter-speed).",
    input: { name: "z.string()", camera_id: "z.string().optional()" },
    guard:
      'if (!isPropertyName(args.name)) throw new ToolError(`get_property(${args.name}) failed: unknown property slug; see get_camera_state for valid names.`);',
    call: "properties.get",
    args: "{ cameraId, propertyName: args.name }",
    errLabel: "get_property(${args.name})",
  },
  {
    name: "half_press",
    op: "halfPress",
    desc: "S1 half-press to lock AF without firing.",
    input: { camera_id: "z.string().optional()" },
    call: "actions.halfPress",
    args: "{ cameraId }",
    errLabel: "half_press",
  },
  {
    name: "focus_step",
    op: "focusNearFar",
    desc: "Discrete manual-focus nudge: -7 (near, fast) to +7 (far, fast); magnitude = speed. 0 is invalid. The primitive for a look->step->look loop.",
    input: { step: "z.number().int()", camera_id: "z.string().optional()" },
    guard:
      'if (args.step === 0 || args.step < -7 || args.step > 7) throw new ToolError("step must be -7..-1 or +1..+7 (0 is invalid).");',
    call: "actions.focusNearFar",
    args: "{ cameraId, step: args.step }",
    errLabel: "focus_step",
    errHint: " (check the lens MF switch and that focus-near-far is enabled).",
  },
  {
    name: "zoom",
    op: "controlZoom",
    desc: "Power-zoom control (requires a power-zoom lens). direction: wide/tele (or in/out); speed 1-8.",
    input: {
      direction: 'z.enum(["wide", "tele", "in", "out"])',
      speed: "z.number().int().default(3)",
      camera_id: "z.string().optional()",
    },
    pre: 'const signed = args.direction === "tele" || args.direction === "in" ? Math.abs(args.speed) : -Math.abs(args.speed);',
    call: "actions.zoom",
    args: "{ cameraId, body: { speed: signed } }",
    errLabel: "zoom",
  },
  {
    name: "toggle_movie_rec",
    op: "toggleMovieRecording",
    desc: "Start/stop movie recording (server toggles). Camera must be in a movie mode.",
    input: { camera_id: "z.string().optional()" },
    call: "actions.movieRec",
    args: "{ cameraId }",
    errLabel: "toggle_movie_rec",
  },
  {
    name: "get_save_config",
    op: "getSaveInfo",
    desc: "Read host save-path config (where auto-transferred files land).",
    input: { camera_id: "z.string().optional()" },
    call: "settings.getSaveInfo",
    args: "{ cameraId }",
    errLabel: "get_save_config",
  },
  {
    name: "set_save_config",
    op: "setSaveInfo",
    desc: "Set host save path / naming for auto-transferred captures.",
    input: {
      path: "z.string().optional()",
      prefix: "z.string().optional()",
      start_no: "z.number().int().optional()",
      camera_id: "z.string().optional()",
    },
    call: "settings.setSaveInfo",
    args: "{ cameraId, path: args.path, prefix: args.prefix, startNo: args.start_no }",
    errLabel: "set_save_config",
  },
  {
    name: "snapshot_camera_settings",
    op: "downloadCameraSettings",
    desc: "Download the camera's full settings to a host file (save a reusable config).",
    input: { filename: "z.string().optional()", camera_id: "z.string().optional()" },
    call: "settings.download",
    args: "{ cameraId, body: { filename: args.filename } }",
    errLabel: "snapshot",
  },
  {
    name: "restore_camera_settings",
    op: "uploadCameraSettings",
    desc: "Upload a previously saved settings file back to the camera (overwrites live config).",
    input: { filename: "z.string()", camera_id: "z.string().optional()" },
    call: "settings.upload",
    args: "{ cameraId, body: { filename: args.filename } }",
    errLabel: "restore",
  },
  {
    name: "list_settings_files",
    op: "listSettingsFiles",
    desc: "List saved camera-settings files on the host.",
    input: { camera_id: "z.string().optional()" },
    call: "settings.listFiles",
    args: "{ cameraId }",
    errLabel: "list_settings_files",
  },
  {
    name: "import_lut",
    op: "importLUT",
    desc: "Import a LUT to the camera (niche; faithful passthrough).",
    input: {
      file_path: "z.string()",
      slot: "z.number().int().optional()",
      camera_id: "z.string().optional()",
    },
    call: "settings.importLut",
    args: "{ cameraId, filePath: args.file_path, slot: args.slot }",
    errLabel: "import_lut",
  },
  {
    name: "list_captures",
    op: "listSDCardFiles",
    desc: "List SD-card files for a slot. Requires remote-transfer or contents mode.",
    input: { slot: "z.number().int().default(1)", camera_id: "z.string().optional()" },
    call: "sdCard.list",
    args: "{ cameraId, slotNumber: args.slot }",
    errLabel: "list_captures",
    errHint: '${ctx.state.mode === "remote" ? " (reconnect in remote-transfer or contents mode)" : ""}',
  },
];

// Operations consumed by hand-written composites (not generated, on purpose).
const HANDWRITTEN = new Set([
  "connectCamera", "disconnectCamera", "getConnectionStatus", "listCameras",
  "setPriorityKey", "getAllProperties", "setProperty",
  "triggerShutter", "afShutter",
  "enableLiveView", "disableLiveView", "startLiveViewStream", "stopLiveViewStream",
  "getLiveViewStatus", "getLiveViewFrame", "enableOSD", "disableOSD",
  "getOSDStatus", "getOSDFrame",
  "downloadSDCardFile", "downloadSDCardThumbnail", "downloadSDCardScreennail",
  "getServerStatus", "getServerLogs", "shutdownServer",
]);

// Endpoints that exist in the spec but are deliberately NOT surfaced as tools.
const NOT_EXPOSED = new Set([
  "getPriorityKey",
  "getAFAreaPosition", "setAFAreaPosition", "getTrackingFrame",
  "getFingerprint",
  "pressCameraButton", "remoteTouch", "cancelRemoteTouch",
]);

// ---------------------------------------------------------------------------
// Coverage gate: every annotated spec operation must land in exactly one bucket.
// ---------------------------------------------------------------------------
const spec = readFileSync(SPEC, "utf8");

// Line-scan: track the current operationId; record which ones carry x-fern-ignore.
const allOps = [];
const ignored = new Set();
let current = null;
for (const line of spec.split("\n")) {
  const m = line.match(/^\s*operationId:\s*(\w+)/);
  if (m) { current = m[1]; allOps.push(current); continue; }
  if (current && /^\s*x-fern-ignore:\s*true/.test(line)) ignored.add(current);
}
if (allOps.length === 0) {
  console.error(`No operationIds found in ${SPEC} — refusing to write.`);
  process.exit(1);
}

const generated = new Set(TOOLS.map((t) => t.op));

// Every manifest op must exist in the spec.
for (const op of generated) {
  if (!allOps.includes(op)) {
    console.error(`Manifest tool references operationId '${op}' which is not in ${SPEC}.`);
    process.exit(1);
  }
}

// Every spec op must be classified.
const unclassified = allOps.filter(
  (op) => !generated.has(op) && !HANDWRITTEN.has(op) && !NOT_EXPOSED.has(op) && !ignored.has(op),
);
if (unclassified.length > 0) {
  console.error(
    `${unclassified.length} spec operation(s) are neither generated, hand-written, nor ignored:\n` +
      unclassified.map((o) => `  - ${o}`).join("\n") +
      `\nClassify each in scripts/gen-tools.mjs (TOOLS manifest / HANDWRITTEN / NOT_EXPOSED).`,
  );
  process.exit(1);
}

// ---------------------------------------------------------------------------
// Emit src/tools/generated.ts
// ---------------------------------------------------------------------------
function emitTool(t) {
  const shape = Object.entries(t.input).map(([k, v]) => `      ${k}: ${v},`).join("\n");
  const lines = [];
  lines.push(`  defineTool(`);
  lines.push(`    server,`);
  lines.push(`    ${JSON.stringify(t.name)},`);
  lines.push(`    ${JSON.stringify(t.desc)},`);
  lines.push(`    {`);
  lines.push(shape);
  lines.push(`    },`);
  lines.push(`    async (args) => {`);
  lines.push(`      const cameraId = ctx.resolveCamera(args.camera_id);`);
  if (t.pre) lines.push(`      ${t.pre}`);
  if (t.guard) lines.push(`      ${t.guard}`);
  lines.push(`      let res;`);
  lines.push(`      try {`);
  lines.push(`        res = await ctx.client.${t.call}(${t.args});`);
  lines.push(`      } catch (e) {`);
  lines.push(`        throw new ToolError(\`${t.errLabel} failed: \${apiMessage(e)}${t.errHint ?? ""}\`);`);
  lines.push(`      }`);
  lines.push(`      return textResult(res);`);
  lines.push(`    },`);
  lines.push(`  );`);
  return lines.join("\n");
}

const body = TOOLS.map(emitTool).join("\n\n");

const out = `// GENERATED by scripts/gen-tools.mjs — do not edit.
// Source: ../api/openapi.yaml (flat 1:1 operations) + the manifest in scripts/gen-tools.mjs.
// Composites, SSE, and session state stay hand-written in the other src/tools/*.ts.
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { z } from "zod";
import type { Ctx } from "../context.js";
import { isPropertyName } from "../api/property-names.js";
import { apiMessage, ToolError } from "../errors.js";
import { defineTool, textResult } from "./helpers.js";

/** Register the generated flat 1:1 tools. */
export function register(server: McpServer, ctx: Ctx): void {
${body}
}
`;

writeFileSync(OUT, out);
console.log(
  `✨ ${OUT} — ${TOOLS.length} flat tools ` +
    `(${HANDWRITTEN.size} hand-written, ${NOT_EXPOSED.size} un-exposed, ${ignored.size} ignored, ${allOps.length} total ops covered)`,
);
