// Action layer: write properties and drive the camera (shutter, focus, zoom).
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { readFileSync } from "node:fs";
import { z } from "zod";
import type { PropertyName } from "../api/client.js";
import { isPropertyName } from "../api/property-names.js";
import type { Ctx } from "../context.js";
import type { CameraEvent } from "../events.js";
import { apiMessage, ToolError } from "../errors.js";
import { downloadPreview, newestSd, savedPathOf, waitNewSd } from "./files.js";
import { defineTool, textResult } from "./helpers.js";

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

// Properties that select a mode; apply before the props they govern.
const MODE_FIRST = new Set(["exposure-program-mode", "focus-mode", "drive-mode", "movie-shooting-mode"]);

export function valueMatches(value: string, confirmed: unknown, formatted: unknown): boolean {
  const v = String(value).trim().toLowerCase();
  const fmt = String(formatted).trim().toLowerCase();
  if (v === String(confirmed).trim().toLowerCase() || v === fmt) return true;
  // Formatted values often carry a label prefix ("ISO 3200"); treat suffix match as a match.
  return v.length > 0 && fmt.replace(/\s/g, "").endsWith(v.replace(/\s/g, ""));
}

interface SetResult {
  name: string;
  requested: string;
  confirmed_value?: unknown;
  confirmed_formatted?: unknown;
  matched: boolean;
  error?: string;
}

/**
 * Resolve a human-readable request to the hex from a property's available_values.
 * Some enum properties (movie-file-format, movie-recording-frame-rate, ...) have no
 * working string parser server-side, so "XAVC S 4K" / "24p" are rejected or mis-parsed;
 * the available_values table is the authority. Returns a hex string or null (no match).
 */
export async function resolveFromAvailable(
  ctx: Ctx,
  cameraId: string,
  propertyName: PropertyName,
  value: string,
): Promise<string | null> {
  let options: Array<Record<string, unknown>>;
  try {
    const res = await ctx.client.properties.get({ cameraId, propertyName });
    options = (res.data?.available_values ?? []) as Array<Record<string, unknown>>;
  } catch {
    return null;
  }
  const want = String(value).trim().toLowerCase();
  for (const opt of options) {
    const hex = String(opt.hex_value ?? opt.value ?? "");
    const fmt = String(opt.formatted ?? "").trim().toLowerCase();
    if (want === fmt || want === hex.trim().toLowerCase()) return hex;
  }
  return null;
}

async function setOne(ctx: Ctx, cameraId: string, name: string, value: string, retry = true): Promise<SetResult> {
  if (!isPropertyName(name)) {
    return {
      name,
      requested: value,
      matched: false,
      error: `unknown property slug '${name}'; see get_camera_state for valid names.`,
    };
  }
  const propertyName = name;
  try {
    await ctx.client.properties.set({ cameraId, propertyName, value: String(value) });
  } catch (e) {
    // Enum properties can reject or mis-parse a human-readable value. Fall back to
    // the hex from available_values and retry once before giving up.
    const hex = await resolveFromAvailable(ctx, cameraId, propertyName, value);
    if (!hex || hex === String(value)) {
      return { name, requested: value, matched: false, error: apiMessage(e) };
    }
    try {
      await ctx.client.properties.set({ cameraId, propertyName, value: hex });
    } catch (e2) {
      return { name, requested: value, matched: false, error: apiMessage(e2) };
    }
  }
  let confirmed: unknown;
  let formatted: unknown;
  try {
    const res = await ctx.client.properties.get({ cameraId, propertyName });
    confirmed = res.data?.value;
    formatted = res.data?.formatted;
  } catch (e) {
    return { name, requested: value, matched: false, error: apiMessage(e) };
  }
  const matched = valueMatches(value, confirmed, formatted);
  // A read-back miss right after connect/priority-key is usually a settle race,
  // not real coercion. Re-send once; genuine coercion still reports matched=false.
  if (!matched && retry) {
    await sleep(400);
    return setOne(ctx, cameraId, name, value, false);
  }
  return { name, requested: value, confirmed_value: confirmed, confirmed_formatted: formatted, matched };
}

async function fireShutter(ctx: Ctx, cameraId: string, autofocus: boolean): Promise<void> {
  try {
    if (autofocus) await ctx.client.actions.afShutter({ cameraId });
    else await ctx.client.actions.shutter({ cameraId });
  } catch (e) {
    const msg = apiMessage(e);
    if (msg.includes("focus_mode_not_af") || msg.includes("Manual Focus")) {
      throw new ToolError(
        "AF+shutter failed: camera is in Manual Focus. Switch focus-mode to AF_S/AF_C, " +
          "or use capture_and_review(autofocus=false).",
      );
    }
    throw new ToolError(`capture failed: ${msg}`);
  }
}

/** Update lastCaptureRef from a save/transfer event; return the ref. */
export function recordSaved(ctx: Ctx, ev: CameraEvent): { savedPath: string; filename?: string } | null {
  const path = savedPathOf(ev.data);
  if (!path) return null;
  const ref = { savedPath: path, filename: ev.data.filename as string | undefined };
  ctx.state.lastCaptureRef = ref;
  return ref;
}

