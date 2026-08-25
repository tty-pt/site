# CX — markup ergonomics for C renderers (staged proposal)

Status: **proposal**. Nothing here is implemented yet. This doc captures the
full design so the work can start without re-deriving it.

Two stages:

1. **Stage 1 — library pass on `bud_jsx.h`.** Small, immediate, zero build
   impact, clangd-perfect everywhere. Do this first regardless of anything
   else.
2. **Stage 2 — the `cx` transpiler.** A pure-C source-to-source tool that lets
   renderers embed XML/HTML-style markup literals. Gated behind a decision
   gate: only build it if real Stage-1 usage still shows painful residual
   friction.

The staging exists because of a fundamental trade-off that shaped this whole
proposal: **clangd can only understand what clang can parse, and anything
clang can parse is C.** A syntax that clangd understands natively is not a
transpiler target — it is a library-design problem. Conversely, true
angle-bracket markup will never be clangd-parseable in place; the best it can
get is clangd attached to generated files plus curated editor support. Stage 1
buys most of the ergonomics at zero tooling cost; Stage 2 buys the rest at a
permanent, known price.

## Requirements

Carried through both stages; they are the acceptance lens for every decision
below.

- **R1 — clang language server understands renderer code.**
  Stage 1: natively, because everything is ordinary C.
  Stage 2: fully, on the generated `.gen.c` truth via `compile_commands.json`;
  `.cx` buffers are deliberately kept outside the compilation database (see §5.5).
- **R2 — native vim highlighting.**
  Stage 1: free (plain C).
  Stage 2: committed `ftdetect` / `syntax` / `ftplugin` files, no plugins
  required (see §5.5).
- **R3 — portable tooling.** Pure C99 tool, no dependencies; POSIX suffix
  rules; must build and drive identically under GNU make and BSD make.
- **R4 — output invariants.** SSR HTML byte-identical after any migration;
  bud node-id alignment preserved by construction (C-ISOMORPHIC-BUD §3);
  UX purity and boundary checks stay green.

Non-goals for both stages: component/props syntax, control-flow tags,
alternate emission backends, MSVC, a formatter.

---

## Stage 1 — `bud_jsx.h` ergonomic pass

### 1.1 Evidence

Current builders force three recurring frictions. Counts from a
`grep -cE 'lx_el|lx_frag|lx_text|lx_node|lx_bind|lx_raw|bud_raw'` sweep of
`mods/*/ux/*.c`: 34 UX files, 21 contain builder calls (gig/detail 59,
gig/edit 50, grp/detail 41, song/detail 37, common/site_forms 33,
common/site_media 24, common/site_layout 22, index/list_filters 18,
common/site_chrome 15, gig/song_picker 14, index/list_layout 13,
auth/login 11, auth/register 11, poem/detail 8, index/home 3, gig/add 3,
song/form 2, poem/form 2, grp/add 2, grp/edit 2, index/list_render 1).

1. **`.data.node` noise.** `lx_el`/`lx_frag` return `bud_arg`, so every use
   in C context (return value, variable, nested helper) ends in
   `.data.node`. This is the single largest visual cost — see
   `mods/song/ux/detail.c`.
2. **Conditional attributes.** Every optional attr is a ternary ending in
   `lx_none()`:
   `i == app_state.transpose ? lx_attr("selected", "") : lx_none()`.
3. **Dynamic text.** Formatted text needs an out-of-band `snprintf` into a
   stack buffer before `lx_text(buf)`.

### 1.2 Changes

All in `external/bud/include/bud/bud_jsx.h` (+ one function in
`external/bud/src/libbud.c`). Existing macros keep their exact behavior;
these are additive.

