# Audit — leftover issues

Read-only review of the stack (2026-08-17). Site-only batches 1–3 +
batch 4 (submodules) + batch 5 (A6, A9, F8) + batch 6 (A7, C3, F9) +
batch 7 (C5, C6, D1, D2, D4, D8, D10)
already landed; this file
lists **what is still open**. Do not re-fix anything not listed here.

Batches 1–3 were site-only pre-rewrite fixes; their IDs are not
reconstructed here (they were dropped from this leftover file).

**Not claimed:** live exploit verification, fuzzing, or network testing.

Deferred on purpose (do not start without a plan): **C7** (GET `/api/song/prefs`
writes; WASM depends on it), **E16** (`per_page` clamp must rewrite `qs_copy`
or hyle still sees 0), **F15** (root-doc deletion; user declined),
**F10** (`AUTH_SKIP_CONFIRM` unconditional in `start.sh` — paired with B8).

---

## 1. Corrections vs the first-pass audit

| First-pass claim | Actual |
|---|---|
| `GET /api/dataset/song.items/%2e%2e` is an API id traversal | Static mapping runs **before** handlers (`libaxil.c:2249-2250`). That URL serves `./htdocs` as a directory 200. The real primitive is `htdocs /*` + decode-after-`..`-check. `GET /%2e%2e/etc/shadow` and `/api/dataset/song.items/%2e%2e/%2e%2e/etc/shadow` both reach jail `./etc/shadow`. |
| `qmap_put` takes ownership | It **copies** (`libqmap.c:1189-1218`). Stack buffers are safe; `malloc`+`put` without `free` leaks. |
| PUT/DELETE `/api/dataset` are live write surfaces | axil only dispatches GET/POST. PUT/DELETE handlers are registered (`source-http.c:1004-1007`) but never called. **POST create/update is live** and sufficient for IDOR + empty-body CSRF skip. |
| `get_doc_root` is a jail escape | Inverted return just forces `"."` (`common_storage.c:184-190`). Works only because non-root `chdir`s into the tree. |
| Host `/etc/shadow` always leaked | Only if not chrooted **and** readable. Root+chroot still leaks jail `./etc/shadow` (password hashes). |

---

## 2. Severity key

| Severity | Meaning |
|---|---|
| **Critical** | Remote unauthenticated or low-bar exploit; data breach, RCE, or stack smash |
| **High** | Authenticated exploit, data loss, or persistent incorrect behavior |
| **Medium** | Correctness bug, memory waste, degraded UX, or defense-in-depth gap |
| **Low** | Cosmetic, doc rot, minor inefficiency |

---

## 3. What is done well

- Framework-pair model and hyle neutrality are real and enforced.
- No-JS works; wasm is additive.
- HTML mutating routes generally use `with_item_access` + ownership + CSRF.
- QSESSION: `HttpOnly; SameSite=Lax` (`libaxil-auth.c:34`); tokens from `/dev/urandom`.
- Passwords: bcrypt `$2b$` cost 12; username allowlist `[A-Za-z0-9_-]` 2–32.
- `get_cookie` tokenizes correctly at cookie boundaries (CSRF parser should reuse it).
- Title → id slugify on HTML create (`axil_slugify`).
- Owner field not writable in schemas (`EXCL_FIELD` wr=0).
- `lx_text` / `lx_attr` values are escaped via `bud_buf_append_escaped`.
- Accent-sensitive search is intentional and correctly folded (`stoma_fold`).
- XY `RTLD_LOCAL` isolation is the right model.
- hyle core has **no** bud symbols; boundary rule is holding.

---

### Fix batch 4 — 2026-08-19

Submodule + site fixes (axil, axil-auth, site call sites):

| ID | What | File |
|---|---|---|
| **A21** | HTTP/2 preface + `SSL_shutdown` on dead peer — `SSL_free` only; ALPN `http/1.1` | `external/axil/src/libaxil.c` |
| **A2** | `axil_env_get` gains `dest_len` param; `strcpy` → `strlcpy`; all ~50 call sites bounded | `external/axil/include/ttypt/axil.h`, `external/axil/src/libaxil.c`, all `mods/` call sites |
| **A3** | `axil_read` loop capped at 64 rounds (≈512 KiB); returns partial read instead of blocking | `external/axil/src/libaxil.c:672` |
| **A4** | `descr_new` + WS upstream: reject `fd >= FD_SETSIZE`, close leaked fd | `external/axil/src/libaxil.c:603,2275` |
| **B1** | `redirect_target` validates same-origin relative path: `/` prefix, reject `//`, control chars | `external/axil-auth/src/libaxil-auth.c:122` |
| **B3** | `struct session` with `created_at`; 24h TTL; per-user cap of 8 with oldest-evict | `external/axil-auth/src/libaxil-auth.c`, `external/axil-auth/include/ttypt/auth.h` |
| **B4** | `generate_token` / `generate_bcrypt_salt` return error; no `abort()` / `time()` fallback | `external/axil-auth/src/libaxil-auth.c` |

### Fix batch 5 — 2026-08-19

