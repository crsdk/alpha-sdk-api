// Files layer: browse the SD card, pull previews, download full-res selects.
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { readFileSync } from "node:fs";
import { z } from "zod";
import type { Ctx } from "../context.js";
import type { CameraEvent } from "../events.js";
import { apiMessage, ToolError } from "../errors.js";
import { defineTool, imageResult, textResult } from "./helpers.js";

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

/** Host path from a completion event (downloadComplete=savedPath, transferProgress=filename). */
export function savedPathOf(data: Record<string, unknown>): string | undefined {
  return (data.savedPath as string | undefined) ?? (data.filename as string | undefined);
}

/** Highest (content_id, file_id) in a listing — the most recent capture. */
export function pickNewest(
  files: ReadonlyArray<{ content_id: number; file_id: number }>,
): [number, number] | null {
  if (!files.length) return null;
  const newest = files.reduce((a, b) =>
    b.content_id > a.content_id || (b.content_id === a.content_id && b.file_id > a.file_id) ? b : a,
  );
  return [newest.content_id, newest.file_id];
}

/** Newest SD file (contentId, fileId) tuple, or null. */
export async function newestSd(ctx: Ctx, cameraId: string, slot = 1): Promise<[number, number] | null> {
  try {
    const res = await ctx.client.sdCard.list({ cameraId, slotNumber: slot });
    return pickNewest(res.files ?? []);
  } catch {
    return null;
  }
}

/** Poll the SD listing until a file newer than `before` appears. */
export async function waitNewSd(
  ctx: Ctx,
  cameraId: string,
  before: [number, number] | null,
  timeoutMs: number,
  slot = 1,
): Promise<[number, number] | null> {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    const newest = await newestSd(ctx, cameraId, slot);
    if (newest && (!before || newest[0] !== before[0] || newest[1] !== before[1])) return newest;
    await sleep(500);
  }
  return null;
}

/** Pull a thumbnail/screennail and return its bytes; completion arrives via SSE. */
export async function downloadPreview(
  ctx: Ctx,
  cameraId: string,
  slot: number,
  contentId: number,
  fileId: number,
  size: "thumbnail" | "screennail",
  timeoutMs = 12000,
): Promise<{ base64: string; path: string }> {
  const cursor = ctx.buffer.cursor;
  const req = { cameraId, slotNumber: slot, contentId, fileId, body: {} };
  try {
    if (size === "thumbnail") await ctx.client.sdCard.downloadThumbnail(req);
    else await ctx.client.sdCard.downloadScreennail(req);
  } catch (e) {
    throw new ToolError(`${size} download failed: ${apiMessage(e)}`);
  }

  const done = (ev: CameraEvent) => {
    if (ev.type !== "transferProgress" && ev.type !== "downloadComplete") return false;
    const percent = ev.data.percent as number | undefined;
    return savedPathOf(ev.data) !== undefined && (percent === undefined || percent === 100 || ev.type === "downloadComplete");
  };

  const ev = await ctx.buffer.waitFor(done, cursor, timeoutMs);
  if (!ev) throw new ToolError(`${size} download did not complete within ${timeoutMs / 1000}s.`);
  const path = savedPathOf(ev.data)!;
  try {
    return { base64: readFileSync(path).toString("base64"), path };
  } catch {
    throw new ToolError(`${size} reported saved at ${path} but file is missing.`);
  }
}

export function register(server: McpServer, ctx: Ctx): void {
  defineTool(
    server,
    "get_capture_preview",
    "Fetch a lightweight preview (thumbnail or screennail) of one SD file — review " +
      "without a full RAW transfer.",
    {
      content_id: z.number().int(),
      file_id: z.number().int(),
      size: z.enum(["thumbnail", "screennail"]).default("screennail"),
      slot: z.number().int().default(1),
      camera_id: z.string().optional(),
    },
    async ({ content_id, file_id, size, slot, camera_id }) => {
      const cameraId = ctx.resolveCamera(camera_id);
      const { base64 } = await downloadPreview(ctx, cameraId, slot, content_id, file_id, size);
      return imageResult(base64);
    },
  );

  defineTool(
    server,
    "download_capture",
    "Pull a specific full-resolution file to the host — the 'keep this select' " +
      "action. Async: completion arrives as a downloadComplete/transferProgress event.",
    {
      content_id: z.number().int(),
      file_id: z.number().int(),
      slot: z.number().int().default(1),
      camera_id: z.string().optional(),
    },
    async ({ content_id, file_id, slot, camera_id }) => {
      const cameraId = ctx.resolveCamera(camera_id);
      const cursor = ctx.buffer.cursor;
      let res;
      try {
        res = await ctx.client.sdCard.download({ cameraId, slotNumber: slot, contentId: content_id, fileId: file_id, body: {} });
      } catch (e) {
        throw new ToolError(`download_capture failed: ${apiMessage(e)}`);
      }
      ctx.state.lastCaptureRef = { slot, contentId: content_id, fileId: file_id };
      return textResult({
        accepted: res.success,
        message: res.message,
        cursor,
        hint: "Drain poll_events until downloadComplete/transferProgress for savedPath.",
      });
    },
  );
}
