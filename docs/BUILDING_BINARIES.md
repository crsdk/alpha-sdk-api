# Building Platform Binaries

The camera server is **built from source** — it links the Sony Camera Remote SDK,
which cannot be redistributed, so there is no prebuilt binary to download. Most
people should just use the `crsdk` CLI, which wraps this whole recipe:

```bash
./crsdk install --zip <sony-sdk.zip>   # accept the EULA + run scripts/extract-sdk.sh
./crsdk build                          # the CMake steps below
```

Use the manual steps in this document when you want to understand or customize
the build — a platform the CLI does not special-case, a different Sony SDK
version, or a build with local changes.

There is no build workflow in this repository: producing a binary requires the
Sony Camera Remote SDK, and the SDK can only be obtained by a human accepting
Sony's license on the official download page. CI cannot fetch it, so the build is
always a local step.

> **Do not redistribute the Sony SDK.** Do not commit it, attach it to a public
> release, or ship it inside a container image you publish. Each person building
> must download it themselves under Sony's licence. See [SDK_SETUP.md](SDK_SETUP.md).

## Prerequisites

- Sony Camera Remote SDK zip for the target platform ([download](https://support.d-imaging.sony.co.jp/app/sdk/en/index.html))
- CMake 3.16+ and a C++17 toolchain
- Linux only: `sudo apt-get install -y build-essential cmake pkg-config libudev-dev`

macOS and Windows runners already ship CMake. There is no OpenSSL or jsoncpp
dependency — jsoncpp is vendored under `api/server/third_party/`.

## Platform matrix

Build on the target platform; these are not cross-compiled.

| Target | Build host | `extract-sdk.sh` argument |
|---|---|---|
| `darwin-arm64` | macOS, Apple Silicon | `macos` |
| `darwin-x64` | macOS, Intel | `macos` |
| `linux-x64` | Ubuntu x86_64 | `linux-x64` |
| `linux-arm64` | Ubuntu ARM64 | `linux-arm64` |
| `win32-x64` | Windows x64 | `win32-x64` |

`linux-arm` is also accepted by the extraction script for 32-bit ARM.

## Build

```bash
# 1. Place the SDK (headers, libs, OpenCV, and the stock shared/core sources)
./scripts/extract-sdk.sh /path/to/CrSDK.zip macos

# 2. Configure and build
cd api/server
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release
```

Windows uses an explicit architecture flag instead:

```powershell
cd api\server
mkdir build; cd build
cmake -A x64 ..
cmake --build . --config Release
```

On macOS, clear quarantine and ad-hoc sign the SDK/OpenCV dylibs before running,
or the binary fails at launch with `library load disallowed by system policy` —
see [SDK_SETUP.md](SDK_SETUP.md#macos-quarantine-and-signing).

## Verify the binary is self-contained

End users install the binary without Homebrew or any runtime package manager, so
it must not link OpenSSL, jsoncpp, or Homebrew paths. Check before shipping:

```bash
# macOS — expect no ssl / crypto / jsoncpp / homebrew / Cellar matches
otool -L api/server/build/CameraWebApp | grep -iE 'ssl|crypto|jsoncpp|homebrew|Cellar'

# Linux — expect no libssl / libcrypto / libjsoncpp matches
ldd api/server/build/CameraWebApp | grep -iE 'libssl|libcrypto|libjsoncpp'
```

```powershell
# Windows — dumpbin is not on PATH by default; locate it via vswhere
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath  = & $vswhere -latest -property installationPath
$dumpbin = (Get-ChildItem "$vsPath\VC\Tools\MSVC" -Recurse -Filter dumpbin.exe | Select-Object -First 1).FullName
& $dumpbin /dependents api\server\build\Release\CameraWebApp.exe
```

Any match means the build picked up a system library and will not run on a clean
machine.

## What to ship

A platform bundle is the executable plus the SDK runtime libraries it loads:

```text
api/server/build/CameraWebApp        (CameraWebApp.exe under Release\ on Windows)
api/server/build/*.dylib             (macOS: Cr_Core, monitor_protocol, OpenCV)
api/server/build/Contents/**         (macOS framework layout)
```

On Linux and Windows the equivalent `.so` / `.dll` files sit alongside the
executable. Keep the `CrAdapter/` subdirectory intact — the SDK loads its USB and
PTP/IP adapters from it at runtime.

Verify the result before distributing: start the server and confirm
`GET /api/server/status` reports the expected `sdkVersion`, then connect a camera
and run `api/tests/api_tests.sh`. See [TESTING.md](TESTING.md).
