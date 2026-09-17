# CLI surface examples — desired state

> Normative usage examples for the qmap CLI after the **phase-2B fold (2B-3)**:
> the composed `-g .` query replaces the retired `-Q` mode, and the set-expression
> flag **`-X EXPR`** is the **only query verb** — no `--axis`, no `--params`,
> no `--dl`, no `--open`, no numeric slots, no sticky join words, no `--combine`.
> Plugin-contributed `--<name>=VALUE` configuration flags are permitted (D14 —
> additive per-axis defaults, forwarded to every bound axis that declares them;
> precedence leaf spec > CLI arg > env; inline form only; credentials stay
> env-only). `[slice]` tags say which 2B slice lands each capability; untagged
> examples are already true against today's binary. This file doubles as the
> slice-by-slice acceptance checklist.

Legend: `[2B-3]` the fold (`-X` + `-t`/`-b`, old surface deleted) ·
`[2B-2]` real-file gate · `[2B-4]` write fan-out · `[2B-5]` mm dialect (exact
spellings, U3 settled 2026-09-15) · `[2B-6]` rank convention (first
rank-capable leaf in preorder wins, D2 settled 2026-09-15).

## 1. Put / delete / get — classic, unchanged

```sh
qmap -p 1:hello demo.db:a:s            # auto-index key → ref 1, value "hello"
qmap -p 5:world demo.db:a:s            # ref 5
qmap -g 1 demo.db:a:s                  # → hello
qmap -g . demo.db:a:s                  # '.' = every key (all-records)
qmap -l demo.db:a:s                    # list all values
qmap -l -k demo.db:a:s                 # keys and values
qmap -p hi:Hello demo.db:s:s           # string-keyed put
qmap -m 7 demo.db:a:s                  # all values of ref 7 (multivalue)
qmap -c 7 demo.db:a:s                  # count entries of ref 7
qmap -d 5 demo.db:a:s                  # delete ref 5 (first match)
qmap -D 5 demo.db:a:s                  # delete every entry of ref 5
qmap -r -g hallo demo.db:2s:s          # reverse lookup (value→key), multivalue
qmap -R demo.db:a:s                    # random value of a random key
```

Types: `u` uint32 · `s` string · `a` auto-index key only · `2<type>` key →
multivalue (QM_SORTED|QM_MULTIVALUE). `-r` reverse, `-l` list, `-L`
list-missing, `-k` keys, `-x` bail-on-first, `-q`/`-a` associative chain (§9).

## 2. The `@` roster — declare the load set once

```sh
# first time: @ names the axes; also creates the sidecar data.db.roster
qmap -p 1:hello "demo.db@stoma,joint:a:s"

# every later invocation: @ optional — the persisted roster reloads by name
qmap -g 1 demo.db:a:s

# explicit @ overrides the stored roster for THIS invocation only (never written)
qmap -g 1 "demo.db@joint:a:s"          # only joint loaded today

# expansion: longer list at creation → sidecar stores it
qmap -p 1:m "demo.db@joint,islet,sepal,stoma:a:s"

# unknown axis name → named error, nonzero exit
qmap -g 1 "demo.db@wonka:a:s"          # → unknown axis name: wonka
```

Sidecar = `<primary>.roster`, one map `@roster` (`v`=`1`, `axes`=csv) — a plain
qmap file. Write-if-absent (D7). Heuristic: axis stores sit alongside but no
roster exists → loud hint on stderr, exit 0.

## 3. `--list-axes` — discovery (long-only, standalone)

```sh
qmap --list-axes "demo.db@stub,zed:a:s"
qmap --list-axes demo.db:a:s           # stored roster loads
qmap --list-axes demo.db:a:s           # no roster + no @ + no env libs → header only, exit 0
```

```
slot  name              fill  rank  ctx
0     stub              y     n     y
1     zed               y     n     y
```

## 4. The composed query — `-X EXPR` `[2B-3]`

One flag. One argument. The argument is a set expression whose leaves are axes:

