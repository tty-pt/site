# bud

`bud` is a small C DOM/runtime scaffold used by the site's **bud stack**: it
renders HTML on the server (SSR) and, via a WASM bridge, hydrates and enhances
the SSR'd DOM in the browser from the **same C source** (C-isomorphic, see
`docs/C-ISOMORPHIC-BUD.md`).

It provides:

- a tree model for fragments, elements, and text nodes
- attribute storage and child linking
- child/attr/listener accessors for host-side hydration
- a structured tree walk API for host-side inspection without HTML parsing
- a walk op stream for browser/WASM bridges
- HTML SSR serialization (`bud_render_html`)
- hydrated HTML markers for DOM reattachment (`bud_render_hydrated_html`)
- a hydration callback stream with stable node IDs
- a mutation patch stream for browser-side DOM apply (`bud_render_patch_ops`)
- a runtime wrapper with dirty/flush state management
- host-driven event dispatch with bubbling and stop-propagation
- mount, update, and unmount lifecycle hooks
- a raw WASM host bridge shape for patch emission and event forwarding
- JSON field extraction helpers (no json-c dependency) and a table-driven 5-field UI state
  binder (`bud_state_apply_stride_len` / `bud_field_desc_t`: `key`, `offset`, `size`, `is_int`, `kind`)
  for WASM `wasm_init` and UI hydration with zero database or storage dependencies

## Framework-pair model

bud is ONE component framework. It is used as a pair — SSR (native, via
`external/hyle/c/libhyle-bud`) plus its own client runtime (the WASM bridge
driven by `htdocs/bud-client.js` / `bud-hydrate.js`). It is NOT the platform's
client layer: hyle is framework-neutral, and other frameworks (e.g. React) can
implement the same SSR contract with their own client runtime. See
`docs/ARCHITECTURE.md` and `docs/SSR-CONTRACT.md`.

## Bridge documentation

- `docs/C-ISOMORPHIC-BUD.md` — write one renderer, run it SSR and in the browser.
- `docs/WASM-BRIDGE.md` — `bud-client.js` / `bud-hydrate.js` / `bud_wasm_app.c`.