| ID | What | File |
|---|---|---|
| **A6** | `cmd_new` heap overflow — realloc guard `>` → `>=` ensures NUL terminator in bounds | `external/axil/src/libaxil.c:683` |
| **A9** | `axil_dwritef` stack over-read — clamp `vsnprintf` return to `sizeof(buf) - 1` | `external/axil/src/libaxil.c:709` |
| **F8** | `get_doc_root` inverted return — `axil_env_get(...) > 0` → `== 0` (0 = hit) | `mods/common/common_storage.c:186` |

### Fix batch 6 — 2026-08-20

| ID | What | File |
|---|---|---|
| **A7** | Unchecked `realloc`/`malloc` — NULL checks on 6 alloc sites; safe fallback on global `input` realloc | `external/axil/src/libaxil.c:544,620,659,701,1243,2141` |
| **C3** | Dataset POST ownership + mandatory CSRF — `item_check_ownership` before write/delete; CSRF always validated | `mods/source/source-http.c:164,678,738,840` |
| **F9** | `core` ignores `xy_load` errors — warn on top-level failure, skip dependent modules; check `xy_load` in loop | `mods/core/core.c:13,27,89` |

### Fix batch 7 — 2026-08-20

| ID | What | File |
|---|---|---|
| **C5** | Fail-closed ownership — removed stat-based uid fallback; `item_record_ownership` returns -1 on failure | `mods/auth/auth.c:140-159`, `mods/index/index.c:1032-1050` |
| **C6** | ID sanitization — exposed `is_safe_id`; validate in `item_ctx_load`, `source_update_item`, POST/PUT/DELETE handlers | `mods/common/common.h`, `mods/common/common_storage.c:17`, `mods/auth/auth.c:228`, `mods/source/source.c:745`, `mods/source/source-http.c:669,740,845` |
| **D1** | Poem XSS — `bud_raw(content)` → `lx_text(content)` + `whitespace-pre-wrap` | `mods/poem/ux/detail.c:12` |
| **D2** | Textarea XSS — strip `</` from values before `bud_raw` in textarea branch | `mods/common/ux/site_ui.c:555-580` |
| **D4** | Media URL validation — YouTube ID allowlist `[A-Za-z0-9_-]{11}`, HTTPS-only URL check | `mods/common/ux/site_ui.c:340-370` |
| **D8** | Option patch text node — store `bud_node_child(opt, 0)` instead of option element | `mods/song/ux/detail.c:112` |
| **D10** | Security headers — CSP, nosniff, X-Frame-Options, Referrer-Policy; inline script moved to `bud-client.js` | `mods/common/common_response.c:44-57`, `mods/common/ux/site_ui.c:700-723`, `htdocs/bud-client.js` |

---

## 4. Open issues by layer

### A. axil (submodule `tty-pt/axil`)

#### A1 — Encoded `..` reaches static mapping before handlers

**Severity:** Critical
**Where:** `libaxil.c:2215-2221`, `:2249-2250`

`..` is rejected on the **raw** request target, then `url_decode` decodes
`%2e%2e` to `..`. `serve.allow` is `htdocs /*`; `fnmatch("/*", path, 0)`
matches every URI (`*` matches `/` because `FNM_PATHNAME` is not set).
`request_handle_static` runs at line 2249, **before** any registered handler.
A successful `stat` under the mapped root short-circuits everything.

`GET /%2e%2e/etc/shadow` → `./htdocs/../etc/shadow` → `./etc/shadow` (jail
password hashes). Non-root chdir mode can reach the host tree.