```text
expr    := or
or      := and (OR and)*
and     := not (AND not)*
not     := NOT not | primary
primary := '(' expr ')' | term
term    := instance | reference
instance:= label ':' axis           # labeled instance (D15)
reference:= label                   # reuses an earlier instance's set
axis    := NAME                     # bare NAME ⇒ params from flags only
label   := [_A-Za-z][_A-Za-z0-9]*   # ≤31 chars; not AND/OR/EXCEPT/NOT
```

- The CLI parses **only the set structure** — never an axis's grammar. Each
  leaf's parameters ride plugin-declared flags (`--NAME=VALUE`,
  `--NAME@LABEL=VALUE` / `--NAME@AXIS=VALUE`); a hand-written `NAME=VALUE`
  inside `-X` is now a **hard parse error** pointing at flags (removal
  hint: `qmap: -X: '…=VALUE' no longer allowed in -X; pass values via
  --flags (e.g. --query=… / --query@A=…)`). A `label:axis` leaf gives that
  instance an address; a bare `label` (already defined earlier in the
  expression) reuses its set (**backward references only** — `A AND
  A:stoma` is an error, `A:stoma AND A` is not).
- `AND` / `OR` / `EXCEPT` / `NOT` and `(` `)` are reserved words; `NAME` is a
  registered axis name (case-sensitive). Keywords are uppercase-only and axis
  names are lowercase slugs, so shadowing is effectively unreachable. Labels
  are additionally reserved from the keyword set and must not collide with a
  bound axis name (both are parse-time errors).
- Query is **armed** iff `-X` is present with a nonempty `EXPR`; then `-g .` runs
  the effective query at its argv position. No `-X` (or `-X "  "`) ⇒ `-g .` is
  classic all-records. `[2B-2]`
- `-t N` / `--top N` cap the result count (default `-t 0` = all); `-b F` /
  `--bottom F` is the score floor (drop results below F); both apply to the
  whole expression's result.
- Scoped flag values are transported internally as `key='value'` decode specs
  synthesized into the target leaf — **internal transport**, not user grammar
  (`--NAME@LABEL` is the user-facing surface for per-instance parameters).

```sh
# simplest
qmap -X stoma --query=beacon -g . demo.db          # one axis
qmap -X stoma -g . demo.db                         # bare name: join, no params

# D15 labeled instances: the SAME axis fills twice with different params
# (stoma rebuilt twice; per-instance params via scoped flags)
qmap -X 'A:stoma OR B:stoma' -g . --query@A=beacon --query@B=alpha demo.db

# E_REF backward reference (guarded by NOT/EXCEPT, duplicate axes collapsed)
qmap -X '(A:stoma EXCEPT A)' -g . demo.db      # empty: A \ A

# with -g . in between ops (write-then-query in one invocation)
qmap -X joint --query=2026-09-14 -g . demo.db -p 9:lunch demo.db:a:s
```

## 5. Set algebra — grouping, precedence, NOT/EXCEPT `[2B-3]`

```sh
# (A OR B) AND C  — any precedence, no reordering tricks
qmap -X '(stoma OR joint) AND islet' -g . \
     --query=beacon --since=2026-09-14 --until=2026-09-15 \
     --dim=2 --s=9,1 --l=1,1 demo.db -t 10

# A OR (B AND C) — parens override left-to-right
qmap -X 'stoma OR (joint AND islet)' -g . \
     --query=beacon --since=2026-09-14 --until=2026-09-15 \
     --dim=2 --s=9,1 --l=1,1 demo.db

# EXCEPT = relative setminus: first set minus the second (NOT is unary-only)
qmap -X 'stoma EXCEPT joint' -g . --query=beacon --since=2026-09-14 demo.db -t 5

# leading NOT at root = complement against the primary ref universe
qmap -X 'NOT stoma' -g . --query=beacon demo.db     # every ref not matching "beacon"

# NOT composes with EXCEPT: (complement A) − B
qmap -X 'NOT A:stoma EXCEPT B:stoma' -g . --query@A=alpha --query@B=beta demo.db

# 3+ groups, mixed, score floor
qmap -X '((A:stoma OR B:stoma) AND C:joint) EXCEPT (D:stoma OR E:islet)' -g . \
     --query@A=alpha --query@B=beta \
     --since@C=2026-09-14 --until@C=2026-09-15 \
     --query@D=gamma --dim@E=2 --s@E=0,0 --l@E=1,1 demo.db -b 0.5

# `A NOT B` is a parse error (hint: use EXCEPT)

# D15: labeled instances are first-class in the algebra — the same axis can
# appear twice, referenced by label after its definition
qmap -X '(A:stoma AND NOT (B:stoma EXCEPT A))' -g . --query@A=beacon --query@B=alpha demo.db

# scoped flags reach individual instances (leaf spec > @label > @axis >
# unscoped broadcast > env); they synthesize into the leaf's decode spec
qmap -X '(A:stoma OR B:stoma)' -g . --query@A=beacon --query@B=alpha demo.db
qmap -X '(A:joint AND B:joint)' -g . --since@A=2026-09-14 --until@A=2026-09-15 --since@B=2026-09-13 --until@B=2026-09-16 demo.db
```

