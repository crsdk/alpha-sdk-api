# SDK Generation & Publishing

How the `@alpha-sdk/client` (npm) and `alpha-sdk-client` (PyPI) clients are
produced and released, and what a maintainer needs to keep it running.

## Model

```
api/openapi.yaml  ──fern generate──►  generated/typescript  ──OIDC──►  npm @alpha-sdk/client
   (source of truth)                  generated/python      ──OIDC──►  PyPI alpha-sdk-client
```

- **Generation** is credential-free: `fern generate` runs the Fern generator
  Docker images on the GitHub runner. No Fern account, no `FERN_TOKEN`.
  Config: [`../fern/generators.yml`](../fern/generators.yml).
- **Publishing** uses OIDC Trusted Publishing on both registries — no long-lived
  npm/PyPI tokens are stored anywhere.
- **Contributors** only ever edit `api/openapi.yaml`. Clients are never hand-edited.

## Workflows

| Workflow | Trigger | Secrets | What it does |
|---|---|---|---|
| `sdk-preview.yml` | PR touching `api/openapi.yaml` or `fern/**` | none | `fern check` + generate; uploads SDKs as an artifact for review |
| `sdk-publish.yml` | maintainer pushes a `sdk-v*.*.*` tag | none (OIDC) | generate + publish to npm & PyPI |

## One-time owner setup (Trusted Publishers)

Registry-side, done once in each console by a package owner. Until done, the
matching publish job fails/skips by design.

1. **npm** (`@alpha-sdk/client`): package Settings → Trusted Publisher → GitHub
   Actions → repo `crsdk/alpha-sdk-api`, workflow `sdk-publish.yml`.  **[done]**
2. **PyPI** (`alpha-sdk-client`): project → Manage → Publishing → add a Trusted
   Publisher → GitHub → same repo + workflow.

### Staged rollout

npm and PyPI activate independently. The PyPI publish job is gated on a repo
variable — set **`PUBLISH_PYPI=true`** (repo → Settings → Secrets and variables
→ Actions → Variables) only after the `alpha-sdk-client` Trusted Publisher is
live. Until then, tagging a release publishes npm cleanly and the PyPI job is
skipped, not failed.

### Ownership / bus-factor

`@alpha-sdk/api` on npm is the **hand-built binary launcher** (published by the
private binary-build CI, not Fern) — it needs no Trusted Publisher; leave it alone.

For durability, move publishing authority off any single personal account:
- npm: publish `@alpha-sdk/client` under the `alpha-sdk` org / add a second owner.
- PyPI: the `alpha-sdk-client` project predates the `alpha-sdk` (CRSDK) org, so
  transfer it in once the org is approved (project → Manage → Settings →
  Organization). The Trusted Publisher config carries over unchanged.

## Release runbook (maintainer)

```bash
# 1. Land the spec change (PR reviewed against the SDK Preview artifact).
# 2. Tag a release — the only gate on publishing. Must be > the published version.
git tag sdk-v0.4.0
git push origin sdk-v0.4.0
# 3. sdk-publish.yml generates + publishes via OIDC (npm now; PyPI once enabled).
```

Bump generator versions in `fern/generators.yml` deliberately — a generator bump
can change the emitted API surface. Always review a `sdk-preview` artifact first.

## Publishing generated source to the SDK repos (optional)

The generated code can also be mirrored into the public SDK repos
(`crsdk/alpha-sdk-typescript`, `crsdk/alpha-sdk-python`) so it's browsable on
GitHub and installable via git. Pushing to another repo is the one step that
needs a credential (OIDC can't do cross-repo pushes) — use an **org-owned GitHub
App installation token or a fine-grained PAT**, never a personal classic PAT.
This is intentionally left out of `sdk-publish.yml` until the SDK repos are
restructured (they currently hold example apps); wire it as a follow-up job or
have those repos pull the spec and self-generate.

## Packaging manifests

Fern's `local-file-system` output emits **source only** — no `package.json`,
`tsconfig`, or `pyproject.toml` (the same reason the original SDK repos kept
these hand-authored and `.fernignore`-protected). They are vendored here and
copied onto the generated source in CI before build/publish:

- `fern/packaging/typescript/` — `package.json` (name `@alpha-sdk/client`) + tsconfigs
- `fern/packaging/python/` — `pyproject.toml` (name `alpha-sdk-client`)

The version field is a `0.0.0` placeholder; CI stamps it from the release tag.
The Python generator output is nested under `generated/python/src/alpha_sdk_client`
so the vendored `pyproject.toml` builds a correctly-named wheel.

## Reproduce locally

```bash
npx fern-api@5.91.0 check
npx fern-api@5.91.0 generate --group ts-sdk --local       # needs Docker
npx fern-api@5.91.0 generate --group python-sdk --local
# assemble + build the TS package
cp fern/packaging/typescript/* LICENSE generated/typescript/
( cd generated/typescript && npm install && npm run build )
# assemble + build the Python package
cp fern/packaging/python/pyproject.toml LICENSE generated/python/
cp generated/python/src/alpha_sdk_client/README.md generated/python/README.md
( cd generated/python && python -m build )
```