```c
/* Node-returning builders — the workhorses. Empty varargs invalid in C99,
 * same rule as today: pass lx_none() when there are no children. */
#define lx_n(tag, ...)                                                       \
	bud_el_impl((tag),                                                   \
	            sizeof((bud_arg[]){ __VA_ARGS__ }) / sizeof(bud_arg),    \
	            (bud_arg[]){ __VA_ARGS__ })
#define lx_frag_n(...)                                                       \
	bud_frag_impl(sizeof((bud_arg[]){ __VA_ARGS__ }) / sizeof(bud_arg),  \
	              (bud_arg[]){ __VA_ARGS__ })

/* Formatted text node (libbud.c addition, mirrors bud_attr_fmt ownership):
 * bud_node *bud_textf(const char *fmt, ...);
 * vsnprintf into a bounded stack buffer, bud_strdup into the node. */
#define lx_textf(fmt, ...)                                                   \
	((bud_arg){ .type = BUD_ARG_NODE, .data.node = bud_textf(             \
	            (fmt), ##__VA_ARGS__) })
```

Resulting style:

```c
/* before */                                   /* after */
lx_el("form", lx_attr("id", "t"))              lx_n("form", lx_attr("id", "t"))
    .data.node;
snprintf(zv, sizeof(zv), "%d", z),             lx_textf("%d", z)
lx_text(zv)
```

### 1.3 Tests & demo

- Extend `external/bud/src/bud_test.c`: `bud_textf` ownership/formatting,
  `lx_n`/`lx_frag_n` equivalence with `lx_el(...)...data.node`.
- Rewrite `mods/song/ux/detail.c` renderers onto the new names as the demo.

Deliberately omitted: an `LX_IF(cond, arg)` conditional-splice macro. It is
just `(cond) ? (arg) : lx_none()` with a name on it — API surface without
clarity gain. Optional attributes stay as hand-written ternaries (friction 2
above is accepted; it was not worth sugar).

### 1.4 Exit criteria

- bud tests green; native + wasm rebuilds green.
- SSR HTML byte-diff identical for the song detail page across representative
  states (latin / transposed / media toggles).
- `boundary-check` green; clangd serves the rewritten file with zero
  surprises (trivially true — it is plain C).

### 1.5 Decision gate

Run Stage 1 for **4–6 weeks** of real feature work. Track: hand-rolled local
wrappers people invent per module, subjective readability complaints on new
renderers, onboarding time for a fresh renderer. If friction remains high,
proceed to Stage 2. If not, this doc's Stage 2 becomes a historical record —
which is a fine outcome.

---

## Stage 2 — the `cx` transpiler (full design, preserved)

### 2.1 Identity

`cx` = C with embedded markup literals. Not JSX, not JS: XML-shaped
expressions inside ordinary C, lowered to the existing `bud_jsx.h` builders.
Runs at build time; compilers only ever see plain C; the request path and SSR
contract are untouched.

### 2.2 Tool

Single translation unit `tools/cx/cx.c`, ~1200 lines of C99, zero deps,
builds anywhere with `cc -O2 -o cx cx.c`. Vendored under `tools/cx/`;
lift-and-copy portable to any C project.

Three stages in one TU, over a growable output buffer:

1. **Scanner** — byte state machine copying C verbatim, tracking line/col.
   Skips `//`, `/* */` (multiline), `"…"` / `'…'` with escapes, and
   preprocessor lines including backslash continuations — so
   `#include <bud/bud.h>` can never be seen as markup.
2. **Parser** — recursive descent with checkpoint/rewind (saved buffer offset
   + line/col + input pos). Triggered only at `<` immediately followed by
   `[A-Za-z_]`. Any parse failure ⇒ restore snapshot, emit `<` verbatim.
   This makes `if (a < b && c > d)`, `a <= b`, `x << 2`, and tags inside
   strings/comments all safe without parsing C. Once committed past the tag
   name, failures are hard errors: `detail.cx:42:9: error: expected '>'`.
3. **Emitter** — writes generated calls; after each rewritten region
   re-syncs with `#line <next-orig-line> "detail.cx"`. Verbatim chunks copy
   exactly, so no drift accumulates between regions. With `-DBUD_DEBUG`,
   `__FILE__` resolves through the line markers ⇒ src stamps point at the
   `.cx`.

Guards: max nesting depth 64, OOM-safe growth, deterministic byte-stable
output, locale-independent identifier sets, CRLF tolerated.

CLI: `cx [-o out] [--markers] [-v] input.cx`; exit 0 ok / 1 usage / 2 parse
error. `--markers` emits `/* cx:<line> */` anchors before each generated
construct (consumed by the vim navigation helpers, §5.5).

