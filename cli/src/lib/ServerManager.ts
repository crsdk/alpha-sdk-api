// =============================================================================
// Camera Remote Web API — Server Manager
// Spawns the native CameraWebApp binary and manages its lifecycle
// =============================================================================

import { spawn, type ChildProcess } from 'node:child_process';
import { existsSync } from 'node:fs';
import { createServer } from 'node:net';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = dirname(fileURLToPath(import.meta.url));

export interface ServerManagerOptions {
  port?: number;
  binaryPath?: string;
  autoPort?: boolean;
  detached?: boolean;
}

export class ServerManager {
  private childProc: ChildProcess | null = null;
  private port: number;
  private binaryPath: string;
  private autoPort: boolean;
  private detached: boolean;
  private adopted = false;
  private detachedPid: number | undefined;
  private stdout: string[] = [];
  private stderr: string[] = [];

  constructor(opts?: ServerManagerOptions) {
    this.port = opts?.port ?? 8080;
    this.autoPort = opts?.autoPort ?? false;
    this.detached = opts?.detached ?? false;
    this.binaryPath = opts?.binaryPath ?? this.resolveBinaryPath();
  }

  private resolveBinaryPath(): string {
    // Clone-and-build model: the server is compiled from source (`crsdk build`)
    // and lives in the repo, not fetched as a prebuilt package.
    // Resolution order: CRSDK_BINARY env → walk up from cwd for the built binary.
    const binaryName = process.platform === 'win32' ? 'CameraWebApp.exe' : 'CameraWebApp';

    const envPath = process.env.CRSDK_BINARY;
    if (envPath) {
      if (existsSync(envPath)) return envPath;
      throw new Error(`CRSDK_BINARY is set but not found: ${envPath}`);
    }

    // Walk up from the current directory to find <repo>/api/server/build/…
    let dir = process.cwd();
    while (true) {
      for (const rel of [
        join('api', 'server', 'build', binaryName),
        join('api', 'server', 'build', 'Release', binaryName), // Windows/MSVC
      ]) {
        const candidate = join(dir, rel);
        if (existsSync(candidate)) return candidate;
      }
      const parent = dirname(dir);
      if (parent === dir) break;
      dir = parent;
    }

    throw new Error(
      `Server binary not found. Build it first with "crsdk build", ` +
      `set CRSDK_BINARY=/path/to/${binaryName}, or run from inside the repo.`
    );
  }

  getPort(): number {
    return this.port;
  }

  getBinaryPath(): string {
    return this.binaryPath;
  }

  private static isPortAvailable(port: number): Promise<boolean> {
    return new Promise((resolve) => {
      const server = createServer();
      server.once('error', () => resolve(false));
      server.once('listening', () => {
        server.close(() => resolve(true));
      });
      server.listen(port, '127.0.0.1');
    });
  }

  private static async findAvailablePort(startPort: number): Promise<number> {
    for (let port = startPort; port < startPort + 100; port++) {
      if (await ServerManager.isPortAvailable(port)) {
        return port;
      }
    }
    throw new Error(`No available port found in range ${startPort}-${startPort + 99}`);
  }

  /** Check if a CameraWebApp server is already responding on a port */
  private async isOurServer(port: number): Promise<boolean> {
    try {
      const res = await fetch(`http://127.0.0.1:${port}/api/server/status`, {
        signal: AbortSignal.timeout(15000),
      });
      return res.ok;
    } catch {
      return false;
    }
  }

  async start(): Promise<void> {
    if (this.childProc) {
      throw new Error('Server is already running');
    }

    // Check port availability
    if (!(await ServerManager.isPortAvailable(this.port))) {
      // Port is taken — check if it's an orphaned CameraWebApp from a previous session
      if (await this.isOurServer(this.port)) {
        // Adopt the orphaned server — no new process needed
        this.adopted = true;
        return;
      }

      if (this.autoPort) {
        const newPort = await ServerManager.findAvailablePort(this.port + 1);
        this.port = newPort;
      } else {
        throw new Error(
          `Port ${this.port} is already in use. ` +
          `Either stop the other process, use a different port with { port: ${this.port + 1} }, ` +
          `or enable auto port selection with { autoPort: true }.`
        );
      }
    }

    const cwd = dirname(this.binaryPath);

    // Note: macOS SIP strips DYLD_LIBRARY_PATH from child processes.
    // The binary uses @executable_path/lib rpath instead.
    // On Linux, LD_LIBRARY_PATH is set as a fallback.
    const env = { ...process.env };
    if (process.platform === 'linux') {
      env.LD_LIBRARY_PATH = join(cwd, 'lib');
    }

    if (this.detached) {
      // Detached mode: spawn and let the server outlive this process
      const child = spawn(this.binaryPath, ['--port', String(this.port)], {
        cwd,
        stdio: 'ignore',
        env,
        detached: true,
      });
      this.detachedPid = child.pid;
      child.unref();

      // Poll the status endpoint to confirm it started
      await this.waitForReadyHttp(10000);
    } else {
      this.childProc = spawn(this.binaryPath, ['--port', String(this.port)], {
        cwd,
        stdio: ['ignore', 'pipe', 'pipe'],
        env,
      });

      this.childProc.stdout?.on('data', (data: Buffer) => {
        const line = data.toString().trim();
        if (line) this.stdout.push(line);
      });

      this.childProc.stderr?.on('data', (data: Buffer) => {
        const line = data.toString().trim();
        if (line) this.stderr.push(line);
      });

      this.childProc.on('exit', (code) => {
        this.childProc = null;
        if (code !== 0 && code !== null) {
          console.error(`CameraWebApp exited with code ${code}`);
        }
      });

      // Wait for server to be ready
      await this.waitForReady(10000);
    }
  }

