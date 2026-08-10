// =============================================================================
// MCP build pipeline — regenerate, compile, and pack the Alpha Camera MCP
// server that lives in mcp/, mirroring the C++ server's build flow.
//
//   crsdk gen:mcp    → regenerate mcp/src/api + mcp/src/tools/generated.ts
//                      from api/openapi.yaml (flat tools only; committed)
//   crsdk build:mcp  → bundle both halves (generated flat tools + hand-written
//                      composites/SSE/session) from mcp/src → mcp/dist
//   crsdk pack:mcp   → alpha-camera.mcpb  (+ the sony-camera-control skill note)
//
// The single source of truth is api/openapi.yaml; the MCP codegen reads it via
// mcp/'s "codegen" npm script (../api/openapi.yaml). These commands never touch
// the hand-written composite core.
// =============================================================================

import { existsSync, rmSync, cpSync, mkdtempSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { spawnSync } from 'node:child_process';
import { colors, symbols } from './theme.js';
import { printHeader, printCheck, printKV, printCommand, printBlank } from './components.js';

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
  if (!existsSync(server)) { fail('Server not built. Run "crsdk build:mcp" first.'); return; }
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
    await printKV('Server', colors.dim('run "crsdk start" — the CameraWebApp binary is not bundled'));
  } finally {
    rmSync(stage, { recursive: true, force: true });
  }
}
