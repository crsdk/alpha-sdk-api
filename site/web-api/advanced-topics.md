---
layout: "default"
title: "Advanced Topics"
description: "LUT import, CineEI workflows, and guardrail commands for advanced camera setups"
parent: "REST API"
nav_order: 9
---

## LUTs in CineEI vs Picture Profiles

LUTs serve different purposes depending on your shooting mode.

### CineEI / Log Shooting (Select LUT)

In CineEI, CineEI Quick, or Flexible ISO mode, the camera always **records S-Log3**. The selected LUT (`base-look-value`) controls monitoring output only.

`embed-lut-file` writes the LUT as metadata into the recorded file. It does not bake the LUT into the image.

**Properties used:**

| Property | Purpose |
| -------- | ------- |
| `base-look-value` | Select active monitoring LUT |
| `embed-lut-file` | Embed LUT metadata in the recording |

### Picture Profile with PPLUT (Non-Log)

Without CineEI or Log Shooting, LUTs are used through **PPLUT slots** in Picture Profiles. Standard PP1-PP11 presets do not use LUTs. To apply an actual `.cube` LUT in non-log mode, use **PPLUT1-PPLUT4** (Custom slots `0x41`-`0x44`).

**Properties used:**

| Property | Purpose |
| -------- | ------- |
| `picture-profile` | Select PP1-PP11 or Custom 1-4 |
| `pp-lut-base-look` | Select which PPLUT slot to use as base look |

**Key difference:**

| | CineEI (Select LUT) | PPLUT (Picture Profile) |
| --- | --- | --- |
| Recording | Always S-Log3 | LUT baked into recording |
| LUT purpose | Monitoring only | Applied to recorded video |
| Post flexibility | Full | Limited |


## Import LUT file

