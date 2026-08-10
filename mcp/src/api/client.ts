// Typed HTTP client for the Alpha Camera REST API.
//
// The upstream project retired its generated npm client and now publishes the
// OpenAPI document as the contract (see spec/README.md). This is the thin
// replacement: `openapi-fetch` supplies request typing straight off the vendored
// spec, and the namespaces below preserve the call shapes the tools were already
// written against.
//
// Two deliberate departures from a plain generated client:
//   * Non-2xx responses throw `ApiError` rather than returning an error branch,
//     because every tool call site is written around try/catch.
//   * The two `image/jpeg` endpoints bypass openapi-fetch and hand back the raw
//     `Response`, so callers keep `.arrayBuffer()`.
import createClient from "openapi-fetch";
import type { components, paths } from "./schema.js";

/** A property slug the server knows, e.g. `iso`, `aperture`, `shutter-speed`. */
export type PropertyName = components["schemas"]["PropertyName"];
export type ConnectionMode = components["schemas"]["ConnectionMode"];
export type PriorityKeySetting = components["schemas"]["PriorityKeySetting"];
export type ZoomRequest = components["schemas"]["ZoomRequest"];
export type ErrorResponse = components["schemas"]["ErrorResponse"];

/** Non-2xx from the camera server, carrying the parsed `ErrorResponse` body. */
export class ApiError extends Error {
  constructor(
    readonly statusCode: number,
    readonly body: unknown,
    message?: string,
  ) {
    super(message ?? `HTTP ${statusCode}`);
    this.name = "ApiError";
  }
}

type FetchResult<T> = { data?: T; error?: unknown; response: Response };

/** Collapse openapi-fetch's {data,error} into a value-or-throw. */
function unwrap<T>(res: FetchResult<T>): T {
  if (res.error !== undefined || !res.response.ok) {
    throw new ApiError(res.response.status, res.error, messageFrom(res.error));
  }
  return res.data as T;
}

function messageFrom(body: unknown): string | undefined {
  if (body && typeof body === "object" && "message" in body) {
    const m = (body as { message?: unknown }).message;
    if (typeof m === "string") return m;
  }
  return undefined;
}

export class AlphaApi {
  private readonly http: ReturnType<typeof createClient<paths>>;

  constructor(readonly baseUrl: string) {
    this.http = createClient<paths>({ baseUrl });
  }

  /** Raw JPEG fetch — openapi-fetch would parse the body away. */
  private async jpeg(path: string): Promise<Response> {
    const res = await fetch(`${this.baseUrl}${path}`, { headers: { Accept: "image/jpeg" } });
    if (!res.ok) {
      throw new ApiError(res.status, await res.json().catch(() => undefined));
    }
    return res;
  }

  readonly server = {
    status: async () =>
      unwrap(await this.http.GET("/api/server/status", {})),

    logs: async (query: { lines?: number; level?: "debug" | "info" | "warn" | "error" }) =>
      unwrap(await this.http.GET("/api/server/logs", { params: { query } })),

    /** Graceful shutdown: disconnects cameras and closes SSE before exiting. */
    shutdown: async () => unwrap(await this.http.POST("/api/server/shutdown", {})),
  };

  readonly cameras = {
    list: async () => unwrap(await this.http.GET("/api/cameras", {})),

    connect: async (req: { cameraId: string; mode?: ConnectionMode; reconnecting?: "on" | "off" }) =>
      unwrap(
        await this.http.POST("/api/cameras/{cameraId}/connection", {
          params: { path: { cameraId: req.cameraId } },
          body: { mode: req.mode, reconnecting: req.reconnecting },
        }),
      ),

    disconnect: async (req: { cameraId: string }) =>
      unwrap(
        await this.http.DELETE("/api/cameras/{cameraId}/connection", {
          params: { path: { cameraId: req.cameraId } },
        }),
      ),

    getConnectionStatus: async (req: { cameraId: string }) =>
      unwrap(
        await this.http.GET("/api/cameras/{cameraId}/connection", {
          params: { path: { cameraId: req.cameraId } },
        }),
      ),
  };