  async stop(): Promise<void> {
    if (this.adopted) {
      // Adopted server — shut down via API
      try {
        await fetch(`http://127.0.0.1:${this.port}/api/server/shutdown`, {
          method: 'POST',
          signal: AbortSignal.timeout(3000),
        });
      } catch { /* already dead */ }
      this.adopted = false;
      return;
    }

    if (!this.childProc) return;

    // Try graceful shutdown via API
    try {
      const res = await fetch(`http://127.0.0.1:${this.port}/api/server/shutdown`, {
        method: 'POST',
        signal: AbortSignal.timeout(3000),
      });
      if (res.ok) {
        // Wait for process to exit
        await this.waitForExit(5000);
        return;
      }
    } catch {
      // API not responding, use SIGTERM
    }

    // Fallback: SIGTERM
    if (this.childProc) {
      this.childProc.kill('SIGTERM');
      await this.waitForExit(5000);
    }

    // Last resort: SIGKILL
    if (this.childProc) {
      this.childProc.kill('SIGKILL');
      this.childProc = null;
    }
  }

  /** Synchronous kill — for use in signal handlers where async isn't reliable */
  kill(): void {
    if (this.adopted) {
      // Best-effort shutdown for adopted server — fire and forget
      fetch(`http://127.0.0.1:${this.port}/api/server/shutdown`, {
        method: 'POST',
        signal: AbortSignal.timeout(2000),
      }).catch(() => {});
      this.adopted = false;
      return;
    }
    if (!this.childProc) return;
    this.childProc.kill('SIGTERM');
    this.childProc = null;
  }

  async isRunning(): Promise<boolean> {
    try {
      const res = await fetch(`http://127.0.0.1:${this.port}/api/server/status`, {
        signal: AbortSignal.timeout(15000),
      });
      return res.ok;
    } catch {
      return false;
    }
  }

  getStdout(): string[] {
    return [...this.stdout];
  }

  getStderr(): string[] {
    return [...this.stderr];
  }

  getPid(): number | undefined {
    return this.childProc?.pid ?? this.detachedPid;
  }

  /** Poll the HTTP status endpoint — used for detached mode where we have no stdout */
  private async waitForReadyHttp(timeoutMs: number): Promise<void> {
    const start = Date.now();
    const interval = 500;

    while (Date.now() - start < timeoutMs) {
      if (await this.isRunning()) return;
      await new Promise((r) => setTimeout(r, interval));
    }

    throw new Error(`Server failed to start within ${timeoutMs}ms`);
  }

  private async waitForReady(timeoutMs: number): Promise<void> {
    const start = Date.now();
    const interval = 500;

    while (Date.now() - start < timeoutMs) {
      // Check if process died
      if (!this.childProc) {
        const lastError = this.stderr.slice(-5).join('\n');
        throw new Error(`Server process exited unexpectedly.\n${lastError}`);
      }

      // Check if stdout indicates server is ready
      const lastOutput = this.stdout.join('\n');
      if (lastOutput.includes('started on')) {
        // Server announced it's listening — trust it
        return;
      }

      await new Promise((r) => setTimeout(r, interval));
    }

    throw new Error(`Server failed to start within ${timeoutMs}ms`);
  }

  private async waitForExit(timeoutMs: number): Promise<void> {
    if (!this.childProc) return;

    return new Promise<void>((resolve) => {
      const timer = setTimeout(() => resolve(), timeoutMs);
      this.childProc?.on('exit', () => {
        clearTimeout(timer);
        this.childProc = null;
        resolve();
      });
    });
  }
}
