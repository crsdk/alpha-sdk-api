// Shared helpers for registering tools with uniform error handling + results.
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import type { CallToolResult } from "@modelcontextprotocol/sdk/types.js";
import type { z, ZodRawShape } from "zod";
import { ToolError } from "../errors.js";

export function textResult(data: unknown): CallToolResult {
  return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
}

export function imageResult(base64: string, mimeType = "image/jpeg"): CallToolResult {
  return { content: [{ type: "image", data: base64, mimeType }] };
}

function errorResult(message: string): CallToolResult {
  return { content: [{ type: "text", text: message }], isError: true };
}

/** Register a tool, converting ToolError (and unexpected errors) to isError results. */
export function defineTool<S extends ZodRawShape>(
  server: McpServer,
  name: string,
  description: string,
  schema: S,
  handler: (args: z.objectOutputType<S, z.ZodTypeAny>) => Promise<CallToolResult>,
): void {
  server.registerTool(
    name,
    { description, inputSchema: schema },
    (async (args: z.objectOutputType<S, z.ZodTypeAny>) => {
      try {
        return await handler(args);
      } catch (err) {
        if (err instanceof ToolError) return errorResult(err.message);
        return errorResult(`${name} failed: ${(err as Error).message}`);
      }
    }) as never,
  );
}
