---
layout: "default"
title: "Compatibility"
description: "OS compatibility, camera connection modes, and per-camera API support"
parent: "REST API"
nav_order: 3
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

{: .info }
> macOS universal binary is available from server binary v1.08.00, supporting both Intel and Apple Silicon.


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

{: .info }
> Connection mode details: **R** (Remote) = full camera control with auto-transfer, **C** (Contents) = SD card browsing only, **T** (Remote Transfer) = full control + explicit SD card download. See [Connection Modes]({{ site.baseurl }}/sdk/typescript#connection-modes).


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
| [Live View]({{ site.baseurl }}/web-api/live-view) | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| [OSD]({{ site.baseurl }}/web-api/live-view#osd) | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| [SD Card (C)]({{ site.baseurl }}/web-api/sd-card) | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | — | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| [SD Card (T)]({{ site.baseurl }}/web-api/sd-card#download-file-remote-transfer) | ✓ | ✓ | ✓ | ✓ | — | ✓ | — | — | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | — |
| [Transfer Callback]({{ site.baseurl }}/web-api/events#transferprogress) | ✓ | ✓ | ✓ | ✓ | — | ✓ | — | — | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | — |
| [Settings Download]({{ site.baseurl }}/web-api/settings#download-settings-to-pc) | ✓ | ✓ | ✓ | ✓ | — | ✓ | — | — | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — |
| [Settings Upload]({{ site.baseurl }}/web-api/settings#upload-settings-to-camera) | ✓ | ✓ | ✓ | ✓ | — | ✓ | — | — | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — |

{: .info }
> **SD Card (C)** requires **Contents** mode. **SD Card (T)** and **Transfer Callback** require **Remote Transfer** mode. See the [Camera Connection Compatibility](#camera-connection-compatibility) table above for which modes each camera supports.


---

## Property Compatibility

Most properties (ISO, aperture, shutter speed, white balance, drive mode, focus, exposure program mode, metering mode, etc.) are supported on all cameras. The table below lists only **properties that vary across models**.

### Legend

| Symbol | Meaning |
|--------|---------|
| ✓ | Supported |
| — | Not supported |

### Per-Camera Property Support

{: .note }
> Camera names abbreviated. Properties not listed are universal (all cameras). Data sourced from SDK V2.01.00.


| Property | LR1 | A1II | A1 | A9III | A9II | A7RV | A7RIVa | A7RIV | A7CR | A7SIII | A7V | A7IV | A7CII | A7C | A6700 | FX3A | FX3 | FX2 | FX30 | ZV-E1 | ZV-E10II | RX1RIII | RX0II |
|----------|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|
| [creative-look]({{ site.baseurl }}/web-api/properties#creative-look) | ✓ | ✓ | ✓ | ✓ | — | ✓ | — | — | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — |
| [picture-profile]({{ site.baseurl }}/web-api/properties#picture-profile) | ✓ | ✓ | ✓ | ✓ | — | ✓ | — | — | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — |
| [silent-mode]({{ site.baseurl }}/web-api/properties#silent-mode) | ✓ | ✓ | ✓ | ✓ | — | ✓ | — | — | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | — | — |
| [shutter-type]({{ site.baseurl }}/web-api/properties#shutter-type) | ✓ | ✓ | ✓ | — | — | ✓ | — | — | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | ✓ | ✓ | — | — | — | ✓ | — |
| [shutter-mode]({{ site.baseurl }}/web-api/properties#shutter-mode) | — | — | — | — | — | — | — | — | — | — | — | — | — | — | — | ✓ | ✓ | ✓ | ✓ | — | — | — | — |
| [flicker-less-shooting]({{ site.baseurl }}/web-api/properties#flicker-less-shooting) | — | ✓ | ✓ | ✓ | — | — | — | — | ✓ | — | ✓ | — | ✓ | — | — | — | — | ✓ | — | — | — | — | — |
| [flash-mode]({{ site.baseurl }}/web-api/properties#flash-mode) | — | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | ✓ | — |
| [audio-recording]({{ site.baseurl }}/web-api/properties#audio-recording) | — | ✓ | ✓ | ✓ | — | ✓ | — | — | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — |
| [timecode-*]({{ site.baseurl }}/web-api/properties#timecode-preset) | ✓ | ✓ | ✓ | ✓ | — | ✓ | — | — | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — |
| [image-stabilization]({{ site.baseurl }}/web-api/properties#image-stabilization) | ✓ | ✓ | ✓ | ✓ | — | ✓ | — | — | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — | — | — |
| [movie-stabilization]({{ site.baseurl }}/web-api/properties#movie-stabilization) | ✓ | ✓ | ✓ | ✓ | — | ✓ | — | — | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — |
| [long-exposure-nr]({{ site.baseurl }}/web-api/properties#long-exposure-nr) | ✓ | ✓ | ✓ | ✓ | — | ✓ | — | — | ✓ | ✓ | ✓ | ✓ | ✓ | — | — | ✓ | ✓ | ✓ | — | — | — | ✓ | — |
| [aps-c-s35]({{ site.baseurl }}/web-api/properties#aps-c-s35) | ✓ | ✓ | — | ✓ | — | ✓ | — | — | — | ✓ | ✓ | ✓ | — | — | — | ✓ | ✓ | ✓ | — | ✓ | — | — | — |
| [movie-shooting-mode]({{ site.baseurl }}/web-api/properties#movie-shooting-mode) | ✓ | ✓ | — | ✓ | — | — | — | — | ✓ | ✓ | ✓ | — | ✓ | — | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | — |
| [color-gamut]({{ site.baseurl }}/web-api/properties#movie-shooting-mode-color-gamut) | ✓ | ✓ | — | ✓ | — | — | — | — | ✓ | ✓ | ✓ | — | ✓ | — | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | — |
| [base-look-import]({{ site.baseurl }}/web-api/properties#base-look-import-enable) | ✓ | ✓ | — | ✓ | — | — | — | — | ✓ | — | ✓ | — | ✓ | — | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | — |
| [embed-lut]({{ site.baseurl }}/web-api/properties#embed-lut-file) | ✓ | ✓ | — | ✓ | — | — | — | — | ✓ | ✓ | ✓ | — | ✓ | — | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — |
| [lut-import]({{ site.baseurl }}/web-api/advanced-topics) | ✓ | ✓ | — | ✓ | — | — | — | — | ✓ | ✓ | ✓ | — | ✓ | — | — | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | — |
| [pp-lut-base-look]({{ site.baseurl }}/web-api/properties#pp-lut-base-look) | ✓ | ✓ | — | ✓ | — | ✓ | — | — | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | — |
| [shutter-angle]({{ site.baseurl }}/web-api/properties#shutter-angle) | — | — | — | — | — | — | — | — | — | — | — | — | — | — | — | ✓ | ✓ | ✓ | ✓ | — | — | — | — |
| [exposure-index]({{ site.baseurl }}/web-api/properties#exposure-index) | — | — | — | — | — | — | — | — | — | — | — | — | — | — | — | ✓ | ✓ | ✓ | ✓ | — | — | — | — |
| [base-iso]({{ site.baseurl }}/web-api/properties#base-iso) | — | — | — | — | — | — | — | — | — | — | — | — | — | — | — | ✓ | ✓ | ✓ | ✓ | — | — | — | — |
| [base-look-value]({{ site.baseurl }}/web-api/properties#base-look-value) | ✓ | ✓ | — | ✓ | — | — | — | — | — | ✓ | ✓ | — | — | — | — | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — |
| [shooting-enable]({{ site.baseurl }}/web-api/advanced-topics) | — | ✓ | ✓ | ✓ | — | ✓ | — | — | — | ✓ | — | ✓ | — | — | ✓ | — | — | — | — | — | — | — | — |
| [image-id-string]({{ site.baseurl }}/web-api/properties#image-id-string) | ✓ | ✓ | ✓ | ✓ | — | ✓ | — | — | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — |
| [image-id-num]({{ site.baseurl }}/web-api/properties#image-id-num) | ✓ | ✓ | ✓ | ✓ | — | ✓ | — | — | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — |
| [battery-remain]({{ site.baseurl }}/web-api/properties#battery-remain) | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — |

{: .info }
> Universal properties (all cameras): `recording-state`, `overheating-state`, `zoom-setting`, `media-slot1/2-status`, `media-slot1/2-remaining-photos`, `media-slot1/2-remaining-time`, and all core exposure/focus/white-balance properties.
