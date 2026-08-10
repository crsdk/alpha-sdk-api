// Error helpers: a user-facing ToolError and a formatter for API errors.
import { ApiError } from "./api/client.js";

/** Raised for actionable, user-facing failures surfaced to the MCP client. */
export class ToolError extends Error {}

/** Extract the server's message/hint from an API error body (ErrorResponse). */
export function apiMessage(err: unknown): string {
  if (err instanceof ApiError) {
    const body = err.body as
      | { message?: string; error?: string; data?: { error?: string } }
      | undefined;
    if (body && typeof body === "object") {
      const parts: string[] = [];
      if (body.message) parts.push(String(body.message));
      const nested = body.data?.error ?? body.error;
      if (nested) parts.push(`(${nested})`);
      const msg = parts.join(" ").trim();
      if (msg) return msg;
    }
    return err.message || `HTTP ${err.statusCode ?? "?"}`;
  }
  return err instanceof Error ? err.message : String(err);
}

/** True if the error looks like an SDK "not implemented" / 0x8402 rejection. */
export function isNotImplemented(err: unknown): boolean {
  const m = apiMessage(err).toLowerCase();
  return m.includes("0x8402") || m.includes("not implemented");
}
