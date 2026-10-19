# FIX.md — fd-keyed PTY state + gated disconnect hook → HTTP corruption and unauthenticated RCE

**Status: IMPLEMENTED AND VERIFIED 2026-10-03.** All three layers landed, both
regression tests go red→green, the full e2e suite is green twice with a clean
byte-tap (118|0, 0 GARBAGE both times). A second, related finding (S5.5,
telnet processing applied to HTTP request bytes) was found and fixed during
implementation — see §11. §9 records how the open decisions were resolved.

---

## 1. Read these first

| Document | What it gives you |
|---|---|
| `.pi/quest/future/axilnd-site-e2e-corruption.md` | The investigation journal: symptom counts, the byte-tap, and every hypothesis that was **disproved** (do not redo them) |
| `.pi/quest/future/axilnd-phase4-gates.md` | Phase 4 gate state and what must be re-run before closing |
| `ST.md` §26, §27, §28 | XY contract, module rules, and the Phase 3/4 record for this work |
| `external/axil/SECURITY.md` | The existing numbered-findings format, if you write this up as a security fix (§9) |
| `external/axil/include/ttypt/axil.h:220-232` | **The contract this fix restores.** Read it before touching `axil_disconnect` |
| `external/axil/src/test-auth.c:14-16, 200` | Existing probes that already assert things about this gate — expect to update them |

If you only read one thing, read §3. The bug is not subtle; it was hard to *see*
because it is a lifecycle bug, not a logic bug.

---

## 2. TL;DR

A PTY and its spawned `sh` live in `struct mux_state`, keyed by **raw file-descriptor
number** in a process-global map. The teardown hook is gated twice — behind
`DF_CONNECTED` **and** `DF_AUTHENTICATED` — so a connection that got a PTY but never
authenticated is **never cleaned up**. The kernel recycles that descriptor to the next
connection, which is normally an ordinary site HTTP request. `axil_tty_input()` then
finds the leaked live shell, **writes the HTTP request into the PTY**, and tells axil to
skip dispatching the request. The client receives its own request echoed back, mangled by
the tty line discipline, plus a shell prompt and `command not found`.

Both gates contradict the documented contract in `axil.h:220-232`, which states the hook
*"fires whenever a descriptor is torn down"*. The code does not do what its own header says.

**This is a security bug, not just test flakiness.** The leaked shell executes the HTTP
headers as shell commands, so anyone who can win descriptor allocation gets command
execution as the server user. Treat the fix as a security fix.

---

## 3. Root cause

### 3.1 The invariant that is violated

`external/axil-tty/src/libaxil-tty.c:122-128`

```c
static struct mux_state *
mux_get(socket_t fd)
{
  if (!mux_map)
    return NULL;
  return (struct mux_state *)corm_get(mux_map, &(uint32_t){(uint32_t)fd});
}
```

`mux_map` is keyed by fd number. Descriptor numbers are recycled by the kernel, so an
entry is only meaningful while that exact connection is alive. The module already knows
this — `libaxil-tty.c:753-755` says so explicitly:

> *"fd numbers are reused, so a geometry entry left behind here would be applied to
> whatever connection lands on this fd next"*

That comment guards `mux_wsz_del` **inside the hook**. The flaw is that the guard can be
skipped entirely, because the hook itself is conditional.

### 3.2 The two gates

`external/axil/src/libaxil.c:440-441` (inside `axil_close`):

```c
if ((d->flags & DF_CONNECTED) && axil_disconnect)
	axil_disconnect(fd);
```

`external/axil/src/axil.c:311-316`:

```c
void axil_disconnect(socket_t fd) {
	if (!(axil_flags(fd) & DF_AUTHENTICATED))
		return;

	on_axil_disconnect(fd);
}
```