The retired pieces and why they're gone: `--and/--or/--not` sticky words,
`--combine`, `--params`, `--axis SLOT`, `--dl`, `--open`, `-Q`-mode, `rq_*` —
the expression is complete in `-X`; no combiner flag survives.

## 6. Real multi-axis gates — non-mm space ∩ time ∩ text `[2B-2]`

Fixture seeds the same refs (1/2/3) into: primary `:a:s`, joint
(leading-date), islet (point-lists), sepal (floats); the roster declares the
load set; stoma is rebuilt from the primary at each open (never files).
Joint never hands out handle 0 as a ctx (the jd-0↔NULL collision is burned once
per process, so a `--list-axes` slot always shows `ctx=y`). Sepal has two
columns: floats-direct (the offline default) or embedded strings when
`QMAP_SEPAL_EMBED_URL`+`MODEL` are both set (D8, `rec_axis_env_config` — skipped
unless the vars are set).

Internal per-leaf decode keys (case-sensitive; **not** user grammar — every
key has a flag alias, and flags are the only way to reach a leaf post-flip):

- joint: leaf keys `a=<date>[ b=<date>]`  (interval on the open axis; half-open
  `[a,b)`, defaults `b=0` meaning open-ended; `sscantime` accepts `YYYY-MM-DD`
  and `YYYY-MM-DDTHH:MM:SS`). Flag form: `--since=<date> --until=<date>`,
  plus `--query=<point-timestamp|A..B>` (point → containing calendar day;
  `A..B` space-free; non-parseable → ignored). Fixture:
  `--since=2026-09-14 --until=2026-09-15` — refs 1 before and 2 after that day
  are excluded, ref 3 inside (rank-less, pure filter).
- islet: leaf keys `dim=N s=x,y[,z,…] l=dx,dy[,…]`  (`s` minimum corner, `l`
  span per-lane). Flag form mirrors keys 1:1 (`--dim/--s/--l`); fixture:
  `--dim=2 --s=9,1 --l=1,1` — tight box on (9,1).
- stoma: leaf keys `field=<field> query=<text> [phrase=0|1] [matched=N]`
  (`field` defaults to `""` → `"text"`; `matched` default 0, the first-rank
  effect in the gate: `--field=text --query=beacon --matched=1` on a 3-token
  doc → `1/3 = 0.125`).
- sepal: offline floats are comma-floats `f1,f2,…` written directly; the
  query reads a binary-floats qvec file. Flags: `--file=<q.vec> --qdim=N
  --m=M --min-sim=F` (fixture: floats `0.9,0.1,0.8` written as the query
  vector, `--qdim=3`). First-rank is stoma (the **first rank-capable
  leaf in preorder wins** — the standing rank convention D2,
  `RECALL-KERNEL.md`; pinned by a two-rankable row in `test-real.sh`
  `[2B-6]`); sepal's rank (cosine) ranks its own single-axis query. The embed column stores strings (e.g. `"beacon beacon harbor
  lights"`) through the configured embedder and queries a vector embedded
  from the same endpoint.

