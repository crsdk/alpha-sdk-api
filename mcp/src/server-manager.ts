// On-demand lifecycle for the CameraWebApp binary.
//
// This used to come from the `@alpha-sdk/api` npm package, which bundled the
// native binary. That distribution was retired — the binary is now built from
// source (see README) — so we own the process handling here.
//
// The binary takes a single `--port` flag and gives no machine-readable ready
// signal, so readiness is a poll of `GET /api/server/status`.
import { spawn, type ChildProcess } from "node:child_process";
import { dirname } from "node:path";

export interface ServerManagerOptions {
  port: number;
  baseUrl: string;
  /** Resolved path to the CameraWebApp binary. Required to spawn. */
  binaryPath?: string;
  /** How long to wait for a spawned server to answer /api/server/status. */
  readyTimeoutMs?: number;
  /** Per-probe budget for a single /api/server/status call. */
  probeTimeoutMs?: number;
}

// `/api/server/status` is not a cheap health endpoint: it re-enumerates devices
// and measures ~3.6s with an ILCE-7M5 attached over USB. The probe budget has to
// clear that comfortably or every liveness check reports "down".
const DEFAULT_PROBE_TIMEOUT_MS = 15_000;
const DEFAULT_READY_TIMEOUT_MS = 30_000;

// The binary announces itself on stdout the instant it binds the port. Watching
// for that is immediate, where polling the status endpoint costs seconds per
// attempt — so the marker is the primary readiness signal and HTTP is fallback.
const READY_MARKER = "started on";

export class ServerManager {
  private child: ChildProcess | null = null;
  /** Recent process output, so a startup failure reports why. */
  private output: string[] = [];

  constructor(private readonly opts: ServerManagerOptions) {}

  /** True if anything is answering on the configured base URL. */
  async isRunning(): Promise<boolean> {
    try {
      const res = await fetch(`${this.opts.baseUrl}/api/server/status`, {
        signal: AbortSignal.timeout(this.opts.probeTimeoutMs ?? DEFAULT_PROBE_TIMEOUT_MS),
      });
      return res.ok;
    } catch {
      return false;
    }
  }

  /** True if this process owns the running server (as opposed to adopting one). */
  get owned(): boolean {
    return this.child !== null;
  }

  /** Adopt an already-running server, else spawn one and wait for readiness. */
  async start(): Promise<void> {
    if (await this.isRunning()) return;

    const binary = this.opts.binaryPath;
    if (!binary) {
      throw new Error(
        "No CameraWebApp binary found. Set CAMERA_SERVER_BINARY to its path, or put " +
          "it on your PATH. The binary is built from source — see the README.",
      );
    }

    this.output = [];
    // Run from the binary's own directory: it resolves its SDK and OpenCV
    // dylibs, and its working files, relative to where it sits.
    const child = spawn(binary, ["--port", String(this.opts.port)], {
      cwd: dirname(binary),
      stdio: ["ignore", "pipe", "pipe"],
    });
    this.child = child;

    // The server reports startup progress and bind failures on stdout, not
    // stderr, so capture both or a failure surfaces with no explanation.
    const record = (chunk: Buffer) => {
      this.output.push(chunk.toString());
      if (this.output.length > 40) this.output.shift();
    };
    child.stdout?.on("data", record);
    child.stderr?.on("data", record);
    child.on("exit", () => {
      this.child = null;
    });

    const spawnFailed = new Promise<never>((_, reject) => {
      child.once("error", (err) => reject(new Error(`Could not run ${binary}: ${err.message}`)));
    });

    await Promise.race([this.awaitReady(), spawnFailed]);
  }

  private async awaitReady(): Promise<void> {
    const timeout = this.opts.readyTimeoutMs ?? DEFAULT_READY_TIMEOUT_MS;
    const deadline = Date.now() + timeout;
    while (Date.now() < deadline) {
      if (this.child === null) {
        throw new Error(`camera-server exited during startup. ${this.tail()}`);
      }
      if (this.output.join("").includes(READY_MARKER)) return;
      await sleep(250);
    }
    // No marker: the build may not print one. Fall back to one HTTP probe
    // before giving up, so a working server is never reported as failed.
    if (this.child !== null && (await this.isRunning())) return;
    this.kill();
    throw new Error(`camera-server did not become ready within ${timeout / 1000}s. ${this.tail()}`);
  }

  private tail(): string {
    const text = this.output.join("").trim();
    return text ? `Last output: ${text.slice(-500)}` : "";
  }

  /**
   * Ask the server to shut down cleanly (it disconnects cameras and closes SSE
   * first), then make sure our child is actually gone.
   */
  async stop(): Promise<void> {
    try {
      await fetch(`${this.opts.baseUrl}/api/server/shutdown`, {
        method: "POST",
        signal: AbortSignal.timeout(5000),
      });
    } catch {
      /* server may already be down, or too busy to answer — fall through */
    }

    const child = this.child;
    if (!child) return;

    const exited = new Promise<void>((resolve) => child.once("exit", () => resolve()));
    const deadline = Date.now() + 5000;
    while (this.child !== null && Date.now() < deadline) {
      await Promise.race([exited, sleep(250)]);
    }
    if (this.child) {
      child.kill("SIGTERM");
      await Promise.race([exited, sleep(2000)]);
    }
    if (this.child) child.kill("SIGKILL");
    this.child = null;
  }

  /** Synchronous kill, for signal handlers that cannot await. */
  kill(): void {
    this.child?.kill("SIGKILL");
    this.child = null;
  }
}

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));
