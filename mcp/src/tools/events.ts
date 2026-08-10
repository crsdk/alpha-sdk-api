// Events layer: pull adapter over the server-owned SSE ring buffer.
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { z } from "zod";
import type { Ctx } from "../context.js";
import { recordSaved } from "./action.js";
import { defineTool, textResult } from "./helpers.js";

export function register(server: McpServer, ctx: Ctx): void {
  defineTool(
    server,
    "poll_events",
    "Drain camera events since a cursor. wait_s=0 returns immediately; wait_s>0 " +
      "long-polls up to N seconds for at least one new event. Types include " +
      "downloadComplete, transferProgress, propertyChanged, afStatus, error. If " +
      "dropped is true, events after since_cursor were evicted — re-sync from cursor.",
    { since_cursor: z.number().int().default(0), wait_s: z.number().default(0) },
    async ({ since_cursor, wait_s }) => {
      if (wait_s > 0 && ctx.buffer.since(since_cursor).length === 0) {
        await ctx.buffer.waitNew(since_cursor, wait_s * 1000);
      }
      const dropped = ctx.buffer.dropped(since_cursor);
      const events = ctx.buffer.since(since_cursor);
      for (const ev of events) {
        if (ev.type === "downloadComplete" || ev.type === "transferProgress") recordSaved(ctx, ev);
      }
      return textResult({
        cursor: ctx.buffer.cursor,
        dropped,
        oldest_cursor: ctx.buffer.oldestCursor,
        events: events.map((e) => ({ cursor: e.cursor, type: e.type, data: e.data })),
      });
    },
  );
}