`DF_CONNECTED` is set in only two places: the WebSocket upgrade (`libaxil.c:3142`) and
`axil_auth()` (`axil-posix.c:390`, which sets `DF_AUTHENTICATED | DF_CONNECTED` together).
So for a PTY-bearing connection that is neither WebSocket-upgraded nor authenticated —
i.e. every raw-telnet `/tty` connection — **both gates are unset and the hook never runs.**

### 3.3 The failure chain

| # | What happens | Where |
|---|---|---|
| 1 | A PTY + spawned `sh` are attached to an unauthenticated, non-WS descriptor | `libaxil-tty.c:478` (`axil_tty_attach`), reached from `libaxil-nd.c:180` (`on_axil_connect`) and `libaxil-nd.c:299-309` (`RAW_NEGO`) |
| 2 | The client goes away. `axil_close()` (`libaxil.c:400`) reaches `close(fd)` at `:463` but the hook is gated out by §3.2 | `libaxil.c:440`, `axil.c:312` |
| 3 | **PTY, live child process, and the fd-keyed `mux_state` entry leak permanently** | `libaxil-tty.c:750-774` never runs |
| 4 | The kernel hands that descriptor number to the next connection — a site HTTP request | `libaxil.c:804` memsets the new `descr`, so the stale `mux_state` is untouched |
| 5 | `axil_tty_input()` finds `mux_get(fd)` with `pid > 0` and writes the request into the leaked shell's PTY | `libaxil-tty.c:655-658` |
| 6 | It returns `-1`, so `descr_read` **skips dispatching the request** — no HTTP response is ever produced | `libaxil.c:1253` |
| 7 | The tty line discipline echoes the request back (`ICRNL` then `ONLCR` turns every `\r\n` into `\r\n\r\n`), the PTY emits `\x1b[?2004l`, and the shell runs the headers as commands | kernel tty + `sh` |

Step 6 is why some connections return **zero bytes** (`EMPTY` verdicts) rather than
garbage: the request is consumed and never answered.

---

## 4. Evidence

### 4.1 Wire capture (authoritative)

From the byte-tap run — 84 `GARBAGE`, 10 `EMPTY`, 61 tests failed:

```
cid 2753  req: GET /hyle.css?v=c74d2562 HTTP/1.1  (506 bytes)
          rsp: 47 45 54 20 2f 68 79 6c 65 2e 63 73 73 3f 76 3d   "GET /hyle.css?v="
              63 37 34 64 32 35 36 32 20 48 54 54 50 2f 31 2e   "c74d2562 HTTP/1."
              31 0d 0a 1b 5b 3f 32 30 30 34 6c 0d                "1...[?2004l."
```

```
cid 2756  req: GET /song/ HTTP/1.1\r\nHost: localhost:8080\r\nConnection: keep-alive\r\n...
          rsp: GET /song/ HTTP/1.1\r\n\r\nHost: localhost:8080\r\n\r\nConnection: keep-alive\r\n...
```

Three facts fall out of these two records:

1. **The response is the request.** Not random corruption — the client's own bytes.
2. **Every `\r\n` became `\r\n\r\n`.** That is the signature of text passing through a PTY
   line discipline: `ICRNL` turns CR into NL, then `ONLCR` turns each NL back into CRLF.
   Nothing else in this codebase does that.
3. **`\x1b[?2004l` is emitted by no source file.** `grep -rn '2004'` over
   `axil/src`, `axil-tty/src`, `axil-nd/src` returns nothing. It is a terminal-driver /
   readline sequence, so a **real PTY is alive** on that descriptor.

`/tty` requests appear in the server log immediately before the corruption
(`request_handle: 8 GET /tty`, followed by `command_pty: called for cfd=8 args[0]=(null)`).
`/tty` is not referenced by any e2e test or by site JS — which is itself a symptom worth
a follow-up question, but the entries it creates are exactly the leaked ones.

### 4.2 The entry is pre-existing, not created by the failing request

This was measured, not assumed. I added a temporary trace to the `RAW_NEGO` branch
(`libaxil-nd.c:299`), rebuilt, and confirmed the trace string was in the loaded library:

