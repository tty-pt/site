# WASM bridge — bud-client.js, bud-hydrate.js, bud_wasm_app.c

Internal mechanics of the **bud stack's** browser-side client. This is
bud-specific; the framework-neutral contract is `docs/SSR-CONTRACT.md`.

## 1. Entry: `htdocs/bud-client.js`

- Reads `body[data-modules]` (uses the FIRST token as the module name),
  requires `#bud-root` — returns early without it.
- Fetches `/{module}.wasm`; **a 404 is caught and logged** — modules without a
  wasm (poem/grp today) degrade silently to pure SSR.
- `WebAssembly.instantiate` with `createBudWasmBridge(root).makeImports()`
  (plus a no-op `wasi_snapshot_preview1` proxy), calls
  `_initialize`/`__wasm_call_ctors`.
- If `<script type="application/json" id="bud-state">` is present, passes its
  text to the export `wasm_init(ptr, len)` (malloc'd into wasm memory).
- Calls `bridge.mount()`; sets `body[data-wasm-loaded]`;
  `window.__bud_bridge = bridge`.

## 2. Hydration: `htdocs/bud-hydrate.js`

- `buildHydrationMap(root)`: walks the SSR'd DOM, maps `data-bud-id` attributes
  and `<!--bud-text:N-->`/`<!--bud-fragment:N-->` comment markers → node ids.
  These markers are produced by `bud_render_hydrated_html` on the server.
- `autoBindListeners()`: for every element with `data-bud-on`, parses
  `parseListenerList` (`event` or `event:1` for bubbling), resolves via
  `listenerResolver`, and adds a real DOM listener (delegated to the root when
  bubbling).
- `BudWasmBridge.resolveListener`: if the wasm exports `bud_app_dispatch`,
  returns a wrapper that builds the event payload string
  (`buildEventPayload`: checked/selected/value/textContent of the target),
  mallocs it into wasm memory, and calls
  `dispatchEvent(nodeId, event, bubbles, ptr)`.
- `dispatchEvent` calls the wasm export `bud_app_dispatch`.
- Patch ops: the wasm emits DOM mutations through the import
  `bud_host_emit_patch(op, a, b, c)`; `BudPatchApplier.replay` applies them to
  the SSR'd DOM (patch-text, patch-attr, patch-innerhtml, patch-html, …).
- `mount()` only calls the optional export `bud_app_mount`. **The bridge does
  not re-render the DOM on mount** — hydration attaches to the SSR'd DOM.

## 3. Wasm side: `external/bud/src/bud_wasm_app.c`

Imports (from `env`):

- `bud_host_log(msg, len)`, `bud_host_mark_dirty()`, `bud_host_flush()`,
  `bud_host_fetch(url, url_len, request_id)`,
  `bud_host_set_location(url, url_len)`, `bud_host_emit_patch(op, …)`.
- `wasm_platform_init` (constructor) points `bud_host_fetch_fn` /
  `bud_host_log_fn` / `bud_host_set_location_fn` (bud.h) at these.

Exports:

- `bud_app_mount` — if no runtime, `bud_app_render()`, `bud_runtime_new`,
  `bud_runtime_mount`; emits MOUNT_START/MOUNT_DONE. **The runtime (and thus
  dispatch) is NULL until mount runs.**
- `bud_app_update` — frees runtime, re-renders, re-mounts, marks dirty.
- `bud_app_unmount`, `bud_app_mark_dirty`, `bud_app_flush` (patch replay via
  `bud_render_patch_ops` + `emit_patch_wrapper`).
- `bud_app_dispatch(node_id, event_name, bubbles, event_data)` →
  `find_node_by_id(runtime_root, node_id)` →
  `bud_runtime_dispatch(runtime, target, event, data)`. **Returns -1 if no
  runtime; logs "NOT FOUND" (return -2) if the node id isn't in the wasm tree.
  This is why id alignment matters** — see `docs/C-ISOMORPHIC-BUD.md` §3.
- `bud_patch_innerhtml(node_id, html)`; helpers `bud_patch_attr(node, name,
  value)` and `bud_patch_text(node, value)` (also in `bud_app.h`) emit
  patch-attr/patch-text ops.
- Debug: `wasm_dump_tree`, `wasm_get_tree_text`, `wasm_get_src(id)`
  (`bud_sprint_tree`).
- Non-`__wasm__` builds compile these as **no-op stubs** so `libbud.so` exports
  the wasm_* symbols for native use — don't call them expecting browser
  behavior on the server.

## 4. Event payload

`buildEventPayload` provides `event->user` as a string describing the target:
for checkboxes/selects the checked/selected value, for inputs the `value`, else
`textContent`. WASM handlers read it via `bud_json_str`/`bud_json_int` or
direct string comparison.

## 5. Data flow in one diagram

```
SSR HTML (data-bud-id/data-bud-on + bud-state JSON)
  → bud-client.js: fetch+instantiate, wasm_init(state), bridge.mount()
  → bud_app_mount → bud_app_render() → wasm runtime tree (ids align with DOM)
  → autoBindListeners wires data-bud-on → DOM events
  → bud_app_dispatch(id,event,bubbles,payload) → handler in wasm
  → bud_patch_text/attr/innerhtml → bud_host_emit_patch → BudPatchApplier.replay
  → SSR'd DOM updated in place
```

## 6. Patch-op pitfalls (memorize)

These cost real debugging time — read before emitting patches from wasm code:

- **`bud_host_emit_patch` reads exactly `op_len` bytes** — pass
  `strlen(op_name)`, never `sizeof`. `sizeof("patch-text")` is 11 (the NUL is
  counted) and the JS `switch (op.op)` sees `"patch-text\0"`, which matches
  nothing → **the patch is silently dropped**. This was a real shipped bug
  (`external/bud/src/bud_wasm_app.c`); the JS side cannot detect it.
- **`patch-text` updates a node in place ONLY when the id is a TEXT_NODE.**
  `getNode(id)` for an element id falls back to `createWrappedText(id, text,
  parent)` with a stale `_currentParent` (last `patch-close` of the mount
  stream, i.e. `#bud-root`) → the text is **appended at the end of
  `#bud-root`** and the real node never changes. To patch text reliably,
  capture the actual text node once (`bud_text(...)`), put it in the tree via
  `lx_node(...)`, store the TEXT node in your state, and pass it to
  `bud_patch_text`. `patch-attr` has no such trap (it sets the attribute on
  the element directly).
- **`bud_patch_attr` / `bud_patch_text` are defined in BOTH builds** — the
  real implementations on `__wasm__`, no-op stubs for native. Safe to call
  from hyle-bud / shared renderers in either build.

## 7. Related docs

- `docs/C-ISOMORPHIC-BUD.md` — how to write the renderer/handlers that ride
  this bridge.
- `docs/SSR-CONTRACT.md` — why this bridge must stay bud-specific/additive.
