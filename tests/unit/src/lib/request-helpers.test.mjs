import assert from 'node:assert/strict';
import { describe, it } from 'node:test';
import {
  buildQueryPath,
  cameraApiPath,
  formatError,
  normalizeBaseUrl,
  parseFocusStep,
  requireBodyString,
} from './request-helpers.mjs';

describe('request helpers', () => {
  describe('normalizeBaseUrl', () => {
    it('uses the default local server URL', () => {
      assert.equal(normalizeBaseUrl(), 'http://localhost:8080');
    });

    it('trims whitespace and removes all trailing slashes', () => {
      assert.equal(normalizeBaseUrl('  http://localhost:9090///  '), 'http://localhost:9090');
    });

    it('rejects an empty URL', () => {
      assert.throws(() => normalizeBaseUrl('   '), /baseUrl must not be empty/);
    });
  });

  describe('cameraApiPath', () => {
    it('builds an encoded camera root path', () => {
      assert.equal(cameraApiPath('cam 1'), '/api/cameras/cam%201');
    });

    it('appends a normalized suffix', () => {
      assert.equal(cameraApiPath('cam/1', ' /settings/download '), '/api/cameras/cam%2F1/settings/download');
    });

    it('rejects an empty camera id', () => {
      assert.throws(() => cameraApiPath(''), /cameraId must not be empty/);
    });
  });

  describe('buildQueryPath', () => {
    it('omits the query string when every value is empty', () => {
      assert.equal(buildQueryPath('/api/server/logs', {
        lines: undefined,
        level: null,
        filter: '',
      }), '/api/server/logs');
    });

    it('serializes strings, numbers, and booleans', () => {
      assert.equal(buildQueryPath('/api/server/logs', {
        lines: 50,
        level: 'warn',
        compact: false,
      }), '/api/server/logs?lines=50&level=warn&compact=false');
    });
  });

  describe('requireBodyString', () => {
    it('returns a required string value', () => {
      assert.equal(requireBodyString({ value: '0x00000064' }, 'value'), '0x00000064');
    });

    it('rejects non-object bodies', () => {
      assert.throws(() => requireBodyString(null, 'value'), /Missing "value" in request body/);
    });

    it('rejects arrays', () => {
      assert.throws(() => requireBodyString([], 'value'), /Missing "value" in request body/);
    });

    it('rejects missing values', () => {
      assert.throws(() => requireBodyString({}, 'value'), /Missing "value" in request body/);
    });

    it('rejects non-string values', () => {
      assert.throws(() => requireBodyString({ value: 100 }, 'value'), /Missing "value" in request body/);
    });

    it('rejects empty strings', () => {
      assert.throws(() => requireBodyString({ value: '' }, 'value'), /Missing "value" in request body/);
    });
  });

  describe('parseFocusStep', () => {
    it('accepts negative and positive integer steps inside the allowed range', () => {
      assert.equal(parseFocusStep({ step: -7 }), -7);
      assert.equal(parseFocusStep({ step: 7 }), 7);
    });

    it('rejects non-object bodies', () => {
      assert.throws(() => parseFocusStep(undefined), /step must be -7 to \+7/);
    });

    it('rejects non-number steps', () => {
      assert.throws(() => parseFocusStep({ step: '1' }), /step must be -7 to \+7/);
    });

    it('rejects fractional steps', () => {
      assert.throws(() => parseFocusStep({ step: 1.5 }), /step must be -7 to \+7/);
    });

    it('rejects steps below the lower bound', () => {
      assert.throws(() => parseFocusStep({ step: -8 }), /step must be -7 to \+7/);
    });

    it('rejects steps above the upper bound', () => {
      assert.throws(() => parseFocusStep({ step: 8 }), /step must be -7 to \+7/);
    });

    it('rejects zero', () => {
      assert.throws(() => parseFocusStep({ step: 0 }), /step must be -7 to \+7/);
    });
  });

  describe('formatError', () => {
    it('uses Error messages', () => {
      assert.equal(formatError(new Error('failed')), 'failed');
    });

    it('stringifies non-Error values', () => {
      assert.equal(formatError(404), '404');
    });
  });
});