**Fix:** Decode first, then reject `..` / NUL / `\` / empty segments.
After mapping, `realpath` and require prefix of allowed root. `if
(!S_ISREG) return 0;` in `request_handle_static`. Prefer handlers before
static, or restrict `serve.allow` to explicit paths (`styles.css`,
`hyle.css`, `bud-client.js`, `*.wasm`).

**Land:** axil submodule. Site-side mitigation: tighten `serve.allow`.

#### A5 — `strncpy` URI may omit NUL

**Severity:** High
**Where:** `libaxil.c:2220-2221`

`strncpy(document_uri, argv[1], sizeof(document_uri))` then
`url_decode(document_uri)`. If the target is ≥ `BUFSIZ`, no NUL;
decode walks off stack.

**Fix:** `snprintf` + force NUL. 414 if URI ≥ `BUFSIZ`.

**Land:** axil submodule.

#### A8 — Header injection via decoded CR/LF

**Severity:** High
**Where:** `libaxil.c:225-229` (`axil_header_set`), `:1756-1763`, `:2102-2109`

No CR/LF strip on header values. `%0d%0a` survives decode. Pattern
params and `DOCUMENT_URI` can carry `\r\n` into `Location:` / `Host:`.

**Fix:** Reject control bytes in URI and Host. Sanitize
`axil_header_set` (reject or replace `\r`/`\n`).

**Land:** axil submodule.

#### A10 — Main process remains root after chroot

**Severity:** High
**Where:** `axil-posix.c:164-167` vs `:354-379`

Root path does `chroot` + `chdir("/")` only. All handlers, static
`open`, and item writes run as uid 0 inside the jail.

**Fix:** After bind + chroot, `setgroups`/`setgid`/`setuid`.

**Land:** axil submodule.

#### A11 — `query_parse` hex path copies uninitialized `c`

**Severity:** Medium
**Where:** `libaxil.c:1378-1386`

Failed `sscanf` leaves `c` uninitialized; still writes `decoded[j++]`.

**Fix:** Use `axil_url_decode` (already in `axil-encode.c`).

**Land:** axil submodule.

#### A12 — POST without `Content-Length` is not rejected

**Severity:** Medium
**Where:** `libaxil.c:2042-2160`

Missing CL → `buffer_post_body` returns 0. Body is whatever happened to
be in `input`. Chunked encoding is ignored. Behind a proxy this is
request-smuggling territory.

**Fix:** If POST and no CL → 411. Reject chunked or parse with same cap.

**Land:** axil submodule.

#### A13 — GET/header `input` unbounded; never shrinks

**Severity:** Medium
**Where:** `libaxil.c:119-120`, `:661-680`

No cap on GET. One client can grow `input` to RAM size; later requests
reuse the huge buffer.

**Fix:** Hard max; 431 on overflow; shrink after the request.

**Land:** axil submodule.

#### A14 — `Content-Length` parse uses `strtoul` with no overflow check

**Severity:** Medium
**Where:** `libaxil.c:2047`

`strtoul` with no `ERANGE` / tail check. Huge values wrap to small
numbers; under-read body.

**Fix:** `strtoull` + reject `*end != 0` / `ERANGE` / leading `-`.

**Land:** axil submodule.

#### A15 — Autoindex HTML-injects directory names

**Severity:** Medium (dormant — no `serve.autoindex` on this site)
**Where:** `libaxil.c:1807-1809`

`d_name` interpolated raw into `<a>` tag.

**Fix:** HTML-escape names. Keep autoindex off in production.

**Land:** axil submodule.

#### A16 — Unknown HTTP methods produce no response

**Severity:** Low
**Where:** `axil.c:124-126`

Only GET/POST/PRI are dispatched. HEAD/PUT/DELETE/PATCH get no status.

**Fix:** Default 405. HEAD = GET without body. If REST is wanted,
dispatch PUT/DELETE through `request_handle`.

**Land:** axil submodule.

#### A17 — `axil_respond` sends no `Content-Length`

**Severity:** Low
**Where:** `libaxil.c:260-292`

Relies on `Connection: close`. Fine for this model; sloppy for proxies.

**Fix:** `Content-Length: strlen(body)`.

**Land:** axil submodule.

#### A18 — `select` timeout is 10 seconds

**Severity:** Low
**Where:** `libaxil.c:95` (`SELECT_TIMEOUT 10000` microseconds), `:1186-1187`

Actually 10 seconds, not 10ms as the first pass claimed. Acceptable but
could be 100–500ms for faster timer work.

**Land:** axil submodule.

#### A19 — Missing hardening headers

**Severity:** Low
**Where:** `libaxil.c:198-205`

Only COOP/COEP/CORP. No `X-Content-Type-Options`, `Referrer-Policy`,
`X-Frame-Options`, CSP.

**Fix:** Add `nosniff` + frame deny on HTML. CSP can be set by the site.

**Land:** Both (site can set additional headers via `axil_header_set`).

#### A20 — Stale TODO markers

**Severity:** Low
**Where:** `axil/TODO.md:3-4`, `axil.h:192`

"Fix telnet / verify SSL"; `TODO DESCRIBE` on `axil_fd_tick`.

**Fix:** Implement or delete.

**Land:** axil submodule.

#### B2 — Login/register/logout have no CSRF; logout is GET

**Severity:** High
**Where:** `libaxil-auth.c:797-798`; `mods/auth/ux/login.c`, `register.c`

Logout registered without method prefix (`:797-798`); nav uses a GET link.
Session cookie `SameSite=Lax` → cross-site top-level GET sends the cookie
→ logout CSRF. Login CSRF: attacker's form POSTs their credentials;
victim's browser stores `QSESSION` (forced login).

**Fix:** Add CSRF to login/register. Register `POST:/auth/logout` only;
POST form in nav.

**Land:** axil-auth + site.

#### B5 — Password min 4; buffer truncation

**Severity:** Medium
**Where:** `libaxil-auth.c:641-642`, `:557`, `:620`

`password[64]` silently truncates > 63 chars. Login and register can
disagree.

**Fix:** Min 8–12; reject overflow; 128+ buffer.

**Land:** axil-auth submodule.

#### B6 — User/confirm enumeration; no rate limit

**Severity:** Medium
**Where:** `libaxil-auth.c:569-578`, `:646`, `:720-741`

Distinct messages for "Invalid credentials" / "Username already exists" /
"Account not confirmed". No lockout.

**Fix:** Same message + roughly constant time; rate-limit IP+user.

**Land:** axil-auth submodule.

#### B7 — Confirm codes world-readable + logged

**Severity:** Medium
**Where:** `libaxil-auth.c:695-702`

`mkdir(users_dir, 0755)` / `fopen(..., "w")` with default umask (0644).
Full confirm URL on stderr. `strcmp` not constant-time (`:740`).

**Fix:** `0700` on `users/`; `0600` on `rcode`. Log only "confirm issued
for user X". `CRYPTO_memcmp`. Delete rcode after N failures.

**Land:** axil-auth submodule.

#### B8 — `AUTH_SKIP_CONFIRM` auto-activates with any non-false value

**Severity:** Medium
**Where:** `libaxil-auth.c:109-119`

Any value except `0`/`false`/`FALSE`/`no`/`NO` skips confirm AND
auto-logs in. `make watch` and `start.sh` force it.

**Fix:** Only honor when `AUTH_ENV=dev`. Warn at `auth_init`.

**Land:** axil-auth + site (`start.sh`, `scripts/watch.sh`).

#### B9 — Shadow/passwd append non-atomic, default 0644

**Severity:** Medium
**Where:** `libaxil-auth.c:315-353`

`fopen(..., "a")` — torn writes, no `flock`, default mode. Hashes
readable by every uid.

**Fix:** `open(O_APPEND|O_CREAT|O_WRONLY, 0600)` + lock + fsync.
`umask(077)` around auth file creation.

**Land:** axil-auth submodule.

#### B10 — `crypt()` + global `query_db` hold the last password

**Severity:** Medium
**Where:** `libaxil-auth.c:573`, `:649`; `libaxil.c:1348`

`crypt()` not reentrant. `query_db` retains the last request's password
until the next parse.

**Fix:** `crypt_r`. Per-request query map. `memset` password buffers
after use.

**Land:** axil-auth + axil.

#### B11 — Confirm/logout registered without method prefix

**Severity:** Low
**Where:** `libaxil-auth.c:797-800`

`/auth/logout` and `/auth/confirm` match any method.

**Fix:** `GET:/auth/confirm` (or POST), `POST:/auth/logout` only.

**Land:** axil-auth submodule.

#### B12 — README stale (SHA-512, `/login`, in-memory email)

**Severity:** Low
**Where:** `mods/auth/README.md`

Documents pre-rewrite code. Actual: bcrypt `$2b$`, rcode files, `/auth/*`.

**Fix:** Rewrite to match `libaxil-auth.c`.

**Land:** Site.

---

### C. Site auth / CSRF / ownership

#### C1 — CSRF cookie issues

**Severity:** High
**Where:** `mods/auth/auth.c:41-120`

- Cookie not `HttpOnly` / `Secure` (`:87-90`). XSS can read it.
- `strstr("csrf_token=")` prefix-matches inside `notcsrf_token=`
  (`:73`, `:111`).
- `memcmp` not constant-time (`:120`).
- Generate-fail returns 0 with empty cookie (`:41-58`).

**Fix:** Pair parser like `get_cookie`; `CRYPTO_memcmp`; fail closed on
generate fail; `HttpOnly; SameSite=Strict` (+ `Secure` on TLS).

**Land:** Site.

#### C2 — `/api/csrf` is a token oracle

**Severity:** High
**Where:** `mods/auth/auth.c:327-334`

`GET /auth/api/csrf` mints a token and returns it in the body. Any
origin that can trigger a request can read a CSRF token. e2e helpers
(`tests/e2e/helpers/auth.ts:145-167`) depend on this endpoint.

**Fix:** Require session; `Cache-Control: no-store`; or stop returning
the body (hidden field is enough). Update e2e helpers if the body goes
away.

**Land:** Site.

#### C7 — `GET /api/song/prefs` writes prefs, no CSRF — **deferred**

**Severity:** High
**Where:** `mods/song/song.c:149-176`, `:466-471`

GET and POST share `api_song_viewer_prefs_handler`. GET parses
`QUERY_STRING` and writes zoom/latin/media with no CSRF. `<img
src="/api/song/prefs?z=50">` mutates a logged-in user. WASM currently
depends on the GET write.

**Fix:** Writes only on POST + CSRF. GET read-only. Update the WASM
client in the same change.

**Land:** Site.

#### C8 — Display owner ≠ enforcement owner

**Severity:** Medium
**Where:** `mods/poem/poem.c:89-91`; `mods/source/source.c:330-334`

UI uses `meta.owner` / `getpwuid` of host passwd. Enforcement uses
`item_check_ownership` (uid or owner file). Root mode never writes the
file.

**Fix:** One function for both UI and enforcement. Never `getpwuid` for
site usernames.

**Land:** Site.

#### C9 — Poem static map serves `owner` file

**Severity:** Medium
**Where:** `serve.allow:2` (`items/poem/items /poem/*/*`)

`/poem/:id/owner` is a public static file. Bypasses any future
private-item design.

**Fix:** Remove item-store static map. Serve `pt_PT.html`/media via
handler.

**Land:** Site.

---

### D. XSS / bud / WASM

#### D7 — Empty list skips `#bud-state` → id drift

