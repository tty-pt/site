# C-isomorphic bud — write one renderer, run it SSR *and* in the browser

A **C-isomorphic** renderer is a single C function tree that is compiled twice:

1. **Native** — into a module `.so` (e.g. `mods/song/song.so`), where it runs on
   the server to produce SSR HTML.
2. **WASM** — into `htdocs/<name>.wasm` (wasm32-wasi, reactor model), where it
   runs in the browser via the bud bridge (`bud-client.js` + `bud-hydrate.js`)
   to hydrate and enhance the SSR'd DOM.

The same source must therefore satisfy **both** environments. This doc is the
rulebook. Reference implementations: `mods/song/ux/detail.c` →
`htdocs/song_detail.wasm` (also `mods/gig/ux/detail.c`),
and the **list page** `mods/index/ux/list.c` → `htdocs/list.wasm` (served on
all four list pages via `data-modules="list"`; poem/grp load it too and the
enhancement no-ops).

---

## 1. The hard constraint

A translation unit that is dual-compiled may use **only**:

- the **bud API** (`bud.h`, `bud_jsx.h`, `bud_app.h`) — the `lx_*` builders, the
  `bud_*` node/runtime/json helpers, `bud_patch_*`, `wasm_*` lifecycle exports;
- **pure C** (`string.h`, `stdio.h`, `stdlib.h`, …).

It **must NOT** reference:

- `axil` (HTTP/env/respond),
- `source` / `source_*` (mods/source XY calls),
- `qmap` (`qmap_*`),
- `stoma`,
- any `XY_DECL`/`XY_IMPL` cross-.so function,
- the site's native data registries.

Why it's a hard rule: the WASM is linked with `-Wl,--allow-undefined`. Nothing
stops it from compiling with a stray native call — the symbol is simply left
undefined and **dereferenced at runtime as address 0**, usually a silent crash
or garbage, only when the browser runs it. `make` will not catch it.

### The data rule

Everything the renderer needs to draw must arrive as **data**, not via native
lookups:

- Native SSR: build the data from the qmap/source registries (server-side only),
  serialize it to a `bud-state` JSON, embed it in the page.
- WASM: `wasm_init(json, len)` parses that same JSON into the same state struct.

Both sides then call **one shared render function** over the state struct. This
guarantees the native tree and the WASM tree are structurally identical — see
§3.

## 2. The state table (fields.h pattern)

`mods/song/fields.h`, `mods/gig/fields.h`, `mods/grp/fields.h`, and `mods/poem/fields.h`
define module schemas using canonical `hyle_schema_desc_t` (from `external/hyle/include/hyle/schema.h`):

- server: the table feeds `source_setup` / `index_module_init` (storage, validation, persistence),
- WASM: `hyle_bud_state_apply_len(&app_state, fields, json, len)` fills the struct from the
  `bud-state` JSON by key using stride-aware unpackers (`bud_state_apply_stride_len`).

Pattern:

```c
#include <hyle/schema.h>
#include "../common/state_macros.h"

typedef struct {
    char id[128];
    char title[256];
    char type[2048];
    char author[256];
    char owner[32];
} song_cache_t;

typedef struct {
    song_cache_t cache;
    int transpose;
    int use_latin;
    char chord_html[65536];
} app_state_t;

static const hyle_schema_desc_t song_fields[] = {
    FIELD_TEXT(id, song_cache_t),
    FIELD_TEXT(title, song_cache_t, .required = 1, .min_length = 1, .in_meta = 1),
    FIELD_ARRAY(FIELD_REF, type, song_cache_t, "song.types",
                .ref_inverse = "songs", .filter_style = "dropdown",
                .filter_mode = "and", .allow_add = 1, .in_meta = 1),
    FIELD_REF(author, song_cache_t, "song.authors",
              .ref_inverse = "songs", .filter_style = "dropdown",
              .allow_add = 1, .in_meta = 1),
    FIELD_FILE(data, "data.txt"),
    FIELD_EXCL(owner, song_cache_t),
    FIELD_DERIVED(lyrics, "song.lyrics_from_data"),
    FIELD_END
};

#define SONG_APP_SCHEMA(F_STR, F_INT, st) \
    F_INT(st, transpose) \
    F_INT(st, use_latin)

BUD_STATE_FIELDS(app_state_t, song_app_fields, SONG_APP_SCHEMA)
```

Use `bud_json_str` / `bud_json_int` / `bud_json_data` /
`bud_json_array_for_each` for ad-hoc JSON reads that don't fit the table.

## 3. Id alignment — the trap that makes handlers "disappear"

bud assigns every node a numeric id at construction time (a global counter in
traversal/construction order). The SSR page is serialized with those ids
(`data-bud-id` attributes and `<!--bud-text:N-->` markers) by
`bud_render_hydrated_html`.

