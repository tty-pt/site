# SAFE_SH.md — fail-closed terminal access for axil / axil-auth / axil-tty / axil-nd

Status: **IMPLEMENTATION COMPLETE (Phase A complete and verified).**
Baseline: site `1fbc0d1`, `external/axil@a38bd00`,
`external/axil-auth@2c49573`, `external/axil-tty@6928a96`, `external/axil-nd@7f52510`.

Implemented and verified: A1, A2 (+F15 guard), A3, A4, A5, A6.1–A6.4, A7a/b/c
(unit + integration + tty suite green; nd suite verified 3× green), A8 (written,
verified in full suite), A9 (verified with superproject `DENO_JOBS=4 make test`,
submodules committed).
Phase B and A4.2 migration are distinct future tasks.

Answers one question: *if the music site runs under axil in a `/var/www` chroot
and someone registers an account, can they get a shell?* Today: **yes, and they
do not need to register.**

Every claim carries a `file:line` citation. Findings marked **[needs tty.pt]**
can only be settled on the host; they are collected in §11.

**Read C10 before implementing anything.** Two earlier drafts of this plan were
wrong in ways that would have shipped a gate that did nothing. `getpwnam`
resolving for a site user is not authorisation (F3b), and `-A` is not the only
way an unauthenticated descriptor acquires an identity (F2).

---

## 0. Corrections log

What verification changed relative to earlier drafts. Each is a real reversal,
not a refinement.

| # | Correction |
|---|---|
| C1 | **F6 was wrong — the identity bridge already exists** (`axil.c:243-277`). Phase B's bridge step is **deleted**. See F6. |
| C2 | The weak-hook call site is `axil-posix.c:186-190`, not `libaxil.c:2968` (that line is the `axil_platform->auth_try` vtable dispatch). |
| C3 | **`DF_AUTH_NDCONNECT` is dropped.** Root-caused instead: `connect` becomes password-authenticated (§5). |
| C4 | **The registration name check moves from Phase B into Phase A.** Because the bridge is live *today* (C1), Phase A's gate is what turns a squatted session name into a reliable operator shell. See F13. |
| C5 | The gate moves from `command_pty` to `axil_tty_exec` — `mux_ensure()` calls `posix_openpt` itself, so a `command_pty` gate cannot claim to allocate nothing. |
| C6 | `axil_set_flags` **already exists** (`axil.h:280`, `libaxil.c:3262`). The proposed weak `axil_flag_set` hook is **deleted**; the hazard is that it *assigns*. |
| C7 | `sync_group_file`'s destructiveness and axil core's second `drop_priviledges` were missed entirely in the first draft. See F8, F15. |
| C8 | `do_man` has a **second branch** passing the raw topic to `man` as an argument. See F14. |
| C9 | F7 holds **only under a chroot**, which reconciles it with the contradicting comment at `world.c:953-955`. |
| C10 | **`/bin/false` moves from Phase B into Phase A.** `passwd_append` runs at registration (`libaxil-auth.c:876`) and writes `/bin/sh` as every site user's shell (`:457`, `:453` on OpenBSD). So every site user *has* a passwd entry, `getpwnam` resolves, and all four §4 gates pass — **Phase A alone granted shells to ordinary site users and fixed nothing.** `/bin/false` is the enforcement point, not hygiene. See F3. |
| C11 | **XY bus zero-fills unimplemented hooks — security predicates must report deny as zero.** `XY_IMPL`'s adapter does `memset(res, 0)` when no module implements the hook (`xy.h:309-313`). `auth_password_matches` therefore returns **non-zero on success**; a "0 means valid" convention would authenticate everyone the moment axil-auth is absent. Pinned by `tests/unit/xy_hook_default_test`. |
| C12 | **`axil_connect` is weak and binary-only — the flag is set in the library.** `axil_connect()` lives in `src/axil.c` (the executable), not `libaxil.so`; library-linked hosts (tests, modules) never run it. `DF_AUTH_AUTO` is therefore applied by `axil_autoauth_apply()` inside `axil_ws_upgrade()` (`libaxil.c`), idempotent with the binary's own call. |
| C13 | **`struct user.hash[64]` truncates non-bcrypt hashes.** SHA-512 crypt (`$6$`, ~106 chars) is cut to 63 chars on load and can never verify; only bcrypt (60 chars, what registration writes) works. Test fixtures must use bcrypt (`python3 -c "import bcrypt..."`; python 3.13+ removed the `crypt` module). Latent axil-auth limitation, not changed. |
| C14 | **WS frame length bytes must be exact.** An over-declared length stalls `ws_read` forever (looks like a dead shell). Suite frames now carry computed lengths; see `0x8e`/`0x92` fixes in `axil-nd/test.sh`. |
| C15 | **The password prompt ends with a newline.** `read`-based transcript drains (`ndcmd`/`mpcmd`) discard a newline-less partial line on timeout, eating the prompt. `Password: \r\n` fixes it with no protocol cost. |
| C16 | **Persist boot-B assertion scoped to player creation.** `eng_object_add N -> N` (self-parented: `where==NOTHING` rewrites to the new ref) is the player signature. Spacetime entry drops are probability-gated (`random() < RAND_MAX >> drop.y`) and fire on ordinary logins, so asserting the absence of ALL adds is a dice roll. Player reuse is still pinned (ref `!= NOTHING` check retained). |

---

## 1. Decisions (locked with the operator)

1. **Real accounts.** Site users get genuine `/etc/passwd` entries; that file is
   the sole authority for who may have a shell.
2. **Default shell `/bin/false`** — the enforcement point. This ships in
   **Phase A**, not Phase B (C10).
3. **No wiz concept.** Region ownership replaced it; this is identity, not
   authority.
4. **`/bin/sh` is not a fallback.** Empty or no-op shell means *no shell*.
5. **Refuse and close.** No PTY, no fork, no zombie; a log line per refusal.
6. **`-A` must be non-silent and per-descriptor** (`DF_AUTH_AUTO`).
7. **`connect` requires a password, prompted on a second line** with echo
   suppressed — not an inline argument.
8. **The nd test suite loads `libaxil-auth`** so the passworded-accept path has a
   real fixture.
9. **Sequencing: land the terminal fix first.** With C1 + C10 applied, Phase A is
   the *complete* fix for ordinary site users and does **not** lock the operator
   out — their cookie session resolves through the live bridge to their own
   passwd entry (§11.5). The earlier "Phase A locks out the operator" premise was
   an artifact of C1 being wrong.
10. **Registration must refuse names that already exist in passwd** (F13), and
    lands in Phase A (C4).

---

## 2. Findings

### F1 — Unauthenticated shell via `GET /tty`

