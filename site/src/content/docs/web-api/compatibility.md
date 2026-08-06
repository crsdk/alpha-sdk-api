---
title: "Compatibility"
description: "OS compatibility, camera connection modes, and per-camera API support"
---

## OS Compatibility

Supported operating systems for the Alpha Camera REST API server binary.

| OS | Version / Detail |
|----|-----------------|
| **Windows** | Windows 11 64-bit |
| **Linux x64** | Ubuntu 22.04 LTS, Ubuntu 24.04 LTS |
| **Linux ARM64 — Jetson Orin Nano** | Cortex-A78AE, Ubuntu 20.04.6 |
| **Linux ARM64 — Jetson Nano B01** | Cortex-A57, Ubuntu 18.04.1 |
| **Linux ARM64 — Raspberry Pi 4 4GB** | Cortex-A72, Debian 12 Bookworm |
| **macOS** | 14 (Sonoma), 15 (Sequoia), 26+ (Tahoe) |

:::note
macOS universal binary is available from server binary v1.08.00, supporting both Intel and Apple Silicon.
:::


---

## Camera Connection Compatibility

Supported camera models and their connection mode support per physical layer. Connection modes: **R** = Remote, **C** = Contents, **T** = Remote Transfer.

| Model | USB (R/C/T) | Wired Ethernet (R/C/T) | Wireless (R/C/T) |
|-------|-------------|------------------------|-------------------|
| ILX-LR1 *2 | R / C / T | R / C / T | R / C / T |
| ILCE-1M2 | R / C / T | R / C / T | R / C / T |
| ILCE-1 | R / C / T | R / C / T | R / C / T |
| ILCE-9M3 | R / C / T | R / C / T | R / C / T |
| ILCE-9M2 | R / — / — | R / — / — | — / — / — |
| ILCE-7RM5 | R / C / T | — / — / — | R / C / T |
| ILCE-7RM4A | R / C / — | — / — / — | — / — / — |
| ILCE-7RM4 | R / — / — | — / — / — | — / — / — |
| ILCE-7CR | R / C / T | — / — / — | R / C / T |
| ILCE-7SM3 | R / C / T | — / — / — | R / C / T |
| ILCE-7M5 *1 | R / C / T | — / — / — | R / C / T |
| ILCE-7M4 | R / C / T | — / — / — | R / C / T |
| ILCE-7CM2 | R / C / T | — / — / — | R / C / T |
| ILCE-7C | R / C / — | — / — / — | — / — / — |
| ILCE-6700 | R / C / T | — / — / — | R / C / T |
| ILME-FX3A *2 | R / C / T | R / C / T *2 | R / C / T |
| ILME-FX3 (Ver.2.00+) *2 | R / C / T | R / C / T *2 | R / C / T |
| ILME-FX2 | R / C / T | — / — / — | R / C / T |
| ILME-FX30 *2 | R / C / T | R / C / T *2 | R / C / T |
| ZV-E1 | R / C / T | — / — / — | R / C / T |
| ZV-E10M2 | R / C / — | — / — / — | R / C / — |
| DSC-RX1RM3 | R / C / T | — / — / — | R / C / T |
| DSC-RX0M2 (Ver.3.00+) | R / C / — | — / — / — | — / — / — |

<sup>*1</sup> SSH authentication required for connection.<br/>
<sup>*2</sup> Use a USB Type-C wired LAN adaptor for wired Ethernet.

