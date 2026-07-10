---
name: alpha-sdk-doc-research
description: Research Sony camera development features using the CameraRemoteSDK, CameraHelp, and CameraWebAPI MCP documentation servers. Use before implementing REST API coverage, explaining camera feature behavior, checking SDK symbols, comparing camera model support, or validating public docs for Alpha SDK contributors.
---

# Alpha SDK Doc Research

## Overview

Use this skill to gather evidence before coding or documenting a Sony camera feature.

## Source Selection

- Use `CameraRemoteSDK` for C++/C# SDK APIs, enum names, operation codes, property codes, error codes, compatibility tables, and code examples.
- Use `CameraHelp` for camera model help guides, menu terminology, shooting-mode behavior, and model-specific feature descriptions.
- Use `CameraWebAPI` and `https://crsdk.app/` for existing REST endpoint names, request and response shapes, client SDK examples, and public docs alignment.

## Research Workflow

1. Restate the feature in SDK terms and customer terms. Example: "touch tracking" might map to a camera help-guide feature but not a writable SDK property.
2. Search exact SDK names first when known. Prefer exact symbol lookup over broad semantic search for `CrDeviceProperty_*`, `CrCommandId_*`, and `CrError_*`.
3. Search code examples after finding a likely SDK symbol.
4. Search camera help docs when the implementation depends on body model, lens, shooting mode, storage media, or menu setting.
5. Search Web API docs to choose endpoint names that match the existing REST vocabulary.
6. Produce an implementation brief with SDK symbols, relevant camera models, required connection mode, REST shape, and test plan.

## Output Format

For implementation tasks, summarize:

- SDK symbols or APIs found.
- Whether the feature is a property, action, operation, event, file-transfer flow, or live-view flow.
- Camera/model limitations.
- Proposed REST endpoint and request/response shape.
- Files likely requiring edits.
- Manual/hardware test plan.

## Guardrails

- Do not treat MCP docs as Sony legal guidance. Sony SDK redistribution and license questions must use Sony's official license.
- Do not copy large passages from external docs into repo files.
- Distinguish confirmed SDK facts from implementation inferences.
- If MCP tools are unavailable, use local docs and hosted docs at `https://crsdk.app/`, then state the evidence gap.

Read `docs/MCP_SERVERS.md` or `https://crsdk.app/MCP-Server/overview` for server names and setup details.