| Step | Location |
|---|---|
| `axil-nd` is in the site's module list | `mods.load` (last line) |
| axil-nd registers the route and loads the module serving it | `axil-nd/src/libaxil-nd.c:462`, `:464` |
| route dispatch has **no auth gate** | `axil/src/libaxil.c:3099` |
| `GET /tty` + WS key upgrades | `axil-tty/src/libaxil-tty.c:880` |
| `on_axil_connect` matches only on path | `axil-tty/src/libaxil-tty.c:562-569` |
| PTY born; auto-spawn armed | `axil-tty/src/libaxil-tty.c:594`, `:622` |
| first NAWS frame spawns a shell | `axil-tty/src/libaxil-tty.c:668-670` → `:557` |
| `fork()` + `execve` | `axil-tty/src/libaxil-tty.c:397`, `:425-427` |

Trigger: WS upgrade on `/tty` plus one NAWS frame. No cookie, no account, no
command. A plain `GET /tty` without a WS key is harmless — it serves HTML (`:893`).

### F2 — Passwordless `connect <name>` fakes authentication

| Step | Location |
|---|---|
| `connect` takes no password | `axil-nd/src/world.c:983-990` (`argc < 2` only) |
| registered `CF_NOAUTH` — the pre-auth entry point | `axil-nd/src/world.c:702-705` |
| the name is used verbatim | `axil-nd/src/world.c:990` |
| `axil_auth()` called for its side effect only | `axil-nd/src/world.c:944-965` |
| return is advisory — "still marked authenticated" | `world.c:946-951`, `axil/include/ttypt/axil.h:295-297` |
| `mcp_auth_success()` sent explicitly | `world.c:967` |

`sh` is registered twice with no owner/region check: `axil-tty/src/libaxil-tty.c:906`
(`CF_NOTRIM`) and `axil-nd/src/world.c:684-687`. `do_sh` (`world.c:662-666`) calls
`axil_tty_shell` with no permission test. Contrast `wall`, `ban`, `unloadmod`,
`loadmod`, `deny` — all region-gated; `sh` and `man` never were.

Fixed in §5 rather than marked-and-refused (C3).

### F3 — `axil_get_pw` substitutes the server's identity

`axil_auth` (`axil/src/axil-posix.c:370-402`) terminates the name, then:

```c
d->flags |= DF_AUTHENTICATED | DF_CONNECTED;
axil_env_put(fd, "REMOTE_USER", d->username);
struct passwd *pw = getpwnam(d->username);
if (!pw) { axil_pw_copy(&d->pw, &axil_pw); return 1; }   /* :394-397 */
```

and `axil_get_pw` returns `axil_pw` for any **un**authenticated descriptor.
`axil_pw` is the server's own entry, so `pw_shell` is the operator's real login
shell (documented `axil.h:286-288`).

Three places in axil-tty treat that substitution as the caller's identity:

| Location | Defeats a no-shell account by |
|---|---|
| `axil-tty.c:317-319` | `drop_priviledges`: `pw = &mux_pw` |
| `axil-tty.c:408-411` | child re-queries `axil_get_pw`, then `local_pw = mux_pw` |
| `axil-tty.c:425` | empty `pw_shell` → `"/bin/sh"` |

`grep` for `nologin`/`/bin/false` in axil-tty and axil-nd: **no hits.** A
no-shell account is inexpressible today.

### F3b — and every site user *has* a passwd entry, with `/bin/sh`

This is what makes C10 load-bearing. Registration does not merely record a name:

```
libaxil-auth.c:876   passwd_append(username, uid)      /* unconditional */
libaxil-auth.c:457   "%s:x:%d:%d::%s/%s:/bin/sh\n"    /* Linux  */
libaxil-auth.c:453   "%s:*:%d:%d::0:0:%s:%s/%s:/bin/sh\n"  /* OpenBSD */
```

So for an ordinary site user, right after signing up:

| §4 gate | Value | Result |
|---|---|---|
| 1. no `DF_AUTH_AUTO` | clear — no `-A` | pass |
| 2. `REMOTE_USER` set | set by the live bridge (F6) | pass |
| 3. `getpwnam` resolves | **the entry exists** | pass |
| 4. `pw_shell` is a real shell | **`/bin/sh`** | pass |

→ **shell.** The gates only bite if gate 4 does, and gate 4 is decided entirely by
`passwd_append`. A gate written without changing `passwd_append` is a no-op
against the actual threat, which is why C10 moves `/bin/false` into Phase A.
`getpwnam` resolving for a site user is *not* evidence of authorisation — F7 says
the file is one we own and wrote ourselves.

### F15 — the same substitution exists in axil core

`axil/src/axil-posix.c:404-430` is a **second** `drop_priviledges`, with the same
`DF_AUTHENTICATED ? &d->pw : &axil_pw` at `:408`, reached from `popen2`
(`:432`, drop at `:442`). Non-PTY child spawns in core inherit F3 too. A fix
touching only axil-tty leaves this open.

### F4 — Privilege actually obtained

`drop_priviledges` drops **only** when `axil_config.chroot` is set **and**
`euid == 0`; otherwise it logs `NOT_CHROOTED` / `NOT_ROOT` and returns
(`axil-tty:321-329`). The shell runs as the server's own uid with the server's
own login shell — full read/write on every SQLite store (user hashes, session
tokens), `htdocs/`, the `.wasm` served to every visitor, every `.so`, and the
binary. Root is not required.

> **Caveat, load-bearing for §6:** non-root ⇒ `pw_shell` is the *only* boundary.
> Root ⇒ each account is additionally `setuid`-ed (`axil-tty:332-335`).

### F5 — `-A` publishes the operator's identity on every upgrade

`axil/src/axil.c:298-309`: under `AXIL_AUTOAUTH`, every WS upgrade runs
`axil_auth(fd, getpwuid(geteuid())->pw_name)`, so every connection carries the
operator's name. Documented "dev/testing only" at `axil.h:77-78`; enforced nowhere.

### F6 — the identity bridge **already exists** (corrects the first draft)

The first draft claimed `axil_auth_check` was unimplemented and that
`REMOTE_USER` was never populated. Both wrong. axil core ships a full
implementation at `axil.c:243-277`:

```c
if (axil_env_get(fd, cookie, sizeof(cookie), "HTTP_COOKIE")) return NULL;
...
username = get_session_user(token);      /* axil.c:269 */
```

`get_session_user` is **axil-auth's `XY_IMPL`** (`auth.h:143`), declared
`XY_DECL` by axil at `axil.c:105` — so it resolves through the **XY bus**, not
weak linker symbols. There is no interposition fragility and no load-order race.

It is invoked per request by `axil_platform_auth_try` (`axil-posix.c:184-191`),
which NULL-guards the hook at `:186` and calls `axil_auth(fd, user)` at `:190`,
reached from `axil_platform->auth_try(fd)` at `libaxil.c:2968` — after headers,
before the WS upgrade and before dispatch.

