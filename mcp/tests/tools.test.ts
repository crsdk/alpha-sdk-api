// Pure helpers the tool layer leans on for capture correlation and set-confirm.
import assert from "node:assert/strict";
import { describe, it } from "node:test";
import { valueMatches } from "../src/tools/action.js";
import { pickNewest, savedPathOf } from "../src/tools/files.js";

describe("valueMatches", () => {
  it("matches the raw value the camera echoed back", () => {
    assert.equal(valueMatches("0x1007d", "0x1007D", "1/125"), true);
  });

  it("matches the formatted value", () => {
    assert.equal(valueMatches("1/125", "0x1007d", "1/125"), true);
  });

  it("matches a formatted value carrying a label prefix", () => {
    // The camera reports "ISO 3200" for a requested "3200".
    assert.equal(valueMatches("3200", "0xc80", "ISO 3200"), true);
    assert.equal(valueMatches("5.6", "0x38", "F 5.6"), true);
  });

  it("ignores whitespace differences in the suffix comparison", () => {
    assert.equal(valueMatches("f/5.6", "0x38", "Aperture f/ 5.6"), true);
  });

  it("reports a genuine coercion as a miss", () => {
    // Asked for ISO 5000, camera clamped to 3200.
    assert.equal(valueMatches("5000", "0xc80", "ISO 3200"), false);
  });

  it("does not treat an empty request as a match", () => {
    assert.equal(valueMatches("", "0xc80", "ISO 3200"), false);
  });
});

describe("pickNewest", () => {
  it("returns null for an empty listing", () => {
    assert.equal(pickNewest([]), null);
  });

  it("picks the highest content_id", () => {
    assert.deepEqual(
      pickNewest([
        { content_id: 1, file_id: 9 },
        { content_id: 3, file_id: 1 },
        { content_id: 2, file_id: 5 },
      ]),
      [3, 1],
    );
  });

  it("breaks ties on content_id with the highest file_id", () => {
    assert.deepEqual(
      pickNewest([
        { content_id: 4, file_id: 2 },
        { content_id: 4, file_id: 7 },
        { content_id: 4, file_id: 5 },
      ]),
      [4, 7],
    );
  });

  it("handles a single-entry listing", () => {
    assert.deepEqual(pickNewest([{ content_id: 1, file_id: 1 }]), [1, 1]);
  });
});

describe("savedPathOf", () => {
  it("prefers savedPath, as downloadComplete sends it", () => {
    assert.equal(savedPathOf({ savedPath: "/tmp/a.jpg", filename: "a.jpg" }), "/tmp/a.jpg");
  });

  it("falls back to filename, as transferProgress sends it", () => {
    assert.equal(savedPathOf({ filename: "/tmp/b.arw" }), "/tmp/b.arw");
  });

  it("returns undefined when neither field is present", () => {
    assert.equal(savedPathOf({ percent: 40 }), undefined);
  });
});
