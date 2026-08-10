// Server-owned SSE consumer feeding a bounded event ring buffer.
//
// The REST client ships no SSE client by design (the /api/events endpoint
// doesn't map onto request/response methods), so we own a single fetch-based
// stream reader that parses text/event-stream and appends normalized events to
// a ring buffer with a monotonic cursor. Tools pull from the buffer via
// poll_events and the blocking capture composites; nobody consumes the raw
// stream per call.

export interface CameraEvent {
  cursor: number;
  type: string;
  data: Record<string, unknown>;
}

// Backoff schedule for SSE reconnection (seconds), capped at the last value.
const RECONNECT_BACKOFF = [1, 2, 3, 5, 8, 13];

/** Bounded ring buffer with a monotonic high-water cursor. */
export class EventBuffer {
  private buf: CameraEvent[] = [];
  private cursorValue = 0;
  private waiters: Array<() => void> = [];

  constructor(private readonly maxlen: number) {}

  get cursor(): number {
    return this.cursorValue;
  }

  get oldestCursor(): number {
    return this.buf.length ? this.buf[0].cursor : this.cursorValue;
  }

  append(type: string, data: Record<string, unknown>): CameraEvent {
    this.cursorValue += 1;
    const ev: CameraEvent = { cursor: this.cursorValue, type, data };
    this.buf.push(ev);
    if (this.buf.length > this.maxlen) this.buf.shift();
    const woken = this.waiters;
    this.waiters = [];
    for (const w of woken) w();
    return ev;
  }

  since(sinceCursor: number): CameraEvent[] {
    return this.buf.filter((e) => e.cursor > sinceCursor);
  }

  /** True if events after sinceCursor have already been evicted. */
  dropped(sinceCursor: number): boolean {
    if (!this.buf.length) return false;
    return sinceCursor > 0 && sinceCursor < this.oldestCursor - 1;
  }

  private waitNotify(timeoutMs: number): Promise<void> {
    return new Promise((resolve) => {
      let done = false;
      const finish = () => {
        if (done) return;
        done = true;
        clearTimeout(timer);
        resolve();
      };
      const timer = setTimeout(finish, timeoutMs);
      this.waiters.push(finish);
    });
  }

  /** Block until an event after sinceCursor matches predicate, or timeout. */
  async waitFor(
    predicate: (ev: CameraEvent) => boolean,
    sinceCursor: number,
    timeoutMs: number,
  ): Promise<CameraEvent | null> {
    const deadline = Date.now() + timeoutMs;
    let cursor = sinceCursor;
    for (;;) {
      for (const ev of this.buf) {
        if (ev.cursor > cursor) {
          cursor = ev.cursor;
          if (predicate(ev)) return ev;
        }
      }
      const remaining = deadline - Date.now();
      if (remaining <= 0) return null;
      await this.waitNotify(remaining);
    }
  }

  /** Block up to timeoutMs for at least one event after the cursor. */
  async waitNew(sinceCursor: number, timeoutMs: number): Promise<void> {
    const deadline = Date.now() + timeoutMs;
    while (this.cursorValue <= sinceCursor) {
      const remaining = deadline - Date.now();
      if (remaining <= 0) return;
      await this.waitNotify(remaining);
    }
  }
}

export interface SSEFrame {
  event: string;
  data: string;
}

/**
 * Incremental `text/event-stream` frame parser.
 *
 * Split out from the stream reader so the framing rules — CRLF, multi-line
 * `data:`, keepalive comments, chunk boundaries mid-line — are testable without
 * a socket.
 */
export class SSEFrameParser {
  private buf = "";
  private eventName = "message";
  private dataLines: string[] = [];

