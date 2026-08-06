# Docs site

Astro Starlight. `npm run dev` to preview, `npm run build` to produce `dist/`.
Deployed by `.github/workflows/pages.yml`.

Note this file lives here rather than in `src/content/docs/`: Astro's content
loader picks up every Markdown file in that directory and requires `title`
frontmatter, so a README there fails the build.

## The pages are the source of truth

The pages in this directory were originally produced by a one-time conversion
script that read the Mintlify MDX in the old `docs` repository. **That script
has been retired.** Editing it and re-running it would overwrite everything
here, including changes that were never in the Mintlify source:

- The typed client SDK pages were removed. There are no published
  `@alpha-sdk/*` packages on npm or PyPI, and no `alpha-sdk-client` on PyPI.
  Clients are generated from `api/openapi.yaml` instead — see `sdk/overview.mdx`.
- The `CameraWebAPI` MCP server was removed. It was served by Mintlify itself
  (`/_mintlify/mcp/...` at `crsdk.app/mcp`) and stops existing when that account
  is closed. The other two MCP servers are hosted separately and are unaffected.
- The server is no longer distributed via npm, so `web-api/server.mdx` and
  `cli/camera-server.md` describe the `CameraWebApp` binary and its real flags
  (`--port` / `-p` only) rather than a `camera-server` CLI wrapper.
- Links point at the `crsdk` organisation, not the personal account the repo
  used to live under.

Edit these files directly. The old `docs` repository is an archive.

The changelog is deliberately left as a historical record: those packages did
ship on those dates, and rewriting the entries would make the record false.