  readonly properties = {
    get: async (req: { cameraId: string; propertyName: PropertyName }) =>
      unwrap(
        await this.http.GET("/api/cameras/{cameraId}/properties/{propertyName}", {
          params: { path: { cameraId: req.cameraId, propertyName: req.propertyName } },
        }),
      ),

    set: async (req: { cameraId: string; propertyName: PropertyName; value: string }) =>
      unwrap(
        await this.http.PUT("/api/cameras/{cameraId}/properties/{propertyName}", {
          params: { path: { cameraId: req.cameraId, propertyName: req.propertyName } },
          body: { value: req.value },
        }),
      ),

    getAll: async (req: { cameraId: string }) =>
      unwrap(
        await this.http.GET("/api/cameras/{cameraId}/properties/all", {
          params: { path: { cameraId: req.cameraId } },
        }),
      ),

    setPriorityKey: async (req: { cameraId: string; setting: PriorityKeySetting }) =>
      unwrap(
        await this.http.PUT("/api/cameras/{cameraId}/priority-key", {
          params: { path: { cameraId: req.cameraId } },
          body: { setting: req.setting },
        }),
      ),
  };

  readonly actions = {
    /** `action` is only for continuous shooting: `down` starts, `up` stops. */
    shutter: async (req: { cameraId: string; action?: "down" | "up" }) =>
      unwrap(
        await this.http.POST("/api/cameras/{cameraId}/actions/shutter", {
          params: { path: { cameraId: req.cameraId } },
          body: req.action ? { action: req.action } : {},
        }),
      ),

    afShutter: async (req: { cameraId: string }) =>
      unwrap(
        await this.http.POST("/api/cameras/{cameraId}/actions/af-shutter", {
          params: { path: { cameraId: req.cameraId } },
        }),
      ),

    halfPress: async (req: { cameraId: string }) =>
      unwrap(
        await this.http.POST("/api/cameras/{cameraId}/actions/half-press", {
          params: { path: { cameraId: req.cameraId } },
        }),
      ),

    focusNearFar: async (req: { cameraId: string; step: number }) =>
      unwrap(
        await this.http.POST("/api/cameras/{cameraId}/actions/focus-near-far", {
          params: { path: { cameraId: req.cameraId } },
          body: { step: req.step },
        }),
      ),

    zoom: async (req: { cameraId: string; body: ZoomRequest }) =>
      unwrap(
        await this.http.POST("/api/cameras/{cameraId}/actions/zoom", {
          params: { path: { cameraId: req.cameraId } },
          body: req.body,
        }),
      ),

    movieRec: async (req: { cameraId: string }) =>
      unwrap(
        await this.http.POST("/api/cameras/{cameraId}/actions/movie-rec", {
          params: { path: { cameraId: req.cameraId } },
        }),
      ),
  };

  readonly liveView = {
    getStatus: async (req: { cameraId: string }) =>
      unwrap(
        await this.http.GET("/api/cameras/{cameraId}/live-view/status", {
          params: { path: { cameraId: req.cameraId } },
        }),
      ),

    enable: async (req: { cameraId: string }) =>
      unwrap(
        await this.http.POST("/api/cameras/{cameraId}/live-view/enable", {
          params: { path: { cameraId: req.cameraId } },
        }),
      ),

    disable: async (req: { cameraId: string }) =>
      unwrap(
        await this.http.POST("/api/cameras/{cameraId}/live-view/disable", {
          params: { path: { cameraId: req.cameraId } },
        }),
      ),

    start: async (req: { cameraId: string }) =>
      unwrap(
        await this.http.POST("/api/cameras/{cameraId}/live-view/start", {
          params: { path: { cameraId: req.cameraId } },
        }),
      ),

    stop: async (req: { cameraId: string }) =>
      unwrap(
        await this.http.POST("/api/cameras/{cameraId}/live-view/stop", {
          params: { path: { cameraId: req.cameraId } },
        }),
      ),

    getOsdStatus: async (req: { cameraId: string }) =>
      unwrap(
        await this.http.GET("/api/cameras/{cameraId}/live-view/osd-status", {
          params: { path: { cameraId: req.cameraId } },
        }),
      ),

    enableOsd: async (req: { cameraId: string }) =>
      unwrap(
        await this.http.POST("/api/cameras/{cameraId}/live-view/osd-enable", {
          params: { path: { cameraId: req.cameraId } },
        }),
      ),

    disableOsd: async (req: { cameraId: string }) =>
      unwrap(
        await this.http.POST("/api/cameras/{cameraId}/live-view/osd-disable", {
          params: { path: { cameraId: req.cameraId } },
        }),
      ),

    getFrame: (req: { cameraId: string }) =>
      this.jpeg(`/api/cameras/${encodeURIComponent(req.cameraId)}/live-view/frame`),

    getOsdFrame: (req: { cameraId: string }) =>
      this.jpeg(`/api/cameras/${encodeURIComponent(req.cameraId)}/live-view/osd-frame`),
  };

