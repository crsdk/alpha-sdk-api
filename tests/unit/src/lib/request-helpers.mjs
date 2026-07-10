export function normalizeBaseUrl(baseUrl = 'http://localhost:8080') {
  const trimmed = baseUrl.trim();
  if (!trimmed) {
    throw new Error('baseUrl must not be empty');
  }
  return trimmed.replace(/\/+$/, '');
}

export function cameraApiPath(cameraId, suffix = '') {
  const encodedId = encodeURIComponent(requireNonEmptyString('cameraId', cameraId));
  const trimmedSuffix = suffix.trim();
  if (!trimmedSuffix) {
    return `/api/cameras/${encodedId}`;
  }
  return `/api/cameras/${encodedId}/${trimmedSuffix.replace(/^\/+/, '')}`;
}

export function buildQueryPath(path, query) {
  const params = new URLSearchParams();
  for (const [key, value] of Object.entries(query)) {
    if (value === undefined || value === null || value === '') {
      continue;
    }
    params.set(key, String(value));
  }

  const serialized = params.toString();
  return serialized ? `${path}?${serialized}` : path;
}

export function requireBodyString(body, key) {
  if (!isRecord(body)) {
    throw new Error(`Missing "${key}" in request body`);
  }

  const value = body[key];
  if (typeof value !== 'string' || value.length === 0) {
    throw new Error(`Missing "${key}" in request body`);
  }

  return value;
}

export function parseFocusStep(body) {
  if (!isRecord(body)) {
    throw new Error('step must be -7 to +7 (not 0)');
  }

  const step = body.step;
  if (typeof step !== 'number' || !Number.isInteger(step) || step < -7 || step > 7 || step === 0) {
    throw new Error('step must be -7 to +7 (not 0)');
  }

  return step;
}

export function formatError(error) {
  return error instanceof Error ? error.message : String(error);
}

function requireNonEmptyString(name, value) {
  if (typeof value !== 'string' || value.length === 0) {
    throw new Error(`${name} must not be empty`);
  }
  return value;
}

function isRecord(value) {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}