```
strings external/axil-nd/lib/libaxil-nd.so | grep -c NDRAWTRACE   ->  2
```

During a full e2e run that produced **84 GARBAGE**, the trace fired **0 times**. So the
PTY was *not* attached while handling the bad request; it was already on the descriptor.
That is what distinguishes step 3 (leak) from step 1 (fresh misattach), and it is why the
disconnect hook is the fix.

**That instrumentation has been removed and `libaxil-nd.so` rebuilt clean.** Verify with
`strings external/axil-nd/lib/libaxil-nd.so | grep NDRAWTRACE` → expect no output.

### 4.3 The close path is otherwise sound

- `axil_close()` (`libaxil.c:400`) is the single funnel for a live descriptor; the only
  other `close(fd)` calls (`:780`, `:820`, `:864`) are pre-accept `descr_new` failure paths
  where no per-connection state can exist.
- Only one `libaxil-tty.so` exists (`external/axil-tty/lib/`), so there is no
  vendored-copy shadowing problem at runtime — checked, because
  `external/axil-nd/node_modules/@tty-pt/axil-tty/` contains a second source copy.
- The server does **not** leak: 3,600 requests at concurrency 8 moved RSS 447 MB → 450 MB
  with fds flat at 6 → 8. Do not use RSS as the regression signal; use the tap verdicts.

### 4.4 Ruled out — do not re-investigate

- **`RAW_NEGO` misattach** — instrumented, 0 hits (§4.2).
- **The `/tty` WebSocket route / SIGPIPE crash** — SIGPIPE **is** ignored at rest
  (`/proc/<pid>/status` → `SigIgn: 0x1005` = SIGHUP|SIGQUIT|SIGPIPE) and `axil_close()` does
  reach the hook. The "server died" observations were the sandbox reaping backgrounded
  processes.
- **XY multi-module dispatch** — XY runs **every** implementing module in load order
  (nd first, axil-tty last); `axil.c:337` returns the *last* module's value. Both disconnect
  handlers always run, so dispatch arbitration is not the problem.
- **Stale `d->head_len`** — `descr_new` memsets the descriptor (`libaxil.c:804`).
- **axil-tty hijacking foreign sockets** — `axil_tty_input` returns `-1` only for a
  descriptor it owns with a live PTY (`:655`).
- **Concurrency / volume / single endpoint** — the bug reproduces serially.

---

## 5. The fix

### Layer 1 — remove both gates (this is the actual bug fix)

**1a.** `external/axil/src/libaxil.c:440` — drop the `DF_CONNECTED` condition:

```c
-	if ((d->flags & DF_CONNECTED) && axil_disconnect)
-		axil_disconnect(fd);
+	if (axil_disconnect)
+		axil_disconnect(fd);
```

**1b.** `external/axil/src/axil.c:311-316` — drop the `DF_AUTHENTICATED` early return:

```c
 void axil_disconnect(socket_t fd) {
-	if (!(axil_flags(fd) & DF_AUTHENTICATED))
-		return;
-
 	on_axil_disconnect(fd);
 }
```

