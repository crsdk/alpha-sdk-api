#!/usr/bin/env node
// =============================================================================
// crsdk — Alpha Camera REST API developer CLI (clone-and-build)
//
// One tool for the build-from-source workflow: check the toolchain, build the
// native REST server, run it, and wire up docs MCP servers. The server is
// compiled from this repo (it needs Sony's SDK, placed per docs/SDK_SETUP.md),
// never fetched as a prebuilt binary.
// =============================================================================

import { existsSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { spawnSync } from 'node:child_process';
import { colors, symbols } from '../lib/theme.js';
import {
  printHeader, printCheck, printKV, printSection, printCommand,
  printBlank, printSummary,
} from '../lib/components.js';
import { getPackageVersion, getOsDescription } from '../lib/utils.js';
import { mcpCommand } from '../lib/mcp.js';
import { ServerManager } from '../lib/ServerManager.js';
import { install, update, versions, use, sdkPresent } from '../lib/sdk.js';

// --- repo discovery ----------------------------------------------------------

/** Walk up from cwd to the repo root (the dir holding api/server/CMakeLists.txt). */
function findRepoRoot(): string | null {
  let dir = process.cwd();
  while (true) {
    if (existsSync(join(dir, 'api', 'server', 'CMakeLists.txt'))) return dir;
    const parent = dirname(dir);
    if (parent === dir) return null;
    dir = parent;
  }
}

function flag(argv: string[], name: string): string | undefined {
  const i = argv.indexOf(name);
  return i >= 0 ? argv[i + 1] : undefined;
}

function serverBinary(root: string): string | null {
  const name = process.platform === 'win32' ? 'CameraWebApp.exe' : 'CameraWebApp';
  for (const rel of [['api', 'server', 'build', name], ['api', 'server', 'build', 'Release', name]]) {
    const p = join(root, ...rel);
    if (existsSync(p)) return p;
  }
  return null;
}

function hasTool(cmd: string, args: string[] = ['--version']): boolean {
  const r = spawnSync(cmd, args, { stdio: 'ignore' });
  return r.status === 0;
}

// --- commands ----------------------------------------------------------------

async function doctor(): Promise<void> {
  await printHeader('crsdk doctor');
  const root = findRepoRoot();
  let passed = 0, warnings = 0, failed = 0;

  await printSection('Environment');
  await printKV('OS', getOsDescription());
  await printKV('Node', process.version);

  await printSection('Toolchain');
  const cmake = hasTool('cmake');
  await printCheck(cmake ? 'pass' : 'fail', 'CMake', cmake ? 'found' : 'missing — install cmake');
  cmake ? passed++ : failed++;
  const cc = hasTool('cc') || hasTool('clang') || hasTool('gcc') || process.platform === 'win32';
  await printCheck(cc ? 'pass' : 'fail', 'C++ compiler', cc ? 'found' : 'missing — install a C++ toolchain');
  cc ? passed++ : failed++;

  await printSection('Project');
  await printCheck(root ? 'pass' : 'fail', 'Repository', root ?? 'not found — run inside a clone of alpha-sdk-api');
  root ? passed++ : failed++;

  if (root) {
    const sdk = sdkPresent(root);
    await printCheck(sdk ? 'pass' : 'warn', 'Sony SDK placed', sdk ? 'ok' : 'not found — see docs/SDK_SETUP.md');
    sdk ? passed++ : warnings++;
    const bin = serverBinary(root);
    await printCheck(bin ? 'pass' : 'warn', 'Server built', bin ?? 'not built — run "crsdk build"');
    bin ? passed++ : warnings++;
  }

  printBlank();
  printSummary(passed, warnings, failed);
  process.exit(failed > 0 ? 1 : 0);
}

async function build(argv: string[]): Promise<void> {
  await printHeader('crsdk build');
  const root = findRepoRoot();
  if (!root) { fail('Not inside the repo. Clone alpha-sdk-api and run from within it.'); return; }
  if (!hasTool('cmake')) { fail('CMake not found. Install it, then re-run "crsdk build".'); return; }
  if (!sdkPresent(root)) {
    console.log(`${symbols.warn} Sony SDK not detected under shared/.`);
    console.log(`  Place it per ${colors.accent('docs/SDK_SETUP.md')} (Phase 2: "crsdk install --zip <sdk.zip>").`);
  }
  const clean = argv.includes('--clean');
  const serverDir = join(root, 'api', 'server');
  const buildDir = join(serverDir, 'build');
  if (clean && existsSync(buildDir)) {
    spawnSync(process.platform === 'win32' ? 'rmdir' : 'rm',
      process.platform === 'win32' ? ['/s', '/q', buildDir] : ['-rf', buildDir], { stdio: 'ignore' });
  }
  console.log(`${symbols.arrow} Configuring (Release)…`);
  let r = spawnSync('cmake', ['-S', serverDir, '-B', buildDir, '-DCMAKE_BUILD_TYPE=Release'], { stdio: 'inherit' });
  if (r.status !== 0) { fail('CMake configure failed. Run "crsdk doctor" to check the toolchain and SDK.'); return; }
  console.log(`${symbols.arrow} Building…`);
  r = spawnSync('cmake', ['--build', buildDir, '--config', 'Release'], { stdio: 'inherit' });
  if (r.status !== 0) { fail('Build failed. See the compiler output above.'); return; }
  const bin = serverBinary(root);
  printBlank();
  await printCheck('pass', 'Built', bin ?? buildDir);
  printCommand('crsdk serve');
}

async function serve(argv: string[]): Promise<void> {
  await printHeader('crsdk serve');
  const root = findRepoRoot();
  const bin = root ? serverBinary(root) : null;
  if (!bin) { fail('Server not built. Run "crsdk build" first.'); return; }
  const portArg = argv[argv.indexOf('--port') + 1];
  const port = argv.includes('--port') && portArg ? Number(portArg) : 8080;
  const mgr = new ServerManager({ binaryPath: bin, port, autoPort: true });
  const stop = () => { mgr.kill(); process.exit(0); };
  process.on('SIGINT', stop);
  process.on('SIGTERM', stop);
  console.log(`${symbols.arrow} Starting server on port ${colors.accent(String(port))}…`);
  try {
    await mgr.start();
  } catch (e) {
    fail((e as Error).message); return;
  }
  const p = mgr.getPort();
  await printCheck('pass', 'Server running', `http://localhost:${p}`);
  printKV('Docs', 'https://crsdk.app');
  console.log(`${symbols.bullet} Press ${colors.accent('Ctrl+C')} to stop.`);
  // keep the process alive
  await new Promise(() => {});
}

async function status(): Promise<void> {
  const mgr = new ServerManager({ binaryPath: 'unused', port: 8080 });
  // status only needs the port; probe directly
  const running = await mgr.isRunning().catch(() => false);
  await printCheck(running ? 'pass' : 'fail', 'Server', running ? 'running on :8080' : 'not running');
  process.exit(running ? 0 : 1);
}

async function stop(): Promise<void> {
  try {
    await fetch('http://127.0.0.1:8080/api/server/shutdown', { method: 'POST', signal: AbortSignal.timeout(3000) });
    await printCheck('pass', 'Server', 'stopped');
  } catch {
    await printCheck('warn', 'Server', 'not running (nothing to stop)');
  }
}

async function installCmd(argv: string[], isUpdate = false): Promise<void> {
  const root = findRepoRoot();
  if (!root) { fail('Not inside the repo. Clone alpha-sdk-api and run from within it.'); return; }
  const zip = flag(argv, '--zip');
  if (!zip) { fail('Missing --zip <path-to-sony-sdk.zip>. Download the SDK from Sony — see docs/SDK_SETUP.md.'); return; }
  await (isUpdate ? update : install)({ root, zip, platform: flag(argv, '--platform') });
}

async function versionsCmd(): Promise<void> {
  const root = findRepoRoot();
  if (!root) { fail('Not inside the repo.'); return; }
  await versions(root);
}

async function useCmd(argv: string[]): Promise<void> {
  const root = findRepoRoot();
  if (!root) { fail('Not inside the repo.'); return; }
  await use(root, argv[0]);
}

function fail(msg: string): void {
  console.log(`${symbols.cross} ${msg}`);
  process.exitCode = 1;
}

function help(): void {
  const b = colors.accentBold, m = colors.muted, a = colors.accent;
  console.log(`
${b('crsdk')} ${m('— Alpha Camera REST API developer CLI')}

${colors.white('USAGE')}
  crsdk <command> [options]

${colors.white('SDK')}
  ${a('install')} --zip <p>    Accept Sony's EULA and extract the SDK into shared/
  ${a('update')} --zip <p>     Archive the current SDK, then install a new one
  ${a('versions')}            List the active and archived SDK versions
  ${a('use')} <name>          Swap an archived SDK version back into shared/

${colors.white('SERVER')}
  ${a('doctor')}              Check toolchain, SDK, and build state
  ${a('build')} [--clean]     Compile the native REST server from source
  ${a('serve')} [--port N]    Run the built server (default :8080)
  ${a('status')}              Check whether the server is running
  ${a('stop')}                Stop a running server

${colors.white('AGENTS')}
  ${a('mcp')} <sub>           Manage docs MCP servers (status | install)

${colors.white('MISC')}
  ${a('help')}                Show this help
  ${a('version')}             Print the CLI version

${m('First run:')}  crsdk install --zip <sony-sdk.zip>  →  crsdk build  →  crsdk serve
`);
}

// --- dispatch ----------------------------------------------------------------

async function main(): Promise<void> {
  const [cmd, ...rest] = process.argv.slice(2);
  switch (cmd) {
    case 'doctor': return doctor();
    case 'install': return installCmd(rest);
    case 'update': return installCmd(rest, true);
    case 'versions': return versionsCmd();
    case 'use': return useCmd(rest);
    case 'build': return build(rest);
    case 'serve':
    case 'start': return serve(rest);
    case 'status': return status();
    case 'stop': return stop();
    case 'mcp': return mcpCommand(rest[0], rest[1], rest[2]);
    case 'version':
    case '--version':
    case '-v': console.log(getPackageVersion()); return;
    case 'help':
    case '--help':
    case '-h':
    case undefined: return help();
    default:
      console.log(`${symbols.cross} Unknown command: ${cmd}`);
      help();
      process.exitCode = 1;
  }
}

main().catch((e) => { console.error(e); process.exit(1); });
