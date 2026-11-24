# CX & Native Reactive UI Specification (`libbud` + `liblamina` + `.cx`)

> **Status:** Implemented (Steps 1–3 complete: `bud_reconcile`, `lam-bud.h`, `cx`, `cx-lsp`, `tree-sitter-cx`)  
> **Scope:** Pure-C reactive component runtime (`libbud`), host-agnostic reconciliation (`bud_host_ops`), `liblamina` native/WebGL UI host (`lam-bud.h`), `.cx` JSX-to-C transpiler, and `cx-lsp` `clangd` proxy.

---

## 1. Goal & Architectural Thesis

Write UI components **once** in React-like pure C (`.cx`, or plain C targeting the same runtime ABI) and run them with zero rewrites across **four targets**:

| Target | Host Implementation | Runtime Overhead |
|---|---|---|
| **Server-Side Rendering (`axil`)** | Stateless SSR expansion in `bud_render_html()` | Zero JS engine, direct HTML string emission |
| **Browser DOM (WASM)** | `bud_host_ops` → `stele_wasm` / DOM patch bridge | Tiny freestanding `.wasm` |
| **Native Desktop (`liblamina` GLFW/OpenGL)** | `bud_host_ops` → `lam-bud.h` (`lam_ui_div_t` + CSS Flexbox/Grid) | Native binary, zero Electron/browser |
| **Browser Canvas/WebGL (`liblamina` WebGL)** | `bud_host_ops` → `lam-bud.h` compiled to `wasm32` | GPU-rendered CSS UI inside WebGL |

### Design Invariants (Non-Negotiable)

1. **Zero circular library dependencies:**
   - `libbud` knows **nothing** about `liblamina`, OpenGL, or the browser DOM. It defines only `bud_host_ops` and pure-C tree reconciliation.
   - `liblamina` knows **nothing** about `libbud` at link time. The bridge (`include/ttypt/lam-bud.h`) is a header-only adapter that any consumer linking `-lbud -llamina` includes.
2. **Zero breaking changes to existing `libbud` / `liblamina` callers:**
   - All existing `bud_element`, `bud_tpl`, `bud_vdom_diff`, `bud_slot.h`, and `lam-ui.h` APIs remain ABI- and behavior-compatible.
3. **No C typechecker in the transpiler:**
   - `.cx` lowering relies on C11 `_Generic` for child/attribute type dispatch and explicit typed capture lists for portable C99 closures.

---

## 2. Part 1 — Runtime Data Layout & Host ABI (`libbud`)

### 2.1 `bud_node` Extensions (`include/bud/bud.h`)

Extend `bud_node_kind` with a lazy component node:

```c
typedef enum bud_node_kind {
    BUD_NODE_FRAGMENT  = 0,
    BUD_NODE_ELEMENT   = 1,
    BUD_NODE_TEXT      = 2,
    BUD_NODE_RAW_HTML  = 3,
    BUD_NODE_COMPONENT = 4   /* lazy component call: expanded on reconcile or SSR */
} bud_node_kind;
```

Additional fields on internal `struct bud_node`:
- `char *key` — optional reconciliation key (O(1) lookup; falls back to `data-bud-key` / `key` attr if NULL).
- `bud_component_fn comp_fn` — component render function `bud_node *(*)(void *ctx, const void *props)`.
- `void *comp_ctx` — optional component context pointer.
- `void *comp_props` + `size_t comp_props_size` — owned inline copy of props bytes (safe to construct from stack compound literals like `&(SongRowProps){ ... }`).

Additional fields on internal `struct bud_listener`:
- `void *user` — event handler context pointer (copied inline if `user_size > 0`, raw pointer if `user_size == 0`, freed on listener destroy when owned).
- `size_t user_size` — non-zero when `user` is owned by the listener.

New public constructors in `include/bud/bud.h`:
- `bud_node *bud_component_vnode(bud_component_fn fn, void *ctx, const void *props, size_t props_size);`
- `int bud_set_key(bud_node *node, const char *key);`
- `const char *bud_node_key(const bud_node *node);`
- `int bud_bind_ctx(bud_node *node, const char *event, int bubbles, bud_event_handler_fn handler, const void *ctx, size_t ctx_size);`

During SSR (`bud_render_html`), `BUD_NODE_COMPONENT` runs under a temporary stateless SSR hook context (so `bud_use_state(T, init)` returns initial values and `bud_use_effect` is a no-op), renders its subtree to HTML, and frees the transient tree.

### 2.2 Host Vtable (`include/bud/bud_reconcile.h`)

