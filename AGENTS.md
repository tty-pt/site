# AGENTS.md — documentation index

Pure-C music site (poem, song, songbook, choir) on the axil HTTP library, the
libxylem XY module system, and the bud HTML builder. SSR-first with
progressive WASM enhancement; no Rust/Dioxus/Deno in the request path.

**Read the relevant doc below before touching code.** Start with
`docs/OVERVIEW.md` — read it first, always. Each doc lists its own related
docs. Bootstrap:

```bash
make
make watch          # auto-rebuild + restart on :8080
```

## Topic index

| Topic | Read |
|-------|------|
| 5-minute orientation: stack, framework-pair model, request path, unbreakable rules, repo layout — READ FIRST | `docs/OVERVIEW.md` |
| Architecture, framework-pair model, module load order, XY contract, data-layer invariants | `docs/ARCHITECTURE.md` |
| Design philosophy: encapsulation, "evoke, don't reimplement", abstraction limits | `docs/DESIGN.md` |
| Build/rebuild: make, wasm rebuild trap, stale `/usr/include` headers, cache bust, chroot, server start | `docs/BUILD.md` |
| Testing: commands, e2e prereqs, known pre-existing failures, debug logs | `docs/TESTING.md` |
| C style, handler patterns, form parsing, memory, XY gotchas | `docs/CONVENTIONS.md` |
| Styling: CSS source of truth (hyle submodule), tokens, cache bust, specificity traps | `docs/STYLING.md` |
| SSR contract (plain HTML + `data-*` hooks), incl. the multi-ref dropdown markup | `docs/SSR-CONTRACT.md` |
| Writing one C renderer for SSR + wasm (dual-compile, id alignment, build wiring) | `docs/C-ISOMORPHIC-BUD.md` |
| WASM bridge internals: bud-client.js, bud-hydrate.js, patch ops, patch-target pitfalls | `docs/WASM-BRIDGE.md` |
| Filter semantics: multi-ref storage, query parsing, union/intersect, repeated-key wire format | `docs/FILTERS.md` |
| Schema metadata strings and the opaque UI-hint mechanism (`"f":"dropdown"`) | `docs/SCHEMA.md` |

## Rules that must never be eroded

- hyle stays framework-neutral: no bud/component symbols in `external/hyle/src`
  or `include/hyle`; only `external/hyle/c/libhyle-bud` may depend on bud.
- Frameworks are pairs (bud↔WASM, React↔React); SSR markup + the hyle query
  API are the ONLY cross-framework interface.
- No-JS must always work; client-side enhancement (bud's WASM bridge) is
  optional and additive.
- Route all row writes through hyle `put`/`del` so the FTS index stays live.
- Search is accent-sensitive by design (`pão` ≠ `pao`); no iconv TRANSLIT in
  the fold.
- Stale Rust-SSR claims (`mods/ssr`, wasm-bindgen, `song-client.js`) in old
  docs are removed code — trust `make` + `mods/` + `htdocs/`.