```sh
# all four bound; budget note on stderr (stoma rebuild docs+ms, U4):
qmap --list-axes "greps.db@joint,islet,sepal,stoma:a:s"
# slot  name              fill  rank  ctx
# 0     joint             y     n     y
# 1     islet             y     n     y
# 2     sepal             y     y     y
# 3     stoma             y     y     y
# (first-ever @ is LOUD: alongside-heuristic + stoma's "no primary found"
# notes, never silently empty; later opens emit "rebuilt N docs in X ms")

# space ∩ time ∩ text — the conjunctive winner (full ref score record);
# scoped flags pin each instance's params (joint+islet+stoma all declare
# distinct names except --query, so the stoma query is scoped to be safe):
qmap -X '(A:joint AND B:islet) AND C:stoma' \
     --since@A=2026-09-14 --until@A=2026-09-15 \
     --dim@B=2 --s@B=9,1 --l@B=1,1 --query@C=beacon --field@C=text --matched@C=1 \
     -g . "greps.db@joint,islet,sepal,stoma:a:s" -t 100
# → 3 0.125000 2026-09-14T12:00:00:Beacon Harbor lights
# (3 docs corpus in the gate: ~0.02–0.03 ms rebuild; budget recorded, U4)

# sepal column (offline floats-direct default):
#   q.vec = binary floats of ref 3's vector; q.dim = 3 written by the seeder
qmap -X 'sepal' --file=$qvec --qdim=3 --m=2 --min-sim=0.5 -g . \
     "greps.db@joint,islet,sepal,stoma:a:s" -t 100
# → 3 1.000000 2026-09-14T12:00:00:Beacon Harbor lights
#   1 0.906867 2026-09-13T20:00:00:Beacon Harbor lights
```

Rendering (locked): ref-led `ref[ score] record` per line — score iff any queried
axis ranks; best-first, ties asc ref; pure-filter (no ranker) asc. Dangling refs
(no primary record) skipped with a stderr count, exit 0. `-k`/`-r` ignored under
an armed query. With zero plugins loaded the same incantation answers plain:

```sh
qmap -g 1 greps.db:a:s                  # classic get, byte-identical to legacy
qmap -g . greps.db:a:s                  # classic all-records when no -X present
```

## 7. Write fan-out `[2B-4]` (with D11..D13)

`-p`/`-d` write `{primary} ∪ {@roster} ∪ {target}` — each store exactly once;
the whole `-p` *payload* fans out as `(ref, blob,len,qtype)` to every target
(`qmap.h:241` `qmap_type_len`); string primaries (`:a:s`) go via the string
`rec_axis_store(ctx,spec,ref, const char *value)` as before, binary primaries
(any `qmap_reg` type — D12) via the additive
`rec_axis_store_typed(ctx,spec,ref, blob,len,qtype)` (CLI prefers the typed
symbol when `vtype != QM_STR`, else the string one; missing typed export ⇒
text-only axis). Each axis parses the whole payload in its own grammar (2A).
`rec_axis_open(spec)` decides per deployment whether the store is file-backed
or derived/rebuilt (`<dir>/<name>.db` vs memory — D13, mask `4095` `qmap.h:204`
as initial hint, auto-grow unless `QM_NOGROW`; `QMAP_MASK` env overrides for
benches).

```sh
qmap -p 1:"2026-09-14T09:00:Beacon Harbor lights" "demo.db@joint,stoma:a:s"
#            primary ref1   joint date         stoma text

qmap -d 1 "demo.db@joint,stoma:a:s"      # unstore on every axis (idempotent)
qmap -D 1 "demo.db@joint,stoma:a:s"      # same (documented -d/-D collapse on axes)

# loud partials: one axis rejects → all attempted, failures named, exit nonzero
# compensation = idempotent forget: re-run -d 1 → 0
```

Missing `store(_typed)` in a target's `.so` ⇒ that target is read-only, skipped
on writes, reported as a partial; `stoma` stays derived (no file), `sepal`
file-backed, `joint`/`islet` either (D13).

## 8. mm dialect — documented invocations on the same surface `[2B-5]`