```c
typedef struct bud_host_ops {
    void *(*create_el)(void *ctx, const char *tag);
    void *(*create_text)(void *ctx, const char *text, size_t len);
    void  (*set_text)(void *ctx, void *host_node, const char *text, size_t len);
    void  (*set_attr)(void *ctx, void *host_node, const char *name, const char *val);
    void  (*remove_attr)(void *ctx, void *host_node, const char *name);
    void  (*insert_before)(void *ctx, void *parent, void *child, void *before);
    void  (*remove_child)(void *ctx, void *parent, void *child);
    void  (*destroy_node)(void *ctx, void *host_node);
    void  (*set_event)(void *ctx, void *host_node, const char *event,
                       int bubbles, bud_event_handler_fn fn, void *ud);
    /* Optional O(1) bulk clear when subtree has no BUD_CELL_HAS_CLEANUP effects: */
    void  (*clear_children)(void *ctx, void *parent);
} bud_host_ops;
```

**Ownership & move invariant:**
- `insert_before(ctx, parent, child, before)` inserts or **moves** `child` before `before` (or appends when `before == NULL`) without destroying `child`.
- `remove_child(ctx, parent, child)` detaches `child` from `parent` without freeing it.
- `destroy_node(ctx, host_node)` frees a detached host node (and any unmounted host-side listener metadata). This explicit detach/destroy split guarantees zero leaks on native C hosts (`liblamina`) while allowing O(1) keyed node moves.
- `set_event(..., fn == NULL, NULL)` removes an event listener from `host_node`.

### 2.3 Persistent Reconciler Tree (`bud_cell` & `bud_mount`)

The reconciler maintains a persistent tree of `bud_cell` nodes (distinct from `bud_slot.h` layout contribution slots):

```c
enum {
    BUD_CELL_DIRTY       = (1u << 0),
    BUD_CELL_HAS_CLEANUP = (1u << 1), /* subtree contains >=1 use_effect cleanup */
    BUD_CELL_MOUNTED     = (1u << 2)
};
```

Each `bud_cell` owns:
- Identity: `kind`, `tag` / `text` / `comp_fn`, `key`, `depth`, `flags`.
- Host links: `host_node` (concrete host handle for `ELEMENT`/`TEXT`, `NULL` for `FRAGMENT`/`COMPONENT`), `parent_host` (nearest concrete ancestor host node).
- Cached element state: committed attributes + committed listener names (for O(a) attribute/event diffing without retaining full previous child VNode trees).
- Children: dynamic pointer array `bud_cell **children` (`child_count`, `child_cap`).
- Hooks: per-component `bud_hook *hooks` array (`hook_count`, `hook_cap`).

### 2.4 Stable-Pointer State & Hooks (`bud_state_slot`)

To allow event closures to capture state pointers (`int *likes`) without separate setter function pointers or dangling addresses on `hooks` reallocation, each state/ref hook allocates a stable block with an inline header:

```c
typedef struct bud_state_slot {
    struct bud_cell *owner;
    uint32_t         hook_idx;
    uint32_t         size;
    max_align_t      payload[]; /* aligned storage returned as T * */
} bud_state_slot;
```

- `T *p = bud_use_state(T, init_val)` returns `&slot->payload[0]`.
- `bud_set(p, new_val)` steps back by `offsetof(bud_state_slot, payload)`:
  1. `memcmp` bailout — if bytes are identical, zero dirtying and zero re-render.
  2. Copies `new_val` into `payload`.
  3. Marks `slot->owner` dirty in `slot->owner->mount` (`BUD_CELL_DIRTY`).
- `T *r = bud_use_ref(T, init_val)` returns a stable pointer whose mutation never schedules a re-render.
- `bud_use_effect(fn, ud, deps, deps_size)` compares `deps` bytes against the prior commit (or runs on mount if `deps == NULL` with `deps_size == 0` for once-on-mount, or every commit if `deps == BUD_DEPS_ALWAYS`). Post-commit `bud_mount_flush()` invokes prior cleanup, runs `fn(ud)`, stores the returned cleanup function, and propagates `BUD_CELL_HAS_CLEANUP` up ancestor cells.

### 2.5 5-Step Child Reconciliation Algorithm

`bud_patch_children(parent, new_list, new_count)`:
1. **Empty target (`new_count == 0`):** If `host->clear_children && !(parent->flags & BUD_CELL_HAS_CLEANUP)`, free `bud_cell` metadata recursively and call `host->clear_children(ctx, parent_host)` in O(1); otherwise unmount each old child cleanly (running effect cleanups).
2. **Empty source (`old_count == 0`):** Bulk-mount all new children.
3. **Common prefix / suffix trim:** Advance `s` from start and shrink `oe`/`ne` from end while `same_cell(old, new)` holds, patching in-place.
4. **Middle diff:**
   - **Unkeyed fast path:** Positional patch up to `min(rem_old, rem_new)`, then mount extras or unmount tail.
   - **Keyed LIS path:** Build key lookup over remaining old cells, match remaining new children (patching matched cells in-place, mounting fresh ones, unmounting unmatched old cells), compute Longest Increasing Subsequence (O(k log k) binary search) of old indices, and issue `insert_before` **only** for non-LIS nodes right-to-left using right-sibling anchoring (`bud_cell_next_host`).

