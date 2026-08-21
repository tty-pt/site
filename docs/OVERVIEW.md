# Overview — what this project is and how it fits together

Read this first. It is the 5-minute orientation; every claim here is expanded
in a `docs/` file linked below. The golden rule for every other doc:
**the SSR markup + the hyle query API are the only cross-framework interface.**

## The site

A pure-C music site for poems, songs, gigs, and grps. Server-rendered
HTML (SSR-first) with progressive, optional WASM enhancement in the browser.
**No Rust, Dioxus, or Deno in the request path** — the only WASM is C compiled
to wasm32-wasi for browser-side polish.

## The stack (libraries, not a framework)

| Piece | What it does |
|-------|--------------|
| `external/axil` | HTTP, sessions, auth, uploads, chroot |
| `external/libxylem` | XY cross-.so dispatch (`dlopen` before chroot) |
| `external/libqmap` | opaque data store (never free values) |
| `external/stoma` | tokenization / search fold (accent-sensitive) |
| `external/hyle` | data layer: schema, records, query, filtering, FTS (submodule) |
| `external/hyle/c/libhyle-bud` | the bud component binding — the ONLY code that may depend on bud |
| `external/bud` | C DOM scaffold (SSR) + WASM bridge (`bud-client.js`, `bud-hydrate.js`) |
| `mods/*` | site modules — thin composition layers wiring the libraries together |

## Framework-pair model (the governing design rule)

Each component framework comes in a **pair**: one SSR implementation and its
own client runtime (bud↔bud-WASM, React↔React-hydration). The two sides of a
pair agree on the **SSR contract** (plain HTML + `data-*` hooks). hyle stays
framework-neutral and carries metadata (including UI hints) **opaquely**;
the component layer interprets it. See `docs/ARCHITECTURE.md`.

## Request path (what happens when a URL loads)

1. `axil` on :8080 receives the request; all handlers are C in `mods/*/*.so`.
2. Modules were dlopen'd at boot (before the chroot) in the order enforced by
   `mods/core/core.c` `xy_install`.
3. A handler collects data via hyle `source_query`, renders HTML with bud
   (or calls a shared C renderer), and responds through the common page
   helpers (`site_ui_respond_page`, …).
4. If the page opts in (`data-modules="list"` etc.), the browser fetches
   `/{module}.wasm`, hydrates via the bud bridge, and enhances. No-JS pages
   work identically minus the polish.

## The unbreakable rules

- **No-JS must always work.** Client enhancement is optional and additive.
- **hyle stays neutral.** No bud/component symbols in `external/hyle/src` or
  `include/hyle`; only `external/hyle/c/libhyle-bud` may depend on bud.
- **All row writes go through hyle `put`/`del`** (`source_update_item` /
  `source_delete_item`) — writing qmaps directly freezes the FTS index.
- **Search is accent-sensitive by design** (`pão` ≠ `pao`); no iconv
  TRANSLIT in the search fold. TRANSLIT survives only in `axil_slugify`.
- **SSR markup is the contract.** `data-bud-*`/patch ops are bud-stack-internal
  and additive; a React consumer implements the same contract independently.

## Repo layout

```
AGENTS.md          doc index — route from here (read this first)
 docs/              the docs (OVERVIEW, GOALS, VIOLATIONS, ARCHITECTURE,
                    DESIGN, BUILD, TESTING, CONVENTIONS, STYLING,
                    SSR-CONTRACT, C-ISOMORPHIC-BUD, WASM-BRIDGE, FILTERS,
                    SCHEMA, AUDIT)
external/          axil, libxylem, libqmap, stoma, hyle (submodule), bud
mods/              core, common, source, index, auth, mpfd, song, poem,
                   gig, grp, bud_demo
htdocs/            static web root: styles.css, hyle.css, *.wasm, bud-client.js
var/<source>       data directories (server chroots at the repo root)
tests/             unit (per-module test.sh), pages, e2e (Deno+Playwright)
debug/             captured build/runtime/test logs (gitignored)
```

## Bootstrap

```bash
make
make watch          # auto-rebuild + restart on :8080
```

Then open `http://localhost:8080`. The server runs inside a chroot rooted at
the repo root (prereqs in `docs/BUILD.md`).

## Where to go next

| You want to… | Read |
|--------------|------|
| Understand what the architecture wants to be | `docs/GOALS.md` |
| Check all known departures from those goals | `docs/VIOLATIONS.md` |
| Understand the architecture / load order / XY contract | `docs/ARCHITECTURE.md` |
| Understand the design philosophy before designing anything | `docs/DESIGN.md` |
| Build, rebuild, or hit a build gotcha | `docs/BUILD.md` |
| Run or write tests | `docs/TESTING.md` |
| Write idiomatic C / handlers / follow conventions | `docs/CONVENTIONS.md` |
| Change styling or CSS assets | `docs/STYLING.md` |
| Write SSR markup for a widget | `docs/SSR-CONTRACT.md` |
| Write a dual-compiled SSR+wasm renderer | `docs/C-ISOMORPHIC-BUD.md` |
| Touch the WASM bridge / patch ops | `docs/WASM-BRIDGE.md` |
| Touch filtering / multi-ref semantics | `docs/FILTERS.md` |
| Touch schema strings / UI hints | `docs/SCHEMA.md` |
| Deep audit: full issue catalog, proposed fixes, and phased roadmap | `docs/AUDIT.md` |
