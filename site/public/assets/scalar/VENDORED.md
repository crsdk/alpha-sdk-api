# Vendored Scalar bundle

`standalone.js` is a **committed copy** of Scalar's prebuilt browser bundle. It
is deliberately not loaded from a CDN.

| | |
|---|---|
| Package | `@scalar/api-reference` |
| Version | **1.64.0** |
| Source path in package | `dist/browser/standalone.js` |
| Size | 3,748,834 bytes (~1.05 MB gzipped over the wire) |
| SHA-256 | `25e0a1ef537dc7f1aa41dd8d22b94d8703dcfab34361f4d2ee84ac0600c8a457` |
| Vendored on | 2026-08-06 |

## Why vendored

Every Scalar quickstart hands you
`<script src="https://cdn.jsdelivr.net/npm/@scalar/api-reference"></script>`.
That makes a third-party CDN a runtime dependency of a Sony-branded API
reference, with nobody watching if it breaks, changes behaviour, or starts
serving a version whose config keys have moved.

This mirrors what this repo already does deliberately elsewhere: jsoncpp is
vendored and OpenSSL was removed so end users need zero runtime dependencies.
The same reasoning applies here and more strongly, because this site is expected
to outlive anyone actively maintaining it.

`standalone.js` is fully self-contained — no `./chunks/` imports, no dynamic
`import()` calls — so this one file is the whole dependency.

## Two defaults that are overridden at the call site

Both live in `site/public/api-reference/index.html`. Re-check them after any upgrade,
because they are Scalar defaults, not opt-ins:

- **`proxyUrl: ''`** — Scalar otherwise proxies console requests through
  `https://proxy.scalar.com`. For this API that is both broken (the server is on
  the *reader's* `localhost`, which Scalar's proxy cannot reach) and a privacy
  regression (it would forward the reader's credentials to a third party).
- **`withDefaultFonts: false`** — otherwise the theme fetches webfonts from
  `https://fonts.scalar.com`, reintroducing exactly the outside runtime
  dependency vendoring was meant to remove.

## Updating

There is no automation for this and that is intentional — an unattended
auto-update of a 3.7 MB third-party bundle is a worse failure mode than a stale
one.

```bash
npm pack @scalar/api-reference
tar xzf scalar-api-reference-*.tgz
cp package/dist/browser/standalone.js site/public/assets/scalar/standalone.js
shasum -a 256 site/public/assets/scalar/standalone.js   # update the table above
```

Then verify, in a browser, before merging:

1. The reference renders and lists every operation.
2. DevTools → Network shows **no** requests to `scalar.com` or any CDN.
3. A "Send" against a running local server reaches `http://localhost:8080`
   directly.