`POST /api/cameras/{cameraId}/lut/import` uploads a `.cube` LUT file to the camera. The import is asynchronous — the endpoint returns `202 Accepted` and the result is delivered through the [`lutImportResult`]({{ site.baseurl }}/web-api/events#lutimportresult) SSE event.

Body: `filePath` (absolute path on the server filesystem), `slot` (1-16).

**curl**


```bash
curl -X POST http://localhost:8080/api/cameras/D10F60149B0C/lut/import \
  -H "Content-Type: application/json" \
  -d '{"filePath": "/path/to/MyLUT.cube", "slot": 1}'
```


### SSE result

```json Success
{
  "slot": 1,
  "status": "success"
}
```

```json Error
{
  "slot": 1,
  "status": "error",
  "error": "invalid_file"
}
```

**Error codes:**

| Code | Description |
| ---- | ----------- |
| `general_failure` | Unspecified import failure |
| `invalid_filename` | File name not accepted by camera |
| `device_busy` | Camera is busy |
| `storage_full` | Camera storage is full |
| `invalid_parameter` | Invalid slot number or parameter |
| `invalid_file` | File is not a valid `.cube` LUT |
| `invalid` | Generic validation failure |

## CineEI overview


CineEI is Sony's cinema-style log shooting system. The camera records **S-Log3** footage at a fixed Base ISO, and **Exposure Index (EI)** shifts the monitoring LUT without changing the recorded signal.

| Mode | Hex | ISO Control | Use Case |
| ---- | --- | ----------- | -------- |
| Off | `0x1` | Normal ISO | Standard video |
| CineEI | `0x0301` | Exposure Index | Locked base ISO workflow |
| CineEI Quick | `0x0302` | Exposure Index | Simplified CineEI |
| Flexible ISO | `0x0501` | Normal ISO range | Log recording with variable ISO |

### Switching modes

```bash
# Enable CineEI
curl -X PUT .../properties/movie-shooting-mode \
  -d '{"value": "0x0301"}'

# Enable CineEI Quick
curl -X PUT .../properties/movie-shooting-mode \
  -d '{"value": "0x0302"}'

# Enable Flexible ISO
curl -X PUT .../properties/movie-shooting-mode \
  -d '{"value": "0x0501"}'
```

{: .note }
> The camera must be in a movie exposure mode before switching `movie-shooting-mode`. Set `exposure-program-mode` first.


### Exposure Index vs ISO

| | Normal ISO | Exposure Index (EI) |
| --- | --- | --- |
| What it controls | Sensor gain | Monitoring LUT brightness only |
| Affects recording | Yes | No |
| Noise impact | Direct | None |
| Purpose | Expose the image | Preview post exposure |

### Full CineEI workflow

**1. Switch to movie mode**

Set `exposure-program-mode` to a movie mode such as Movie Manual.

**2. Enable CineEI**

Set `movie-shooting-mode` to `0x0301`.

**3. Set color gamut**

Set `movie-shooting-mode-color-gamut` to `0x1` (S-Gamut3.Cine).

**4. Choose Base ISO**

Set `base-iso` to High or Low.

**5. Set Exposure Index**

Set `exposure-index` to the desired EI value.

**6. Select monitoring LUT**

Set [`base-look-value`]({{ site.baseurl }}/web-api/properties#base-look-value) to the desired LUT.

**7. Enable LUT embedding (optional)**

Set `embed-lut-file` to `0x2`.


## CineEI LUT workflow

Use LUT import together with CineEI when you want monitoring looks without baking them into the recording.

**1. Enable CineEI**

Set `movie-shooting-mode` to `0x0301`.

**2. Import LUT**

Call `POST /api/cameras/{id}/lut/import` with the `.cube` file path and target slot.

**3. Select the monitoring LUT**

Set [`base-look-value`]({{ site.baseurl }}/web-api/properties#base-look-value) to the imported LUT slot.

**4. Enable LUT embedding (optional)**

Set `embed-lut-file` to `0x2` to include LUT metadata in the recorded file.


## PPLUT workflow

For non-log shooting, apply an imported LUT through a Custom Picture Profile. In this path the LUT is baked into the recording.

**1. Import the LUT**

Call `POST /api/cameras/{id}/lut/import` with a `.cube` file and slot number.

**2. Set a Custom Picture Profile**

Set [`picture-profile`]({{ site.baseurl }}/web-api/properties#picture-profile) to `0x41`-`0x44` (Custom 1-4).

**3. Select the PPLUT slot**

Set [`pp-lut-base-look`]({{ site.baseurl }}/web-api/properties#pp-lut-base-look) to the desired slot.


## Guardrail commands

Guardrail commands prevent unwanted camera operations in tethered and mixed-use environments.

### Shutter lock (`shooting-enable`)

The `shooting-enable` property disables or enables the shutter via the SDK.

{: .warning }
> This property requires a **Volume Photography Commands** license from Sony. Without the license, the property will not appear.


### Read the current state

```bash
curl http://localhost:8080/api/cameras/{id}/properties/shooting-enable
```

### Disable the shutter

```bash
curl -X PUT .../properties/shooting-enable \
  -H "Content-Type: application/json" \
  -d '{"value": "0x2"}'
```

### Re-enable the shutter

```bash
curl -X PUT .../properties/shooting-enable \
  -H "Content-Type: application/json" \
  -d '{"value": "0x1"}'
```

## Guardrail workflows

### ISO limit enforcement

Lock the shutter if ISO exceeds a studio threshold, then unlock it once the operator brings ISO back into range.

### Scan and Tag subject tracking

In volume photography workflows, write [`image-id-string`]({{ site.baseurl }}/web-api/properties#image-id-string) for the current subject and lock the shutter when no valid subject ID is present or the shot limit is reached.

## Compatibility

`shooting-enable` requires:

1. A camera that supports `CrDeviceProperty_ShootingEnableSettingLicense`
2. An active **Volume Photography Commands** license installed on the camera

Without the license, the property returns empty or not writable.


{: .note }
> For typed workflows around LUTs, live view, and guardrail-related automation, use the SDK pages and examples section.
