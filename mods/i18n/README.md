# mods/i18n — Internationalization & Localization Module

Zero-dependency, pure-C internationalization (i18n) module with European Portuguese (`pt-PT`) and English (`en`) support for server-side HTML rendering (SSR) and browser-side WASM hydration.

## Features

- **HTTP Locale Negotiation**:
  1. `?lang=...` query string parameter (explicit per-request override).
  2. `HTTP_COOKIE` (`lang=pt` / `lang=en`).
  3. Authenticated session user preference (`user_pref_read(username, "lang")`).
  4. `HTTP_ACCEPT_LANGUAGE` quality-weighted header negotiation (e.g. `pt-PT,pt;q=0.9,en;q=0.8`).
  5. Default fallback: English (`en`).
- **European Portuguese (`pt-PT`) Catalog**:
  Canonical terminology (e.g. *Nome de utilizador*, *Palavra-passe*, *Registar*, *Guardar*, *Eliminar*, *Atuações*).
- **Pure-C Isomorphic Dictionary**:
  `i18n_dict.h` provides `i18n_t(lang, msgid)` with zero external dependencies, compatible with native `.so` (SSR) and `wasm32-wasi` (`.wasm`) targets.
- **Language Switch Endpoint**:
  `GET /i18n/set?lang=pt&return=/path` sets the persistent `lang` cookie, saves user preferences if logged in, and redirects with 303.

## XY Interface

```c
#include "../i18n/i18n.h"

/* Server XY Hooks */
const char *lang = i18n_resolve_locale(fd);
const char *translated = i18n_translate(lang, "Submit");
i18n_set_user_locale("alice", "pt");
```

## Isomorphic UX / WASM Usage

In dual-compiled UI components (`mods/*/ux/*.c`):

```c
#include "i18n_dict.h"

bud_node *btn = bud_tpl("<button type='submit'>%s</button>", i18n_t(lang, "Submit"));
```

## Build & Test

```bash
make -C mods/i18n
./mods/i18n/test.sh
```
