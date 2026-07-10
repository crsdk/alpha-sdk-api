---
name: alpha-sdk-publish
description: Prepare the Alpha Camera REST API for public GitHub publishing. Use when checking CI workflows, release readiness, public repository safety, branch status, GitHub Actions results, or pushing publication branches.
---

# Alpha SDK Publish

## Overview

Use this workflow before pushing or publishing this repository to public GitHub. The goal is to verify the repository is safe to publish, CI is understandable, and branch state is intentional.

## Public Safety Checks

Before any public push:

1. Inspect `git status --short` and separate your changes from other contributors' work.
2. Confirm the change does not add Sony Camera Remote SDK files, SDK binaries, generated build folders, camera media, local credentials, access tokens, private certificates, or machine-local paths.
3. Confirm deprecated `api/clients/camera-remote-web-api` work is not being expanded for public package support. The supported public packages are `@alpha-sdk/api` and `@alpha-sdk/client`.
4. Review `.gitignore`, `README.md`, `CONTRIBUTING.md`, and `docs/SDK_SETUP.md` when publication safety or setup expectations change.
5. Do not rewrite history, force-push, delete branches, or remove packages unless the user explicitly asks.

## CI Workflow Checks

Check local workflow definitions before pushing:

```bash
find .github/workflows -maxdepth 1 -type f -print
sed -n '1,220p' .github/workflows/build-binaries.yml
```

If GitHub CLI is authenticated, inspect remote workflow status:

```bash
gh workflow list
gh run list --limit 10
gh run view --log-failed
```

When validating a specific branch or PR, prefer the branch-aware checks:

```bash
BRANCH="$(git branch --show-current)"
gh run list --branch "$BRANCH" --limit 10
gh pr checks
```

If a workflow run fails, inspect the failed logs before changing code:

```bash
gh run view <run-id> --log-failed
```

## Local Pre-Push Checks

Run the relevant local checks for the changed surface:

```bash
git status --short
git diff --stat
git diff --check
```

For C++ server changes, build locally without editing CMake as part of publish prep:

```bash
cd api/server
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release
```

For OpenAPI or client changes, run the repo's existing package checks if present. Use `rg --files -g package.json` to find package roots and inspect scripts before running broad package commands.

## Pushing To Public GitHub

Before pushing:

1. Confirm the remote with `git remote -v`.
2. Confirm the target branch with `git branch --show-current`.
3. Confirm the diff with `git diff --stat` and, when appropriate, `git diff`.
4. Ask the user before the first public push or any push that includes other contributors' uncommitted work.

Use a normal push for an intentional public branch:

```bash
git push origin HEAD
```

After pushing, verify Actions started or completed:

```bash
BRANCH="$(git branch --show-current)"
gh run list --branch "$BRANCH" --limit 5
```

## Reporting

Summarize:

- Local checks run and results.
- Workflow files inspected and notable risks.
- Remote, branch, and push command used or intentionally skipped.
- GitHub Actions run URLs or run ids when available.
- Any publication blockers that require user decision.