export function register(server: McpServer, ctx: Ctx): void {
  defineTool(
    server,
    "set_property",
    "Set one property and read the accepted value back — the 'set ISO to 5000, " +
      "confirm it took' primitive. value is human-readable (f/5.6, 1/250, 400) or a " +
      "hex from available_values. matched=false surfaces a camera coercion.",
    { name: z.string(), value: z.string(), camera_id: z.string().optional() },
    async ({ name, value, camera_id }) => {
      const cameraId = ctx.resolveCamera(camera_id);
      return ctx.state.lock.run(async () => textResult(await setOne(ctx, cameraId, name, value)));
    },
  );

  defineTool(
    server,
    "configure",
    "Apply multiple properties in one call, each set-with-confirm. Mode-type props " +
      "are applied first; reports per-property results rather than aborting.",
    { settings: z.record(z.string(), z.string()), camera_id: z.string().optional() },
    async ({ settings, camera_id }) => {
      const cameraId = ctx.resolveCamera(camera_id);
      const ordered = Object.entries(settings).sort(
        (a, b) => (MODE_FIRST.has(a[0]) ? 0 : 1) - (MODE_FIRST.has(b[0]) ? 0 : 1),
      );
      return ctx.state.lock.run(async () => {
        const results: SetResult[] = [];
        for (const [name, value] of ordered) results.push(await setOne(ctx, cameraId, name, value));
        return textResult(results);
      });
    },
  );

  defineTool(
    server,
    "capture",
    "Faithful capture primitive. Returns immediately with a cursor so long " +
      "exposures / RAW transfer don't blow the tool timeout. autofocus=true uses " +
      "af-shutter; false fires the shutter directly (needed in MF). For " +
      "kind=continuous pass phase down/up. Drain poll_events then get_last_capture.",
    {
      autofocus: z.boolean().default(true),
      kind: z.enum(["single", "continuous"]).default("single"),
      phase: z.enum(["down", "up"]).optional(),
      camera_id: z.string().optional(),
    },
    async ({ autofocus, kind, phase, camera_id }) => {
      const cameraId = ctx.resolveCamera(camera_id);
      const cursor = ctx.buffer.cursor;
      await ctx.state.lock.run(async () => {
        try {
          if (kind === "continuous" || phase !== undefined) {
            await ctx.client.actions.shutter({ cameraId, action: phase ?? "down" });
          } else if (autofocus) {
            await ctx.client.actions.afShutter({ cameraId });
          } else {
            await ctx.client.actions.shutter({ cameraId });
          }
        } catch (e) {
          const msg = apiMessage(e);
          if (msg.includes("focus_mode_not_af")) {
            throw new ToolError(
              "AF+shutter failed: camera is in Manual Focus. Switch to AF, or use " +
                "focus_step then capture(autofocus=false).",
            );
          }
          throw new ToolError(`capture failed: ${msg}`);
        }
      });
      return textResult({ cursor, hint: "poll_events for downloadComplete/transferProgress." });
    },
  );

  defineTool(
    server,
    "capture_and_review",
    "Fire, wait for the shot to land up to timeout_s, and return the image in the " +
      "same turn. Mode-aware: remote waits for the auto-transfer; remote-transfer " +
      "detects the new SD file and pulls its screennail. Falls back to an async " +
      "cursor on timeout.",
    { autofocus: z.boolean().default(true), timeout_s: z.number().default(10), camera_id: z.string().optional() },
    async ({ autofocus, timeout_s, camera_id }) => {
      const cameraId = ctx.resolveCamera(camera_id);
      const mode = ctx.state.mode;
      const cursor = ctx.buffer.cursor;
      const timeoutMs = timeout_s * 1000;

      const before = mode === "remote-transfer" || mode === "contents" ? await newestSd(ctx, cameraId) : null;
      await ctx.state.lock.run(() => fireShutter(ctx, cameraId, autofocus));

      let data: string | null = null;
      let ref: Record<string, unknown> | null = null;

      if (mode === "remote") {
        const isSave = (ev: CameraEvent) =>
          (ev.type === "downloadComplete" || ev.type === "transferProgress") &&
          (savedPathOf(ev.data) !== undefined || ev.data.percent === 100);
        const ev = await ctx.buffer.waitFor(isSave, cursor, timeoutMs);
        if (!ev) {
          return textResult({
            timed_out: true,
            cursor,
            hint: "No auto-transfer yet; ensure still-image-store-destination includes the host, or poll_events then get_last_capture.",
          });
        }
        const saved = recordSaved(ctx, ev);
        if (saved) {
          try {
            data = readFileSync(saved.savedPath).toString("base64");
            ref = { ...saved };
          } catch {
            /* fall through to note */
          }
        }
      } else {
        const newest = await waitNewSd(ctx, cameraId, before, timeoutMs);
        if (!newest) {
          return textResult({
            timed_out: true,
            cursor,
            hint: "New SD file not detected yet; poll_events / list_captures then get_capture_preview.",
          });
        }
        const [contentId, fileId] = newest;
        ctx.state.lastCaptureRef = { slot: 1, contentId, fileId };
        const pulled = await downloadPreview(ctx, cameraId, 1, contentId, fileId, "screennail");
        data = pulled.base64;
        ref = { slot: 1, content_id: contentId, file_id: fileId, saved_path: pulled.path };
      }

      if (!data) {
        return textResult({ saved: true, cursor: ctx.buffer.cursor, source_ref: ref, note: "Shot saved but preview bytes unavailable; use get_last_capture." });
      }
      return {
        content: [
          { type: "image", data, mimeType: "image/jpeg" },
          { type: "text", text: JSON.stringify({ saved: true, cursor: ctx.buffer.cursor, source_ref: ref }) },
        ],
      };
    },
  );
}