> Exact spellings settled 2026-09-15 (U3) by testing the built 2B-4
> surface over real joint+stoma files (`test-mm.sh`); these are the
> recipes pi-mm issues (phase 3). The dialect is joint+stoma (time+text):
> one payload string must satisfy every axis grammar, and
> `<DATE>:<TEXT>` is legible to joint (leading date) and stoma (text)
> but not to islet/sepal grammars.
>
> Notes baked in from the build: store needs **explicit refs** (`-p
> VALUE` splits at the first colon, so auto-ref ingest cannot carry
> `<DATE>:<TEXT>` — pi-mm owns the ref counter). The time leaf is always
> a **bounded window** — `joint="a=D"` with no `b` matches nothing, and
> with open `[date,∞)` store intervals the effective filter is `date <
> b` (presence). `-d` needs the roster-backed file (classic
> `:a:s` deletes by string key and no-ops on numeric refs). stoma is
> derived — a forgotten ref returns to text queries if still in the
> primary (by design, D13), so forget deletes primary *and* axes in one
> op. Reset has no single flag — it is the enumerate+forget loop below
> (the `-1` empty sentinel is skipped; re-running is a no-op).
>
> **D14 amendment (round 2, 2026-09-16):** `-X` is now PURE structure — bare
> axis names only. ALL params ride plugin-declared flags
> (`--since/--until/--field/--matched/--query/--min-sim`); pi-mm emits exactly
> the shape below; store/forget/reset unchanged. Hand-written `NAME=VALUE`
> inside `-X` is a **hard parse error** (removal hint in the message) — leaf
> params can no longer be authored in the grammar.

```sh
# store
qmap -p 1:"2026-09-14:Beacon Harbor lights" "mem.db@joint,stoma:a:s"
# search (bare names + flags; joint window optional per level/until)
qmap -X '(joint AND stoma AND sepal)' --field=text --matched=1 --since=2026-09-14 --until=2026-09-16 --query=beacon --min-sim=0.2 -g . mem.db -t 10
# forget
qmap -d 1 "mem.db@joint,stoma:a:s"
# reset
for ref in $(qmap -g . mem.db | grep -E '^[0-9]+$'); do qmap -d "$ref" "mem.db@joint,stoma:a:s"; done
```

## 9. Associative chain — `-q` / `-a` (classic, unchanged)

`-q file[:k[:v]]` = db for string lookups/printing · `-a file` = reversed string
lookups · processed in order · `-x` bail-on-first · `-r` reverse.

```sh
qmap -p hi:0 -p hallo:1 names.db:s:u      # name → id
qmap -q names.db:s:u -g hi names.db:s:u   # chain resolves hi → 0, prints 0
qmap -q names.db:s:u -rg hallo names.db   # reversed resolution
qmap -a secondary.db -rl primary.db       # reversed association listing
qmap -a b.db -a s.db -rl a.db             # multi-hop, order kept
qmap -xa b.db -a s.db -l a.db             # -x bails on first result
```

## 10. Library API — typed, not strings (except at the axis boundary)

libqmap `qmap_put/del/get` are typed `const void*` — the type is fixed at
`qmap_open`, so a library user is **not** forced to feed strings:

```c
uint32_t hd = qmap_open("data.db", NULL, QM_AINDEX, QM_STR, 0xFF, 0);
uint32_t ref = qmap_put(hd, NULL, "hello");       /* a-key: NULL key → new ref */
const char *v = qmap_get(hd, &ref);
qmap_del(hd, &ref);

uint32_t hd2 = qmap_open(NULL, NULL, QM_STR, QM_U32, 0xFF, 0);
qmap_put(hd2, "alice", &(uint32_t){100});         /* u32 value — not a string */
const uint32_t *val = qmap_get(hd2, "alice");
```

The **payload boundary is the axis store** (2A, D12): `rec_axis_store` (string
whole-value) / `rec_axis_store_typed` (binary `blob,len,qtype`) / `unstore` /
`readback` take one opaque whole-payload, parsed axis-side. Anyone feeding an
axis — CLI or library — passes the primary's payload by design (principle 1:
axes own their grammar). String primaries (`:a:s`) go through the string
entry point, binary primaries (any `qmap_reg` type) through the typed one;
missing typed export ⇒ text-only axis, missing both ⇒ read-only.

