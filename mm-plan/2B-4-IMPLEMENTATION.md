# 2B-4 — Implementation detail (write fan-out + forget/reset)

> Precise, normative spec for slice 2B-4 of `external/libqmap`. Read these
> first, in order: `mm-plan/CLI-SURFACE-EXAMPLES.md` (§7 normative surface),
> `mm-plan/PHASE-2-CLI.md` (§2B "the `@` roster" write set, decisions
> D1/D11/D12/D13, the 2B-4 slice), `external/libqmap/docs/RECALL-KERNEL.md`
> (`rec_axis_store` / `rec_axis_store_typed` / `rec_axis_unstore` /
> `rec_axis_readback` conventional exports), then
> `external/libqmap/src/qmap.c` (current shape).

## 0. Scope & file deltas

| File | Action |
|---|---|
| `src/qmap.c` | `QDBE_MASK` 4095 + `QMAP_MASK`; per-slot capability table at bind; fan-out store/unstore; `gen_put`/`gen_del`/`gen_del_all` → int; `qmap_write_ref`; exit-code threading |
| `src/librec_axis_fold.c` | write-capable ctx (`fill` + `.wr` stash), store/typed/unstore/readback, stash union in fill, env mask |
| `src/librec_axis_plain.c` | **new** string-only test axis (no typed export — binary primary must loud-skip) |
| `tests/fanout_verify.c` | **new** dlopen-only readback/pu32 probe (no link deps) |
| `test-fanout.sh` | **new** 2B-4 gate (see §7) |
| `Makefile` | plain `.so` standalone rule, `all:` addition, `test:` addition |
| `external/libstoma` `src/libstoma.c` | sidecar-scan primary open: env-or-4095 mask (D11; applied to the in-site submodule — the `~/libstoma` sibling checkout was removed in the 2026-09-15 migration) |
| docs (this pass) | `CLI-SURFACE-EXAMPLES.md` §7 D11..D13 sync — **already applied**; `PHASE-2-CLI.md` 2B-4 DONE — **applied here**; this file |

Ground rules: work only in `external/libqmap` (+ the one-line-shape
libstoma mask sync); kernel (`rec.h`/`rec.c`, `idm.c`) untouched; no
`/usr` installs (D4); TDD red → green; commit only when asked.

---

## 1. The write set (locked; examples §7)

Each `-p`/`-d`/`-D` writes `{primary} ∪ {@ roster} ∪ {target}`, each
store exactly once. `{target}` is the `@` roster — the D9 load-set
extension (`-X` names, `QMAP_AXIS_LIBS`) binds query-only axes that are
never fan-out targets. The roster is the effective one: explicit `@`
(or the stored sidecar roster read back by `qmap_axes_setup`).

Firing rule: the composed write path fires **only** when `roster_n > 0`
(effective roster) **and** the primary key type is `QM_HNDL` (`:a:`).
Everything else is the byte-identical classic path (primary op only,
no extra opens, `EXIT_SUCCESS`).

## 2. Ref + payload resolution

