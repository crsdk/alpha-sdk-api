// Session layer: discovery, connect/disconnect, status, diagnostics, lifecycle.
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { z } from "zod";
import type { Ctx } from "../context.js";
import { apiMessage, isNotImplemented, ToolError } from "../errors.js";
import { defineTool, textResult } from "./helpers.js";

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

/** Poll connection status until the camera reports connected, or timeout. */
async function awaitConnected(ctx: Ctx, cameraId: string, timeoutMs: number): Promise<void> {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    try {
      const status = await ctx.client.cameras.getConnectionStatus({ cameraId });
      if (status.camera?.connected) return;
    } catch {
      /* transient during settle */
    }
    await sleep(400);
  }
}

export function register(server: McpServer, ctx: Ctx): void {
  defineTool(
    server,
    "list_cameras",
    "Enumerate cameras connected via USB/network. Returns {id, model, connected, " +
      "connection_type}. Starts the camera-server on demand if it isn't running.",
    {},
    async () => {
      await ctx.ensureServer();
      const res = await ctx.client.cameras.list();
      return textResult(
        (res.cameras ?? []).map((c) => ({
          id: c.id,
          model: c.model,
          connected: c.connected,
          connection_type: c.connectionType,
        })),
      );
    },
  );

  defineTool(
    server,
    "connect_camera",
    "Connect and make the camera immediately controllable. Hides the mandatory " +
      "pc-remote priority-key step. mode: remote-transfer (default, most capable), " +
      "remote (auto-transfer every shot), or contents (browse-only). reconnecting=on " +
      "enables SDK auto-reconnect. Starts the background event consumer on success.",
    {
      camera_id: z.string().optional(),
      mode: z.enum(["remote", "remote-transfer", "contents"]).default("remote-transfer"),
      reconnecting: z.enum(["on", "off"]).default("on"),
      warm_live_view: z.boolean().default(false),
    },
    async ({ camera_id, mode, reconnecting, warm_live_view }) => {
      if (ctx.state.activeCameraId) {
        throw new ToolError(`Already connected to ${ctx.state.activeCameraId}. Disconnect first.`);
      }
      await ctx.ensureServer();

      let cameraId = camera_id;
      if (!cameraId) {
        const listing = await ctx.client.cameras.list();
        const cams = listing.cameras ?? [];
        if (cams.length === 0) throw new ToolError("No cameras discovered. Check USB/network.");
        if (cams.length > 1) {
          throw new ToolError(`Multiple cameras discovered (${cams.map((c) => c.id).join(", ")}); pass camera_id.`);
        }
        cameraId = cams[0].id;
      }

      try {
        await ctx.client.cameras.connect({ cameraId, mode, reconnecting });
      } catch (e) {
        throw new ToolError(`Connect failed: ${apiMessage(e)}`);
      }

      // Settle race: connect returns before the SDK reports connected.
      await awaitConnected(ctx, cameraId, 8000);

      // Priority key: required to drive settings. FX3/cinema bodies don't
      // implement it (0x8402) and auto-enter PC Remote → treat as set.
      let priorityKeySet = true;
      try {
        await ctx.client.properties.setPriorityKey({ cameraId, setting: "pc-remote" });
      } catch (e) {
        if (isNotImplemented(e)) priorityKeySet = true;
        else throw new ToolError(`Priority-key set failed: ${apiMessage(e)}`);
      }

      const status = await ctx.client.cameras.getConnectionStatus({ cameraId });
      const activeMode = status.data?.mode ?? mode;
      const model = status.camera?.model ?? null;

      ctx.state.activeCameraId = cameraId;
      ctx.state.model = model;
      ctx.state.mode = activeMode;
      ctx.state.priorityKeySet = priorityKeySet;

      // Redirect host-written files into the managed work dir (not the CWD).
      try {
        await ctx.client.settings.setSaveInfo({ cameraId, path: ctx.workdir("captures") });
      } catch {
        /* best effort */
      }

      ctx.sse.start();

      if (warm_live_view) {
        try {
          await ctx.client.liveView.enable({ cameraId });
          await ctx.client.liveView.start({ cameraId });
          ctx.state.liveViewWarm = true;
        } catch {
          ctx.state.liveViewWarm = false;
        }
      }

      return textResult({
        camera: { id: cameraId, model },
        mode: activeMode,
        priority_key_set: priorityKeySet,
        live_view_warm: ctx.state.liveViewWarm,
      });
    },
  );

  defineTool(
    server,
    "disconnect_camera",
    "Clean teardown: stop live view if warm, stop the event consumer, disconnect.",
    { camera_id: z.string().optional() },
    async ({ camera_id }) => {
      const cameraId = ctx.resolveCamera(camera_id);
      if (ctx.state.liveViewWarm) {
        try {
          await ctx.client.liveView.stop({ cameraId });
        } catch {
          /* ignore */
        }
      }
      await ctx.sse.stopConsumer();
      try {
        await ctx.client.cameras.disconnect({ cameraId });
      } catch (e) {
        throw new ToolError(`Disconnect failed: ${apiMessage(e)}`);
      }
      ctx.state.clear();
      return textResult({ camera: cameraId, disconnected: true });
    },
  );

  defineTool(
    server,
    "get_connection_status",
    "Report current connection state and active mode.",
    { camera_id: z.string().optional() },
    async ({ camera_id }) => {
      const cameraId = ctx.state.activeCameraId ? ctx.resolveCamera(camera_id) : camera_id;
      if (!cameraId) return textResult({ connected: false, active_camera_id: null });
      const status = await ctx.client.cameras.getConnectionStatus({ cameraId });
      return textResult({
        connected: status.camera?.connected ?? false,
        model: status.camera?.model ?? null,
        id: status.camera?.id ?? cameraId,
        mode: status.data?.mode ?? null,
      });
    },
  );

  defineTool(
    server,
    "stop_camera_server",
    "Release the camera and stop the camera-server binary so it isn't held while " +
      "idle. A later list_cameras / connect_camera starts it again on demand.",
    { camera_id: z.string().optional() },
    async () => {
      if (ctx.state.activeCameraId) {
        try {
          if (ctx.state.liveViewWarm) await ctx.client.liveView.stop({ cameraId: ctx.state.activeCameraId });
        } catch {
          /* ignore */
        }
        try {
          await ctx.client.cameras.disconnect({ cameraId: ctx.state.activeCameraId });
        } catch {
          /* ignore */
        }
      }
      await ctx.sse.stopConsumer();
      ctx.state.clear();
      const disposition = await ctx.stopServer();
      const note = {
        stopped: "camera-server stopped; will restart on next connect.",
        "left-running": "An externally-managed server is running; not stopping it.",
        "not-running": "No camera-server was running.",
      }[disposition];
      return textResult({ disposition, message: note });
    },
  );

  defineTool(
    server,
    "get_server_diagnostics",
    "Server health + recent logs for troubleshooting the binary in-conversation.",
    { log_lines: z.number().int().default(100), level: z.enum(["debug", "info", "warn", "error"]).default("info") },
    async ({ log_lines, level }) => {
      if (!(await ctx.serverRunning())) {
        return textResult({ status: "stopped", message: "camera-server is not running; connect a camera to start it." });
      }
      const status = await ctx.client.server.status();
      const logs = await ctx.client.server.logs({ lines: log_lines, level });
      return textResult({
        status: "ok",
        version: status.server?.version ?? null,
        sdk_version: status.server?.sdkVersion ?? null,
        uptime: status.server?.uptime ?? null,
        connected_count: status.cameras?.connected ?? null,
        discovered_count: status.cameras?.discovered ?? null,
        sse_healthy: ctx.sse.healthy,
        logs: (logs.logs ?? []).map((l) => ({ level: l.level, message: l.message })),
      });
    },
  );
}