On the browser side, `bud_app_mount` runs `bud_app_render()` to build a WASM-side
`bud_runtime`. When a DOM event fires, the bridge calls the WASM export
`bud_app_dispatch(node_id, event, bubbles, payload)`, which does
`find_node_by_id(runtime_root, node_id)` and dispatches the handler on it. **If
the node id is not in the WASM tree, the handler silently does nothing**
(bud_wasm_app.c logs "dispatch: node=%d NOT FOUND").

Therefore:

> **The WASM tree must contain the same nodes, in the same order, with the same
> global ids as the SSR tree.** Any divergence (extra element, missing row,
> different count, conditional node that the two data paths disagree on) breaks
> handlers for everything after the divergence point.

Practically this means:

- Native SSR and WASM must run **the exact same render function** over **the
  exact same state struct contents**. Do not special-case the two sides inside
  the renderer.
- The page should be wrapped in `<div id="bud-root">…</div>` (the bridge and
  `bud-client.js` look for `#bud-root`).
- The native SSR for a dual-compiled page goes through `site_ui_page(title,
  extra_head_with_bud_state, module, layout)` — the `module` argument is what
  sets `body[data-modules]`, which drives the wasm fetch. `layout` must be the
  `#bud-root`-wrapped tree produced by the shared render function.

## 4. The life of a dual-compiled page

Server (native):

1. Handler (e.g. song.c:374) collects data from registries → `app_state`.
2. `layout = bud_app_render()` (shared render fn, wrapped in `#bud-root`).
3. Serialize state to JSON → `bud-state` `<script>` (via `extra_head`).
4. `site_ui_respond_page(fd, title, state_buf, "<module>", layout)`.

Client:

1. `bud-client.js` sees `body[data-modules]`, finds `#bud-root`, fetches
   `/{module}.wasm`, instantiates with the bridge's imports.
2. Feeds the `bud-state` JSON text to the export `wasm_init(ptr, len)`.
3. Calls `bridge.mount()` → exports `bud_app_mount` → `bud_app_render()` builds
   the WASM runtime tree (ids align with the SSR DOM).
4. `bud-hydrate.js` `autoBindListeners()` walks the SSR DOM, maps `data-bud-on`
   attributes to `bud_app_dispatch` calls.
5. User interacts → dispatch → WASM handler runs → emits patch ops via
   `bud_host_emit_patch` → `BudPatchApplier` mutates the SSR'd DOM.

No DOM is re-rendered at mount (mount emits no patches unless the runtime marks
itself dirty). Handlers use `bud_patch_text(node, …)`, `bud_patch_attr(node, …)`,
`bud_patch_innerhtml(node_id, html)` to touch only what changed.

## 5. The API you actually use

Builders (`bud_jsx.h`):

```c
lx_el("div", lx_attr("class", "x"), lx_attr("data-foo", "1"),
      lx_text("hi"), lx_node(child), lx_none())
lx_frag(lx_node(a), lx_node(b))        /* fragment */
lx_bind("change", 0, handler)          /* 0 = no bubbling; 1 = bubbling */
lx_raw(html)                           /* raw HTML sink */
```

Listeners: a `bud_event` gives you `event->type`, `event->target`,
`event->current_target`, `event->user` (your payload), `event->bubbles`. The
bridge passes an event-data string (target's `checked`/`selected`/`value`/
`textContent`) as `event->user`. Stop propagation / prevent default with
`bud_event_stop_propagation` / `bud_event_prevent_default`.

Runtime & accessors (`bud.h`): `bud_fragment`, `bud_append`, `bud_set_attr`,
`bud_set_bool_attr`, `bud_get_attr`, `bud_add_class`, `bud_remove_class`,
`bud_toggle_class`, `bud_node_id`, `bud_node_kind_of`, `bud_node_tag`,
`bud_node_text`, `bud_node_child_count`, `bud_node_child`, attribute/listener
accessors, `bud_raw` + `bud_raw_set_text`.

Patch helpers (bud_wasm_app.c, also declared in `bud_app.h`):
`bud_patch_text(node, value)`, `bud_patch_attr(node, name, value)`,
`bud_patch_innerhtml(node_id, html)`.

Lifecycle exports (bud_wasm_app.c): `bud_app_mount`, `bud_app_update`,
`bud_app_unmount`, `bud_app_mark_dirty`, `bud_app_flush`, `bud_app_dispatch`,
plus your own `wasm_init`, and optional `wasm_fetch_callback(request_id, data,
len)` (called when a `bud_host_fetch` resolves; drives `bud_api_action_handler`
flows).

Fetch & navigation host functions (`bud_app.h`/bud.h): the platform sets
`bud_host_fetch_fn`, `bud_host_log_fn`, `bud_host_set_location_fn` in
`bud_wasm_app.c`'s constructor.