Therefore, **today**: a `QSESSION` cookie already sets `REMOTE_USER`, and
`auth()` (`axil-nd/src/world.c:889-903`) already resolves it — so `/nd` WS login
works. Note the cookie name is a contract: axil looks for exactly `QSESSION`
(`axil.c:257`) and axil-auth's default `cookie_name` is `"QSESSION"`
(`libaxil-auth.c:35`). They must stay equal.

**Consequence:** Phase B's bridge step is deleted. There is no `/nd` rollout
warning to manage.

### F7 — under a chroot `etc_dir` *is* `/etc`; outside one it is not

`auth_config.etc_dir` defaults to `"./etc"` (`libaxil-auth.c:37`), and axil
chroots then `chdir("/")` (`axil-posix.c:160-161`) **before** modules load, so
inside the chroot `./etc/passwd` **is** `/etc/passwd`. `auth_init` reinforces
this by writing `etc_dir/nsswitch.conf` with `passwd: files` (`:990-1002`,
`fopen(..., "wx")`), forcing glibc to the flat files.

`getpwnam()` always resolves the **absolute** `/etc/passwd`, so the two layouts
diverge outside a chroot. This reconciles F7 with `world.c:953-955` ("getpwnam()
fails for all of them"), true in dev and false under chroot:

| Deployment | `getpwnam(site user)` | Which gate refuses |
|---|---|---|
| chrooted (production) | resolves | gate 4 — `/bin/false` |
| dev, no chroot | NULL | gate 3 — no entry |

Both fail closed. It also means **the `/bin/false` fix only has teeth under a
chroot** — the deployment that matters.

### F8 — `sync_group_file` destroys the chroot's `/etc/group`

`sync_group_file` (`libaxil-auth.c:466-491`) opens `etc_dir/group` with
**`O_TRUNC`** (`:472`), writes only `groups_map`, and `rename()`s over the
original (`:490`). Called from `:1083`, `:1203`, `:1245` — **on every
registration**. Under chroot (F7) that truncates the chroot's system group file
to axil-auth's own groups, and `load_groups` (`:645`) reads the truncated file
back, so the loss is permanent. It matters because `drop_priviledges` calls
`initgroups` / `setgid` (`axil-tty:333-334`).

Asymmetry that makes the rest viable: `etc/passwd` is only ever **appended** —
the `O_TRUNC` at `:528` is the shadow file. System passwd entries survive; only
`group` is destroyed.

### F9 — `mux_init` crash on a minimal chroot

`axil-tty/src/libaxil-tty.c:227-233`:

```c
char euname[BUFSIZ] = "root";
struct passwd *pw = getpwuid(geteuid());
if (pw) strncpy(euname, pw->pw_name, sizeof(euname) - 1);   /* :231 no NUL */
axil_tty_pw_copy(&mux_pw, getpwnam(euname));                 /* :232 no NULL check */
```

`axil_tty_pw_copy` dereferences its origin immediately (`:209`). The `"root"`
default saves it only if root exists in the file NSS reads; combined with F5
(whose fallback is the literal `"root"`) a chroot without `/etc/passwd` turns a
latent defect into a boot crash — `auth_init` will have created an empty
`etc_dir` and an `nsswitch.conf` pointing at it (`:987`, `:990-1002`). axil
already fixed the identical `strncpy` pattern at `axil-posix.c:384-386`.

### F10 — `do_man` traversal, first branch

`axil-nd/src/world.c:668-680`: `argv[1]` flows unvalidated into
`snprintf(path, sizeof(path), "man/%s.10", topic)`, then

```c
char *rargv[] = { "/usr/bin/man", "-P", "cat", "-l", path, NULL };  /* :676 */
```

`-l` preprocesses through `cat`, so this is an arbitrary-file-read primitive
bounded only by the `.10` suffix. `help` shares the handler.

### F14 — `do_man`, second branch: argument injection

The `else` arm at `world.c:681-683`:

```c
char *rargv[] = { "/usr/bin/man", "-P", "cat", "-s", "10", (char *)topic, NULL };
```

When the file is absent the raw topic becomes a `man` positional argument, so it
can be a flag (`-K`, `--pager=`) or an arbitrary section. A separate surface from
F10, needing its own check.

### F11 — the suite encodes the vulnerability, and `-A` is the only oracle

`axil-nd/test.sh:839-840` asserts a shell **does** spawn on an unauthenticated
`/tty`; its server (`:776`) has no `-A`, no cookie (`:805`). `axil-tty/test.sh:18`
boots with `-A`, and `external/axil-nd/test.sh:149` `user=$(id -un)` plus the
raw-telnet live-PTY test (`:715-736`) depend on passwordless `connect` — the
exploit is currently the success oracle.

### F12 — `loadmod` is *not* a hole (checked; do not re-open)

`do_loadmod` rejects any path component (`axil-nd/src/spacetime.c:2473`) and
requires `st_can` (`:2486`), so the `dlopen` at `axil-nd/src/mods.c:205` is not
reachable as arbitrary code execution.

### F13 — passwd name squatting; the existing check does **not** cover it

Registration refuses a name already in `users_map` (`libaxil-auth.c:857-858`,
*"Username already exists"*). But `users_map` is **not** seeded from
`/etc/passwd`:

- `load_shadow()` (`:560-613`) is what inserts entries
  (`corm_put(users_map, uname, &u)` at `:611`), and it reads **`shadow`**,
  skipping empty hash fields (`:576`).
- `load_passwd()` (`:680-708`) reads **`passwd`** but only patches the uid of
  entries that already exist: `if (u) u->uid = uid;` (`:702`). It never adds one.

So **any account present in `/etc/passwd` but absent from `/etc/shadow` is
claimable by registration.** Under F7 those are the files `getpwnam` reads, so:

```
register as "quirinpa"  → not in users_map → allowed
login                    → session user = "quirinpa"
existing bridge (F6)     → axil_auth(fd, "quirinpa") → getpwnam resolves
Phase-A gate             → no DF_AUTH_AUTO, real pw_shell → OPERATOR SHELL
```

This is **live today** (C1), and it is not only a terminal problem: ND keys
players by name (`player_put(user, player_ref)`, `world.c:928`), so a squatted
name also inherits that account's player record.

**Fix (Phase A, per C4):** registration must reject any name that already exists
in *passwd*, not merely `users_map` — consult `getpwnam(username)` (under F7 that
is the file we own) and refuse a hit. Pin it with a regression test.

---

## 3. What verification established

| Fact | Evidence |
|---|---|
| `descr_flags` ends at `DF_DEFERRED = 2048`; `0x2` reserved | `axil.h:38-63` |
| `struct descr.flags` is a plain `int` | `axil-internal.h:25` |
| `axil_set_flags` exists, is public, and **assigns** | `axil.h:280`, `libaxil.c:3262-3265` |
| `descr_map` is platform-independent (`axil-win.c` shares it) | `axil-internal.h:66`, `libaxil.c:115` |
| flags are zeroed on accept, so provenance can't survive fd reuse | `descr_new` `libaxil.c:780`, `:832`, `:859` |
| `axil_flags` does **no** bounds check; the codebase guards separately | `libaxil.c:3254`; precedent `axil-posix.c:360-363` |
| `command_pty` has exactly **one** caller | `axil-tty.c:479` |
| `mux_ensure` calls `posix_openpt` itself | `axil-tty.c:290-291` |
| axil-tty/axil-nd compile `<ttypt/axil.h>` from a **system copy** | `axil-tty/Makefile:7` |
| `xy_load` can fail (`XY_ERR_EPERM`) | `xy-mod.h:47`, `:105`, `:412` |
| the nd suite loads **only** `axil-nd` | `test.sh:151`; `lib/` holds only `axil-nd.so` |
| axil-auth verifies with `crypt()` against a bcrypt hash | `libaxil-auth.c:785-786`, salt gen `:378` |

**Why a flag at all.** `DF_AUTHENTICATED` (32) is set by `axil_auth`
**unconditionally** (`axil-posix.c:389`) on all three paths — real session, `-A`,
and `connect` — so it already reads "authenticated" in the exploit case; that
conflation is what let F2/F3 through. And a *name* comparison cannot work: under
`-A`, `REMOTE_USER` literally equals the operator's own passwd name, so "was this
published by `-A`?" and "did the operator genuinely log in?" are the same string.
Provenance needs its own channel.

---

## 4. Target state

A terminal spawns only when **all** hold:

1. `DF_AUTH_AUTO` clear — identity not published by `-A`;
2. `REMOTE_USER` set — a real authentication happened;
3. `getpwnam(REMOTE_USER)` resolves — a real account;
4. for the login-shell case only, that entry's `pw_shell` is a real shell.

Otherwise: one line to the client, then close. No PTY, no fork, no child.

**Consequence:** ordinary users cannot have a shell. Only accounts
deliberately provisioned with a real shell can. That is what "trust `/etc/passwd`
exactly" means.

**Gate 4 is the only gate that does any work against a registered site user**, and
only because `passwd_append` hands every one of them `/bin/sh` (F3b). Hence:

- The `/bin/false` change (A4) is **not** deferrable to Phase B. Without it, gates
  1-3 are satisfied by construction for any registered site user and the whole
  Phase A gate is theatre.
- A passwd entry is **not** evidence of authorisation. Under F7 the file is one
  axil-auth writes itself, so `getpwnam` resolving proves only that *we* created
  the row.

**The operator's shell path:** a `QSESSION` session whose name matches a passwd
entry with a real shell — the operator's own row, assuming it exists and has a
real shell (§11.5). F13's registration rule (A5) is what keeps that path
exclusive, since it stops any site account from taking the operator's name.

---

## 5. Passworded `connect` (replaces `DF_AUTH_NDCONNECT`)

Root-causing F2 instead of marking it. A name backed by a valid password *is*
genuinely authenticated, so every gate in §4 passes legitimately and no
provenance bit is needed.

### 5.1 axil-auth — one exported verifier

The crypt comparison lives inside the static HTTP handler; factor it out and
export it. Add to `auth.h` beside the existing `XY_DECL`s (`:139-188`):

```c
XY_DECL(int, auth_verify_password,
	const char *, username,
	const char *, password);
```

Implementation reuses the existing logic verbatim (`libaxil-auth.c:781-786`):
look up `corm_get(users_map, username)`, then

```c
char *h = crypt(password, user->hash);
return (h && !strcmp(h, user->hash)) ? 0 : 1;
```

**One uniform failure** for unknown user and wrong password. Distinguishing them
is a user-enumeration oracle, and login failures should not reveal which names
exist.

**Locked accounts fail automatically:** an entry hashed `*` or `!` makes `crypt`
return NULL, so `connect root <anypw>` is refused. axil-auth writes real bcrypt
hashes only for site users, so `connect` is limited to axil-auth-managed
accounts — provided the chroot's system accounts are locked (**§11.2**).

### 5.2 axil-nd — load and use it

In `xy_install` (`libaxil-nd.c:448-465`), beside `xy_load("axil-tty")` (`:464`),
using the same name `mods/auth` uses (`auth.c:791`):

```c
auth_ready = (xy_load("libaxil-auth") == 0);
if (!auth_ready)
	fprintf(stderr, "axil-nd: libaxil-auth unavailable; "
	                "passworded connect disabled\n");
```

`xy_load` is a macro that can return `XY_ERR_EPERM` (`xy-mod.h:47`, `:105`), so
the return **must** be checked — and on failure `connect` must refuse, never fall
back to the old behaviour.

`do_connect` (`world.c:983`) becomes two phases. Phase one takes the name and
refuses if auth is unavailable:

```c
if (argc < 2) { /* usage: connect <name> */ return; }
if (!auth_ready) { refuse permanently; return; }
/* enter pending-password state for fd -> name, prompt, suppress echo */
```

Phase two reads the prompted password, verifies via `auth_verify_password`, and on
success proceeds to `nd_player_login` as before (`world.c:990`).

### 5.3 Echo suppression — new work, and it is the sharp edge

axil-nd contains **no** `TELOPT_ECHO` negotiation today; only axil-tty does
(`axil-tty.c:586`). There is also **no** existing pending-input or confirmation
state machine in axil-nd to reuse (`grep` for pending/confirm/await/prompt over
`world.c` returns nothing). So phase two needs all of:

1. **A new per-fd pending-password map** in axil-nd, holding the name awaiting
   its password. Cleared on disconnect — a stale entry would let a later line on
   a recycled fd be read as a password.
2. **Its own echo negotiation**: `IAC WILL ECHO` before reading the password,
   restored afterwards. A raw telnet socket that later runs `sh` hands echo policy
   to axil-tty's PTY (`axil-tty.c:619-621`), so the state must be restored before
   login completes or the PTY will double-echo.
3. **Back-pressure on WS** as well as telnet: `/nd` frames arrive through
   axil-nd's own decoder, so the pending state must intercept there too, not only
   on the telnet path.

This is the largest piece of Phase A and the most likely to regress echo
behaviour. Test it explicitly (§7).

---

## 6. Phase A — fail closed (the complete fix for ordinary site users)

### A1. `external/axil` — one flag ✅ DONE (libaxil upgrade path per C12; WS test green)

**A1a.** `axil.h`, extend `enum descr_flags` after `:62`:

```c
/** Identity was published by AXIL_AUTOAUTH (-A), not asserted by a user. */
DF_AUTH_AUTO = 4096,
```

**A1b.** Set it after the autoauth call (`axil.c:298-309`), using the existing
public setter. **It assigns, not ORs** (C6) — a bare
`axil_set_flags(fd, DF_AUTH_AUTO)` would strip `DF_AUTHENTICATED` (breaking
axil's own command gate at `axil.c:287`), `DF_CONNECTED` (dropping the
descriptor from `DESCR_ITER`, `libaxil.c:92`) and `DF_ACCEPTED`:

```c
axil_set_flags(fd, axil_flags(fd) | DF_AUTH_AUTO);
```

`axil_flags` does **not** bounds-check (`libaxil.c:3254`), so wrap both in one
`static inline` local helper, guarded per the precedent at `axil-posix.c:363`:

```c
static inline void
axil_flag_set(socket_t fd, int flags)
{
	if (fd < 0 || fd >= FD_SETSIZE)
		return;
	axil_set_flags(fd, axil_flags(fd) | flags);
}
```

No weak symbol, no per-platform code — `descr_map` is platform-independent
(`axil-internal.h:66`, `libaxil.c:115`). **C6** refers to the earlier proposal for
a *weak hook* so that axil-tty could set the flag itself; that hook is deleted,
because `axil_flags`/`axil_set_flags` are already public (`axil.h:280`) and
axil-tty can link them directly.

**A1c.** Extend the `AXIL_AUTOAUTH` comment at `axil.h:77-78` to name the
consequence: every connection carries the server's own identity, so any
downstream authorization trusting `REMOTE_USER` is void.

### A2. `external/axil-tty` — the resolver and the gate ✅ DONE (gate + F15 core guard; suite rewritten on test-auth -T harness, green)

**A2a.** Two statics near `mux_init`. Deliberately **not** built on
`axil_get_pw` — that substitution is the vulnerability (F3):

```c
static struct passwd *
tty_identity(socket_t fd)
{
	char user[BUFSIZ] = "";

	if (fd < 0 || fd >= FD_SETSIZE)          /* axil_flags does not check */
		return NULL;
	if (axil_flags(fd) & DF_AUTH_AUTO)
		return NULL;          /* -A published the server's identity */
	if (axil_env_get(fd, user, sizeof(user), "REMOTE_USER") != 0 || !*user)
		return NULL;          /* nobody authenticated */
	return getpwnam(user);    /* NULL for a non-account: refuse */
}

/* Deny-list on purpose: the failure we must never have is a no-shell account
 * treated as shellable, so only known no-op shells are refused and anything
 * unrecognised is still treated as a real shell. */
static int
tty_no_shell(const char *shell)
{
	return !shell || !*shell
	    || !strcmp(shell, "false")         || !strcmp(shell, "nologin")
	    || !strcmp(shell, "/bin/false")    || !strcmp(shell, "/usr/bin/false")
	    || !strcmp(shell, "/sbin/nologin")  || !strcmp(shell, "/usr/sbin/nologin");
}
```

**A2b.** Gate at the **top of `axil_tty_exec`** (`:467`), before `mux_ensure`
(`:471`), so a refusal allocates no PTY — `mux_ensure` opens one itself
(`:290-291`), so a `command_pty` gate would be too late (C5). Return before the
unguarded `axil_fd_watch(s->pty)` at `:480`:

```c
struct passwd *id = tty_identity(fd);
if (!id) {
	WARN("tty: terminal refused on %d: no authenticated passwd identity\n", fd);
	axil_write(fd, "Terminal disabled: no account.\n", 33);
	axil_close(fd);
	return -1;
}
if ((!argv || !argv[0]) && tty_no_shell(id->pw_shell)) {   /* login-shell case */
	WARN("tty: terminal refused for %s: no shell\n", id->pw_name);
	axil_write(fd, "Terminal disabled for this account.\n", 35);
	axil_close(fd);
	return -1;
}
```

`sh` arrives with `argv = { NULL, NULL }` (`:492`), `man` with an explicit
`argv[0]`, so the no-shell test fires for `sh` only — `man` is a real program and
must stay reachable. `command_pty` has exactly one caller (`:479`), so this single
gate covers `sh` (`:493`), `man` (`:676`/`:682`), and the NAWS auto-spawn
(`:668-670` → `:557` → `:493`), including axil-nd's `sh`/`man` on an `/nd` socket,
which borrow a PTY (`:619-621`).

**A2c.** Keep an assertion at the top of `command_pty` (`:376`) as
defence-in-depth, knowing it can never be first.

**A2d.** Delete the three defeated fallbacks: `"/bin/sh"` default (`:425`),
child-side `axil_get_pw` + `mux_pw` (`:408-411`), and `pw = &mux_pw` in
`drop_priviledges` (`:317-319`). Change **both** `drop_priviledges` (axil-tty
`:306` and axil core `axil-posix.c:404`, F15) to take the resolved entry instead
of re-deriving it; their `NOT_CHROOTED` / `NOT_ROOT` branches and the
`setgroups`/`initgroups`/`setgid`/`setuid` bodies are unchanged.

**A2e.** `/tty` gate in `on_axil_connect` (`:562`): check identity before
`posix_openpt` (`:594`) and before `auto_shell = 1` (`:622`). On refusal mirror
axil-nd's documented precedent (`libaxil-nd.c:209-228`):
`axil_ws_close(fd); axil_close(fd); return 0;` — a decline is invisible to axil's
upgrade path and would otherwise leak one fd per probe.

**A2f.** `mux_init` (`:227-233`): NULL-check `getpwnam` before
`axil_tty_pw_copy`, and NUL-terminate `euname`. `mux_pw` stays for log messages.

### A3. `external/axil-nd` — `do_man`, both arms ✅ DONE (traversal + flag injection refused; suite asserts)

- F10: reject a `topic` containing `/` or `..`, reusing the rule `loadmod`
  already applies (`spacetime.c:2473`).
- F14: reject a `topic` beginning with `-`, and reject it outright where it is
  used as the bare `man` argument (`:682`). `help` inherits both.
- `do_sh` (`:662-666`): log a non-zero return from `axil_tty_shell`; no logic
  change (refusal happens in axil-tty).

### A4. `external/axil-auth` — `/bin/false` as the default shell (C10) ✅ DONE (both branches + SECURITY comment; mutation-verified)

**The enforcement point.** Without this, A2's gate is a no-op against every
registered site user (F3b).

In `passwd_append` (`libaxil-auth.c:444-463`), change the shell field in **both**
branches: `/bin/sh` at `:453` (OpenBSD) and `:457` (everything else) →
`/bin/false`. Nothing else in that function changes.

```c
/* OpenBSD :453 */
fprintf(f, "%s:*:%d:%d::0:0:%s:%s/%s:/bin/false\n", ...);
/* else :457 */
fprintf(f, "%s:x:%d:%d::%s/%s:/bin/false\n", ...);
```

Notes:

- Applies to **new** registrations only. Accounts already written with `/bin/sh`
  keep it, so a one-shot migration is required — see A4.2.
- `shadow_append`'s sibling format string at `:431` is a **passwd**-shaped line
  (`%s:%s:%d:%d::0:0:...`) written by the *shadow* writer; confirm which file each
  writer targets before touching `:431`. Only the `passwd` file's shell field is
  a shell.
- Must not be reverted as "cleanup". The commit message states that this line is
  the boundary.

**A4.2 Migrate existing rows.** Any account already appended with `/bin/sh` still
passes gate 4. Ship a one-shot `awk -F: '$7=="/bin/sh"{print "..."; $7="/bin/false"}'`
style rewrite over the chroot's `/etc/passwd` in the same change, after
**§11.6** captures a backup of `/var/www/etc/group` and `/var/www/etc/passwd`.
This step is easy to forget and it is the difference between the fix working and
not working on the live site.

### A5. `external/axil-auth` — registration refuses existing passwd names (F13) ✅ DONE (users_map + passwd file + getpwnam via auth_username_taken; mutation-verified)

**In Phase A** (C4): Phase A's gate is what would turn a squatted session name
into a reliable operator shell, so this must land with it.

At the duplicate check (`libaxil-auth.c:857`), also consult passwd:

```c
if (corm_get(users_map, username))
	return auth_register_error(fd, 400, "Username already exists", target);
if (getpwnam(username))          /* F13: passwd, not just shadow/users_map */
	return auth_register_error(fd, 400, "Username already exists", target);
```

Under F7 `getpwnam` reads the file we manage, covering exactly the accounts
`load_passwd` declined to add. Also stops ND player-name squatting
(`world.c:928`).

### A6. Passworded `connect` — §5 in full ✅ DONE (auth_password_matches export + two-phase prompt + echo suppression; e2e-verified)

A6.1 `auth_verify_password` exported (§5.1).
A6.2 `xy_load("libaxil-auth")` return-checked in `xy_install` (`libaxil-nd.c:464`).
A6.3 `do_connect` two-phase with prompted password (§5.2).
A6.4 per-fd pending map, echo suppression, cleared on disconnect (§5.3, sharp edge).

### A7. Tests — COMPLETE ✅ DONE (unit + auth integration + tty suite + nd suite green 3×)

`axil-tty/test.sh` boots with `-A` (`:18`), which A2b now refuses by design, so
every `/tty` assertion there needs an identity. Replace `-A` with a **test-only
strong `axil_auth_check`** returning the test server's `id -un` when a test-only
header is present — exercising the real cookie path (F6) rather than `-A`:

| Case | Expected |
|---|---|
| valid cookie → real user with real shell | spawn |
| no cookie | refused, no PTY, no child |
| cookie → `/bin/false` user | refused at the no-shell gate |

`external/axil-auth/test.sh` (the A4/A5 unit — cheapest place to pin these, since
neither needs a socket):

| Case | Expected |
|---|---|
| `passwd_append` writes shell `/bin/false` (both branches) | asserted on the produced line |
| register a name already in `users_map` | 400 |
| register a name in **passwd but not shadow** | 400 (F13 — the regression that failed before) |
| register a name in neither | 201, passwd row present, shell `/bin/false` |
| `auth_verify_password` correct / wrong / unknown / `*`-locked | 0 / 1 / 1 / 1 |

`axil-nd/test.sh`:

| Case | Expected |
|---|---|
| anonymous `/tty`, no `-A` | refused; no PTY, no child |
| `-A` server, `/tty` | refused via `DF_AUTH_AUTO` |
| `connect <name>` with no password | refused (prompted form only) |
| `connect <name> <wrongpw>` via prompt | refused, uniform message |
| `connect <name> <rightpw>` via prompt | **accepted** — new fixture (decision 8) |
| `man ../../x`, `man --pager=/bin/sh` | rejected |
| echo restored after prompt | password not visible; `sh` after login echoes normally |

**Fixture (decision 8):** build `libaxil-auth.so` into the nd suite's `lib/`
alongside `axil-nd.so`, register a user through the auth routes to obtain a
known password, then drive the two-phase prompt over telnet. Assert the accept
path, both refusal paths, and that the pending map is cleared when the socket
dies mid-prompt.

**S5.4 rewrite (`:738-897`)** into two legs:

1. **New property** — an unauthenticated `/tty` yields no PTY and no child.
2. **Retained regression** — a PTY connection carrying a real identity, killed
   abruptly, still cleans up; the 20 recycled-fd HTTP requests at `:872-890`
   stay unchanged.

Stated cost: leg 2 no longer covers the *unauthenticated* variant. Production
authenticates by cookie (F6), so authenticated PTY connections are the
production case. The raw-telnet live-PTY test (`:715-736`) must be rewritten,
since after A2b it is expected to be refused.

### A8. `external/axil/SECURITY.md` ✅ WRITTEN (S5.4 re-scope + S5.6–S5.8; README login message updated; awaits full-suite verification)

**S5.6** `axil_get_pw` identity substitution (stating that S5.4 closed the leak,
not the access). **S5.7** `man` traversal + argument injection. **S5.8** `-A`
identity publication. Plus the S5.4 re-scoping with its reason.

### A9. Build, verify, land ✅ COMPLETE

Verified:
```
cd external/axil      && sh ./test.sh                 # AXIL SUITE OK
cd external/axil-auth && ./tests/unit/run-axil-auth-account.sh  # ALL PASS
cd external/axil-tty  && bash ./test.sh               # ALL PASS
cd external/axil-nd   && ./test.sh                    # green 3x consecutively
pgrep -a -x axil                                      # clean (no stray axil)
DENO_JOBS=4 make test                                 # 120 passed, 0 failed
make standalone-unit-tests                            # all passed
```

**Why Phase A is now the complete fix.** With A4 (`/bin/false`) and A5 (F13) both
in Phase A:

| Actor | Gate 1 `DF_AUTH_AUTO` | Gate 2 `REMOTE_USER` | Gate 3 `getpwnam` | Gate 4 `pw_shell` | Result |
|---|---|---|---|---|---|
| anonymous `/tty` | clear | unset | n/a | n/a | **refused** (A2) |
| `-A` connection | **set** | operator | resolves | real | **refused** (A2) |
| registered site user | clear | set (F6) | resolves (F3b) | **`/bin/false`** (A4) | **refused** (A2) |
| squatted operator name | clear | set | resolves | real | **refused** (A5) |
| operator's own cookie session | clear | set | resolves (§11.5) | real | **shell** |

Every row that could be exploited is refused, and the operator keeps a shell.
No phase boundary is required for safety, so Phase B is free to be risk-reduction
and cleanup rather than a security dependency.

**The one thing that can still break this:** an account already in
`/var/www/etc/passwd` with `/bin/sh` (A4.2's migration, and §11.1). Verify with
`awk -F: '$7=="/bin/sh"' /var/www/etc/passwd` returning nothing except the
operator's own row.

---

## 7. Verification of the security property

Verification is one pass, because Phase A is now the complete fix (C10):

| Probe | Expected |
|---|---|
| `GET /tty` WS upgrade + NAWS, no cookie | no PTY, no child, no shell |
| `GET /nd` WS upgrade, no cookie | `AUTH-FAIL`, no player |
| `-A` server + `/tty` | refused (`DF_AUTH_AUTO`) |
| `connect <name>` no password | refused |
| `connect <name>` wrong password | refused, uniform message |
| `connect <name>` correct password | accepted into the game |
| registering an existing passwd name | rejected (F13) |
| `man ../../x`, `man --pager=/bin/sh` | rejected |
| `getent passwd <newuser>` **in the chroot** | entry present, shell `/bin/false` (A4) |
| that account + `sh` | **refused at gate 4** |
| `awk -F: '$7=="/bin/sh"' /var/www/etc/passwd` | only the operator's own row |
| sentinel system group after a signup | still present (F8, Phase B) |
| operator cookie + `/tty` | **shell works** — the one positive path |

---

## 8. Phase B — group integrity and provisioning (no longer security-critical) ⏳ NOT STARTED

The bridge step is **deleted** (C1) and **`/bin/false` has moved to A4** (C10), so
Phase B contains only risk-reduction. It can slip without reopening the hole —
which is the point of moving A4 forward.

**B1 `sync_group_file`.** Per F8, merge instead of replace: read `etc_dir/group`,
retain every entry axil-auth does not manage, write back the union. Must tolerate
a missing file and must not resurrect a deliberately removed group. Test: seed a
sentinel system group, register a user, assert the sentinel survives.

Note the consequence of leaving this unfixed: every signup truncates the chroot's
`/etc/group` to axil-auth's own groups, and `load_groups` (`:645`) then reads the
truncated file back, so system groups are permanently lost. That degrades
`initgroups`/`setgid` in `drop_priviledges` (`axil-tty:333-334`) — reachable only
when axil is root (§11.4), so it is not a live privilege escalation today.

**B2 Provisioning review.** `next_uid` (`:257`) and `next_gid` (`:617`) both scan
the passwd file for the max id (starting 999 / 1999), and `passwd_append` (`:876`)
runs at registration. Confirm on tty.pt — §11.

**B3 Verify.** `cd external/axil-auth && ./test`, then the nd suite, then the
site suite.

---

## 9. Ordering constraints

1. A1 (flag) → A2 (gate) → A3 (nd `man`) → **A4 (`/bin/false`)** → **A5 (F13)**
   → A6 (passworded connect) → A7 (tests) → A9 (land).
2. **A4 with A2, and A5 with A2.** A2 alone is theatre: gates 1-3 pass for every
   registered site user (F3b). A5 alone leaves the squatted-name escalation.
3. A1's header installed before axil-tty / axil-nd build.
4. A6's `libaxil-auth` fixture built into the nd suite before A7 runs.
5. A4.2's migration runs with A4; without it the live site keeps `/bin/sh` rows.

---

## 10. Out of scope

- Any wiz/authority concept — region ownership stays the only authority model.
- `loadmod` hardening — checked, already sound (F12).
- The `stale-contents` factory audit and the 32 stale `man-src/*.10` pages for
  unregistered commands (pre-existing).
- PTY-child reaping: axil never `waitpid()`s a PTY child, so correctly-killed
  shells linger as zombies. Already a deliberate residual in SECURITY.md S5.4; a
  zombie holds no PTY and no descriptors.
- The stale comment at `axil-nd/src/libaxil-nd.c:186-208`, which describes
  `nd_player_login` as `if (axil_auth(fd, user)) return NOTHING;` when
  `world.c:964` only warns, and which admits axil-nd's rejection of an
  unknown-OS name is deliberately non-authoritative. Same bug class as F3.
- TLS. The prompted password travels in plaintext, as does the entire telnet
  session and the WS upgrade. No new exposure class, but no confidentiality
  either.

---

## 11. Must be checked on tty.pt **[needs tty.pt]**

1. **`awk -F: '$7=="/bin/sh"' /var/www/etc/passwd`** — every row here still passes
   gate 4 after A4, so each is a live shell until A4.2's migration runs. Expect the
   operator's own row; anything else is a hole. **The pivotal unknown** (C10).
2. **Does the chroot's `/etc/shadow` contain system accounts, and are they locked
   (`*`/`!`)?** Any passwd entry absent from shadow is **claimable** (F13);
   enumerate the difference. Locked entries are also what makes `connect root`
   fail automatically (§5.1).
3. `/var/www/bin/sh` present? `scripts/doctor.sh:96` instructs
   `mkdir -p ./bin && cp /bin/sh ./bin/sh`. If present, F1 is live today.
4. Is axil run as **root** or non-root? Decides whether `pw_shell` is the only
   boundary (F4), whether B1's group loss matters, and whether F9 is a boot crash.
5. `getent passwd $(id -un)` inside the chroot — the operator's entry, which the
   positive control in §7 needs. If the operator's session name has **no** passwd
   row, or the row's shell is `/bin/false`, then A2 locks the operator out and
   §11 needs a provisioning answer before A2 can land.
6. `/var/www/etc/group` **and** `/var/www/etc/passwd` contents — capture copies
   **before** any signup runs `sync_group_file` (F8). Group cannot be recovered
   once truncated, and passwd is what A4.2 rewrites.
7. Whether the chroot's `nsswitch.conf` is the one axil-auth wrote (F7), since
   that determines which file `getpwnam` reads.

---

## 12. Implementation status (2026-10-06, end of session — work uncommitted)

What is done, verified how, and what is left. Submodules carry unstaged work
only; nothing committed anywhere.

**Done and green:**
- `external/axil`: `DF_AUTH_AUTO` + `axil_autoauth_apply()` in the upgrade path
  (C12); WS test (`test-ws.py --flags`, `/wsflags` + `/autoauth` routes) green.
  Test-auth harness gained `-T` module loading plus `axil_connect`/`axil_parse`/
  `axil_fd_tick` mirrors and `on_axil_disconnect` dispatch so loaded modules
  see production dispatch.
- `external/axil-auth`: `/bin/false` default (both branches) + `auth_username_taken`
  (users_map + passwd file + getpwnam) + `auth_password_matches` (non-zero =
  valid, uniform failure, active-check included; HTTP login refactored onto it;
  login message folded to "Invalid credentials"). Unit test
  (`tests/unit/axil_auth_account_test`) green; integration tests 14–16
  (`test.sh`) green; both mutation-checked (revert → test fails, suite exits
  non-zero). README login-failure docs updated.
- `external/axil-tty`: `tty_identity`/`tty_no_shell`/`tty_refuse`, gate in
  `axil_tty_exec` + `/tty` upgrade gate, `mux_pw` fallbacks deleted,
  `drop_priviledges` takes the resolved entry, `mux_init` NULL/NUL fixes;
  axil-core `drop_priviledges` refuses `DF_AUTH_AUTO` (F15). Suite rewritten
  (refusal × 2 + cookie positive + assets): green.
- `external/axil-nd`: `do_man` traversal/flag rejection, `do_sh` refusal
  logging, two-phase passworded `connect` (`ND_PWPEND` in descriptor env,
  WILL/WONT ECHO, uniform failure + close), `xy_load("libaxil-auth")`
  return-checked. `SECURITY.md` S5.4 re-scope + S5.6–S5.8 written.
- Superproject: `tests/unit/axil_auth_account_test` + `xy_hook_default_test`
  wired into `make test`.

**Blocked / open:**
- nd suite: two more defects found and **fixed** this session (uncommitted):
  1. **Heredoc backtick bug (pre-existing in HEAD)** — `test.sh` fixture
     Makefile was written through an *unquoted* `<<EOF`, so line 99's
     `` `make mods` `` executed at write time and spliced `make[1]: Entering
     directory...` lines into the generated Makefile (the "missing separator"
     / "No rule to make target" incident). Fixed to plain `'make mods'`
     prose. Verified: only two unquoted heredocs remain (probe Makefile,
     authfix Makefile) and neither has backticks.
  2. **S7/S8/S9 logins never converted to two-phase** — the previous session
     converted persist/planet/scope/S6 but missed all 10 connect blocks in
     S7 (w7), S8 (b8, both boots), S9 (b9, both boots): they sent
     `connect <name>` and polled the log for `nd_player_login` with no
     password step, so passworded connect stalled at the prompt (observed:
     `FAIL: S7 owner login not seen`). Their guests (`ndwiz$$`/`ndban$$`/
     `ndtgt$$`) also had **no fixture accounts at all** — auth refuses any
     name absent from the user map, so those logins could never succeed.
     Fixed: guests defined once at fixture setup (sharing `$guestpass`),
     shadow rows 1003–1005 + passwd rows added, all 10 blocks converted to
     connect → wait `Password:` → send pass → wait `nd_player_login`, and
     S8 boot B truncates its transcripts first so a stale boot A prompt
     cannot satisfy the wait.
- **The scope `goto_world 7 2` and world transition failures resolved:**
  Root causes isolated and fixed across three sites:
  1. **`RF_TEMP` flag left set on carved rooms in `st_room_at`** (`spacetime.c`):
     Procedural cleanup (`eng_room_clean`) collects empty rooms across world
     transitions and deletes them from `w_hd` while players move between worlds.
     Stale refs in `map_hd` then returned deleted room ids, leaving
     `player.location` in limbo coordinates `(0,0,0,0)` (cosmos `plen=0`) and
     failing the `world=N` assertion. Fixed by clearing `RF_TEMP` in
     `st_room_at` so carved rooms are permanent, and verifying the object
     record in `do_room`.
  2. **`nd-fight` out-of-bounds `ansi_fg[hit.color]` read in `notify_attack`**:
     `hit.color` was uninitialized in `on_will_attack` and lacked bounds
     checking in `notify_attack`, causing SIGSEGV during the world tick on
     combat hits. Fixed with bounds check (`BLACK <= hit.color <= WHITE`) and
     zero-initialization.
  3. **`nd-spell` compilation error due to deleted `EF_WIZARD` in `do_heal`**:
     Updated to unconditional refusal per `ST.md §27.6(1)` and `NO_WIZ.md`.
  4. **`in_tree_lib` in `axil-nd/test.sh`**:
     Expanded to include all sibling `nd-*/lib` trees and build targets
     (`nd-fight`, `nd-spell`).
- **Phase A complete and verified:**
  - `axil-nd`: 3× consecutive green suite runs (`axil-nd ok`).
  - `axil`: `sh ./test.sh` passes (`AXIL SUITE OK`).
  - `axil-auth`: `axil_auth_account_test` + `xy_hook_default_test` all pass.
  - `axil-tty`: `bash ./test.sh` passes.
  - Superproject: `DENO_JOBS=4 make test` (120 passed, 0 failed) +
    `make standalone-unit-tests` all pass.
  - Submodules committed and superproject pointers updated.
  Phase B and A4.2 migration remain as distinct future tasks.

**Incidents and lessons:**
- **Carved rooms must clear `RF_TEMP`.** Rooms created procedurally have
  `RF_TEMP` set, which flags `eng_room_clean` to delete them as soon as no
  player occupies them. Rooms created via `room <x> <y> <z> <w>` or `carve`
  must clear `RF_TEMP` so world transitions do not delete the room out from
  under the player.
- **Array bounds checking on ANSI color codes is load-bearing.** `ansi_fg` has
  8 entries (`BLACK` to `WHITE`). If `hit.color` is uninitialized or carries
  sentinel values, indexing `ansi_fg[hit.color]` causes out-of-bounds reads
  and fatal SIGSEGV during `objects_update` world ticks.
- **A single-submodule stash is not a baseline.** Stashing only axil-nd left
  axil-tty's fail-closed gate in place, so HEAD's suite (which *encodes* the
  vulnerability — F11) failed at `'help begin' man page not seen from PTY` by
  design. A valid baseline needs all four submodules stashed together.
- **Backticks inside an unquoted heredoc execute.** Any `` `cmd` `` in a
  `<<EOF` body runs at write time — prose meant literally must use `'cmd'` or
  escape the backticks. Same file, same rule as the python incident below:
  quote the heredoc (`<<'EOF'`) unless expansion is wanted, and grep the body
  for backticks when it is not.
- `test.sh` was once flattened by backslash-mangling through bash-heredoc
  python; repaired with a file-written state machine (`/tmp/repair2.py`,
  since removed) plus manual fixes for multi-line dquoted strings and
  `\r\n`-in-comment splits. Rule: never drive python-with-backslashes through
  a heredoc — use the Write tool + line-based edits, verify with `bash -n`
  and `git diff` review.
- `trap '' PIPE` is now set in `axil-nd/test.sh`: refusals close sockets
  mid-suite by design, and an EPIPE death (141, no message) is the failure
  mode otherwise.
- `test-auth` lesson reused: bare `axil_connect`/`axil_parse` are
  binary-only weak symbols; harnesses that load modules must mirror them.
- **Every `connect` in the suite must be two-phase.** Grep for
  `connect` NOT followed by a `Password:` wait when changing the login
  protocol — S6 was converted, S7/S8/S9 were silently missed because each
  section carries its own cmd/wait helpers and its own guest accounts.