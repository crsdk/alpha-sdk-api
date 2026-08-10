// EventBuffer semantics and the hand-rolled SSE frame parser.
import assert from "node:assert/strict";
import { describe, it } from "node:test";
import { EventBuffer, HandshakeFilter, SSEFrameParser, type SSEFrame } from "../src/events.js";

describe("EventBuffer", () => {
  it("assigns monotonically increasing cursors", () => {
    const buf = new EventBuffer(10);
    assert.equal(buf.cursor, 0);
    assert.equal(buf.append("a", {}).cursor, 1);
    assert.equal(buf.append("b", {}).cursor, 2);
    assert.equal(buf.cursor, 2);
  });

  it("evicts oldest events past capacity but keeps the cursor climbing", () => {
    const buf = new EventBuffer(2);
    buf.append("a", {});
    buf.append("b", {});
    buf.append("c", {});
    assert.deepEqual(
      buf.since(0).map((e) => e.type),
      ["b", "c"],
    );
    assert.equal(buf.cursor, 3);
    assert.equal(buf.oldestCursor, 2);
  });

  it("returns only events after the given cursor", () => {
    const buf = new EventBuffer(10);
    buf.append("a", {});
    buf.append("b", {});
    assert.deepEqual(
      buf.since(1).map((e) => e.type),
      ["b"],
    );
    assert.deepEqual(buf.since(2), []);
  });

  it("reports dropped events only once the caller's cursor fell off the ring", () => {
    const buf = new EventBuffer(2);
    buf.append("a", {});
    buf.append("b", {});
    // Caller is current — nothing lost.
    assert.equal(buf.dropped(2), false);
    buf.append("c", {});
    // Caller at 0 is a fresh reader, not a lagging one.
    assert.equal(buf.dropped(0), false);
    buf.append("d", {});
    // Oldest retained is cursor 3, so a caller at 1 missed cursor 2.
    assert.equal(buf.dropped(1), true);
  });

  it("waitFor resolves on the first matching event after the cursor", async () => {
    const buf = new EventBuffer(10);
    buf.append("noise", {});
    const pending = buf.waitFor((e) => e.type === "target", buf.cursor, 1000);
    buf.append("other", {});
    buf.append("target", { hit: true });
    const ev = await pending;
    assert.equal(ev?.type, "target");
    assert.deepEqual(ev?.data, { hit: true });
  });

  it("waitFor ignores matches at or before the cursor", async () => {
    const buf = new EventBuffer(10);
    buf.append("target", { stale: true });
    const ev = await buf.waitFor((e) => e.type === "target", buf.cursor, 60);
    assert.equal(ev, null);
  });

  it("waitFor returns null on timeout", async () => {
    const buf = new EventBuffer(10);
    const ev = await buf.waitFor(() => true, 0, 50);
    assert.equal(ev, null);
  });

  it("waitNew returns as soon as an event lands", async () => {
    const buf = new EventBuffer(10);
    const started = Date.now();
    const pending = buf.waitNew(0, 5000);
    setTimeout(() => buf.append("a", {}), 10);
    await pending;
    assert.ok(Date.now() - started < 4000, "should not have waited for the full timeout");
  });
});

/** Feed a whole stream through the parser in one chunk. */
function parse(stream: string): SSEFrame[] {
  const parser = new SSEFrameParser();
  return [...parser.push(stream), ...parser.flush()];
}

describe("SSEFrameParser", () => {
  it("parses named events with their payloads", () => {
    const frames = parse('event: propertyChanged\ndata: {"cameraId":"CAM1","codes":[1,2]}\n\n');
    assert.deepEqual(frames, [
      { event: "propertyChanged", data: '{"cameraId":"CAM1","codes":[1,2]}' },
    ]);
  });

  it("ignores keepalive comments", () => {
    const frames = parse(': keepalive\n\nevent: afStatus\ndata: {"state":"focused"}\n\n');
    assert.equal(frames.length, 1);
    assert.equal(frames[0].event, "afStatus");
  });

  it("handles CRLF framing and joins multi-line data", () => {
    const frames = parse('event: error\r\ndata: {"code":\r\ndata: 42}\r\n\r\n');
    assert.deepEqual(frames, [{ event: "error", data: '{"code":\n42}' }]);
  });

  it("defaults the event name to `message` when none is given", () => {
    assert.equal(parse('data: {"x":1}\n\n')[0].event, "message");
  });

  it("resets the event name between frames", () => {
    const frames = parse("event: afStatus\ndata: a\n\ndata: b\n\n");
    assert.deepEqual(
      frames.map((f) => f.event),
      ["afStatus", "message"],
    );
  });

  it("reassembles frames split across chunk boundaries", () => {
    const parser = new SSEFrameParser();
    assert.deepEqual(parser.push("event: downloa"), []);
    assert.deepEqual(parser.push('dComplete\ndata: {"sav'), []);
    const frames = parser.push('edPath":"/tmp/a.jpg"}\n\n');
    assert.deepEqual(frames, [
      { event: "downloadComplete", data: '{"savedPath":"/tmp/a.jpg"}' },
    ]);
  });

  it("emits a dangling frame on flush when the stream ends without a blank line", () => {
    const parser = new SSEFrameParser();
    assert.deepEqual(parser.push("event: error\ndata: boom\n"), []);
    assert.deepEqual(parser.flush(), [{ event: "error", data: "boom" }]);
  });

  it("flushes nothing when no data lines are pending", () => {
    assert.deepEqual(new SSEFrameParser().flush(), []);
  });
});

describe("HandshakeFilter", () => {
  it("drops the first `connected` but keeps later ones", () => {
    const filter = new HandshakeFilter();
    assert.equal(filter.accept("connected"), false);
    assert.equal(filter.accept("connected"), true);
  });

  it("never drops other event types", () => {
    const filter = new HandshakeFilter();
    assert.equal(filter.accept("disconnected"), true);
    assert.equal(filter.accept("propertyChanged"), true);
    // The handshake allowance is still unspent.
    assert.equal(filter.accept("connected"), false);
  });
});