Debug: `bud_sprint_tree`, exported `wasm_dump_tree`, `wasm_get_tree_text`,
`wasm_get_src(id)`; build with `make wasm-debug` (`-DBUD_DEBUG`) for source
stamps.

## 6. Build wiring

`build.mk` (repo root) provides the generic rule:

```make
$(WASM_PATH)/%.wasm:            # WASM_PATH = $(REPO_ROOT)/htdocs
	$(WASI_CC) $($*-cflags) $(WASM_COMMON_CFLAGS) $(EXTRA_CFLAGS) \
		$(WASM_CFLAGS) $(WASM_LDFLAGS) -o $@ $($*-src) $(WASM_COMMON_SRC)
```

- `WASI_CC` default `clang`, `--target=wasm32-wasi -mexec-model=reactor
  -Wl,--export-all -Wl,--allow-undefined`.
- `WASM_COMMON_SRC = external/bud/src/libbud.c external/bud/src/bud_wasm_app.c`.
- The rule self-probes (`echo 'int main(void){}' | $(WASI_CC) … -c -`) and skips
  with "Skipping WASM build of $@" when no WASI clang is available.

Per module (`mods/song/Makefile` — **before** `include ../../build.mk`):

```make
WASM_TARGETS = $(WASM_PATH)/song_detail.wasm
song_detail-src     = ux/detail.c
song_detail-cflags  = -I$(REPO_ROOT)/mods/common
```

- `X-src` may list multiple files; relative paths are relative to the module
  dir. `X-cflags` receives include paths for anything the unit pulls in.
- Include-source composition is the established pattern: `detail.c` does
  `#include "../../common/ux/site_ui.c"`. `site_ui.c`
  and `mods/index/ux/list.c` are written to be WASM-compilable — keep them that
  way.
- Native-only helpers (qmap/source/axil) must live in a DIFFERENT file (e.g.
  `mods/index/index.c`, `mods/source/source.c`) so they never end up in the
  wasm translation unit.
- **The wasm rule has NO prerequisites** — a `.wasm` only rebuilds when the
  target file is missing. After any change to a wasm-compiled source, force it:
  `rm -f htdocs/<target>.wasm && make`. Plain `make` silently ships the stale
  wasm (see `docs/BUILD.md`).

## 7. Checklist: add a new dual-compiled page

1. Define `app_state_t` + `bud_field_desc_t[]` (fields.h pattern) carrying every
   render input.
2. Write the shared render fn (`bud_app_render` or `BUD_APP_ROUTE`) — pure bud +
   state, wrapped in `<div id="bud-root">`.
3. Implement `wasm_init(json, len)` (via `bud_state_apply` and/or
   `bud_json_*`), handlers (via `lx_bind`), patches (`bud_patch_*`).
4. Native handler: build state from registries, serialize to `bud-state` JSON,
   embed it (extra_head), respond via `site_ui_respond_page(fd, title, json,
   "<module>", layout)`.
5. Add the wasm target to the module Makefile (§6).
6. Verify SSR HTML has `#bud-root`, `data-modules`, and `data-bud-id`/
   `data-bud-on` markers; verify the wasm loads (`data-wasm-loaded` on body) and
   handlers fire.
7. No-JS/no-WASM must still work (progressive enhancement) — the SSR output is
   the contract; the wasm is additive.

## 8. Pitfalls (memorize)

- **`--allow-undefined` hides mistakes.** A stray native symbol compiles and
  "links"; it crashes only in the browser. Grep your wasm unit for native-only
  symbols before building.
- **Id drift breaks handlers silently.** Never add/remove nodes in only one of
  the two data paths.
- **Reactor model**: no `main()`, no `stdout`; init happens via
  `bud_app_mount`/`wasm_init`, not a constructor that assumes a CLI.
- **`bud_app_dispatch` needs a runtime.** If `bud_app_mount` hasn't run (or
  failed), dispatch returns -1 and handlers are dead. `bud_patch_*` on a node
  not in the runtime tree emits nothing.
- **The native `.so` builds the same wasm_* symbols as no-op stubs**
  (bud_wasm_app.c non-`__wasm__` branch) — don't call them from native server
  code expecting browser behavior.
- **`bud_patch_text` must target a TEXT node, not an element.** The JS
  `patch-text` handler rewrites in place only for TEXT_NODE ids; an element id
  hits the stale-parent `createWrappedText` fallback and the text lands
  elsewhere (see `docs/WASM-BRIDGE.md` §6). Capture `bud_text(...)` once,
  wire it into the tree with `lx_node(...)`, and keep the TEXT node in your
  state.
- **Node ids are not stable across unrelated pages** — they're per-render. Never
  hard-code an id.
- **Keep `data-hyle-*`/framework-neutral hooks in the markup.** bud's
  `data-bud-id`/`data-bud-on` and the patch stream are bud-stack-internal and
  additive. See `docs/SSR-CONTRACT.md`.