## 11. Reference

| Flag | Meaning |
|---|---|
| `-r` `-l` `-L` `-R` `-p` `-d` `-D` `-g` `-m` `-c` `-x` `-k` `-q` `-a` | classic (unchanged, see §1/§9) |
| `-X EXPR` | set-expression query (arms `-g .`) `[2B-3]`; leaves `label:axis`, backward-only bare `label` refs (D15) — **structure only**: `NAME=VALUE` inside `-X` is a parse error; params ride flags (`--NAME=VALUE` / `--NAME@LABEL` / `--NAME@AXIS`) |
| `-t N` / `--top N` | cap result count (default 0 = all) `[2B-3]` |
| `-b F` / `--bottom F` | score floor (drop below F) `[2B-3]` |
| `--rank[=LABEL]` | core rank override: `--rank=A` ranks exactly the labeled instance A; `--rank@A` is the scoped form; without it, same-axis rank-capable instances aggregate as the per-ref MAX score (order-independent; distinct axes keep D2 first-rankable-in-preorder) `[D15]` |
| `--NAME=VALUE` | plugin-contributed per-axis config flag (D14): inline form only; accepted by getopt via the dynamic table, forwarded after bind to every bound axis whose `rec_axis_cli_options()` declares NAME; unknown/rejected/misformed → usage + exit 1; bare `--NAME` requires `--NAME=VALUE`; declared names: joint `since`/`until` (time_t window) + `query` (point timestamp → containing day, or space-free `A..B`; non-parseable → ignored), stoma `field`/`phrase`/`matched`/`query` (field default `text`), sepal `file`/`qdim`/`query`/`min-sim`/`m` (pool size, 0=default), islet `dim`/`s`/`l`; `-X` is PURE structure (bare names + AND/OR/EXCEPT/NOT); **unscoped `--NAME` broadcasts to every bound axis declaring NAME** — for one axis use `@label`/`@axis` (joint's `--query` ignore-non-parseable exists so a shared broadcast `--query` never aborts a mixed run); e.g. `-X '(joint AND stoma AND sepal)' --field=text --matched=1 --since=2026-09-14 --until=2026-09-16 --query='the old lighthouse beacon' --min-sim=0.2` |
| `--NAME@LABEL=VALUE` / `--NAME@AXIS=VALUE` | scoped config (D15): applied to the labeled instance, or to every bare instance of AXIS, instead of the unscoped broadcast. Precedence: leaf spec > `@label` > `@axis` > unscoped > env. The `@`-token never reaches a plugin's broadcast `cfg()`; it is synthesized into the target leaf's decode spec (`<base>=<value>`, single-quoted with `\` escapes when the value contains space/tab/quote/paren). Unknown scope → `unknown label or axis 'N'`, exit 1 |
| `--list-axes` | long-only: registered-axis table, standalone |

Reserved expression words: `AND OR EXCEPT NOT ( )`. Keywords are uppercase-only;
axis names are lowercase slugs, so an axis named `except` stays reachable.
Labels (D15): `[_A-Za-z][_A-Za-z0-9]*`, ≤31 chars, must not be a keyword and
must not collide with a bound axis name; duplicate labels are a parse error;
references are backward-only (a bare name is a label only once defined, else
an axis name).
Env: `QMAP_AXIS_LIBS` (colon paths, dlopen'd first), `QMAP_AXIS_PATH`
(dir list for `lib<name>.so`, default `/usr/lib`), `QMAP_MASK` (D11, per-store
initial mask override, default `4095` `2^12-1` — auto-grow, `qmap.h:204`),
`QMAP_SEPAL_EMBED_URL` / `_MODEL` / `_KEY` (D8, env-only).

Errors: unknown axis name → `unknown axis name: N` (nonzero); malformed `EXPR`
→ named parse error, nonzero; bad `-X` before any `-g .` → error. Mid-run fill
failures keep kernel skip semantics; slot+ctx pre-validation before the query
run (2B-3). No `/usr` installs in this phase (D4).