// Environment-driven configuration for the Alpha MCP server.
import { accessSync, constants } from "node:fs";
import { delimiter, join } from "node:path";
import { homedir } from "node:os";

export interface Config {
  /** REST server the camera binary listens on. */
  baseUrl: string;
  /** Port parsed from baseUrl. */
  port: number;
  /** Path to the CameraWebApp binary, if one could be found. */
  serverBinary?: string;
  /** When false, assume an already-running server (adopt-only). */
  manageBinary: boolean;
  /** Directory captures / previews / snapshots are written to. */
  workDir: string;
  /** Event ring-buffer capacity. */
  eventBufferSize: number;
}

const BINARY_NAME = "CameraWebApp";

/**
 * Locate the camera server binary. Nothing ships it any more — it is built from
 * source — so an explicit path wins and PATH is the only fallback.
 */
export function resolveServerBinary(explicit?: string): string | undefined {
  if (explicit) return explicit;
  for (const dir of (process.env.PATH ?? "").split(delimiter)) {
    if (!dir) continue;
    const candidate = join(dir, BINARY_NAME);
    try {
      accessSync(candidate, constants.X_OK);
      return candidate;
    } catch {
      /* keep looking */
    }
  }
  return undefined;
}

export function loadConfig(): Config {
  const baseUrl = process.env.ALPHA_BASE_URL ?? "http://localhost:8080";
  const port = Number.parseInt(baseUrl.slice(baseUrl.lastIndexOf(":") + 1), 10) || 8080;
  const manage = process.env.ALPHA_MANAGE_BINARY;
  return {
    baseUrl,
    port,
    serverBinary: resolveServerBinary(process.env.CAMERA_SERVER_BINARY || undefined),
    manageBinary: !(manage === "0" || manage === "false" || manage === "no"),
    workDir: process.env.ALPHA_WORK_DIR ?? join(homedir(), ".alpha-mcp"),
    eventBufferSize: Number.parseInt(process.env.ALPHA_EVENT_BUFFER ?? "512", 10),
  };
}