  readonly sdCard = {
    list: async (req: { cameraId: string; slotNumber: number }) =>
      unwrap(
        await this.http.GET("/api/cameras/{cameraId}/sd-card/slot/{slotNumber}/files", {
          params: { path: { cameraId: req.cameraId, slotNumber: slot(req.slotNumber) } },
        }),
      ),

    download: async (req: SdFileRequest) =>
      unwrap(
        await this.http.POST(
          "/api/cameras/{cameraId}/sd-card/slot/{slotNumber}/files/{contentId}/{fileId}/download",
          { params: { path: sdPath(req) }, body: req.body ?? {} },
        ),
      ),

    downloadThumbnail: async (req: SdFileRequest) =>
      unwrap(
        await this.http.POST(
          "/api/cameras/{cameraId}/sd-card/slot/{slotNumber}/files/{contentId}/{fileId}/thumbnail",
          { params: { path: sdPath(req) }, body: req.body ?? {} },
        ),
      ),

    downloadScreennail: async (req: SdFileRequest) =>
      unwrap(
        await this.http.POST(
          "/api/cameras/{cameraId}/sd-card/slot/{slotNumber}/files/{contentId}/{fileId}/screennail",
          { params: { path: sdPath(req) }, body: req.body ?? {} },
        ),
      ),
  };

  readonly settings = {
    getSaveInfo: async (req: { cameraId: string }) =>
      unwrap(
        await this.http.GET("/api/cameras/{cameraId}/settings/save-info", {
          params: { path: { cameraId: req.cameraId } },
        }),
      ),

    setSaveInfo: async (req: { cameraId: string; path?: string; prefix?: string; startNo?: number }) =>
      unwrap(
        await this.http.PUT("/api/cameras/{cameraId}/settings/save-info", {
          params: { path: { cameraId: req.cameraId } },
          body: { path: req.path, prefix: req.prefix, startNo: req.startNo },
        }),
      ),

    download: async (req: { cameraId: string; body: { filename?: string } }) =>
      unwrap(
        await this.http.POST("/api/cameras/{cameraId}/settings/download", {
          params: { path: { cameraId: req.cameraId } },
          body: req.body,
        }),
      ),

    upload: async (req: { cameraId: string; body: { filename?: string } }) =>
      unwrap(
        await this.http.POST("/api/cameras/{cameraId}/settings/upload", {
          params: { path: { cameraId: req.cameraId } },
          body: req.body,
        }),
      ),

    listFiles: async (req: { cameraId: string }) =>
      unwrap(
        await this.http.GET("/api/cameras/{cameraId}/settings/files", {
          params: { path: { cameraId: req.cameraId } },
        }),
      ),

    importLut: async (req: { cameraId: string; filePath: string; slot?: number }) =>
      unwrap(
        await this.http.POST("/api/cameras/{cameraId}/lut/import", {
          params: { path: { cameraId: req.cameraId } },
          body: { filePath: req.filePath, slot: req.slot ?? 1 },
        }),
      ),
  };
}

interface SdFileRequest {
  cameraId: string;
  slotNumber: number;
  contentId: number;
  fileId: number;
  body?: { save_path?: string };
}

function sdPath(req: SdFileRequest) {
  return {
    cameraId: req.cameraId,
    slotNumber: slot(req.slotNumber),
    contentId: req.contentId,
    fileId: req.fileId,
  };
}

/** The spec only defines slots 1 and 2; reject anything else before the round-trip. */
export function slot(n: number): 1 | 2 {
  if (n !== 1 && n !== 2) throw new ApiError(400, undefined, `Invalid SD card slot ${n} (expected 1 or 2).`);
  return n;
}
