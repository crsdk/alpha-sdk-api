// =============================================================================
// SDK extraction — native (no-bash) port of the old scripts/extract-sdk.sh.
//
// Extracts a Sony Camera Remote SDK zip into the repo's shared/ layout, entirely
// in Node so `crsdk install` works on macOS, Linux, and Windows without a POSIX
// shell or an `unzip` binary. The zip is read with a pure-JS reader (adm-zip).
//
// Layout produced (matches what shared/*/CMakeLists.txt expect):
//   shared/sdk/lib/                 core runtime libs
//   shared/sdk/lib/CrAdapter/       transport adapter libs
//   shared/sdk/include/             SDK headers (*.h)
//   shared/opencv/<platform>/…      OpenCV runtime libs
//   shared/opencv/include/opencv2/  OpenCV headers (only if missing)
//   shared/core/                    stock SDK sample helpers (CameraDevice.cpp …)
// =============================================================================

import AdmZip from 'adm-zip';
import {
  existsSync, mkdirSync, mkdtempSync, readdirSync, rmSync, statSync,
  copyFileSync, cpSync,
} from 'node:fs';
import { basename, join } from 'node:path';
import { tmpdir } from 'node:os';
import { colors, symbols } from './theme.js';

export type SdkPlatform = 'macos' | 'linux-x64' | 'linux-arm64' | 'linux-arm' | 'win32-x64';

/** One glob copy: everything in external/opencv/<fromRel> matching `match` → shared/opencv/<toRel>. */
interface OpenCvCopy {
  fromRel: string;
  toRel: string;
  match: (file: string) => boolean;
}

interface PlatformSpec {
  core: string[];      // → shared/sdk/lib/
  adapter: string[];   // → shared/sdk/lib/CrAdapter/
  opencv: OpenCvCopy[];
}

const SPECS: Record<SdkPlatform, PlatformSpec> = {
  macos: {
    core: ['libCr_Core.dylib', 'libmonitor_protocol.dylib', 'libmonitor_protocol_pf.dylib'],
    adapter: ['libCr_PTP_IP.dylib', 'libCr_PTP_USB.dylib', 'libssh2.dylib', 'libusb-1.0.0.dylib'],
    opencv: [{
      fromRel: join('Darwin', 'Release', 'macos', 'bin'),
      toRel: join('Darwin', 'Release', 'macos', 'bin'),
      match: (f) => f.startsWith('libopencv_') && f.endsWith('.dylib'),
    }],
  },
  'linux-x64': linuxSpec(),
  'linux-arm64': linuxSpec(),
  'linux-arm': linuxSpec(),
  'win32-x64': {
    core: ['Cr_Core.dll', 'Cr_Core.lib', 'monitor_protocol.dll', 'monitor_protocol_pf.dll'],
    adapter: ['Cr_PTP_IP.dll', 'Cr_PTP_USB.dll', 'libssh2.dll', 'libusb-1.0.dll'],
    opencv: [
      {
        fromRel: join('Windows', 'x86_64', 'Release', 'bin'),
        toRel: join('Windows', 'x86_64', 'Release', 'bin'),
        match: (f) => f.startsWith('opencv_') && f.endsWith('.dll'),
      },
      {
        fromRel: join('Windows', 'x86_64', 'Release', 'lib'),
        toRel: join('Windows', 'x86_64', 'Release', 'lib'),
        match: (f) => f.startsWith('opencv_') && f.endsWith('.lib'),
      },
    ],
  },
};

function linuxSpec(): PlatformSpec {
  return {
    core: ['libCr_Core.so', 'libmonitor_protocol.so', 'libmonitor_protocol_pf.so'],
    adapter: ['libCr_PTP_IP.so', 'libCr_PTP_USB.so', 'libssh2.so', 'libusb-1.0.so'],
    opencv: [{
      fromRel: 'Linux',
      toRel: 'Linux',
      // Linux OpenCV libs are versioned: libopencv_core.so.408
      match: (f) => f.startsWith('libopencv_') && f.includes('.so'),
    }],
  };
}

// Stock SDK sample helper sources placed into shared/core (Sony's, not
// redistributed in the repo). RemoteCli.cpp is intentionally NOT copied — the
// REST server has its own entry point.
const CORE_FILES = [
  'CameraDevice.cpp', 'CameraDevice.h',
  'PropertyValueTable.cpp', 'PropertyValueTable.h',
  'ConnectionInfo.cpp', 'ConnectionInfo.h',
  'Text.cpp', 'Text.h',
  'MessageDefine.cpp', 'MessageDefine.h',
  'OpenCVWrapper.cpp', 'OpenCVWrapper.h',
  'CrDebugString.cpp', 'CrDebugString.h',
];

function step(msg: string): void {
  console.log(`  ${colors.dim(msg + '…')}`);
}

function hasExternal(dir: string): boolean {
  return existsSync(join(dir, 'external'));
}

/** First immediate subdirectory of `base` that contains an `external/` folder. */
function firstDirWithExternal(base: string): string | null {
  let entries: string[];
  try { entries = readdirSync(base); } catch { return null; }
  for (const e of entries) {
    const d = join(base, e);
    try { if (statSync(d).isDirectory() && hasExternal(d)) return d; } catch { /* skip */ }
  }
  return null;
}

