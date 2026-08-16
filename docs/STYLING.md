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

## Cache bust — mandatory after any CSS change

Both `site_ui_page` paths in `mods/common/ux/site_ui.c` emit the stylesheet
links with `?v=N` (four lines total: `styles.css?v=` + `hyle.css?v=`, twice).
Bump `?v=7`→`?v=8` on all four, then rebuild + restart (see
`docs/BUILD.md`). Forget this and browsers serve stale CSS.

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