  /** Feed a chunk; returns whatever complete frames it terminated. */
  push(chunk: string): SSEFrame[] {
    const frames: SSEFrame[] = [];
    this.buf += chunk;
    let nl: number;
    while ((nl = this.buf.indexOf("\n")) >= 0) {
      let line = this.buf.slice(0, nl);
      this.buf = this.buf.slice(nl + 1);
      if (line.endsWith("\r")) line = line.slice(0, -1);
      if (line.startsWith(":")) continue; // keepalive comment
      if (line === "") {
        const frame = this.take();
        if (frame) frames.push(frame);
        continue;
      }
      if (line.startsWith("event:")) this.eventName = line.slice(6).trim();
      else if (line.startsWith("data:")) this.dataLines.push(line.slice(5).replace(/^ /, ""));
    }
    return frames;
  }

  /** Emit any frame left dangling when the stream ends. */
  flush(): SSEFrame[] {
    const frame = this.take();
    return frame ? [frame] : [];
  }

  private take(): SSEFrame | null {
    if (!this.dataLines.length) return null;
    const frame = { event: this.eventName, data: this.dataLines.join("\n") };
    this.eventName = "message";
    this.dataLines = [];
    return frame;
  }
}

/**
 * The server sends a `connected` event immediately on stream open as the SSE
 * handshake, indistinguishable by name from a real camera connect. Swallow the
 * first one; everything after it is a genuine state change.
 */
export class HandshakeFilter {
  private seenFirstConnected = false;

  accept(eventName: string): boolean {
    if (eventName === "connected" && !this.seenFirstConnected) {
      this.seenFirstConnected = true;
      return false;
    }
    return true;
  }
}

/** Single long-lived task holding the SSE stream open, with reconnect. */
export class SSEConsumer {
  private running = false;
  private stop = false;
  private controller: AbortController | null = null;
  private readonly handshake = new HandshakeFilter();
  healthy = false;

  constructor(
    private readonly baseUrl: string,
    private readonly buffer: EventBuffer,
  ) {}

  start(): void {
    if (this.running) return;
    this.running = true;
    this.stop = false;
    void this.run();
  }

  async stopConsumer(): Promise<void> {
    this.stop = true;
    this.controller?.abort();
    this.running = false;
    this.healthy = false;
  }

  get isRunning(): boolean {
    return this.running;
  }

  private async run(): Promise<void> {
    let attempt = 0;
    const url = `${this.baseUrl}/api/events`;
    while (!this.stop) {
      this.controller = new AbortController();
      try {
        const res = await fetch(url, {
          headers: { Accept: "text/event-stream" },
          signal: this.controller.signal,
        });
        if (!res.ok || !res.body) throw new Error(`SSE HTTP ${res.status}`);
        this.healthy = true;
        attempt = 0;
        await this.consume(res.body);
      } catch (err) {
        if (this.stop) break;
        this.healthy = false;
        const delay = RECONNECT_BACKOFF[Math.min(attempt, RECONNECT_BACKOFF.length - 1)];
        attempt += 1;
        // Surface the stall so dependent tools don't hang blind.
        this.buffer.append("error", {
          source: "sse_consumer",
          message: (err as Error).message,
          reconnect_in: delay,
        });
        await new Promise((r) => setTimeout(r, delay * 1000));
      }
    }
    this.running = false;
    this.healthy = false;
  }

  private async consume(body: ReadableStream<Uint8Array>): Promise<void> {
    const reader = body.getReader();
    const decoder = new TextDecoder();
    const parser = new SSEFrameParser();
    for (;;) {
      const { value, done } = await reader.read();
      if (done) break;
      for (const f of parser.push(decoder.decode(value, { stream: true }))) {
        this.emit(f.event, f.data);
      }
    }
    for (const f of parser.flush()) this.emit(f.event, f.data);
  }

  private emit(eventName: string, raw: string): void {
    let data: Record<string, unknown>;
    try {
      data = raw ? JSON.parse(raw) : {};
    } catch {
      data = { raw };
    }
    if (!this.handshake.accept(eventName)) return;
    this.buffer.append(eventName, data);
  }
}
