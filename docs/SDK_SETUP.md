# Sony Camera Remote SDK Setup

Sony Camera Remote SDK files are intentionally excluded from this open-source repository. Each contributor must download the SDK from Sony and place the files locally.

This includes both the SDK headers/libraries **and** the SDK integration wrapper under `shared/core/` (`CameraDevice`, `PropertyValueTable`, `CrDebugString`, `MessageDefine`, `ConnectionInfo`, `Text`, `OpenCVWrapper`). Those wrapper sources are Sony sample-derived, ship inside the SDK download, and are placed locally by the extraction helper — they are not redistributed in this repository. Only `shared/core/CMakeLists.txt` and `shared/core/README.md` are tracked.

## Download

Download the Camera Remote SDK from Sony's official developer site:

https://support.d-imaging.sony.co.jp/app/sdk/en/index.html

Use the latest Camera Remote SDK version supported by Sony. The codebase has been prepared against SDK `V2.02.00` and should remain backward compatible with `V2.01.00` where the SDK symbols are unchanged.

## Expected Local Layout

After extraction, the build expects this local structure:

```text
shared/sdk/
  include/
    CameraRemote_SDK.h
    CrCommandData.h
    CrControlCode.h
    CrDefines.h
    CrDeviceProperty.h
    CrError.h
    CrImageDataBlock.h
    CrOperationCode.h
    CrTypes.h
    ICrCameraObjectInfo.h
    IDeviceCallback.h
  lib/
    platform SDK libraries, including libCr_Core.dylib, libCr_Core.so, or Cr_Core.lib
    CrAdapter/
      platform adapter libraries
```

The SDK integration wrapper is placed alongside the tracked build files in
`shared/core/`:

```text
shared/core/
  CMakeLists.txt        (tracked in this repo)
  README.md             (tracked in this repo)
  CameraDevice.cpp/.h        (from SDK download — not tracked)
  PropertyValueTable.cpp/.h  (from SDK download — not tracked)
  CrDebugString.cpp/.h       (from SDK download — not tracked)
  MessageDefine.cpp/.h       (from SDK download — not tracked)
  ConnectionInfo.cpp/.h      (from SDK download — not tracked)
  Text.cpp/.h                (from SDK download — not tracked)
  OpenCVWrapper.cpp/.h       (from SDK download — not tracked)
```

The extraction helper looks for these wrapper sources inside the SDK archive
(under `app/rest-api-core/`, `rest-api-core/`, or the sample app root) and copies
them into `shared/core/`.

On macOS, the expected SDK runtime placement is:

```text
shared/sdk/lib/
  libCr_Core.dylib
  libmonitor_protocol.dylib
  libmonitor_protocol_pf.dylib
  CrAdapter/
    libCr_PTP_IP.dylib
    libCr_PTP_USB.dylib
    libssh2.dylib
    libusb-1.0.0.dylib
```

OpenCV runtime files are also expected under `shared/opencv/` by the current CMake files. For the public release, prefer documenting package-manager installation or a setup script instead of committing vendor binaries.

The current CMake files also expect OpenCV headers at:

```text
shared/opencv/include/opencv2/
```

The extraction helper below copies these headers from Sony's SDK sample archive when they are not already present.

## Extraction Helper

The repository includes:

```bash
./scripts/extract-sdk.sh /path/to/CrSDK.zip macos
./scripts/extract-sdk.sh /path/to/CrSDK.zip linux-x64
./scripts/extract-sdk.sh /path/to/CrSDK.zip linux-arm64
./scripts/extract-sdk.sh /path/to/CrSDK.zip linux-arm
./scripts/extract-sdk.sh /path/to/CrSDK.zip win32-x64
```

The script copies SDK headers into `shared/sdk/include/`, SDK libraries into `shared/sdk/lib/`, adapter libraries into `shared/sdk/lib/CrAdapter/`, OpenCV libraries into `shared/opencv/`, and the SDK integration wrapper sources into `shared/core/`.
It also copies OpenCV headers into `shared/opencv/include/` when that directory is missing.

## macOS Quarantine and Signing

Sony SDK and bundled OpenCV `.dylib` files downloaded from the internet may be quarantined by macOS or rejected because they are unsigned. If `CameraWebApp` exits with a `dyld` error such as `library load disallowed by system policy`, clear quarantine attributes and apply an ad-hoc signature to the local runtime files:

```bash
find shared/sdk/lib shared/opencv -type f -name "*.dylib" -exec xattr -dr com.apple.quarantine {} \;
find shared/sdk/lib shared/opencv -type f -name "*.dylib" -exec codesign --force --sign - {} \;
```

These commands are for local development only. Do not commit signed SDK/OpenCV binaries to the public repository.

## Build Errors When SDK Files Are Missing

CMake checks the SDK headers and platform core library during configure. If files are missing, the error should point back to this document and list the expected path, for example:

```text
Sony Camera Remote SDK headers are missing.
Expected placement: shared/sdk/include/*.h
Download the SDK from Sony and follow docs/SDK_SETUP.md.
```

If you see a compiler error for `CameraRemote_SDK.h` or a linker error for `Cr_Core`, first verify the `shared/sdk/` layout above.

## Do Not Commit SDK Files

Before publishing or opening a PR, verify that these are not tracked:

- `shared/sdk/include/`
- `shared/sdk/lib/`
- `shared/binaries/`
- `shared/core/` wrapper sources (`*.cpp`, `*.h`) — only `CMakeLists.txt` and `README.md` are tracked
- `shared/opencv/include/`
- `shared/opencv/Darwin/`
- `shared/opencv/Linux/`
- `shared/opencv/Windows/`
- `api/distribution/package/package/`
- SDK zip files
- copied OpenCV header or binary bundles

The public repository should contain setup instructions and extraction scripts, not Sony SDK redistribution artifacts.
