// Perception layer: read properties and pull frames/captures for review.
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { readFileSync } from "node:fs";
import { z } from "zod";
import type { Ctx } from "../context.js";
import { apiMessage, ToolError } from "../errors.js";
import { downloadPreview } from "./files.js";
import { defineTool, imageResult, textResult } from "./helpers.js";

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

async function ensureStreaming(ctx: Ctx, cameraId: string): Promise<void> {
  const status = await ctx.client.liveView.getStatus({ cameraId });
  if (!status.data?.enabled) await ctx.client.liveView.enable({ cameraId });
  if (!status.data?.streaming) await ctx.client.liveView.start({ cameraId });
  ctx.state.liveViewWarm = true;
}

/** Pull a frame, retrying briefly through the cold-start 404 window. */
async function frameWithRetry(ctx: Ctx, cameraId: string, osd: boolean, attempts = 6): Promise<string> {
  let last: unknown;
  for (let i = 0; i < attempts; i++) {
    try {
      const bin = osd
        ? await ctx.client.liveView.getOsdFrame({ cameraId })
        : await ctx.client.liveView.getFrame({ cameraId });
      const ab = await bin.arrayBuffer();
      if (ab.byteLength > 0) return Buffer.from(ab).toString("base64");
    } catch (e) {
      last = e;
    }
    await sleep(250);
  }
  throw new ToolError(`No ${osd ? "OSD " : ""}frame available after retries: ${last ? apiMessage(last) : ""}`);
}

export function register(server: McpServer, ctx: Ctx): void {
  defineTool(
    server,
    "get_camera_state",
    "Read every property the camera reports, with available_values — how you " +
      "discover the valid range for THIS body before writing. Returns a map of " +
      "property -> {current_value, current_hex_value, current_formatted, writable, available_values[]}.",
    { camera_id: z.string().optional() },
    async ({ camera_id }) => {
      const cameraId = ctx.resolveCamera(camera_id);
      const res = await ctx.client.properties.getAll({ cameraId });
      const props = res.data?.properties ?? {};
      const out: Record<string, unknown> = {};
      for (const [name, entry] of Object.entries(props)) {
        out[name] = {
          current_value: entry.current_value,
          current_hex_value: entry.current_hex_value,
          current_formatted: entry.current_formatted,
          writable: entry.writable,
          available_values: (entry.available_values ?? []).map((v) => ({
            value: v.value,
            hex_value: v.hex_value,
            formatted: v.formatted,
          })),
        };
      }
      return textResult(out);
    },
  );

  defineTool(
    server,
    "get_live_frame",
    "Return a single current-scene JPEG on demand. Prefers the OSD composite so " +
      "the real histogram/readouts are baked into the pixels (a clean JPEG is " +
      "tone-mapped, not a reliable exposure reference). overlay: auto | osd | clean.",
    { overlay: z.enum(["auto", "osd", "clean"]).default("auto"), camera_id: z.string().optional() },
    async ({ overlay, camera_id }) => {
      const cameraId = ctx.resolveCamera(camera_id);
      await ensureStreaming(ctx, cameraId);

      if (ctx.state.osdSupported === null) {
        try {
          const osd = await ctx.client.liveView.getOsdStatus({ cameraId });
          ctx.state.osdSupported = Boolean(osd.data?.supported);
        } catch {
          ctx.state.osdSupported = false;
        }
      }

      const wantOsd = overlay === "osd" || (overlay === "auto" && ctx.state.osdSupported === true);
      if (overlay === "osd" && !ctx.state.osdSupported) {
        throw new ToolError("OSD overlay not supported on this body; use overlay='clean'.");
      }

      let used = "clean";
      let data = "";
      if (wantOsd) {
        try {
          await ctx.client.liveView.enableOsd({ cameraId });
          data = await frameWithRetry(ctx, cameraId, true);
          used = "osd";
        } catch (e) {
          if (overlay === "osd") throw e;
          data = "";
        }
      }
      if (!data) {
        data = await frameWithRetry(ctx, cameraId, false);
        used = "clean";
      }

      return {
        content: [
          { type: "image", data, mimeType: "image/jpeg" },
          { type: "text", text: JSON.stringify({ overlay_used: used }) },
        ],
      };
    },
  );

  defineTool(
    server,
    "get_last_capture",
    "Return the most recent capture for review. remote mode: read the auto-" +
      "transferred file from disk; remote-transfer: pull the last SD file's screennail.",
    { prefer: z.enum(["screennail", "full"]).default("screennail"), camera_id: z.string().optional() },
    async ({ prefer, camera_id }) => {
      const cameraId = ctx.resolveCamera(camera_id);
      const ref = ctx.state.lastCaptureRef;
      if (!ref) throw new ToolError("No capture recorded yet this session.");

      if (ref.savedPath) {
        try {
          return imageResult(readFileSync(ref.savedPath).toString("base64"));
        } catch {
          throw new ToolError(`Auto-transferred file not found at ${ref.savedPath}.`);
        }
      }
      if (prefer === "full") throw new ToolError("Use download_capture for full-resolution pulls.");
      if (ref.contentId === undefined || ref.fileId === undefined) {
        throw new ToolError("No SD reference for the last capture.");
      }
      const { base64 } = await downloadPreview(ctx, cameraId, ref.slot ?? 1, ref.contentId, ref.fileId, "screennail");
      return imageResult(base64);
    },
  );
}
