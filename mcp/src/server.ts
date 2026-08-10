#!/usr/bin/env node
// Entry point: build the MCP server, register all tools, run on stdio.
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { loadConfig } from "./config.js";
import { Ctx } from "./context.js";
import * as action from "./tools/action.js";
import * as events from "./tools/events.js";
import * as files from "./tools/files.js";
import * as perception from "./tools/perception.js";
import * as session from "./tools/session.js";
import * as generated from "./tools/generated.js";

const INSTRUCTIONS =
  "Control a physically-connected Sony Alpha camera. Typical flow: connect_camera " +
  "-> discover limits with get_camera_state -> configure/set_property -> " +
  "capture_and_review -> inspect the returned frame -> adjust. Valid values for " +
  "THIS body come from get_camera_state/get_property available_values, never hardcode.";

async function main(): Promise<void> {
  const ctx = new Ctx(loadConfig());
  const server = new McpServer(
    { name: "alpha-camera", version: "0.2.0" },
    { instructions: INSTRUCTIONS },
  );

  for (const mod of [session, perception, action, events, files, generated]) {
    mod.register(server, ctx);
  }

  // Lazy lifecycle: nothing starts here. Clean up on exit.
  const shutdown = () => {
    try {
      ctx.killServer();
    } catch {
      /* best effort */
    }
    process.exit(0);
  };
  process.on("SIGINT", shutdown);
  process.on("SIGTERM", shutdown);

  // A client that closes the pipe without signalling would otherwise leave the
  // camera-server orphaned, still holding the camera. Claude Desktop sends
  // SIGTERM, but stdin EOF is the more general end-of-session signal.
  process.stdin.on("end", shutdown);
  process.stdin.on("close", shutdown);

  const transport = new StdioServerTransport();
  await server.connect(transport);
}

main().catch((err) => {
  process.stderr.write(`fatal: ${err instanceof Error ? err.stack : String(err)}\n`);
  process.exit(1);
});
