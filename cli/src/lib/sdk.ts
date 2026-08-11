// =============================================================================
// SDK lifecycle — accept Sony's EULA, extract the SDK into the repo's shared/
// layout (natively, see extract.ts), and archive/swap SDK versions.
//
// Reconstructed from the standalone @alpha-sdk/crsdk CLI and retargeted from
// Sony's cpp-sample/ layout to this repo's shared/ layout (see docs/SDK_SETUP.md).
// =============================================================================

import { existsSync, mkdirSync, readdirSync, readFileSync, renameSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { spawnSync } from 'node:child_process';
import { createInterface } from 'node:readline';
import { colors, symbols } from './theme.js';
import { printCheck, printHeader, printKV, printSection } from './components.js';
import { extractSdk, type SdkPlatform } from './extract.js';

export const SONY_LICENSE_URL =
  'https://support.d-imaging.sony.co.jp/app/sdk/licenseagreement_d/en.html';

const paths = (root: string) => ({
  shared: join(root, 'shared'),
  sdk: join(root, 'shared', 'sdk'),
  archive: join(root, 'shared', 'sdk-archive'),
  license: join(root, 'shared', '.license-accepted'),
  meta: join(root, 'shared', '.sdk-meta.json'),
});

/** Map the host to the SDK layout token used by the extractor. */
export function detectPlatform(): SdkPlatform {
  const p = process.platform, a = process.arch;
  if (p === 'darwin') return 'macos';
  if (p === 'linux') return a === 'arm64' ? 'linux-arm64' : a === 'arm' ? 'linux-arm' : 'linux-x64';
  if (p === 'win32') return 'win32-x64';
  throw new Error(`Unsupported platform: ${p}-${a}`);
}

/** Best-effort SDK version from a Sony zip filename (e.g. CrSDK_v2.01.00_… → 2.01.00). */
function versionFromZip(zip: string): string {
  const m = zip.match(/v?(\d+\.\d+\.\d+)/i);
  return m ? m[1] : 'unknown';
}

function prompt(q: string): Promise<string> {
  const rl = createInterface({ input: process.stdin, output: process.stdout });
  return new Promise((res) => rl.question(q, (a) => { rl.close(); res(a); }));
}

export function sdkPresent(root: string): boolean {
  const p = paths(root);
  return existsSync(join(p.sdk, 'include')) || existsSync(join(root, 'shared', 'core', 'CameraDevice.h'));
}

function readMeta(root: string): { version?: string; installedAt?: string } {
  const p = paths(root);
  if (!existsSync(p.meta)) return {};
  try { return JSON.parse(readFileSync(p.meta, 'utf8')); } catch { return {}; }
}

async function acceptEula(root: string): Promise<boolean> {
  const p = paths(root);
  if (existsSync(p.license)) { await printCheck('pass', 'Sony EULA', 'already accepted'); return true; }
  console.log(`\n  Before continuing you must accept Sony's license agreement:`);
  console.log(`    ${colors.accent(SONY_LICENSE_URL)}\n`);
  const ans = (await prompt('  Accept the Sony Camera Remote SDK License Agreement? (y/n) ')).trim().toLowerCase();
  if (ans !== 'y' && ans !== 'yes') {
    console.log(`\n  ${symbols.cross} License not accepted. Cannot continue.\n`);
    return false;
  }
  mkdirSync(p.shared, { recursive: true });
  writeFileSync(p.license, JSON.stringify({ accepted: new Date().toISOString() }, null, 2) + '\n');
  await printCheck('pass', 'Sony EULA', 'accepted');
  return true;
}

/** macOS-only: strip quarantine and ad-hoc sign the SDK/OpenCV dylibs. */
function clearQuarantineMac(root: string): void {
  if (process.platform !== 'darwin') return;
  const dirs = ['shared/sdk/lib', 'shared/opencv'].map((d) => join(root, d)).filter(existsSync);
  if (dirs.length === 0) return;
  spawnSync('bash', ['-c',
    `find ${dirs.map((d) => `'${d}'`).join(' ')} -type f -name '*.dylib' ` +
    `-exec xattr -dr com.apple.quarantine {} + 2>/dev/null; ` +
    `find ${dirs.map((d) => `'${d}'`).join(' ')} -type f -name '*.dylib' ` +
    `-exec codesign --force --sign - {} + 2>/dev/null`,
  ], { stdio: 'ignore' });
}

export interface InstallOpts { root: string; zip: string; platform?: string; }

export async function install(opts: InstallOpts): Promise<void> {
  await printHeader('crsdk install');
  const { root } = opts;
  const p = paths(root);

  if (!existsSync(opts.zip)) { console.log(`  ${symbols.cross} Zip not found: ${opts.zip}`); process.exitCode = 1; return; }

  if (!(await acceptEula(root))) { process.exitCode = 1; return; }

  const platform = (opts.platform as SdkPlatform | undefined) ?? detectPlatform();
  await printSection('Extracting');
  await printKV('Zip', opts.zip);
  await printKV('Platform', platform);

  // Native, cross-platform extraction (no bash / unzip binary required).
  try {
    extractSdk({ root, zip: opts.zip, platform });
  } catch (err) {
    console.log(`  ${symbols.cross} Extraction failed: ${(err as Error).message}`);
    console.log(`  See ${colors.accent('docs/SDK_SETUP.md')} for the expected archive layout.`);
    process.exitCode = 1; return;
  }

  clearQuarantineMac(root);
  writeFileSync(p.meta, JSON.stringify({ version: versionFromZip(opts.zip), platform, installedAt: new Date().toISOString() }, null, 2) + '\n');

  await printCheck('pass', 'SDK installed', `shared/  (v${versionFromZip(opts.zip)})`);
  if (process.platform === 'darwin') await printCheck('pass', 'Quarantine', 'cleared + ad-hoc signed');
  console.log(`\n  Next: ${colors.accent('crsdk build')}\n`);
}

/** Move the current shared/sdk into shared/sdk-archive/<version>/. */
export function archiveCurrent(root: string): string | null {
  const p = paths(root);
  if (!existsSync(p.sdk)) return null;
  const version = readMeta(root).version ?? 'unknown';
  mkdirSync(p.archive, { recursive: true });
  let name = version, dest = join(p.archive, name), n = 2;
  while (existsSync(dest)) { name = `${version}-${n++}`; dest = join(p.archive, name); }
  renameSync(p.sdk, dest);
  if (existsSync(p.meta)) renameSync(p.meta, join(dest, '.sdk-meta.json'));
  return name;
}

export async function update(opts: InstallOpts): Promise<void> {
  const archived = archiveCurrent(opts.root);
  if (archived) console.log(`  ${symbols.arrow} Archived current SDK → shared/sdk-archive/${archived}`);
  await install(opts);
}

export async function versions(root: string): Promise<void> {
  await printHeader('crsdk versions');
  const p = paths(root);
  const meta = readMeta(root);
  await printKV('Active', sdkPresent(root) ? `${meta.version ?? 'unknown'}  ${colors.dim('(shared/sdk)')}` : colors.dim('none installed'));
  const archives = existsSync(p.archive) ? readdirSync(p.archive) : [];
  if (archives.length === 0) { console.log(`\n  ${colors.dim('No archived SDK versions.')}\n`); return; }
  await printSection(`Archived (${archives.length})`);
  for (const a of archives) console.log(`  ${symbols.bullet} ${a}`);
  console.log(`\n  Run ${colors.accent('crsdk use <name>')} to swap an archive into shared/sdk.\n`);
}

export async function use(root: string, name?: string): Promise<void> {
  const p = paths(root);
  if (!name) { console.log(`\n  Usage: ${colors.accent('crsdk use <archive-name>')}  (see ${colors.accent('crsdk versions')})\n`); process.exitCode = 1; return; }
  const src = join(p.archive, name);
  if (!existsSync(src)) { console.log(`  ${symbols.cross} No archived version named "${name}".`); process.exitCode = 1; return; }
  const archived = archiveCurrent(root); // preserve current first (reversible swap)
  renameSync(src, p.sdk);
  const savedMeta = join(p.sdk, '.sdk-meta.json');
  if (existsSync(savedMeta)) renameSync(savedMeta, p.meta);
  if (archived) console.log(`  ${symbols.arrow} Archived previous → shared/sdk-archive/${archived}`);
  await printCheck('pass', 'SDK swapped', `${name} → shared/sdk`);
}
