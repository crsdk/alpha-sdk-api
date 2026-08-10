// Single-session state held across tool calls, plus a tiny async mutex.

export interface CaptureRef {
  savedPath?: string;
  filename?: string;
  slot?: number;
  contentId?: number;
  fileId?: number;
}

/** Serialises mutating operations against one camera (promise-chain mutex). */
export class Mutex {
  private tail: Promise<void> = Promise.resolve();

  async run<T>(fn: () => Promise<T>): Promise<T> {
    const prev = this.tail;
    let release!: () => void;
    this.tail = new Promise((r) => (release = r));
    await prev;
    try {
      return await fn();
    } finally {
      release();
    }
  }
}

export class SessionState {
  activeCameraId: string | null = null;
  model: string | null = null;
  mode: string | null = null; // remote | remote-transfer | contents
  priorityKeySet = false;
  liveViewWarm = false;
  osdSupported: boolean | null = null;
  lastCaptureRef: CaptureRef | null = null;
  readonly lock = new Mutex();

  clear(): void {
    this.activeCameraId = null;
    this.model = null;
    this.mode = null;
    this.priorityKeySet = false;
    this.liveViewWarm = false;
    this.osdSupported = null;
    this.lastCaptureRef = null;
  }
}
