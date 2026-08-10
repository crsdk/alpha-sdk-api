// resolveFromAvailable: map a human-readable request to the hex from available_values,
// so enum properties without a working server-side string parser (movie-file-format,
// movie-recording-frame-rate) can still be set. Reproduces the 4K/24p case.
import assert from "node:assert/strict";
import { describe, it } from "node:test";
import type { Ctx } from "../src/context.js";
import type { PropertyName } from "../src/api/client.js";
import { resolveFromAvailable } from "../src/tools/action.js";

// A Ctx whose client returns a fixed available_values list for properties.get.
function ctxWith(available: Array<Record<string, unknown>>): Ctx {
  return {
    client: {
      properties: {
        get: async () => ({ data: { available_values: available } }),
      },
    },
  } as unknown as Ctx;
}

const FRAME_RATE = [
  { value: "0x503", formatted: "60p" },
  { value: "0x107", formatted: "24p" },
];
const FILE_FORMAT = [
  { value: "0x2", formatted: "XAVC S 4K" },
  { value: "0x3", formatted: "XAVC S HD" },
];

describe("resolveFromAvailable", () => {
  it("resolves a formatted label to its hex (24p -> 0x107)", async () => {
    const hex = await resolveFromAvailable(ctxWith(FRAME_RATE), "cam", "movie-recording-frame-rate" as PropertyName, "24p");
    assert.equal(hex, "0x107");
  });

  it("resolves a multi-word formatted label (XAVC S 4K -> 0x2), case-insensitively", async () => {
    const hex = await resolveFromAvailable(ctxWith(FILE_FORMAT), "cam", "movie-file-format" as PropertyName, "xavc s 4k");
    assert.equal(hex, "0x2");
  });

  it("passes a hex request straight through when it is a known value", async () => {
    const hex = await resolveFromAvailable(ctxWith(FRAME_RATE), "cam", "movie-recording-frame-rate" as PropertyName, "0x503");
    assert.equal(hex, "0x503");
  });

  it("returns null when the request is not an available value (24p while only 60p is offered)", async () => {
    const only60 = [{ value: "0x503", formatted: "60p" }];
    const hex = await resolveFromAvailable(ctxWith(only60), "cam", "movie-recording-frame-rate" as PropertyName, "24p");
    assert.equal(hex, null);
  });

  it("supports the get_camera_state shape (separate hex_value field)", async () => {
    const shaped = [{ value: 263, hex_value: "0x107", formatted: "24p" }];
    const hex = await resolveFromAvailable(ctxWith(shaped), "cam", "movie-recording-frame-rate" as PropertyName, "24p");
    assert.equal(hex, "0x107");
  });
});