### 2.6 Dirty Subtree Flushing

`bud_mount_flush(bud_mount *m)`:
1. Collects all dirty `bud_cell`s in `m`, sorts by ascending `depth` (parent before child).
2. Re-renders only mounted dirty component cells (skipping any descendant already reconciled by a dirty ancestor).
3. Executes queued `use_effect` cleanups and effects post-commit.

### 2.7 C11 `_Generic` Ergonomic Helpers (`include/bud/bud_reconcile.h`)

```c
#define bud_child_any(x) _Generic((x), \
    bud_node *:   (x), \
    const char *: bud_text((x)), \
    char *:       bud_text((x)), \
    int:          bud_text_int((x)), \
    long:         bud_text_long((x)), \
    size_t:       bud_text_size((x)), \
    double:       bud_text_double((x)) \
)
```

---

## 3. Part 1B — `liblamina` Native/WebGL Host Adapter (`lam-bud.h`)

### 3.1 Minimal Tree Mutation Primitives Added to `lam-ui.h` / `src/ui.c`

`liblamina`'s `struct lam_ui_div` already stores `parent`, `first_child`, `last_child`, `next_sibling`, `tag`, `id`, `class_name`, `text`, `inline_style`, `attrs`, and `user_data`. Only three small primitives are needed for full reconciler parity:

```c
void lam_ui_insert_before(lam_ui_div_t *parent, lam_ui_div_t *child, lam_ui_div_t *before);
void lam_ui_remove_child(lam_ui_div_t *parent, lam_ui_div_t *child);
void lam_ui_remove_attr(lam_ui_div_t *div, const char *name);
```

### 3.2 `include/ttypt/lam-bud.h` Mapping

| `bud_host_ops` Call | `liblamina` Realization |
|---|---|
| `create_el(ctx, tag)` | `lam_ui_new(NULL, NULL)` + `lam_ui_set_tag(d, tag)` |
| `create_text(ctx, s, n)` | `lam_ui_new(NULL, NULL)` + `lam_ui_set_tag(d, "#text")` + `lam_ui_text(d, s)` |
| `set_text(ctx, d, s, n)` | `lam_ui_text(d, s)` |
| `set_attr(ctx, d, k, v)` | `"class"` → `lam_ui_class`, `"id"` → `lam_ui_set_id`, `"style"` → `lam_ui_set_inline_style`, else `lam_ui_set_attr` |
| `remove_attr(ctx, d, k)` | `"class"` → `lam_ui_class(d, NULL)`, `"id"` → `lam_ui_set_id(d, NULL)`, `"style"` → `lam_ui_set_inline_style(d, NULL)`, else `lam_ui_remove_attr` |
| `insert_before(ctx, p, c, b)` | `lam_ui_insert_before(p, c, b)` |
| `remove_child(ctx, p, c)` | `lam_ui_remove_child(p, c)` |
| `destroy_node(ctx, d)` | free per-div event list in `user_data` + `lam_ui_destroy(d)` |
| `clear_children(ctx, p)` | recursively free child event lists + `lam_ui_clear(p)` |
| `set_event(ctx, d, ev, fn, ud)` | update per-div `lam_bud_ev_list` stored via `lam_ui_set_user_data` |

**Event dispatch:**
- `lam_bud_dispatch_event(host, target_div, event_name, payload)` walks `target_div → lam_ui_parent(...)`, dispatching matching listeners (respecting `bubbles` and `bud_event_stop_propagation`), and returns how many handlers ran.
- `lam_bud_dispatch_click(host, root_div, px, py)` calls `lam_ui_hit_test(root_div, px, py)` and dispatches `"click"` on the hit node.

---

## 4. Part 2 — `.cx` Transpiler Grammar & Lowering Rules (Step 2)

A `.cx` file is **standard C11** extended with four tightly scoped constructs. Output is 100% portable C11 + `#line` directives + a binary/JSON span map.

### 4.1 Unambiguous JSX Entry

`<ident` or `<>` starts a JSX expression **only** in expression-start positions:
`return <`, `= <`, `( <`, `, <`, `? <`, `: <`, or inside a JSX `{ ... }` hole.
Everywhere else (`if (a < b)`), `<` is lexed as standard C.

### 4.2 Elements, Components, Attributes & Children

- Lowercase tags (`<div>`, `<button>`, `<tr>`) lower to `bud_element("...")` + `bud_set_attr` / `bud_set_key` / `bud_bind_ctx` + `bud_append`.
- Uppercase tags (`<SongRow key={s->id} song={s} />`) lower to:
  ```c
  bud_component_vnode(SongRow_render, NULL, &(SongRow_props){ .song = s }, sizeof(SongRow_props))
  ```
  plus `bud_set_key(node, ...)` if `key={...}` is present.
