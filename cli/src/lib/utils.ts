// =============================================================================
// CLI Utilities — Extracted from camera-server.ts
// =============================================================================

import { existsSync, readFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = dirname(fileURLToPath(import.meta.url));
// From dist/cli/ → dist/ → package root
export const pkgRoot = join(__dirname, '..', '..');

export const PLATFORMS = ['darwin-arm64', 'linux-x64', 'linux-arm64', 'win32-x64'] as const;
export const NPM_SCOPE = '@alpha-sdk';
export const DOCS_URL = 'https://crsdk.github.io/alpha-sdk-api';
export const SONY_LICENSE_URL = 'https://support.d-imaging.sony.co.jp/app/sdk/licenseagreement_d/en.html';
export const LICENSE_ACCEPTED_FILE = join(pkgRoot, '.license-accepted');

export function getPackageVersion(): string {
  try {
    const pkg = JSON.parse(readFileSync(join(pkgRoot, 'package.json'), 'utf-8'));
    return pkg.version;
  } catch {
    return 'unknown';
  }
}

export function getCurrentPlatform(): string {
  return `${process.platform}-${process.arch}`;
}

export function getBinaryName(platform?: string): string {
  const p = platform ?? getCurrentPlatform();
  return p.startsWith('win32') ? 'CameraWebApp.exe' : 'CameraWebApp';
}

export function getNpmPkgDir(platform: string): string {
  return join(pkgRoot, 'node_modules', NPM_SCOPE, platform);
}

export function getLocalPlatformDir(platform: string): string {
  return join(pkgRoot, 'platform', platform);
}

export function findBinary(platform: string): { path: string; source: 'npm' | 'local' } | null {
  const binary = getBinaryName(platform);

  // Walk up from package root to find hoisted node_modules
  // npm hoists optionalDependencies to the project root, not inside this package
  let dir = pkgRoot;
  while (true) {
    const candidate = join(dir, 'node_modules', NPM_SCOPE, platform, binary);
    if (existsSync(candidate)) return { path: candidate, source: 'npm' };
    const parent = dirname(dir);
    if (parent === dir) break;
    dir = parent;
  }

  // Fallback to local platform/ (development)
  const localPath = join(getLocalPlatformDir(platform), binary);
  if (existsSync(localPath)) return { path: localPath, source: 'local' };

  return null;
}

export function parsePlatformArg(): string {
  const idx = process.argv.indexOf('--platform');
  if (idx !== -1 && process.argv[idx + 1]) {
    const platform = process.argv[idx + 1];
    if (!(PLATFORMS as readonly string[]).includes(platform)) {
      console.error(`Unknown platform: ${platform}`);
      console.error(`Available: ${PLATFORMS.join(', ')}`);
      process.exit(1);
    }
    return platform;
  }
  return getCurrentPlatform();
}

export function getOsDescription(): string {
  const platform = process.platform === 'darwin' ? 'macOS' :
    process.platform === 'win32' ? 'Windows' :
    process.platform === 'linux' ? 'Linux' : process.platform;
  return `${platform} (${process.arch})`;
}
