# tty.pt → Tailwind CSS Mapping Reference

## CSS Class Mapping

| tty.pt | Tailwind Equivalent |
|--------|------------------|
| `.f` | `flex` |
| `.v0` | `flex flex-col h-screen m-0` |
| `.v` | `flex flex-col gap-4` |
| `.v8` | `flex flex-col gap-2` |
| `.h` | `flex gap-4` |
| `.h8` | `flex gap-2` |
| `.fg` | `flex-1` |
| `.fix` | `fixed` |
| `.p0` | `p-0` |
| `.p8` | `p-2` |
| `.p12` | `p-3` |
| `.p` | `p-4` |
| `.tac` | `text-center` |
| `.ttc` | `capitalize` |
| `.fic` | `items-center` |
| `.round` | `rounded-full w-4 h-4 flex items-center justify-center text-base` |
| `.dn` | `hidden` |
| `.rel` | `relative` |
| `.abs` | `absolute` |
| `.cp` | `cursor-pointer` |

## Colors (CSS Variables to Tailwind)

Light mode:
- `--cb` (background): `#f9f9f9` → `bg-[#f9f9f9]`
- `--cf` (foreground/text): `#3c403c` → `text-[#3c403c]`
- `--c0` (borders): `#2c2c2c` → `border-[#2c2c2c]`
- `--c5` (links): `#6e5f8a` → `text-[#6e5f8a]`
- `--c13` (link hover): `#877cb3` → `hover:text-[#877cb3]`

Dark mode (via `prefers-color-scheme: dark`):
- `--cdb`: `#3c403c` (dark bg)
- `--cdf`: `#c1c3da` (dark text)
- `--cd0`: `#2c2c2c` (dark borders)
- `--cdc`: `#9589c5` (dark caret/link)
- `--cd13`: `#877cb3` (dark links)

## Component Styles

### Buttons
```html
<!-- tty.pt -->
<button class="btn">Text</button>

<!-- Tailwind equivalent -->
<button class="bg-gray-50 border border-gray-200 px-2 py-1 hover:brightness-105 shadow-sm">
  Text
</button>
```

### Inputs
```html
<!-- tty.pt -->
<input type="text" />

<!-- Tailwind equivalent -->
<input type="text" class="border border-[#2c2c2c] p-2" />
```

### Links
```html
<!-- tty.pt -->
<a href="/path">Link</a>

<!-- Tailwind equivalent -->
<a href="/path" class="text-[#6e5f8a] hover:text-[#877cb3] no-underline">
```

### Menu Button
```html
<!-- tty.pt -->
<div class="round btn p12 m c0">☰</div>

<!-- Tailwind equivalent -->
<div class="rounded-full w-4 h-4 flex items-center justify-center bg-gray-50 border border-gray-200 p-3">
  ☰
</div>
```

## Layout Structure

```html
<!-- tty.pt structure -->
<label class="f v0 menu main">
  <input type="checkbox" />
  <span id="menu-button" class="p8 f fic h fix">
    <div class="round btn p12 m c0">☰</div>
  </span>
  <span class="fg ts ttc functions" id="menu">
    <Menu />
  </span>
  <div id="main" class="fg p v f fic">
    <h2 id="title" class="ttc tac">Title</h2>
    {children}
  </div>
</label>
```

## Dark Mode

Tailwind uses `@media (prefers-color-scheme: dark)` automatically for `dark:` prefix.
For inline styles, need to use CSS variables or conditional classes.

For SSR with CSS variables in HTML:
```html
<style>
:root {
  --cb: #f9f9f9;
  --cf: #3c403c;
  --c0: #2c2c2c;
  /* ... */
}
@media (prefers-color-scheme: dark) {
  :root {
    --cb: #3c403c;
    --cf: #c1c3da;
    --c0: #2c2c2c;
  }
}
</style>
```