- `-p REF:VALUE` / `-p VALUE`: the legacy `gen_lookup` colon split is
  unchanged (value = whole remainder after the first colon — the same
  string the primary stores). The fan-out ref is the primary key when
  explicit (`memcpy` 4 bytes off `key_ptr` — the parsed u32), else the
  auto-assigned `id` (AINDEX refs are positions; auto refs must not be
  mixed with explicit refs in one file — legacy quirk, pinned by
  `test-fanout.sh`'s separate `auto.db`).
- The fan-out payload is what the primary actually stored:
  `qmap_get(prim_hd, &ref)` (string fallback `value_ptr` only when the
  get misses). `len`/`qtype` come from `metas[prim_hd].types[1]` +
  `qmap_type_len(vtype)` (0 for `QM_STR`, fixed size for the rest).
- `-d REF` / `-D REF`: `qmap_write_ref` — whole-arg decimal u32, else
  the primary reverse-view name lookup (`qmap_get(prim_hd + 1, name)`).
  Resolved **before** `gen_lookup` (it truncates the operand). In
  composed mode the primary is deleted **by u32 ref** (`qmap_del` /
  `qmap_del_all` on `prim_hd`) — this is the mm `forget`; the legacy
  string-keyed delete was a silent no-op on `:a:` maps and stays
  untouched in classic mode.
- Unresolvable write operand in composed mode: loud
  `qmap: write ref 'X': no primary record`, `EXIT_FAILURE` (reads
  degrade; writes fail). Non-`:a:` primary + roster on `-p`: primary
  written, loud `axis fan-out needs an :a:-type primary`, `EXIT_FAILURE`.

## 3. Typed dispatch (D12)

| Primary vtype | Axis exports | CLI calls | Meaning |
|---|---|---|---|
| `QM_STR` | string store | `rec_axis_store(ctx, NULL, ref, payload)` | verbatim string |
| `!= QM_STR` | typed export present | `rec_axis_store_typed(ctx, NULL, ref, payload, len, qtype)` | binary payload |
| `!= QM_STR` | typed export absent | **loud skip** (`binary payload, text-only axis (no store_typed), skipped`) | no silent corruption |

Rationale for the skip (bring-up finding): the CLI cannot produce real
typed values through its string-shaped `-p` (a CLI-written `:a:u`
payload is pointer garbage), and a string store would `strlen()` over
raw bytes. Forward-nothing-loudly beats corrupt-silently. Missing
**both** store symbols ⇒ read-only partial
(`read-only (no store symbol), skipped`).

`spec` is always `NULL` (reserved — never credentials). `unstore` needs
no payload. `readback` is dlsym'd and recorded (read-only detection)
but has **no CLI surface** (deferred — query + write cover the mm flow;
`-g`/`-m`/`-c` stay byte-identical).

## 4. Loud partials

`qmap_fanout_store` / `qmap_fanout_unstore` attempt **every** roster
target, name every failure on stderr (`axis '<name>': <reason>`), and
return the failure count; the op returns `EXIT_FAILURE` iff nonzero
(threaded through pass 2 via `rc |=`). No cross-store transaction
(single-writer); compensation = idempotent forget: `-d REF` re-run
returns 0 (`rec_axis_unstore` absent-ref → 0; `-d` == `-D` on axes —
documented collapse). Missing ctx (`no ctx bound`) also fails loud
(never a silent empty fan-out).

Canonical new errors:

```
qmap: axis '<name>': store skipped (no ctx bound)
qmap: axis '<name>': read-only (no store symbol), skipped
qmap: axis '<name>': binary payload, text-only axis (no store_typed), skipped
qmap: axis '<name>': store rejected ref <r>
qmap: axis '<name>': unstore skipped (no ctx bound)
qmap: axis '<name>': read-only (no unstore symbol), skipped
qmap: axis '<name>': unstore rejected ref <r>
qmap: write ref '<operand>': no primary record
qmap: axis fan-out needs an :a:-type primary (refs must exist); primary written, axes skipped
qmap: invalid QMAP_MASK '<e>' (need 2^n-1)
```

## 5. Test plugins (`librec_axis_fold`, `librec_axis_plain`)

`fold` ctx is now `fold_ctx_t { fill, stash }`: `fill` = the
alongside `<dir>/<name>.db` a:u store (read — unchanged `-X` source);
`stash` = alongside `<dir>/<name>.wr` (HNDL→string, `"hd"`, effective
mask — the write target). `fill` unions both handles' refs
(stash-empty in `test-cli.sh` → its 25 rows byte-identical).
`rec_axis_store` = verbatim put (same-ref replace);
`rec_axis_store_typed` = `"T:<u32dec>"` tag for 4-byte built-ins
(lets the gate prove *which* symbol the CLI chose), raw for `QM_STR`,
`-1`/`EINVAL` otherwise; `unstore` = `qmap_del` (idempotent, returns
0); `readback` = malloc'd stored string (absent → NULL/0, still 0).

`plain` ("plain" axis, separate `.so`) is the string-only control:
file-backed stash, fill+rank, `store`/`unstore`/`readback` — but **no
typed export**. Same standalone-rule Makefile pattern as the other
test plugins (shared-mk `LIB` aggregation bug workaround).

## 6. Mask (D11)

`QDBE_MASK` is `(4096 - 1)` — an initial hint only (auto-grow).
`qmap_mask_effective()` derives `QMAP_MASK`-or-`4095` (validated,
exit-1 on garbage) and is used for the primary open and both roster
sidecar opens. `fold`/`plain`/`stoma` (sidecar-scan) share the same
derivation so co-opened files always match (2B-3's dbid/mask-mismatch
truncation bug must not recur). `aqs[]` stay on `QDBE_QMASK` (in-memory).

## 7. Gate (`test-fanout.sh`, 40 cases)

String fan-out verbatim (alpha+beta) · `-X` answers written refs ·
reopen-persist · auto-ref on a fresh db · typed dispatch on `:a:u`
(`9:T:<primary-u32>`) · text-only verbatim on `:a:s` · binary+text-only
loud skip + unstored · named forget (`-d hello` via reverse view) ·
`-d REF` + idempotent re-run (`-g .` → `-1` on empty — legacy `nonce`) ·
`-D` collapse · read-only partials (stub) · mixed partial
(attempt-all/report-all/nonzero + stored target still landed) ·
non-`:a:` + roster declined · `QMAP_MASK=255` round-trip + invalid
rejected · unresolvable composed `-d` · classic no-roster sanity.

## 8. Bring-up fixes (recorded)

1. **Forward declared the fan-out trio** (`static` defs live after
   `qmap_axes_list`; `gen_*` call them earlier) — first build failed
   on implicit declarations.
2. **Auto refs are positions**: seeding `1,2` then `-p <auto>` assigns
   ref 2 and *replaces* record 2 (AINDEX key space == position space).
   The gate uses a fresh db for auto-ref; production flows use
   explicit refs.
3. **`-g .` on an empty `:a:` prints `-1`** (legacy `nonce` path) — the
   forget cases assert that, not the empty string.
4. **DBs must stay type-pure** in tests: reusing one file as `:a:u`
   then `:a:s` leaks the earlier records across the `"hd"` map
   (same-file type mixing is a documented deferred area).
5. **No string-forwarding of binary payloads** (§3 skip): discovered
   red — `strlen()` over CLI-written u32 garbage is not a payload.