**Severity:** High
**Where:** `mods/index/index.c:634-647` vs `:584-619`

Non-empty lists serialize state and set `data-modules="list"`. Empty
page still sets `data-modules="list"` but passes `extra_head=NULL`.
`wasm_init` never runs; trees diverge.

**Fix:** Always emit `list_state_to_json` + `#bud-state`.

**Land:** Site.

#### D9 — Wasm rule has no prerequisites

**Severity:** High
**Where:** `build.mk:44-49`

A present `.wasm` is never rebuilt. XSS/id-alignment fixes stay off the
wire until someone `rm`s the artifact.

**Fix:**

```make
$(WASM_PATH)/%.wasm: $($*-src) $(WASM_COMMON_SRC)
```

Fail if WASI clang is missing when targets are set.

**Land:** Site.

#### D13 — Songbook media subtree changes child count with `show_media`

**Severity:** Medium
**Where:** `mods/songbook/ux/detail.c:427-444`

Always builds `div > bud_raw("")`, then replaces with full iframe tree
when media exists. Different child counts → id drift.

**Fix:** Stable empty slot; fill via `patch-innerhtml`.

**Land:** Site.

#### D14 — `bud_json_*` is `strstr`, not a parser

**Severity:** Medium
**Where:** `external/bud/src/libbud.c:2371-2587`