- `<>...</>` lowers to `bud_fragment()`.
- `{expr}` children lower to `bud_child_any((expr))` (resolved at C compile time by `_Generic`).
- Attribute values `attr={expr}` lower via `_Generic` (`bud_set_attr_any(el, "attr", (expr))`: `const char *`, `int`, `bool` boolean attrs, `NULL` omits attr).

### 4.3 List Comprehensions

```c
<ul>
    {for (size_t i = 0; i < n; i++) <SongRow key={songs[i].id} song={&songs[i]} />}
</ul>
```

Lowers to a C block appending each loop iteration's node into the parent element/fragment — zero closures, zero heap arrays.

### 4.4 Portable C99 Closure Lifting

```c
<button onClick={[AppState *app, int id = song->id](bud_event *e) {
    delete_song(app, id);
}}>Delete</button>
```

Lifts to file scope (emitted before the enclosing function):

```c
typedef struct { AppState *app; int id; } __cx_cap_1;
static int __cx_fn_1(bud_event *e, void *__raw) {
    __cx_cap_1 *__c = (__cx_cap_1 *)__raw;
    AppState *app = __c->app;
    int id = __c->id;
    (void)e;
    { delete_song(app, id); }
    return 0;
}
```

And at the call site:

```c
bud_bind_ctx(__el, "click", 1, __cx_fn_1_bridge,
             &(__cx_cap_1){ .app = app, .id = song->id }, sizeof(__cx_cap_1))
```

*(Within `component Foo(T1 a, T2 b)`, declared parameter types are already known to the transpiler, so `[a]` does not need to repeat `T1`.)*

### 4.5 `#line` Directives & Span Map

- Non-JSX lines remain 1-to-1 with original line numbers.
- Every lowered block is bracketed with `#line <orig_line> "<file.cx>"`.
- The transpiler optionally writes `<file>.cx.map` containing sorted fixed-width records:
  `struct cx_span { uint32_t cx_off, cx_len, c_off, c_len; uint8_t kind; };`
  covering every `{expr}`, capture body, tag name, and component reference.

---

## 5. Part 3 — `cx-lsp` Proxy over `clangd` (Step 3)

`cx-lsp` is a thin JSON-RPC stdio proxy sitting between the editor (Neovim / VSCode / Helix) and unmodified `clangd`:

1. **`didOpen` / `didChange`**: runs the in-memory `.cx → .c` transpiler (<1 ms), caches the span table, and forwards the shadow `.c` buffer to `clangd` (mapped to the same `compile_commands.json` flags).
2. **Cursor requests (`completion`, `hover`, `definition`, `references`, `signatureHelp`)**:
   - If cursor lies inside a JSX tag/attribute name (outside `{}`), `cx-lsp` serves HTML/CSS tag + component prop completions directly.
   - Otherwise, binary-searches the span table to map `(cx_line, cx_col) → (c_line, c_col)`, forwards to `clangd`, and maps returned `Location`/`TextEdit` ranges back `.c → .cx`.
3. **Diagnostics (`publishDiagnostics`)**: maps every `clangd` diagnostic range `.c → .cx` before forwarding to the editor.
4. **Incomplete-edit resilience**: if a JSX block has an unclosed tag/brace while the user is mid-keystroke (`<div class={foo.`), the transpiler recovers at statement/brace boundaries and still emits valid C up to the cursor so `clangd` member completion (`foo.`) continues working live.

---

## 6. Phased Rollout & Verification Plan

| Phase | Deliverable | Tier 0 Verification |
|---|---|---|
| **Step 1A** | `libbud` `BUD_NODE_COMPONENT`, `bud_bind_ctx`, `bud_host_ops`, `bud_cell` reconciler, `bud_use_state`/`bud_use_ref`/`bud_use_effect`, keyed LIS, SSR component expansion | `make -C external/libbud test` (existing `bud_test` + new `bud_reconcile_test` asserting exact host op counts & zero leaks) |
| **Step 1B** | `liblamina` `lam_ui_insert_before`/`remove_child`/`remove_attr` + `lam-bud.h` adapter + interactive native/SSR integration test | `make -C external/liblamina test-build` + `test_ui_layout`, `test_css_parser`, `test_bud_lamina` |
| **Step 2** | `cx` transpiler binary (`.cx → .c` + `#line` + span map) + sample `.cx` app compiling to SSR + native `liblamina` + `wasm32` | Standalone transpiler test suite + round-trip compile/run tests |
| **Step 3** | `cx-lsp` `clangd` proxy + Tree-sitter grammar | Automated LSP JSON-RPC protocol tests (diagnostics, hover, completion inside `{}` and `<tag>`) |