### 2.3 Language spec (v1)

```
element := '<' name attr* ('/>' | '>' body '</' name '>')
         | '<>' body '</>'
attr    := name '=' ('"' text '"' | '{' cexpr '}')     /* bare attr -> lx_attr(name,"") */
body    := (text | '{' ['&'] cexpr '}' | element)*
```

Pinned rules:

- **No unclosed void tags.** `<br>` is an error; `<br/>` required. This alone
  eliminates the classic `a < b > c` ambiguity.
- Uppercase/component tags: hard error ("call the function directly") — props
  structs are not worth inventing yet.
- C block comments allowed wherever whitespace is legal inside markup;
  dropped. `{/* note */}` children likewise dropped.
- Braces scan balanced `{}` respecting strings/chars/comments — not a C
  expression parser.

Hybrid `{expr}` semantics:

| Position | Input | Emitted |
|---|---|---|
| attr | `k="v"` | `lx_attr("k", "v")` |
| attr | `k={atomic}` | `lx_attr("k", "%s", expr)` |
| attr | `on*={fn}` | `lx_bind("<evt>", 0, fn)` |
| attr | complex (`?:`, comma, `(`, `lx_` prefix) | verbatim `bud_arg` splice |
| child | bare text | `lx_text("…")` (escaped; newline-separated whitespace-only runs dropped) |
| child | `{atomic}` | `CX_CHILD(expr)` |
| child | `{&expr}` | `lx_raw(expr)` |
| child | complex | verbatim splice |

*Atomic* = identifier / member chain (`app_state.cache.title`) or single call
`f(args)` — classified syntactically; anything else splices raw, so existing
idioms (`media_slot ? lx_node(media_slot) : lx_none()`, `site_ui_checkbox(…)`
wrappers) work unchanged.

`CX_CHILD(e)` (new, lives in `bud_jsx.h`; keeps the tool backend-agnostic):

```c
#define CX_CHILD(e) _Generic((e)+0,                                          \
	char *:            lx_node(e),                                       \
	const char *:      lx_node(e),   /* text via lx_text(e) — see impl */\
	unsigned char *:   lx_node(e),                                       \
	bud_node *:        lx_node(e),                                       \
	const bud_node *:  lx_node(e),                                       \
	int:               lx_textf("%d", e),                                \
	long:              lx_textf("%ld", e),                               \
	default:           lx_text((const char *)(e)))
```

(The `(e)+0` forces array decay so `char title[256]` state fields hit the
text path; scalars/pointers only — documented limitation.)

Worked example (real code from `mods/song/ux/detail.c`):

```c
/* detail.cx */                                    /* emitted in detail.gen.c */
return <form id="transpose-form"                   return lx_n("form",
  action={app_state.path}>                                     lx_attr("id", "transpose-form"),
    <label>Key:</label>                                        lx_attr("action", "%s", app_state.path),
    <select onchange={bud_api_action_handler}                  lx_n("label", lx_text("Key:")),
             name="t">{key_options}</select>                   lx_n("select",
</form>;                                                          lx_bind("change", 0,
                                                                    bud_api_action_handler),
                                                                  CX_CHILD(key_options))));
```

Id alignment: output is the same compound-literal-array construct as
hand-written `lx_el`, args in source order ⇒ identical construction order ⇒
identical global ids (C-ISOMORPHIC-BUD §3). Proven empirically by the §5.7
byte-diff gates.

### 2.4 Support additions to bud

Same as Stage 1's `bud_textf`/`lx_textf`, plus `CX_CHILD` above. All
in-repo (`external/bud` is not a submodule). Tested in
`external/bud/src/bud_test.c`.

### 2.5 Build integration — POSIX suffix rules, both makes

One classic double-suffix inference rule serves GNU make and bmake alike
(no `%` patterns introduced; `$<` is POSIX in inference rules):

```make
# shared build.mk — every module inherits
.SUFFIXES: .cx .gen.c
.cx.gen.c:
	$(REPO_ROOT)/tools/cx/cx -o $@ $<
clean:
	...
	find . -name '*.gen.c' -delete
```

Root Makefile:

```make
CX_TOOL = tools/cx/cx
mods clients: cx-tool
cx-tool: $(CX_TOOL)
$(CX_TOOL): tools/cx/cx.c
	$(CC) -O2 -o $@ $<
clean: cx-clean
cx-clean:
	rm -f $(CX_TOOL)
test: test-cx
test-cx:
	sh tests/cx/run.sh
.PHONY: cx-tool cx-clean test-cx
```

Module opt-in is mechanical: list `ux/detail.gen.c` where `ux/detail.c` stood
in `SRC` / `*-src` and in header-prereq lines. Explicit lists, house style.
Depfiles chain naturally: the gen target rebuilds when `.cx` is newer;
`-MM` on gen.c covers its includes.

Portability matrix (acceptance): gmake 4.3 and bmake, × tool built by clang
and gcc (`-Wall -Wextra -Werror -std=c99` clean), × `make -j4`, plus
verification that `scripts/watch.sh` regenerates gen files through the same
make path.

Foreign-project recipe: copy `tools/cx/`, add the two-line suffix rule +
tool-build rule, list `foo.gen.c` in your sources. CMake/ninja equivalents
are one custom-command line each (documented, not shipped here).

### 2.6 Migration inventory (all UX renderers)

Grep-classified (§1.1): **21 markup-bearing files convert; 13 logic/aggregator
files stay `.c`** (`music.c`, `chrome.c`, `site_paths.c`, `site_page.c`,
`site_ui.c`, `list.c`, `list_fe.c`, `list_query.c`, `list_json.c`,
`index/all.c`, `grp/all.c`, `poem/all.c`, `auth/all.c`).

Conversion order is dependency-first: **common/ux first** (everything depends
on it), then auth → poem → index → song → gig → grp. Each step lands
compilable.

Include-graph rewiring — the subtle part:

- Aggregators that stay `.c` may still need edits when their included leaves
  convert: any `#include ".../leaf.c"` whose target became `.cx` flips to
  `leaf.gen.c` (e.g. `site_ui.c` pulling `site_forms.gen.c`).
- Native handlers including UX sources flip likewise:
  `song.c:340`, `gig.c:465-467`, `grp.c:382`, `index.c:42`, `poem.c:19`,
  `auth.c:25`, `common.c:10`.
- Cross-module sanctioned includes become `.gen.c` paths: 10× `site_ui.c`,
  4× `list.c`, 3× `music.c` includers.
- `scripts/check-module-boundaries.sh:26` allowlist updates from
  `site_ui.c|list.c|music.c` to the same names with `.gen.c` (its regex on
  line 11 already matches `.gen.c` since the pattern requires a `.c` suffix).
  AGENTS.md guideline text updated to match.

### 2.7 Repo plumbing

`.gitignore` += `*.gen.c`, `tools/cx/cx`. Root `format:` / `lint:` targets
exclude `-name '*.gen.c'` (generated surface is covered by golden tests and
compilers; avoids mtime churn relinking `.so`s). Docs: rows in `AGENTS.md`
topic index & guidelines, `OVERVIEW.md` repo-layout, `BUILD.md` wiring,
`C-ISOMORPHIC-BUD.md` §5 API additions, `CONVENTIONS.md` pointer. Boundary /
purity scripts otherwise untouched — their `find mods -name '*.c'` globs
already sweep generated outputs.

### 2.8 Editor package (R1 + R2)

Division of labor: **compilers/debuggers get `#line`; clangd gets the
generated files; vim gets curated highlighting for `.cx` and full LSP one
keypress away.**

- **clangd**: new `make compile-commands` (backed by
  `scripts/gen-compile-commands.sh`) emits `compile_commands.json` entries
  for native modules, wasm targets, and — critically — every `.gen.c`, with
  flags mirrored from `build.mk` variables (`bear -- make` noted as an
  alternative). Result: completion, hover, go-to-def, diagnostics, renames
  all work on the compiled truth. `.cx` files are deliberately **absent**
  from the database: clangd ignores unknown extensions without a compile
  entry, which means silence instead of a flood of false markup errors.