First `"key":` wins — matches inside strings and suffixes. Truncates
elements to 4096.

**Fix:** Real parser, or length-prefixed format.

**Land:** bud submodule (or site workaround).

#### D17 — Attribute/tag names not escaped

**Severity:** Medium
**Where:** `external/bud/src/libbud.c:373-380`, `:502-509`

Values are escaped; names and tags are copied raw. A future
`bud_set_attr(n, user_key, v)` is attribute injection.

**Fix:** Allowlist `^[A-Za-z_][A-Za-z0-9_-]*$` for names/tags.

**Land:** bud submodule.

#### D18 — `--allow-undefined --export-all`

**Severity:** Low
**Where:** `build.mk:37-38`

Stray native symbols link as address 0 and crash in the browser.

**Fix:** Export allowlist; CI `nm` check for no native symbols in wasm.

**Land:** Site.

#### D21 — `bud_attr_fmt` 4-slot rotating buffer

**Severity:** Low
**Where:** `external/bud/src/libbud.c:970-981`

Four 256-byte slots. Fifth formatted attr in one `lx_el` aliases.

**Fix:** `strdup` into the attr.

**Land:** bud submodule.

---

### E. Data layer (site + hyle + stoma + qmap)

#### E1 — HTML delete skips `hyle_source_del`

**Severity:** High
**Where:** `mods/index/index.c:955-1013`

Delete uses `qmap_del_all` on `row_hd` / `fields_hd` and never sets
`e->stoma_dirty`. Deleted IDs appear in search results.

**Fix:** Replace raw `qmap_del_all` with `source_delete_item` →
`hyle_source_del`.

**Land:** Site.

#### E2 — Inverse-ref cleanup is memory-only; disk resurrects

**Severity:** High
**Where:** `mods/source/source.c:391-403`

`qmap_del(target->fields_hd, "ref_key:field")` zeros memory. On
restart, `source_scan_item` reloads old refs from disk.

**Fix:** After clearing the field, rewrite the target item through
`hyle_source_put` + `write_item_child_file`.

**Land:** Site.

#### E3 — Ref positions use wrong map

**Severity:** High
**Where:** `mods/source/source.c:557-559` vs
`external/hyle/src/source.c:484-486`

Write uses `row_hd`; filter/display use `fields_hd`. Different IDM
position spaces. After delete+insert, they diverge.

**Fix:** Store `fields_hd` as `target_hd` everywhere.

**Land:** Site.

#### E4 — API item IDs allowlist too broad

**Severity:** High
**Where:** `mods/source/source.c:745-750`;
`mods/source/source-http.c:669`

Path-join hole closed by C6 (`is_safe_id` rejects `/ \ . ..` controls).
Residual: allowlist is broader than `[a-z0-9_-]+`. Tighten to reject
non-alphanumeric characters, or slugify-and-reject-if-changed.

**Fix:** Accept only `[a-z0-9_-]+` or slugify and reject if changed.

**Land:** Site.

#### E5 — Auto-id counters reset on restart; `EEXIST` is success

**Severity:** High
**Where:** `mods/source/source.c:735-749`;
`mods/source/source-http.c:661-663`

Two `static` counters start at 1. `mkdir` + `EEXIST` continues and
overwrites files. Song e2e 403: `items/song/items/{1,3,6,9}` exist from
earlier runs; `http_auto_seq` generates `1`; C5 fail-closed ownership
denies.

**Fix:** `max(existing numeric keys)+1` from row map + dir scan. One
function, never reuse on create.

**Land:** Site.

#### E6 — List pages leak result and schema qmaps