:::note
Connection mode details: **R** (Remote) = full camera control with auto-transfer, **C** (Contents) = SD card browsing only, **T** (Remote Transfer) = full control + explicit SD card download. See [Connection Modes](/alpha-sdk-api/web-api/overview/#connection-modes).
:::


---

## API Compatibility

Not all APIs are available on every camera. The table below shows per-camera support for APIs that vary across models. Universal APIs (connect, disconnect, get/set properties, send commands) are supported on all cameras and are not listed.

Data sourced from Camera Remote SDK V2.01.00 `api_list`.

### Legend

| Symbol | Meaning |
|--------|---------|
| ✓ | Supported |
| — | Not supported |

### Per-Camera API Support

| API | LR1 | A1II | A1 | A9III | A9II | A7RV | A7RIVa | A7RIV | A7CR | A7SIII | A7V | A7IV | A7CII | A7C | A6700 | FX3A | FX3 | FX2 | FX30 | ZV-E1 | ZV-E10II | RX1RIII | RX0II |
|-----|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|
| [Live View](/alpha-sdk-api/web-api/live-view/) | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| [OSD](/alpha-sdk-api/web-api/live-view/#osd) | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| [SD Card (C)](/alpha-sdk-api/web-api/sd-card/) | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | — | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| [SD Card (T)](/alpha-sdk-api/web-api/sd-card/#download-file-remote-transfer) | ✓ | ✓ | ✓ | ✓ | — | ✓ | — | — | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | — |
| [Transfer Callback](/alpha-sdk-api/web-api/events/#transferprogress) | ✓ | ✓ | ✓ | ✓ | — | ✓ | — | — | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | — |
| [Settings Download](/alpha-sdk-api/web-api/settings/#download-settings-to-pc) | ✓ | ✓ | ✓ | ✓ | — | ✓ | — | — | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — |
| [Settings Upload](/alpha-sdk-api/web-api/settings/#upload-settings-to-camera) | ✓ | ✓ | ✓ | ✓ | — | ✓ | — | — | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — |


### Capabilities discovered at runtime

Some newer endpoints are deliberately absent from the table above, because
support for them is **not a fixed per-model fact** and a ✓/— matrix would
misrepresent them. The camera reports what it will accept, and the answer can
change with the camera's current settings.

| API | What determines support | How to find out |
|---|---|---|
| [Button press](/alpha-sdk-api/web-api/actions/#button-press) | The body's own key list, which is much shorter than the full enum. An ILCE-7M4 accepts 24 keys — the D-pad, `enter`, `menu`, `fn`, `playback`, `c1`–`c4`, `movie`, `ael`, `af-on` — but not `delete`, `mode`, `c5`–`c7`, `display`, `home` or `thumbnail`. | A rejected press returns `400` with the camera's supported list in `data.supported_buttons`. |
| [AF frame position](/alpha-sdk-api/web-api/actions/#af-frame-position) | The current `focus-area`, not the model. Only areas that actually draw a box report one — Flexible Spot, Expand Flexible Spot and the Tracking variants. | `400` while the focus area draws no box. Read [`focus-area`](/alpha-sdk-api/web-api/properties/) to know which mode you are in. |
| [Remote touch](/alpha-sdk-api/web-api/actions/#remote-touch-and-tracking) | Alpha bodies accept a touch, but cannot choose what it does — `FunctionOfRemoteTouchOperation` is unsupported on Alpha including the ILCE-7M4. Only broadcast bodies can select the function. | Attempt the touch; the response reports what happened. |

Querying at runtime is the reliable approach for all three: the camera is the
authority, and it answers in the error body rather than requiring a lookup
table that would go stale as firmware changes.

:::note
**SD Card (C)** requires **Contents** mode. **SD Card (T)** and **Transfer Callback** require **Remote Transfer** mode. See the [Camera Connection Compatibility](#camera-connection-compatibility) table above for which modes each camera supports.
:::


---

## Property Compatibility

Most properties (ISO, aperture, shutter speed, white balance, drive mode, focus, exposure program mode, metering mode, etc.) are supported on all cameras. The table below lists only **properties that vary across models**.

### Legend

| Symbol | Meaning |
|--------|---------|
| ✓ | Supported |
| — | Not supported |

### Per-Camera Property Support

:::note
Camera names abbreviated. Properties not listed are universal (all cameras). Data sourced from SDK V2.01.00.
:::


| Property | LR1 | A1II | A1 | A9III | A9II | A7RV | A7RIVa | A7RIV | A7CR | A7SIII | A7V | A7IV | A7CII | A7C | A6700 | FX3A | FX3 | FX2 | FX30 | ZV-E1 | ZV-E10II | RX1RIII | RX0II |
|----------|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|
| [creative-look](/alpha-sdk-api/web-api/properties/#creative-look) | ✓ | ✓ | ✓ | ✓ | — | ✓ | — | — | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — |
| [picture-profile](/alpha-sdk-api/web-api/properties/#picture-profile) | ✓ | ✓ | ✓ | ✓ | — | ✓ | — | — | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — |
| [silent-mode](/alpha-sdk-api/web-api/properties/#silent-mode) | ✓ | ✓ | ✓ | ✓ | — | ✓ | — | — | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | — | — |
| [shutter-type](/alpha-sdk-api/web-api/properties/#shutter-type) | ✓ | ✓ | ✓ | — | — | ✓ | — | — | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | ✓ | ✓ | — | — | — | ✓ | — |
| [shutter-mode](/alpha-sdk-api/web-api/properties/#shutter-mode) | — | — | — | — | — | — | — | — | — | — | — | — | — | — | — | ✓ | ✓ | ✓ | ✓ | — | — | — | — |
| [flicker-less-shooting](/alpha-sdk-api/web-api/properties/#flicker-less-shooting) | — | ✓ | ✓ | ✓ | — | — | — | — | ✓ | — | ✓ | — | ✓ | — | — | — | — | ✓ | — | — | — | — | — |
| [flash-mode](/alpha-sdk-api/web-api/properties/#flash-mode) | — | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | ✓ | — |
| [audio-recording](/alpha-sdk-api/web-api/properties/#audio-recording) | — | ✓ | ✓ | ✓ | — | ✓ | — | — | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — |
| [timecode-*](/alpha-sdk-api/web-api/properties/#timecode-preset) | ✓ | ✓ | ✓ | ✓ | — | ✓ | — | — | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — |
| [image-stabilization](/alpha-sdk-api/web-api/properties/#image-stabilization) | ✓ | ✓ | ✓ | ✓ | — | ✓ | — | — | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — | — | — |
| [movie-stabilization](/alpha-sdk-api/web-api/properties/#movie-stabilization) | ✓ | ✓ | ✓ | ✓ | — | ✓ | — | — | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — |
| [long-exposure-nr](/alpha-sdk-api/web-api/properties/#long-exposure-nr) | ✓ | ✓ | ✓ | ✓ | — | ✓ | — | — | ✓ | ✓ | ✓ | ✓ | ✓ | — | — | ✓ | ✓ | ✓ | — | — | — | ✓ | — |
| [aps-c-s35](/alpha-sdk-api/web-api/properties/#aps-c-s35) | ✓ | ✓ | — | ✓ | — | ✓ | — | — | — | ✓ | ✓ | ✓ | — | — | — | ✓ | ✓ | ✓ | — | ✓ | — | — | — |
| [movie-shooting-mode](/alpha-sdk-api/web-api/properties/#movie-shooting-mode) | ✓ | ✓ | — | ✓ | — | — | — | — | ✓ | ✓ | ✓ | — | ✓ | — | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | — |
| [color-gamut](/alpha-sdk-api/web-api/properties/#movie-shooting-mode-color-gamut) | ✓ | ✓ | — | ✓ | — | — | — | — | ✓ | ✓ | ✓ | — | ✓ | — | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | — |
| [base-look-import](/alpha-sdk-api/web-api/properties/#base-look-import-enable) | ✓ | ✓ | — | ✓ | — | — | — | — | ✓ | — | ✓ | — | ✓ | — | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | — |
| [embed-lut](/alpha-sdk-api/web-api/properties/#embed-lut-file) | ✓ | ✓ | — | ✓ | — | — | — | — | ✓ | ✓ | ✓ | — | ✓ | — | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — |
| [lut-import](/alpha-sdk-api/web-api/advanced-topics/) | ✓ | ✓ | — | ✓ | — | — | — | — | ✓ | ✓ | ✓ | — | ✓ | — | — | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | — |
| [pp-lut-base-look](/alpha-sdk-api/web-api/properties/#pp-lut-base-look) | ✓ | ✓ | — | ✓ | — | ✓ | — | — | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | — |
| [shutter-angle](/alpha-sdk-api/web-api/properties/#shutter-angle) | — | — | — | — | — | — | — | — | — | — | — | — | — | — | — | ✓ | ✓ | ✓ | ✓ | — | — | — | — |
| [exposure-index](/alpha-sdk-api/web-api/properties/#exposure-index) | — | — | — | — | — | — | — | — | — | — | — | — | — | — | — | ✓ | ✓ | ✓ | ✓ | — | — | — | — |
| [base-iso](/alpha-sdk-api/web-api/properties/#base-iso) | — | — | — | — | — | — | — | — | — | — | — | — | — | — | — | ✓ | ✓ | ✓ | ✓ | — | — | — | — |
| [base-look-value](/alpha-sdk-api/web-api/properties/#base-look-value) | ✓ | ✓ | — | ✓ | — | — | — | — | — | ✓ | ✓ | — | — | — | — | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — |
| [shooting-enable](/alpha-sdk-api/web-api/advanced-topics/) | — | ✓ | ✓ | ✓ | — | ✓ | — | — | — | ✓ | — | ✓ | — | — | ✓ | — | — | — | — | — | — | — | — |
| [image-id-string](/alpha-sdk-api/web-api/properties/#image-id-string) | ✓ | ✓ | ✓ | ✓ | — | ✓ | — | — | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — |
| [image-id-num](/alpha-sdk-api/web-api/properties/#image-id-num) | ✓ | ✓ | ✓ | ✓ | — | ✓ | — | — | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — |
| [battery-remain](/alpha-sdk-api/web-api/properties/#battery-remain) | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — |

:::note
Universal properties (all cameras): `recording-state`, `overheating-state`, `zoom-setting`, `media-slot1/2-status`, `media-slot1/2-remaining-photos`, `media-slot1/2-remaining-time`, and all core exposure/focus/white-balance properties.
:::
