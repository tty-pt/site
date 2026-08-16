# SSR contract — the framework-neutral component contract

This document defines what a server-rendered component MUST emit so that any
client framework (bud, React, dioxus, vanilla JS) can enhance it — and exactly
what is **bud-specific and additive** vs **framework-neutral and required**.

## 1. The principle

> SSR emits plain, no-JS-usable HTML: native form controls + CSS classes +
> `data-*` hooks. The interactive layer (vanilla JS, React, or bud's WASM
> bridge) is optional and must never be required. No widget may depend on bud's
> patch/op stream (`BudWasmBridge`) to function.

Consequences:

- The SSR output must be **ordinary HTML with no hidden runtime dependency**
  (hyle may move to WASM-only later; its SSR output must keep working).
- A React/Dioxus consumer reimplements components against the **same DOM
  contract** — same classes, same `data-*` hooks, same native controls — and
  never touches bud's machinery.
- CSS classes and `data-*` hooks are the stable public API of a widget. The
  `data-bud-*` attributes are bud-internal and MUST NOT be part of the contract.

## 2. What the contract requires of a widget

1. **Native HTML controls for every interactive input.** Checkboxes, selects,
   inputs, buttons, `<details>`/`<summary>` — whatever a no-JS user needs to
   operate. No custom `<div>` acting like a checkbox.
2. **The control state is the source of truth.** SSR must render `checked`,
   `selected`, `value`, `aria-*` correctly from server state. Client code reads
   and writes the native controls; it does not maintain a parallel state that
   the form ignores.
3. **Form submission works with zero JS.** The widget must sit in a real
   `<form>`; native controls submit the exact values the server understands
   (see `docs/FILTERS.md` for the multi-ref repeated-keys wire format).
4. **`data-*` hooks** for everything the client needs to find/enhance:
   `data-hyle-ms`-style namespace attributes. They identify the widget, its
   parts, and its current state in a framework-agnostic way.
5. **CSS classes** carry all styling; no inline layout scripting.

## 3. Example contract (multi-ref dropdown — `data-hyle-ms`)

```html
<details class="hyle-multiselect" data-hyle-ms="type" data-hyle-ms-label="Type">
  <summary class="hyle-ms-trigger">
    <span class="hyle-ms-values" data-hyle-ms-values>Comunhão; Natal</span>
    <span class="hyle-ms-caret" aria-hidden="true">▾</span>
  </summary>
  <div class="hyle-ms-panel" data-hyle-ms-panel>
    <input type="search" class="hyle-ms-search" data-hyle-ms-search
           placeholder="Search…" aria-label="Search options">
    <div class="hyle-ms-options" data-hyle-ms-options>
      <label class="hyle-ms-option">
        <input type="checkbox" name="type" value="comunhao" checked> Comunhão
      </label>
      <!-- one per option; checked per current selection -->
    </div>
  </div>
</details>
```

- No-JS: `<details>` opens natively; checkboxes submit repeated keys; the
  summary shows pre-rendered labels.
- Enhanced (any client): live option search, click-outside/Esc close, summary
  label sync — implemented against `data-hyle-ms-*` only.

## 4. What the bud stack adds (and why it's not the contract)

When a page is dual-compiled (see `docs/C-ISOMORPHIC-BUD.md`), bud's serializer
adds to the same markup:

- `data-bud-id="N"` on elements and `<!--bud-text:N-->`/`<!--bud-fragment:N-->`
  comment markers — hydration map keys.
- `data-bud-on="event:N"` — listener wiring that `bud-hydrate.js`
  `autoBindListeners()` maps to `bud_app_dispatch(node_id, event, bubbles,
  payload)`.
- The WASM patch-op stream (`bud_host_emit_patch` → `BudPatchApplier`) — bud's
  DOM mutation channel.

These are **bud-stack-internal implementation details**. A React consumer
ignores them entirely and reimplements hydration with React's own markers.
Removing all `data-bud-*` from the HTML must leave a fully functional widget.

## 5. Guardrails (enforced in code review)

1. No `bud` symbols/types in `external/hyle/src` or `include/hyle`; only
   `external/hyle/c/libhyle-bud` may depend on bud.
2. The widget's client behavior must never REQUIRE `bud-client.js`, the WASM
   bridge, or the patch stream.
3. All interactivity must have a no-JS path; all input must use native controls
   that submit server-understood values.
4. Keep `data-hyle-*` hooks stable and documented when adding a widget; treat
   `data-bud-*` as free to change.
5. hyle may become WASM-only later — SSR output must stay plain HTML with no
   hidden runtime dependency.
6. Server writes still route through hyle `put`/`del` (`source_update_item` /
   `source_delete_item`) so the FTS index stays live.

## 6. Related docs

- `docs/ARCHITECTURE.md` — the framework-pair model that this contract serves.
- `docs/C-ISOMORPHIC-BUD.md` — the bud stack's dual-compile implementation.
- `docs/FILTERS.md` — wire formats the controls must submit.