**Severity:** High
**Where:** `mods/index/index.c:509-632` (`result_hd` never closed);
`mods/source/source.c:1125-1185` (`source_get_schema_hd` opens a new
map every call, never closed)

**Fix:** Close `result_hd` on every exit. Cache schema on
`source_def_t` or close in callers.

**Land:** Site.

#### E8 — `source_after_update` is song-global, ignores `dataset_id`

**Severity:** Medium
**Where:** `mods/song/song.c:269-291`; called from
`mods/source/source.c:868-869`

XY missing impl = success. Song impl ignores `dataset_id` and writes
`data.txt` for every successful `source_update_item`.

**Fix:** Gate on `strcmp(dataset_id, "song.items")==0` or delete the
hook (`data.txt` is already a field file via `EXCL_FIELD_VF`).

**Land:** Site.

#### E9 — Songbook empty `data.txt`

**Severity:** Medium (known pre-existing)
**Where:** `mods/songbook/songbook.c:99-152`, `:316-350`;
`mods/songbook/test.sh:91-93`

Save runs even when 0 songs matched → empty file. Format-line vs
type-slug mismatch in `get_random_repertoire_by_type`. Test now WARNs
instead of failing.

**Fix:** Don't save empty partition / fail create on zero songs. Match
via in-memory choir.songs. Restore hard fail in `test.sh`.

**Land:** Site.

#### E10 — `source_dsv_load` saves after every line

**Severity:** Medium
**Where:** `mods/source/dsv.c:86-87`

`ordered_append` calls `ordered_save` per line. Loading large
`data.txt` rewrites it N times.

**Fix:** Load-row without save; save once after.

**Land:** Site.

#### E13 — `hyle_source_put` ignores `qmap_field_put` failure

**Severity:** Medium
**Where:** `external/hyle/src/source.c:163-165`

Failed ref resolve returns `QM_MISS` but put returns 0 (success).

**Fix:** Check result; fail the put.

**Land:** hyle submodule.

#### E14 — API vs HTML delete semantics disagree

**Severity:** Medium
**Where:** `mods/source/source-http.c:807-825` vs
`mods/index/index.c:955-1013`

API returns 409 if referenced, then `source_delete_item`. HTML clears
inverse refs in memory and proceeds, skipping hyle.

**Fix:** One delete function: optional 409, persist inverse-field
rewrites, then `source_delete_item` + directory remove.

**Land:** Site.

#### E15 — API body parser truncates large fields

**Severity:** Medium
**Where:** `mods/source/source-http.c:106-121`

Stack `val[4096]`; truncation → 500. Multipart path allocates correctly.
Song lyrics via API fail.

**Fix:** Heap-allocate or 413 with clear error.

**Land:** Site.

#### E16 — Unbounded `per_page` on dataset API — **deferred**

**Severity:** Medium
**Where:** `mods/source/source-http.c:601-623`

`per_page=0` means "return all rows". Any logged-in user can dump a
full dataset. A local clamp is not enough: hyle still reads the original
`qs_copy`.

**Fix:** Default + hard cap (e.g. 100). Treat 0 as default. Rewrite
`qs_copy` so hyle sees the clamped value.

**Land:** Site.

#### E17 — Choir persist root hardcoded `"."`

**Severity:** Medium
**Where:** `mods/choir/choir.c:457-461`

Songbook copies `resolve_doc_root` into `g_doc_root`. Choir never
updates `static char doc_root[256] = "."`.

**Fix:** Same pattern as songbook: resolve once in `xy_install`.

**Land:** Site.

#### E18 — FTS full rebuild on first query after any mutation

**Severity:** Medium
**Where:** `external/hyle/src/source.c:606-638`

`stoma_dirty` → `stoma_clear` + re-index every searchable field of
every row. `qmap_get` also allocates a cursor per lookup
(`external/libqmap/src/libqmap.c:1429-1476`). Fine at hymnbook scale;
will not scale.

**Fix:** Incremental per-row stoma update. Document the cost.

**Land:** hyle + stoma submodules.

#### E19 — `hyle_filter_rows` naive substring scan

**Severity:** Low
**Where:** `external/hyle/src/view.c:124-193`

O(rows × fields × q). Residual filter still runs even when all filter
values are NULL.

**Fix:** Compact NULL filters before scanning.

**Land:** hyle submodule.

#### E20 — C vs Rust query parse asymmetry

**Severity:** Low
**Where:** `external/hyle/src/query.c:161-171` vs
`external/hyle/crates/hyle/src/query.rs:55-65`

C creates one filter per repeated key; Rust joins with commas. Site uses
C. `docs/FILTERS.md` §4 already documents union-within-field.

**Fix:** Fix Rust crate or mark unused.

**Land:** hyle submodule.

#### E25 — `stoma_fold` Latin-1 only; `STOMA_MAX_TOKENS` 64

**Severity:** Low
**Where:** `external/stoma/src/token.c:23-44`, `libstoma.c:7`

Intentional accent-sensitive fold. Extra query tokens silently dropped.

**Fix:** Keep as designed; document coverage + token cap.

**Land:** stoma submodule + docs.