/**
 * Find the directory that holds `external/` and `app/`. Handles the layouts the
 * shell script did: external at the top level, one directory down, or nested
 * inside a RemoteCli.zip (V2.01+ archives).
 */
function resolveExtractRoot(temp: string): string {
  if (hasExternal(temp)) return temp;
  const sub = firstDirWithExternal(temp);
  if (sub) return sub;

  const innerZip = join(temp, 'RemoteCli.zip');
  if (existsSync(innerZip)) {
    const innerDir = join(temp, 'RemoteCli');
    new AdmZip(innerZip).extractAllTo(innerDir, true);
    if (hasExternal(innerDir)) return innerDir;
    const innerSub = firstDirWithExternal(innerDir);
    if (innerSub) return innerSub;
    return innerDir;
  }
  return temp;
}

function copyInto(srcFile: string, dstDir: string): void {
  if (!existsSync(srcFile)) {
    throw new Error(`missing expected SDK file: ${srcFile}`);
  }
  mkdirSync(dstDir, { recursive: true });
  copyFileSync(srcFile, join(dstDir, basename(srcFile)));
}

function copyGlob(srcDir: string, match: (f: string) => boolean, dstDir: string): number {
  if (!existsSync(srcDir)) {
    throw new Error(`missing expected SDK directory: ${srcDir}`);
  }
  mkdirSync(dstDir, { recursive: true });
  let n = 0;
  for (const f of readdirSync(srcDir)) {
    if (match(f)) { copyFileSync(join(srcDir, f), join(dstDir, f)); n++; }
  }
  return n;
}

/**
 * Extract the SDK zip into <root>/shared. Throws on a malformed archive or a
 * missing required file/dir (caller maps that to a friendly error + exit code).
 */
export function extractSdk(args: { root: string; zip: string; platform: SdkPlatform }): void {
  const { root, zip, platform } = args;
  const spec = SPECS[platform];
  if (!spec) throw new Error(`unknown platform: ${platform}`);
  if (!existsSync(zip)) throw new Error(`zip not found: ${zip}`);

  const temp = mkdtempSync(join(tmpdir(), 'crsdk-sdk-'));
  try {
    new AdmZip(zip).extractAllTo(temp, true);
    const extractRoot = resolveExtractRoot(temp);

    const sdkSrc = join(extractRoot, 'external', 'crsdk');
    const opencvSrc = join(extractRoot, 'external', 'opencv');
    let headerSrc = join(extractRoot, 'app', 'CrSDK');
    if (!existsSync(headerSrc)) headerSrc = join(extractRoot, 'app', 'CRSDK');

    if (!existsSync(sdkSrc)) {
      throw new Error('could not find external/crsdk in the zip — see docs/SDK_SETUP.md for the expected archive layout.');
    }
    if (!existsSync(headerSrc)) {
      throw new Error('could not find SDK headers (app/CrSDK or app/CRSDK) — see docs/SDK_SETUP.md.');
    }

    const sdkLib = join(root, 'shared', 'sdk', 'lib');
    const adapterDst = join(sdkLib, 'CrAdapter');
    const opencvBase = join(root, 'shared', 'opencv');

    step('Copying SDK libraries');
    for (const f of spec.core) copyInto(join(sdkSrc, f), sdkLib);
    for (const f of spec.adapter) copyInto(join(sdkSrc, 'CrAdapter', f), adapterDst);

    step('Copying OpenCV libraries');
    for (const op of spec.opencv) {
      copyGlob(join(opencvSrc, op.fromRel), op.match, join(opencvBase, op.toRel));
    }

    const ocvInclude = join(opencvBase, 'include', 'opencv2');
    if (!existsSync(ocvInclude)) {
      step('Copying OpenCV headers');
      cpSync(join(opencvSrc, 'include', 'opencv2'), ocvInclude, { recursive: true });
    }

    step('Copying SDK headers');
    copyGlob(headerSrc, (f) => f.endsWith('.h'), join(root, 'shared', 'sdk', 'include'));

    // Stock SDK sample helper sources (shared/core). These live in the sample
    // app root (app/) of the archive, next to the CRSDK headers folder.
    const coreSrc = join(extractRoot, 'app');
    const coreDst = join(root, 'shared', 'core');
    if (existsSync(join(coreSrc, 'CameraDevice.cpp'))) {
      step('Copying stock SDK sample helper sources into shared/core');
      mkdirSync(coreDst, { recursive: true });
      for (const f of CORE_FILES) {
        const s = join(coreSrc, f);
        if (existsSync(s)) copyFileSync(s, join(coreDst, f));
        else console.log(`    ${colors.dim(`warning: ${f} not found in the SDK archive app/`)}`);
      }
    } else {
      console.log(`  ${symbols.warn} Stock helper sources (CameraDevice.cpp, …) not found in app/ — the C++ server cannot build without shared/core. See docs/SDK_SETUP.md.`);
    }
  } finally {
    rmSync(temp, { recursive: true, force: true });
  }
}