Both changes restore the contract already written at `axil.h:220-232` (*"Fires whenever a
descriptor is torn down, not only when a peer hangs up"*).

**Why this is safe.** Every hook implementation already tolerates being called for a
descriptor it does not own:

- axil-tty `on_axil_disconnect` (`:750-759`) returns `0` when `mux_map` is NULL or
  `mux_get(fd)` finds nothing.
- nd `on_axil_disconnect` returns early unless its own session is authenticated.

That self-gating is exactly what the header means by *"a module cannot tell an
authenticated-but-pty-less connection from any other — a hook here must tolerate a missing
pty and a missing upstream"*. The gates therefore bought nothing, while leaking a shell.

**Expect to update `external/axil/src/test-auth.c`** — `:14-16` and `:200` assert on this
gate and will need their expectations rewritten, not deleted.

### Layer 2 — stop trusting the bare fd number (closes the bug *class*)

Layer 1 fixes this instance. Layer 2 makes *any* future missed-cleanup path harmless
instead of exploitable. Recommended, but see §9 decision 1.

1. In `struct descr` (`libaxil.c`), add `unsigned long long generation;`
2. Add a file-static `unsigned long long conn_generation;` next to `descr_map`, and in
   `descr_new()` (`:768`, after the `memset`) set `d->generation = ++conn_generation;`
3. Expose it. Preferred: a new XY hook in `include/ttypt/axil-xy.h` +
   `XY_DEF` in `src/axil.c`, mirroring the existing block at `:117-127`:
   ```c
   XY_DEF(unsigned long long, axil_conn_generation, socket_t, fd);
   ```
   Fallback if you would rather not widen the hook ABI: set it in `descr_new()` via
   `axil_env_put(fd, "CONN_ID", ...)`, which reads back with `axil_env_get`.
4. In axil-tty, add `unsigned long long gen;` to `struct mux_state`, populate it in
   `mux_put()`, and make `mux_get()` validate:
   ```c
   static struct mux_state *
   mux_get(socket_t fd)
   {
     if (!mux_map)
       return NULL;
     struct mux_state *s = corm_get(mux_map, &(uint32_t){(uint32_t)fd});
     if (s && s->gen != current_generation(fd))
       return NULL;          /* stale: this fd number is a different connection */
     return s;
   }
   ```
5. Update every `mux_get` caller: `libaxil-tty.c:243` (`mux_ensure`), `:348`
   (`mux_ensure_pty` path), `:468`, `:474`, `:486` (`axil_tty_attach`), `:605`
   (`axil_tty_input`), `:757` (`on_axil_disconnect`). Apply the same treatment to
   `mux_wsz_get`/`mux_wsz_put` (`:130-152`), which is the map the existing
   `:753-755` comment was worried about.

Do this **after** Layer 1 is green, as its own commit.

### Layer 3 — fail loud, never write to a PTY you do not own

In `axil_tty_input` (`:655-658`), when refusing to write, emit one diagnostic per
descriptor under an opt-in env var so a residual case is visible in the log instead of
being a silent corruption:

```c
if (s && s->pid > 0 && i < nread) {
    if (getenv("AXIL_TTY_TRACE"))
        fprintf(stderr, "axil_tty: fd=%d handing %d bytes to pty %d\n", fd, nread - i, s->pty);
    write(s->pty, input + i, nread - i);
    return -1;
}
```

The real assertion to add is ownership: refuse when the descriptor is not a terminal
connection for this module. With Layer 2 in place the generation check *is* that
assertion, which is why 3 is cheap once 2 exists.

---

## 6. Regression test

Add to `external/axil-nd/test.sh` — cheap, no browser, no Playwright, ~1 s. Place it next
to the existing raw-telnet block (`:634`).

The scenario that must fail today: attach a shell to a connection that never
authenticates, close it, then send plain HTTP on the recycled descriptor.

1. Open a TCP socket to the nd port, send the terminal-negotiation bytes that make
   `axil_tty_attach` + a spawned shell happen on an **unauthenticated, non-WS** descriptor
   (mirror what the `/tty` route does; do not authenticate).
2. Assert the shell exists (the run should see `command_pty` / the spawn) — otherwise the
   test is not exercising the bug.
3. Close the socket.
4. **Assert the invariant directly**: no `mux_state` survives for that fd. Cheapest
   observable proxy — immediately issue plain HTTP requests on freshly opened sockets in a
   loop (a few dozen, to force descriptor reuse) and require that **every** response
   begins with a valid status line, and that no response contains `?2004`, a shell prompt,
   or `command not found`.
5. Also assert the exit is clean: no orphaned `sh` child for that connection
   (`pgrep -P <axil-pid>` before/after), since step 2 of §3.3 leaks the child too.

Before implementing the fix, confirm the test **fails**; after, confirm it passes. A
regression test that never went red is not evidence.

---

## 7. Verification gate

Run in this order; do not skip to the full suite.

```sh
cd external/axil-nd && make && ./test.sh          # expect: axil-nd ok
./test.sh && ./test.sh                # 3 consecutive green; 33 warnings are pre-existing
cd ../axil && make && cd ../..
cd ../.. && make                                 # site build
sh scripts/check-module-boundaries.sh
sh scripts/check-wasm-imports.sh
sh scripts/check-no-site-specific-js.sh
sh scripts/check-ux-purity.sh                     # whichever four G6 scripts apply
```

Then the full suite, twice, with `axil-nd` present in `mods.load` (**never** remove it to
make this pass):

```sh
DENO_JOBS=4 make test
DENO_JOBS=4 make test
```

With the byte-tap in front (`8080 → 8081`, `/tmp/opencode/tap.py`) the acceptance signal
is **0 `GARBAGE`, 0 `EMPTY`** — deno's own pass/fail is noisier and can pass while
responses are still corrupt. Re-create the tap if `/tmp` was cleared; it is described in
`.pi/quest/future/axilnd-site-e2e-corruption.md`.

Definition of done:

- [x] Layer 1 landed, `test.sh` green ×3 — **done**: axil suite green,
  `axil-nd/test.sh` green (multiple runs, exit 0)
- [x] Regression test in `test.sh` fails before / passes after — **done** for
  both S5.4 (live-child 1→0) and S5.5 (banner→HTTP), each shown red on the
  vulnerable build and green on the fixed one
- [x] Full e2e green twice, tap clean — **done**: run3 and run5, `118 passed |
  0 failed`, tap `0 GARBAGE / 0 BADLINE` both times (was 84 GARBAGE pre-fix).
  Two intermediate runs failed only on picker-store pollution (see §11), wire
  clean in all runs.
- [x] `ST.md` updated (root cause + fix) — §29
- [x] `SECURITY.md` written (§9 decision 3)
- [x] Quest journal updated; no debug instrumentation anywhere
  (`grep -rn 'TTYTRACE\|NDRAWTRACE\|S54TRACE' external/` → clean)
- [ ] Committed

---

## 11. Found during implementation: S5.5 (telnet processing of HTTP bytes)

After Layer 1–3 the suite went from 61 failures to 1: `song-add-invalid-utf8`
("invalid HTTP version parsed"), with 1 tap GARBAGE whose response was the nd
raw-telnet banner answering a `POST /song/add`. Deterministic, not a race
(6/6 clean-body → HTTP, 6/6 `0xFF`-body → banner).

Mechanism: every read chunk passes through `on_axil_parse` before dispatch, and
`axil_tty_input()` scanned the *whole* chunk — head plus body — for `0xFF`. A
`0xFF` body byte read as IAC, the return value made nd slide the request head
off the front of the input, and the RAW check then fired on the remainder.
`buffer_post_body()` reads segmented bodies directly off the socket (bypassing
the hooks), so the single-chunk head+body case was the whole bug.

Fix: a chunk that opens with an HTTP request line passes through untouched —
no IAC scan, no slide, no RAW classification (`on_axil_parse` in
`external/axil-nd/src/libaxil-nd.c`; same gate for non-WebSocket connections in
axil-tty's own `on_axil_parse`, since shell bytes ride WS frames and must still
reach the PTY). A raw stream never opens with `METHOD SP` and WS payloads are
handled on their own branch, so neither path changes behavior.

Regression test: `axil-nd/test.sh` §S5.5 posts a multipart body with `0xFF`
bytes and requires an `HTTP/1.1` status with no banner. Shown red (non-HTTP
response) with the gates reverted, green with them in.

## 12. Suite-isolation note (not a product bug)

Full e2e runs pollute `var/song.types` with junk picker entries, and the *next*
run fails six song-type/picker tests on the dirty store ("run
scripts/gc-picker-junk.sh"). Both green runs above were preceded by that
cleanup (72→36 and 76→36 entries). The wire was clean in every post-fix run
regardless. Making the suite hermetic (per-run store or post-run cleanup) is
open work, unrelated to this fix.

---

## 8. Operational notes (these will bite you)

- **Memory.** `tests/e2e/helpers/browser.ts:18` launches **one Chromium per Deno worker**,
  closed only on `unload`. `deno test --parallel` with no cap runs one worker per CPU —
  **22 here** — so ~22 browsers ≈ 7 GB. Two earlier attempts were killed by the OOM killer.
  `DENO_JOBS` **is** honoured by deno. `Makefile:177` already used it; `Makefile:181`
  (`e2e-tests`) and `Makefile:238` (`test-capture`) now export `DENO_JOBS=$${DENO_JOBS:-4}`
  too. **Keep those edits.** Never pair a sanitizer with the browser suite.
- **Add a floor to any ad-hoc runner**: abort if `/proc/meminfo` available < 1400 MB.
- **Kill by PID, never `pkill -f <pattern>`** — if the pattern appears in your own
  `bash -c` command line, `pkill -f` kills your own shell and the tool call hangs. Use
  `ss -ltnp` / `pgrep` and kill by PID.
- **Backgrounded servers get reaped by the harness between tool calls.** A server that
  "died" between calls is usually an artifact, not a crash. Start the server *inside* the
  same invocation as the work that uses it.
- The tap emits on connection close, so browser keep-alive connections are undercounted.
  Its `GARBAGE`/`EMPTY` counts are still valid; its `OK` count is not a total.

---

## 9. Open decisions — resolved during implementation

1. **Layer 2 in or out?** Included, separate commit after Layer 1. The
   `axil_generation()` accessor went in as a plain core function (same pattern
   as `axil_flags()`), not a new XY hook — no hook-ABI change was needed.
2. **Audit scope.** Kept strictly to the two gates plus the S5.5 bypass below.
   No sweep of `on_axil_tick` / `axil_fd_unwatch` / WebSocket teardown.
   One adjacent leak was found and deliberately left open (documented, not
   fixed): axil never `waitpid()`s a PTY child, so a correctly-killed shell
   lingers as a zombie — PID-table pressure only, harmless to correctness, and
   the regression test counts live children only for exactly this reason.
3. **Disclosure.** `external/axil/SECURITY.md` created with S5.4 and S5.5. (No
   such file existed; the S-series numbers continue the audit series cited in
   the tree. Only our two findings are recorded there — earlier entries live
   upstream and were not reconstructed.)

---

## 10. Final tree state (as committed)

| Path | Change |
|---|---|
| `external/axil` | Layer 1 (both gates + comments), Layer 2 (`generation` field, counter, `axil_generation()` + header doc), `test-auth.c` comments, `test.sh` inverted assertion rewritten, new `SECURITY.md` (S5.4, S5.5) |
| `external/axil-tty` | Layer 2 (`gen` in `mux_state` + `mux_wsz` wrapper, validated accessors, stamped puts), Layer 3 (`AXIL_TTY_TRACE`), S5.5 HTTP gate, `-I../axil/include` so the module builds against the library it loads |
| `external/axil-nd` | S5.5 HTTP gate, S5.4 regression section (own no-`-A` server), S5.5 regression case, in-tree `PATH`/`LD_LIBRARY_PATH` + build-axil-and-tty preamble in `test.sh` |
| `Makefile` (site) | `DENO_JOBS` caps at `:181` and `:238` — intentional, keep |
| `FIX.md`, `ST.md` | this record |
| `.pi/quest/**` | gitignored |

Baseline commits: site `f8441f1`, `external/axil-nd` `85af699`.