#### E26 — `CBUG()` on qmap malloc aborts the process

**Severity:** Low
**Where:** `external/libqmap/src/libqmap.c:118`, `:545-550`

One allocation failure kills the server.

**Fix:** Return error; don't abort.

**Land:** libqmap submodule.

#### E27 — qmap stale TODOs

**Severity:** Low
**Where:** `external/libqmap/src/qmap.c:275`, `:712`;
`external/libqmap/BUGS.md`

"TODO free idml"; "TODO m1 can be inferred"; BUGS.md lists bugs as open
that were fixed in v0.7.0.

**Fix:** Free; infer; rewrite BUGS.md as changelog.

**Land:** libqmap submodule.

---

### F. XY / mpfd / common / core / build / tests

#### F1 — `xy_call` missing impl = `XY_OK` + zeroed ret

**Severity:** High
**Where:** `external/libxylem/src/libxylem-dispatch.c:315-321`

After dispatch loop, `ran == 0` → zeroes return, returns `XY_OK`.
`XY_DECL` wrappers return that zeroed value. If a required `.so` failed
to load, every hook silently succeeds with 0.

**Fix:** Return `XY_ERR_NOTFOUND` when `ran == 0`. Change `XY_DECL` to
check. Write-path wrappers must fail closed.

**Land:** libxylem submodule.

#### F2 — `RTLD_NODELETE` makes reload a lie

**Severity:** High
**Where:** `external/libxylem/src/libxylem.c:813`;
`libxylem-module.c:206-210`

glibc reuses the old mapping even if the file changed. `xy_reload`
cannot pick up a rebuilt `.so`. `xy_uninstall` is documented but never
called.

**Fix:** Drop `RTLD_NODELETE` or load via unique inode copy. Heap-copy
adapters. Implement or delete `xy_uninstall`.

**Land:** libxylem submodule.

#### F3 — XY error handling holes

**Severity:** High
**Where:** `libxylem.c:380-1204`; `libxylem-dispatch.c:348-366`

Unchecked malloc/strdup in `xy_deny`, `region_ensure_root`,
`_xy_claim_for_load`. `module_rekey_region_index` silently drops
modules past 512. `xy_areg` fail returns huge unsigned.

**Fix:** Check all allocs; grow or fail; return `XY_INVALID` on areg
fail.

**Land:** libxylem submodule.

#### F4 — `/tmp/xy_bind.log` on every bind

**Severity:** Medium
**Where:** `libxylem.c:882-948`; `libxylem-module.c:4-8`

World-writable `/tmp` + module paths + function pointers logged on
every module bind.

**Fix:** Delete the `fopen` blocks. Gate on `#ifdef XY_TRACE`.

**Land:** libxylem submodule.

#### F5 — mpfd trusts Content-Length as parse window

**Severity:** High
**Where:** `mods/mpfd/mpfd.c:278-284`

`strtoul` CL → `parse_multipart` with that as body length. No local
`strlen` check. Missing / zero CL → immediate `-1`.

**Fix:** Pass actual buffered length. Reject chunked-without-CL (415).

**Land:** Site.

#### F7 — mpfd parser correctness gaps

**Severity:** Medium
**Where:** `mods/mpfd/mpfd.c` throughout

- Case-sensitive `boundary=` / `multipart` (`:76`, `:274`): use
  `strcasestr`.
- Unquoted `name=` ignored (`:141-148`).
- No closing delimiter required (`:199-204`).
- Partial puts on error (`:228-247`): `mpfd_clear()` first.
- O(n·m) scan (`:59-64`): use `memmem`.

**Fix:** RFC 2046 compliant boundary parse; apply fixes above.

**Land:** Site.

#### F10 — `start.sh` always enables `AUTH_SKIP_CONFIRM`

**Severity:** Critical
**Where:** `start.sh:24`

`export AUTH_SKIP_CONFIRM=1` is unconditional. The gdb test
(`test ! -z "$GDB"`) is already correct. Broader rework (gate on
`AUTH_ENV=dev`) is B8.

**Land:** Site.

#### F11 — Stale `/usr/include` headers shadow repo

**Severity:** High (if include order regresses)
**Where:** `build.mk:23`

`build.mk` puts repo `-I` first. Default CFLAGS don't include
`external/hyle/include`. Any compile outside `build.mk` picks stale
system headers.

**Fix:** Add `-I$(REPO_ROOT)/external/hyle/include` to default CFLAGS.
Add header-resolution check to `make`.

**Land:** Site.

#### F12 — CSS `?v=` not computed at build

**Severity:** Low
**Where:** `mods/common/ux/site_ui.c`

`#define SITE_CSS_V "?v=9"` is the single source. Cksum-at-build still
open — bumping the define is still a manual step.

**Land:** Site.

#### F13 — Confirm helper tails the wrong log

**Severity:** Medium
**Where:** `tests/e2e/helpers/auth.ts`

Confirm helper tails `/tmp/site.log` only; `make watch` logs to
`debug/runtime/axil.log`.

**Land:** Site.