- **vim** (committed, plugin-free):
  - `ftdetect/cx.vim` — `autocmd BufRead,BufNewFile *.cx setfiletype cx`
  - `syntax/cx.vim` (~80 lines) — loads `syntax/c.vim` as the base cluster;
    recursive container-tag regions (self-closing tags as one-line regions;
    containers span to their matching close and contain further regions);
    highlight classes for tag names, attribute names, attribute strings, and
    brace expressions containing the C cluster. Known heuristics: `>` inside
    attribute strings/braces can confuse region ends — acceptable for
    coloring; tree-sitter fixes properly later.
  - `ftplugin/cx.vim` — commentstring, matchit tag pairs.
- **Navigation**: with `--markers`, an autoload pair `:CxGen` / `:CxSource`
  jumps cursor↔generated position via the `/* cx:<n> */` anchors (grep or
  binary search; deterministic output makes this stable). Roadmap:
  nvim-treesitter grammar with C/markup injections; a small `cx-lsp` proxy
  translating positions between `.cx` and gen over clangd for full in-file
  LSP.

### 2.9 Testing

- **Golden tests** `tests/cx/{run.sh,cases/}` — exact-output diffs
  (`in.cx` ↔ expected gen), covering: comparison disambiguation
  (`a<b&&c>d`, `p<q)return`, `x<=y`, `<<`), tags inside strings/chars/
  comments/includes/#define bodies, nesting depth, fragments, self-closing,
  empty element → `lx_none()`, every §2.3 semantics row, atomic-vs-complex
  classifier edges, whitespace policy, multi-line text, comments-in-markup,
  CRLF, determinism (run twice identical; gen re-fed to cx unchanged),
  `#line` placement, and error cases (unclosed tag, close-name mismatch,
  uppercase tag, stray `</>`, `<br>` void).
- **Unit**: `bud_test.c` additions (§2.4).
- **Migration gates per page** (song/gig/grp/poem detail, forms, auth pages,
  lists, home): SSR HTML **byte-diff identical** pre/post across
  representative states (latin/transposed/media/auth variants), wasm targets
  rebuilt (`song_detail`, `gig_detail`, `list`, `site_chrome`),
  `boundary-check` green, targeted e2e subset green, `wasm-debug` tree dump
  shows src stamps pointing at `.cx` files.

### 2.10 Phases

| Phase | Content | Exit criteria |
|---|---|---|
| P1 | `cx.c` + tools Makefile + README | golden tests pass; clang/gcc clean |
| P2 | Golden suite in repo; root Makefile targets | `make test-cx` green both makes |
| P3 | bud additions (`bud_textf`, `CX_CHILD`) + tests | bud tests pass; wasm builds |
| P4 | build.mk suffix rule + gitignore + compile-commands + plumbing | toy `.cx` round-trips in one module; clangd indexes gen |
| P5 | Convert common/ux, then auth→poem→index→song→gig→grp; handlers' includes; Makefiles; boundary-script allowlist | §2.9 migration gates all green |
| P6 | Docs cross-links, vim files polished, portability matrix run | gmake+bmake matrix green; docs merged |

### 2.11 Risks & mitigations

| Risk | Mitigation |
|---|---|
| clangd cannot parse `.cx` in place | Accepted by design; full LSP on `.gen.c` via compile-commands; `--markers` jumps bridge the gap; roadmap cx-lsp proxy |
| Backtracking misfires on exotic C | Trigger requires `<ident`; void-tag rule kills `a<b>c`; failure mode is verbatim passthrough; golden-pinned risky shapes |
| `_Generic` surprises | Enumerated types + decay trick tested; default branch casts; C11 documented |
| Include-graph misses (stale `.c` include) | Inventory-driven checklist (§2.6); boundary script fails loudly on unresolved/prohibited includes |
| Stale wasm (known BUILD.md caveat) | Same discipline as today; depfiles track `.cx` transitively through gen targets |
| bmake unavailable locally | Matrix may complete on the OpenBSD deploy host; gmake coverage until then |
| Vim syntax heuristics miscolor edge cases | Documented; tree-sitter grammar is the roadmap fix |

---

## Out of scope (both stages)

Components/props syntax, control-flow tags (`<for>`/`<if>` elements),
multiple emission backends, in-place markers inside `.c` files, MSVC, a
code formatter for generated or source files.
