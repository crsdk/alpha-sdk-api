// Error-message extraction from the API's ErrorResponse envelope.
import assert from "node:assert/strict";
import { describe, it } from "node:test";
import { ApiError } from "../src/api/client.js";
import { apiMessage, isNotImplemented } from "../src/errors.js";

describe("apiMessage", () => {
  it("uses the server's message", () => {
    const err = new ApiError(400, { success: false, message: "Camera not connected" });
    assert.equal(apiMessage(err), "Camera not connected");
  });

  it("appends a nested error in parentheses", () => {
    const err = new ApiError(400, {
      success: false,
      message: "Property set failed",
      data: { error: "focus_mode_not_af" },
    });
    assert.equal(apiMessage(err), "Property set failed (focus_mode_not_af)");
  });

  it("falls back to the status when the body carries nothing useful", () => {
    assert.equal(apiMessage(new ApiError(503, undefined)), "HTTP 503");
    assert.equal(apiMessage(new ApiError(500, {})), "HTTP 500");
  });

  it("passes plain errors and non-errors through", () => {
    assert.equal(apiMessage(new Error("socket hang up")), "socket hang up");
    assert.equal(apiMessage("something odd"), "something odd");
  });
});

describe("isNotImplemented", () => {
  it("recognises the SDK's 0x8402 rejection", () => {
    const err = new ApiError(400, { success: false, message: "SDK error 0x8402" });
    assert.equal(isNotImplemented(err), true);
  });

  it("recognises a plain not-implemented message, case-insensitively", () => {
    const err = new ApiError(400, { success: false, message: "Priority key Not Implemented" });
    assert.equal(isNotImplemented(err), true);
  });

  it("does not fire on ordinary failures", () => {
    const err = new ApiError(400, { success: false, message: "Camera not connected" });
    assert.equal(isNotImplemented(err), false);
  });
});
