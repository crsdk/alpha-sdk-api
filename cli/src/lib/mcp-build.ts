// =============================================================================
// MCP build pipeline — regenerate, compile, and pack the Alpha Camera MCP
// server that lives in mcp/, mirroring the C++ server's build flow.
//
//   crsdk gen:mcp    → regenerate mcp/src/api + mcp/src/tools/generated.ts
//                      from api/openapi.yaml (flat tools only; committed)
//   crsdk build:mcp  → bundle both halves (generated flat tools + hand-written
//                      composites/SSE/session) from mcp/src → mcp/dist
//   crsdk pack:mcp   → alpha-camera.mcpb — self-contained: bundles the built
//                      CameraWebApp + its Sony SDK / OpenCV runtime by default
//                      (builds the binary first if needed)
//
// The single source of truth is api/openapi.yaml; the MCP codegen reads it via
// mcp/'s "codegen" npm script (../api/openapi.yaml). These commands never touch
// the hand-written composite core.
// =============================================================================

import { existsSync, rmSync, cpSync, mkdtempSync, mkdirSync, readdirSync, chmodSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { spawnSync } from 'node:child_process';
import { colors, symbols } from './theme.js';
import { printHeader, printCheck, printKV, printCommand, printBlank, printSection } from './components.js';

function fail(msg: string): void {
  console.log(`${symbols.cross} ${msg}`);
  process.exitCode = 1;
}

function noRepo(): void {
  fail('Not inside the repo. Clone alpha-sdk-api and run from within it.');
}

/** Locate mcp/ under the repo root, or report why it's missing. */
function mcpDir(root: string | null): string | null {
  if (!root) { noRepo(); return null; }
  const dir = join(root, 'mcp');
  if (!existsSync(dir)) { fail('mcp/ not found in this repo — nothing to build.'); return null; }
  return dir;
}

/** Run an npm script in a directory, streaming output. Returns true on success. */
function npm(cwd: string, args: string[]): boolean {
  const r = spawnSync('npm', args, { cwd, stdio: 'inherit' });
  return r.status === 0;
}

/** Ensure node_modules is present in mcp/ before a codegen/build that needs it. */
function ensureDeps(dir: string): boolean {
  if (existsSync(join(dir, 'node_modules'))) return true;
  console.log(`${symbols.arrow} Installing MCP dependencies (first run)…`);
  return npm(dir, ['install']);
}

// --- native-binary bundling (pack:mcp) --------------------------------------

/** Locate the built CameraWebApp under the repo, or null if it isn't built. */
function serverBinary(root: string): string | null {
  const name = process.platform === 'win32' ? 'CameraWebApp.exe' : 'CameraWebApp';
  for (const rel of [['api', 'server', 'build', name], ['api', 'server', 'build', 'Release', name]]) {
    const p = join(root, ...rel);
    if (existsSync(p)) return p;
  }
  return null;
}

/** Shared-object extension for the host, used to pick runtime libs to bundle. */
function libExt(): string {
  return process.platform === 'darwin' ? '.dylib' : process.platform === 'win32' ? '.dll' : '.so';
}

/** The platform's OpenCV runtime-lib directory inside shared/opencv, if present. */
function opencvLibDir(root: string): string | null {
  const candidates =
    process.platform === 'darwin' ? [['Darwin', 'Release', 'macos', 'bin']] :
    process.platform === 'win32' ? [['Windows', 'Release', 'x64', 'bin'], ['Windows', 'x64', 'vc17', 'bin']] :
    [['Linux', 'Release', 'x64', 'lib'], ['Linux', 'Release', 'aarch64', 'lib']];
  for (const rel of candidates) {
    const p = join(root, 'shared', 'opencv', ...rel);
    if (existsSync(p)) return p;
  }
  return null;
}

/** Copy every shared lib (and any CrAdapter subdir) from src into dest, flat. */
function copyLibs(src: string, dest: string): number {
  if (!existsSync(src)) return 0;
  mkdirSync(dest, { recursive: true });
  let n = 0;
  for (const e of readdirSync(src, { withFileTypes: true })) {
    if (e.isDirectory() && e.name === 'CrAdapter') {
      // The SDK loads its USB/PTP transport plugins from a CrAdapter/ dir that
      // must sit next to libCr_Core — preserve the subdir verbatim.
      cpSync(join(src, e.name), join(dest, e.name), { recursive: true });
      n += readdirSync(join(src, e.name)).length;
    } else if (e.isFile() && e.name.includes(libExt())) {
      cpSync(join(src, e.name), join(dest, e.name));
      n++;
    }
  }
  return n;
}

/** macOS: strip quarantine + ad-hoc re-sign everything so Gatekeeper lets it run. */
function codesignMac(dir: string): void {
  if (process.platform !== 'darwin') return;
  spawnSync('bash', ['-c',
    `find '${dir}' -type f \\( -name '*.dylib' -o -perm +111 \\) ` +
    `-exec xattr -dr com.apple.quarantine {} + 2>/dev/null; ` +
    `find '${dir}' -type f \\( -name '*.dylib' -o -perm +111 \\) ` +
    `-exec codesign --force --sign - {} + 2>/dev/null`,
  ], { stdio: 'ignore' });
}

/**
 * Bundle the native server + its Sony SDK / OpenCV runtime into <stage>/server/,
 * building it first if it isn't built. Returns true on success.
 *
 * The MCP server spawns the binary with cwd = its own directory, and the binary
 * carries an @executable_path rpath, so co-locating every dylib (plus the SDK's
 * CrAdapter/) beside the executable makes the bundle self-resolving on any host.
 */
function bundleServerBinary(root: string, stage: string): boolean {
  let bin = serverBinary(root);
  if (!bin) {
    // "always bundle, build if not built": drive the CLI's own build subcommand.
    console.log(`${symbols.arrow} Server not built — building it first (crsdk build)…`);
    const self = process.argv[1];
    const r = spawnSync(process.execPath, [self, 'build'], { cwd: root, stdio: 'inherit' });
    if (r.status !== 0) { fail('Build failed. Place the SDK ("crsdk install --zip") and retry.'); return false; }
    bin = serverBinary(root);
  }
  if (!bin) { fail('Could not find CameraWebApp even after building.'); return false; }

  const serverOut = join(stage, 'server');
  mkdirSync(serverOut, { recursive: true });
  const binOut = join(serverOut, process.platform === 'win32' ? 'CameraWebApp.exe' : 'CameraWebApp');
  cpSync(bin, binOut);
  chmodSync(binOut, 0o755);

  const sdkLibs = copyLibs(join(root, 'shared', 'sdk', 'lib'), serverOut);
  const cvDir = opencvLibDir(root);
  const cvLibs = cvDir ? copyLibs(cvDir, serverOut) : 0;
  if (sdkLibs === 0) {
    fail('No Sony SDK runtime libs found under shared/sdk/lib — the bundled binary would not run. Place the SDK ("crsdk install --zip") and retry.');
    return false;
  }
  codesignMac(serverOut);

  printKV('Binary', `server/${process.platform === 'win32' ? 'CameraWebApp.exe' : 'CameraWebApp'}`);
  printKV('SDK libs', `${sdkLibs} (incl. CrAdapter)`);
  printKV('OpenCV libs', cvDir ? String(cvLibs) : colors.dim('not found — assuming static/linked'));
  return true;
}

export async function genMcp(root: string | null): Promise<void> {
  await printHeader('crsdk gen:mcp');
  const dir = mcpDir(root);
  if (!dir) return;
  if (!ensureDeps(dir)) { fail('npm install failed in mcp/.'); return; }

  console.log(`${symbols.arrow} Regenerating types + flat tools from ${colors.accent('api/openapi.yaml')}…`);
  if (!npm(dir, ['run', 'codegen'])) {
    fail('MCP codegen failed (see output above). A new/unclassified endpoint fails the coverage gate — classify it in mcp/scripts/gen-tools.mjs.');
    return;
  }
  printBlank();
  await printCheck('pass', 'Generated', 'mcp/src/api/{schema.d.ts,property-names.ts} + mcp/src/tools/generated.ts');
  printCommand('crsdk build:mcp');
}

export async function buildMcp(root: string | null): Promise<void> {
  await printHeader('crsdk build:mcp');
  const dir = mcpDir(root);
  if (!dir) return;
  if (!ensureDeps(dir)) { fail('npm install failed in mcp/.'); return; }

  console.log(`${symbols.arrow} Compiling mcp/src → mcp/dist (flat tools + composites/SSE/session)…`);
  if (!npm(dir, ['run', 'build'])) { fail('MCP build failed (see the tsc output above).'); return; }

  const server = join(dir, 'dist', 'server.js');
  printBlank();
  await printCheck(existsSync(server) ? 'pass' : 'warn', 'Built', existsSync(server) ? server : 'dist/server.js not found');
  printCommand('crsdk pack:mcp');
}

export async function packMcp(root: string | null): Promise<void> {
  await printHeader('crsdk pack:mcp');
  const dir = mcpDir(root);
  if (!dir) return;

  const server = join(dir, 'dist', 'server.js');
  if (!existsSync(server)) { fail('MCP server not built. Run "crsdk build:mcp" first.'); return; }
  if (!existsSync(join(dir, 'manifest.json'))) { fail('mcp/manifest.json missing — cannot pack.'); return; }

  // Pack from a clean staging dir with a production-only install, so the .mcpb
  // carries just dist/ + the 3 runtime deps — not the dev toolchain (tsc/tsx/
  // openapi-typescript), which would bloat the bundle ~5x.
  const stage = mkdtempSync(join(tmpdir(), 'alpha-mcpb-'));
  try {
    for (const f of ['manifest.json', 'package.json', 'package-lock.json', 'NOTICE']) {
      const src = join(dir, f);
      if (existsSync(src)) cpSync(src, join(stage, f));
    }
    cpSync(join(dir, 'dist'), join(stage, 'dist'), { recursive: true });

    // Bundle the native server + its Sony SDK / OpenCV runtime by default, so
    // the .mcpb is self-contained (Claude Desktop spawns it — no separate
    // "crsdk start"). Builds the binary first if it isn't built yet.
    await printSection('Bundling the camera server');
    if (!bundleServerBinary(root!, stage)) return;

    console.log(`${symbols.arrow} Installing production dependencies…`);
    const install = existsSync(join(stage, 'package-lock.json'))
      ? spawnSync('npm', ['ci', '--omit=dev'], { cwd: stage, stdio: 'inherit' })
      : spawnSync('npm', ['install', '--omit=dev'], { cwd: stage, stdio: 'inherit' });
    if (install.status !== 0) { fail('Production install failed (see output above).'); return; }

    const out = join(dir, 'alpha-camera.mcpb');
    if (existsSync(out)) rmSync(out);
    console.log(`${symbols.arrow} Packing the MCP bundle…`);
    const r = spawnSync('npx', ['--yes', '@anthropic-ai/mcpb', 'pack', stage, out], { stdio: 'inherit' });
    if (r.status !== 0 || !existsSync(out)) { fail('mcpb pack failed (see output above).'); return; }

    printBlank();
    await printCheck('pass', 'Bundle', out);
    // The Skill installs through a separate door — it cannot live inside the .mcpb.
    await printKV('Skill', colors.dim('mcp/skills/sony-camera-control (install separately)'));
    printBlank();
    console.log(`  ${symbols.warn} ${colors.yellow('This bundle embeds Sony\'s Camera Remote SDK runtime (libCr_Core,')}`);
    console.log(`     ${colors.yellow('CrAdapter, libusb/libssh2) + OpenCV. Distributing it redistributes')}`);
    console.log(`     ${colors.yellow('Sony\'s proprietary runtime — you are responsible for complying with')}`);
    console.log(`     ${colors.yellow('Sony\'s SDK EULA and each library\'s license, and (macOS) for signing/')}`);
    console.log(`     ${colors.yellow('notarizing before you ship it. Fine for local install as-is.')}`);
  } finally {
    rmSync(stage, { recursive: true, force: true });
  }
}
