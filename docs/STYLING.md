# Styling — CSS source of truth, tokens, cache bust, filter-bar traps

Where the CSS actually lives, how to ship a change, and the specificity traps
that make widget styles silently not apply.

## Source of truth — edit the hyle crate, not htdocs

`htdocs/hyle.css` is a **gitignored deployed copy**. The committed source of
truth lives in the **hyle submodule**:

```
external/hyle/crates/hyle/assets/hyle.css           ← canonical
external/hyle/crates/hyle-dioxus/assets/hyle.css    ← identical twin
htdocs/hyle.css                                     ← synced, gitignored
```

After editing the crate asset, sync the twin and the deployed copy and verify:

```bash
cp external/hyle/crates/hyle/assets/hyle.css external/hyle/crates/hyle-dioxus/assets/hyle.css
cp external/hyle/crates/hyle/assets/hyle.css htdocs/hyle.css
diff external/hyle/crates/hyle/assets/hyle.css htdocs/hyle.css   # must be empty
```

(`site_ui_page` links `/hyle.css` and `/styles.css`; `styles.css` is the
site-wide sheet in `htdocs/`.)

## Tokens and dark mode

`hyle.css` defines `--hyle-color-*` tokens derived from the consumer's
`--color-*` palette (`--hyle-color-primary/danger/text/surface/border` +
`--hyle-text-muted/subtle`, `--hyle-surface-*`, `--hyle-border-*`,
`--hyle-shadow-sm`). Structural neutrals flip for `prefers-color-scheme: dark`
and `.dark`; primary/danger are the consumer's responsibility. **New widget
styles must use these tokens** so dark mode keeps working.

## Cache bust — automated content hashing

Asset versioning is fully automated via `scripts/gen-asset-version.sh`.
On every build, `mods/common/ux/version.gen.h` is generated from sha256 content
hashes of `htdocs/styles.css`, `hyle.css`, `bud-client.js`, `bud-hydrate.js`,
and `hyle-fragments.js`.

`mods/common/ux/site_page.c` includes `version.gen.h` via `__has_include` fallback
and emits the cache-busting query strings (`SITE_CSS_V`, `SITE_CLIENT_V`,
`SITE_FRAGMENTS_V`). No manual `?v=` bumping is required. After modifying CSS or JS,
running `make` automatically re-hashes the assets and recompiles `common.so`.

To inspect current asset hashes:
```bash
sh scripts/gen-asset-version.sh && cat mods/common/ux/version.gen.h
```

## Filter-bar specificity traps (the widget gotchas)

`.hyle-filter-bar` styles target broad selectors that win against widget
classes unless the widget rules out-specify them:

- **`.hyle-filter-bar label`** (0,1,1) forces `flex-direction: column` plus an
  uppercase 11px/600/muted micro-font on EVERY `<label>` in the bar. Option-row
  labels (e.g. `.hyle-ms-option`, 0,1,0) get stacked vertically below their
  checkbox and inherit the caption typography unless you write
  `.hyle-filter-bar .hyle-ms-option` (0,2,0) with explicit
  `flex-direction: row; align-items: center; font-size: 12px; font-weight:
  normal; text-transform: none; letter-spacing: normal; color:
  var(--hyle-color-text)`.
- **`.hyle-filter-bar input`** (0,1,1) sets 30px height, padding, full border,
  radius, filled background. A widget input inside the bar (e.g.
  `.hyle-ms-search`, 0,1,0) loses its own rules — write
  `.hyle-filter-bar .hyle-ms-search` (0,2,0) to win.
- Field sizing: `.hyle-filter-bar label` fields are `flex: 1 1 120px`. A bare
  widget element is a default flex item (`flex: 0 1 auto`) → content-sized and
  visibly narrower. Wrap the widget in a field container (e.g.
  `.hyle-filter-bar .hyle-ms-field`) that mirrors the label (flex column,
  `flex: 1 1 120px`, caption typography) so its control matches sibling
  inputs.
- The `<summary>` disclosure marker: use `list-style: none` on the trigger and
  `.hyle-ms-trigger::-webkit-details-marker { display: none }` so the native
  triangle doesn't show next to the widget's own caret. Lay the trigger out
  with flexbox (`align-items: center; justify-content: space-between`), not
  floats.

## Related docs

- `docs/SSR-CONTRACT.md` — the widget markup these classes style.
- `docs/BUILD.md` — rebuild + cache-bust flow.