#### F15 — Stale planning docs — **deferred** (do not delete unless asked)

**Severity:** Low

| Path | Problem |
|---|---|
| `HYLE-AO.md` | "IMPLEMENTATION IN PROGRESS" |
| `AND-OR.md` | "PLAN (not yet implemented)" — shipped |
| `DOC.md`, `TESTS.md`, `plans/*`, `scratch/*` | One-shot plans |

`docs/` is canonical. `mods/common/README.md` already matches the tree.

**Land:** Site.

#### F16 — Makefile nits

**Severity:** Low
**Where:** `Makefile:8`, `:142-148`; `build.mk:22`, `:37-38`

- `all` does not build axil/xylem/qmap — relies on prebuilt libs.
- Always `-O0 -g`; no release profile.
- `deploy-wasm` copies `bud_demo.js`.

**Fix:** Depend on those libs; `DEV=1` vs release; exclude demo.

**Land:** Site.

#### F17 — `user_path_build` interpolates username into path

**Severity:** Low
**Where:** `mods/common/common_storage.c:128`

`./home/%s/%s`. Username is already allowlisted in axil-auth (`[A-Za-z0-9_-]`), but no explicit check at this layer.

**Fix:** Reject `/` / `..` explicitly.

**Land:** Site.

#### F18 — Remaining storage hardening

**Severity:** Low
**Where:** `mods/common/common_storage.c`

`slurp_file` already has `fstat` + `S_ISREG` + 10 MiB cap.
`item_path_build_root` already uses `is_safe_id`. Still open:

- `write_file_path`: no `O_NOFOLLOW`, no fsync (`:104-118`)
- `item_remove_path_recursive`: no prefix check under `items/` (`:258-262`)

**Land:** Site.

---

## 5. Phased roadmap (open only)

### Phase 0 — Immediate safety (site)

| ID | What |
|---|---|
| **F10** | Stop unconditional `AUTH_SKIP_CONFIRM` (pairs with B8) |
| **C1** | CSRF cookie: `HttpOnly`, pair parser, constant-time compare |

### Phase 1 — Security hardening (site)

| ID | What |
|---|---|
| **C7** | Prefs: POST + CSRF only (**deferred** — WASM depends on GET write) |
| **C2** | `/api/csrf` oracle (update e2e helpers) |
| **C8, C9** | One owner source; drop poem static map |
| **B2** | CSRF on login/register; POST-only logout |

### Phase 2 — Data layer correctness (site)

| ID | What |
|---|---|
| **E1** | Delete through hyle |
| **E2** | Persist inverse-ref rewrites |
| **E3** | One `target_hd` = `fields_hd` |
| **E4** | Tighten API id allowlist to `[a-z0-9_-]+` |
| **E5** | Durable auto-ids (max existing + 1, never reuse) |
| **E6** | Close leaked qmaps |
| **E8** | Gate or delete `source_after_update` |
| **E9** | Songbook empty `data.txt` |
| **E10** | DSV save-once |
| **E14** | One delete path |
| **E17** | Choir persist root |

### Phase 3 — WASM / build / infra (site)

| ID | What |
|---|---|
| **D9** | Wasm prerequisites in `build.mk` |
| **D7** | Empty list always emits `#bud-state` |
| **D13** | Stable songbook media slot |
| **F11** | Always add repo `-I` for hyle |
| **F5, F7** | mpfd length + parser |
| **F12** | Cksum-at-build CSS `?v=` |

### Phase 4 — Submodule PRs

| IDs | Library | Status |
|---|---|---|
| **A2–A4** | axil: bounded env get, nonblock, accept bounds | **DONE** |
| **B1, B3, B4** | axil-auth: redirect validate, session TTL, urandom in jail | **DONE** |
| **A6, A9, A21** | axil: cmd_new OOB, vsnprintf over-read, HTTP/2 preface | **DONE** |
| **A7** | axil: unchecked alloc | **DONE** |
| **A1, A5, A8, A10–A20** | axil: decode-then-reject, strncpy NUL, header inject, privdrop, etc. | open |
| **B2, B5–B12** | axil-auth: CSRF on login, session no-Secure, password min, etc. | open |
| **F1–F4** | libxylem: missing-impl error, RTLD_NODELETE, error handling, logging | open |
| **E13, E18, E20** | hyle: field-put failure, incremental stoma, query parse | open |
| **E25–E27** | stoma fold + libqmap abort/TODOs | open |
| **D14, D17, D21** | bud: JSON parse, attr/tag allowlist, `bud_attr_fmt` | open |

### Phase 5 — Cleanup

| ID | What |
|---|---|
| **F15** | Delete stale root docs (**deferred**) |
| **F13** | Confirm helper: also tail `debug/runtime/axil.log` |
| **F16** | Makefile cleanup |
| **A20, B12** | Stale TODOs and auth README |
| **E15, E16** | API body truncation; `per_page` cap (**E16 deferred**) |
| **F17, F18** | Username path check; `O_NOFOLLOW`/fsync; delete prefix |
| **D18** | Wasm export allowlist |
