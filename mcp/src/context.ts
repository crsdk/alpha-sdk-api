// Shared application context: REST client, session state, event plumbing, and
// the on-demand camera-server lifecycle (via ServerManager).
import { AlphaApi } from "./api/client.js";
import { ServerManager } from "./server-manager.js";
import { mkdirSync } from "node:fs";
import { join } from "node:path";
import type { Config } from "./config.js";
import { ToolError } from "./errors.js";
import { EventBuffer, SSEConsumer } from "./events.js";
import { SessionState } from "./state.js";

export type StopDisposition = "stopped" | "left-running" | "not-running";

export class Ctx {
  readonly client: AlphaApi;
  readonly state = new SessionState();
  readonly buffer: EventBuffer;
  readonly sse: SSEConsumer;
  private sm: ServerManager | null = null;
  private serverStarted = false;

  constructor(readonly config: Config) {
    this.client = new AlphaApi(config.baseUrl);
    this.buffer = new EventBuffer(config.eventBufferSize);
    this.sse = new SSEConsumer(config.baseUrl, this.buffer);
  }

  private manager(): ServerManager {
    if (!this.sm) {
      this.sm = new ServerManager({
        port: this.config.port,
        baseUrl: this.config.baseUrl,
        binaryPath: this.config.serverBinary,
      });
    }
    return this.sm;
  }

  /** Start the camera-server on demand (adopts an orphan, else spawns). */
  async ensureServer(): Promise<void> {
    if (this.serverStarted) {
      if (await this.manager().isRunning()) return;
      this.sm = null; // process died — reset and restart
      this.serverStarted = false;
    }
    const mgr = this.manager();
    if (!this.config.manageBinary) {
      if (await mgr.isRunning()) {
        this.serverStarted = true;
        return;
      }
      throw new ToolError(
        `No server at ${this.config.baseUrl} and ALPHA_MANAGE_BINARY is off.`,
      );
    }
    try {
      await mgr.start(); // adopts orphan if present, else spawns
    } catch (err) {
      throw new ToolError(`Could not start camera-server: ${(err as Error).message}`);
    }
    this.serverStarted = true;
  }

  /** Stop the camera-server we started; report what happened. */
  async stopServer(): Promise<StopDisposition> {
    if (this.sm && this.serverStarted) {
      await this.sm.stop();
      this.sm = null;
      this.serverStarted = false;
      return "stopped";
    }
    return (await this.manager().isRunning()) ? "left-running" : "not-running";
  }

  async serverRunning(): Promise<boolean> {
    return this.manager().isRunning();
  }

  /** Synchronous kill for signal handlers. */
  killServer(): void {
    this.sm?.kill();
  }

  workdir(sub: string): string {
    const dir = join(this.config.workDir, sub);
    mkdirSync(dir, { recursive: true });
    return dir;
  }

  /** Resolve the target camera, defaulting to the active session camera. */
  resolveCamera(cameraId?: string): string {
    const active = this.state.activeCameraId;
    if (!cameraId) {
      if (!active) throw new ToolError("No active camera. Call connect_camera first.");
      return active;
    }
    if (active && cameraId !== active) {
      throw new ToolError(
        `camera_id ${cameraId} does not match the active camera ${active}. ` +
          "Disconnect first to switch cameras.",
      );
    }
    return cameraId;
  }
}
