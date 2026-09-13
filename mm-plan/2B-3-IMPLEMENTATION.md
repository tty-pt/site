# 2B-3 — Implementation detail (the `-g` fold + the `-X` surface)

> Precise, normative spec for slice 2B-3 of `external/libqmap`. Read these
> first, in order: `mm-plan/CLI-SURFACE-EXAMPLES.md` (§3/§4/§5/§11 normative
> surface), `mm-plan/PHASE-2-CLI.md` (§2B "the `-g` fold", decisions D1-D10,
> the 2B-3 slice), `external/libqmap/docs/RECALL-KERNEL.md` (`rec.h` APIs),
> then `external/libqmap/src/qmap.c` (current shape). References to
> `CLI-SURFACE-EXAMPLES.md` below are "examples".

## 0. Scope & file deltas

| File | Action |
|---|---|
| `src/qmap.c` | delete `-Q` mode; add `-X`/`-t`/`-b`; extend load set (D9); composed `-g .`; exit codes; `--list-axes` standalone |
| `src/librec_axis_fold.c` | **new** test plugin (3 axes: alpha/beta/pure) |
| `Makefile` | `LDLIBS-librec_axis_fold`, standalone rule, `all:` addition |
| `test-cli.sh` | full rewrite (acceptance spec in §7) |
| `test-roster.sh` | untouched, must stay green |
| docs (this pass) | `<mm-plan/CLI-SURFACE-EXAMPLES.md>` §4/§5/§11 + `<mm-plan/PHASE-2-CLI.md>` D10/§2B + `<mm-plan/README.md>` 2B-3 row → EXCEPT sync — **already applied**; §8 drift fixes **already applied** |

Ground rules: work only in `external/libqmap`; kernel (`rec.h`/`rec.c`,
`idm.c`) untouched; no `/usr` installs (D4); TDD red → green; commit only
when asked.

---

## 1. The `-X EXPR` grammar (locked; examples §4/§5)

```
setexpr := orexpr (EXCEPT orexpr)*        → SUB(List[or])
orexpr  := andexpr (OR andexpr)*         → OR(List[and])
andexpr := notexpr (AND notexpr)*        → AND(List[not])
notexpr := NOT notexpr | primary         → NOTP(child)
primary := '(' setexpr ')' | axis
axis    := NAME ('=' VALUE)?
```

NOT is **unary-only**: prefix `NOT` = complement against the primary ref
universe. Relative set difference is the binary keyword `EXCEPT`.
`A NOT B` is a parse error (hint: use EXCEPT). This single grammar makes
every §5 shape parse unambiguously:

- `alpha EXCEPT beta` → SUB[alpha, beta] (relative setminus)
- `(alpha OR beta) AND pure`, `alpha OR (beta AND pure)` → paren/precedence
- `NOT alpha` at root → NOTP → complement
- `NOT NOT alpha` = complement(complement(alpha)) = alpha
- `NOT A EXCEPT B` = `(complement A) − B`; `alpha AND NOT beta` =
  `alpha ∩ (universe − beta)`

**Token kinds:** `(` `)` `AND` `OR` `EXCEPT` `NOT` `NAME` `NAME=VALUE`.

**Tokenizer rules (word-level):**
- Skip leading whitespace. `(` / `)` are single-char tokens.
- A word = run of non-space, non-`=`, non-`(`/`)`. If the word is exactly
  `AND`/`OR`/`EXCEPT`/`NOT` → keyword; else → NAME.
- Keywords are uppercase-only; axis names are lowercase slugs, so operator
  words effectively never shadow an axis name (`-X except` referencing an
  axis literally named `except` stays reachable).
- After NAME, optional `=`: if the next char after the word (skipping
  blanks) is `=`, consume it and parse VALUE. Otherwise the axis is a bare
  `NAME` (params `""`, D6 opt-in). `NAME=` (empty VALUE) ≡ bare `NAME`.
- **VALUE** (whole-string, D5):
  - If it opens with `"` or `'`: consume verbatim to the matching close
    quote; no escapes; unclosed → error `unterminated quote`.
  - Else: consume the remainder of input until the next **unquoted** `(` /
    `)` or a whitespace-delimited AND/OR/EXCEPT/NOT word; internal spaces
    are part of the value (`2026-09-14 00:00..T23:59:59`, `hello world`).
    The value IS whatever precedes the operator — silent truncation is
    precedence, documented in usage (`quote values containing operator
    words`). At a `(` / `)` char the value stops just before it. At
    whitespace: peek the next word; if it is a keyword → value stops
    (keyword stays for parsing); else include the whitespace and continue.

**Named parse errors** (each `fprintf` + `exit(EXIT_FAILURE)` from pass 1):

```
qmap: -X: unexpected token '<tok>'
qmap: -X: unexpected token 'NOT' (use EXCEPT for set difference)
qmap: -X: expected ')'                  # unbalanced parens
qmap: -X: expected expression           # empty parens "()"
qmap: -X: unterminated quote
qmap: -X: too many expression nodes      # arena cap (512)
qmap: -X: too many distinct axes (max 8)
```

Empty expression / whitespace-only `-X` → **unarmed** (parse succeeds,
`expr_root = NULL`).

---

## 2. Parse tree, arena, and eval

### 2a. Node types + arena

```c
enum expr_kind { E_LEAF, E_NOTP, E_AND, E_OR, E_SUB };

struct expr_node {
	enum expr_kind kind;
	const char    *name;     /* E_LEAF only; borrowed from expr_str argv memory */
	const char    *value;    /* E_LEAF only; NULL ⇒ params "" (bare NAME) */
	int            kids_n;   /* 1 for E_NOTP; 1..REC_QUERY_MAX_AXES+1 for folds */
	struct expr_node *kids[REC_QUERY_MAX_AXES + 1];
	rec_set_t     *set;      /* memoized eval result (sealed), NULL until eval */
};
```

- Arena: `static struct expr_node expr_arena[512]; static int expr_used;`
  bump-allocated by the parser (never freed individually; process lifetime).
  Overflow → parse error `too many expression nodes` (512 covers 8 axes × 64
  uses each).
- `expr_names[REC_QUERY_MAX_AXES][QMAP_AXIS_ROSTER_MAX]` + `expr_n`: distinct
  axis names seen by the parser, deduped in order — this drives the D9
  load-set extension (see §4). Cap `REC_QUERY_MAX_AXES` → parse error
  `too many distinct axes (max 8)`.

### 2b. Lexer

Single cursor `static const char *lx;` walking `expr_str`. One lookahead at a
time; `qmap_expr_peek_tok()` materializes `qmap_cur_tok` (a static
`{ enum tok t; const char *word; size_t len; }`). Tokens:

```
enum tok { T_EOF, T_LP, T_RP, T_KW, T_NAME };
```

- skip spaces; `(`/`)` single-char tokens; EOF ⇒ `T_EOF`.
- word = run of non-space, non-`=`, non-`(`/`)`. `strncmp` against exactly
  `AND`/`OR`/`EXCEPT`/`NOT` (uppercase-only) ⇒ `T_KW`; else `T_NAME`.
- After a NAME, optional `=`: consume it and parse VALUE (see §1 rules:
  quote opens → verbatim to the close quote, `unterminated quote` error;
  unquoted → consumes until an unquoted `(`/`)` or a whitespace-delimited
  keyword word; interior spaces kept; empty VALUE ⇒ empty string). The VALUE
  is stored as a second word on the same token.
- Keywords are whitespace-separated by construction (the word run ends at a
  space).

### 2c. Parser (recursive descent; all fns return `struct expr_node *` or
NULL; on failure they fill `static char qmap_expr_err[128]` and pass-1 prints
`qmap: -X: %s` then exits 1)

```c
static struct expr_node *qmap_expr_setexpr(void);  /* orexpr (EXCEPT orexpr)* → SUB */
static struct expr_node *qmap_expr_orexpr(void);   /* andexpr (OR andexpr)*   → OR  */
static struct expr_node *qmap_expr_andexpr(void);  /* notexpr (AND notexpr)*  → AND */
static struct expr_node *qmap_expr_notexpr(void);  /* NOT notexpr | primary   → NOTP */
static struct expr_node *qmap_expr_primary(void);  /* '(' setexpr ')' | axis  → LEAF */
static struct expr_node *qmap_expr_leaf(void);     /* NAME ('=' VALUE)? + expr_names add */
```

- `setexpr`: `n = orexpr()`; while next is `T_KW EXCEPT`: consume, parse
  `orexpr()`, append to a SUB node (children = n + each operand). This is
  the ONLY place `EXCEPT` is consumed — NOT collects left-to-right.
- `orexpr`/`andexpr`: identical fold shape over `OR` / `AND`.
- `notexpr`: if `T_KW NOT` → consume, child = `notexpr()`, wrap E_NOTP
  (NOT is unary; it may chain: `NOT NOT alpha`). Else `primary()`.
- `primary`: if `T_LP` → consume, parse `setexpr()`; require `T_RP` else
  `expected ')'`. If the paren is immediately empty (`T_RP` right after
  `T_LP`) → `expected expression`. Else `qmap_expr_leaf()`.
- `qmap_expr_leaf`: `T_NAME` required (else `unexpected token '<tok>'`,
  tok text trimmed to 32 chars); collect into `expr_names` (dedup, cap);
  optional `=VALUE`; arena node.
- **`A NOT B` legality guard:** after any of `orexpr`/`andexpr`/`notexpr`
  returns at a position where `EXCEPT`, `)`, or EOF is expected, a `T_KW NOT`
  may only be valid as `notexpr`'s *leading* operator. Production: `notexpr`
  is reached only from `andexpr`'s start; a `NOT` seen anywhere else (in
  `setexpr`/`orexpr`/`andexpr` loop positions or as the next token after a
  completed operand) is a parse error → `unexpected token 'NOT' (use EXCEPT
  for set difference)`. This is naturally produced by the grammar (no
  separate state machine) — verify with test rows 19b.
- Entry: `int qmap_expr_parse(const char *expr)` — resets arena + cursor;
  whitespace-only/empty ⇒ success with `expr_root = NULL` (unarmed);
  otherwise `expr_root = qmap_expr_setexpr()`; a trailing garbage token ⇒
  `unexpected token '<tok>'`.

### 2d. Eval

`static rec_set_t *qmap_expr_eval(struct expr_node *n)` — memoizes into
`n->set` (each node evaluated exactly once per run, no shared sub-trees).
On any failure it records a named error and returns NULL (caller frees
everything it owns and exits 1).

- **E_LEAF**: `slot = qmap_axes_find_slot(n->name)` (pre-validated already,
  §3); `params = axis->decode ? axis->decode(n->value) :
  (void *)(n->value ? n->value : "")`; `s = rec_set_new(); axis->fill(ctx,
  params, s); rec_set_seal(s);` `fill < 0` → named error. First leaf with
  `axis->rank` (preorder) also records the RANKER: `ranker_axis = axis;
  ranker_params = params` — used by the rank pass; when every leaf is
  filter-only, `ranker_axis == NULL` ⇒ pure-filter render.
- **E_NOTP(c)**: `d = rec_set_new(); rec_set_subtract(d, UNIVERSE,
  eval(c));` (UNIVERSE = `rec_set_new(); rec_set_fill_qmap_iter(U,
  prim_hd); rec_set_seal(U);` built once per composed run).
- **E_AND / E_OR / E_SUB**: fold — `acc = eval(kids[0])`; for i>0:
  `t = eval(kids[i]); d = rec_set_new();` then
  `rec_set_intersect|union|subtract(d, acc, t)`; `rec_set_free(t);
  rec_set_free(acc); acc = d;`.
- Every intermediate `d` is new; operands freed after each join; exactness /
  approx propagation inherited from the kernel joins (documented in rec.h).
- Freeing: `qmap_expr_free_tree()` walks the arena, `rec_set_free`ing every
  `n->set`; called at the end of each composed get so a second armed `-g .`
  in the same invocation (after interleaved `-p` writes) evaluates fresh.

---

## 3. The composed get — `qmap_composed_get(void)`

Called from pass-2 `case 'g':` iff `optarg == "." && expr_root`; else
`gen_get(optarg)` (byte-for-byte classic). `expr_root` non-NULL only when
armed (`-X` present and nonempty).

Order of operations:

1. **Primary ref-law check:** `qmap_get_ktype(prim_hd) != QM_HNDL` →
   `qmap: composed -g . needs an :a:-type primary`, exit 1.
2. **Pre-validation (named errors, exit 1)** for every heap node in the
   tree (`kind == E_LEAF`):
   - `qmap_axes_find_slot(name) < 0` → `qmap: axis '%s': slot not found`
   - `!axis || !axis->fill` → `qmap: axis '%s': no fill function`
   - `!axis->ctx` → `qmap: axis '%s': no ctx (not bound)`
3. **Eval** (re-seed `ranker_axis = NULL`; reset all memo `set` fields) the
   tree → final sealed `rec_set_t *R` (`rec_set_count(R)` refs).
4. **Rank or pure-filter:**
   - `ranker_axis` set → ranked: `set_count = rec_set_count(R);
     k = top_k ? top_k : set_count;` (`rec_rank_new` returns NULL when
     `top_k == 0` — `k` is always ≥ 1 here, and `k == 0` only when
     `set_count == 0`, which short-circuits before allocating);
     `board = rec_rank_new(k, min_score);` arrays
     `refs = malloc(k * sizeof(*refs)); scores = malloc(k * sizeof(*scores));`;
     for each ref in `R` (`rec_set_at` + `rec_set_count`):
     `ranker->rank(ranker_ctx, ranker_params, ref, &s) == 0` →
     `rec_rank_push(board, ref, s)`; `n = rec_rank_sorted(board, refs,
     scores)`.
   - else pure-filter: `R` is already sealed (asc-ref); walk
     `rec_set_at(R)` directly, no scores, no board.
5. **Render** (locked format): per result `ref`:
   `rec = qmap_get(prim_hd, &ref)`:
   - missing → `dangling++`, skip (stderr count + exit 0, never a render
     line);
   - ranked: `printf("%u %f ", ref, score)`; pure: `printf("%u ", ref)`;
     then `qmape_print(prim_hd, VALUE, rec); putchar('\n')` (`qmape_print`
     keeps the type tables honest for `s`/`u` value types).
6. **Dangling epilogue:** `if (dangling) fprintf(stderr,
   "qmap: %d refs skipped: no primary record\n", dangling);` → exit 0.
7. `-k` / `-r` / `-x` are ignored under an armed `-g .` (composed path never
   consults `print_keys` / `reverse` / `bail`).
8. **Free**: `rec_set_free(R)` (intermediates were freed in the fold),
   `rec_set_free(UNIVERSE)`, `qmap_expr_free_tree()`, `free(refs)`,
   `free(scores)`. Return `EXIT_SUCCESS` → `main` stores it in `rc` (see the
   `main()` wiring in §4: plain `int rc` in `main`, assigned by the
   `case 'g':` branch).

**Exit codes:** `main` gains `int rc = EXIT_SUCCESS;` set by the composed
path; all named errors above `exit(EXIT_FAILURE)` (matches the CLI's
existing style — `_qmape_type`, `gen_open`). Final `return rc;` replaces
today's implicit fall-off.

**Edge: `rec_set_count(R) == 0`** → nothing to rank/render and no boards
are allocated (malloc(0) avoided); still emits the dangling epilogue if any
dangling were seen (impossible — zero refs yields zero lookups). Score
floor `-b` only bites ranked mode (pure-filter has no scores to floor).

---

## 4. `src/qmap.c` concrete edits

**Delete:** the `-Q` guard in `main` (`argv[1]=="-Q"`), `qmap_recall_query`,
`rq_long_opts`, the `RQ_OPT_*` enum, `QMAP_RQ_DEFAULT_TOP` /
`QMAP_RQ_ALL_TOP`, `qmap_rq_usage`, `qmap_rq_parse_join`,
`qmap_rq_dlopen_env_one`. Outcome: `grep -E '\-Q|--dl|--open|--axis|
--params|--combine|qmap_rq|RQ_OPT' src/` returns 0.

**`optstr`:** `"kxla:q:p:d:D:g:m:c:rR:L:X:t:b:?"`

**`cli_long_opts`:** add `{ "top", required_argument, NULL, CLIP_OPT_TOP }`,
`{ "bottom", required_argument, NULL, CLIP_OPT_BOTTOM }`, with
`CLIP_OPT_TOP = 257`, `CLIP_OPT_BOTTOM = 258`. Keep
`CLIP_OPT_LIST_AXES = 256`.

**New globals:** `static const char *expr_str;`
`static struct expr_node *expr_root;`
`static char expr_names[REC_QUERY_MAX_AXES][QMAP_AXIS_ROSTER_MAX];`
`static int expr_n;` `static size_t top_k;` `static float min_score;`

**Pass-1 switch additions:**

```c
case 'X':
	expr_str = optarg;
	if (qmap_expr_parse(optarg) != 0) {    /* sets expr_root + expr_names */
		fprintf(stderr, "qmap: -X: %s\n", qmap_expr_err);
		return EXIT_FAILURE;
	}
	break;
case 't':
case CLIP_OPT_TOP: {
		char *end; long t = strtol(optarg, &end, 10);
		if (*end != '\0' || t < 0) {
			fprintf(stderr, "qmap: invalid --top value '%s'\n", optarg);
			return EXIT_FAILURE;
		}
		top_k = (size_t) t;
		break;
	}
case 'b':
case CLIP_OPT_BOTTOM: {
		char *end; float b = strtof(optarg, &end);
		if (end == optarg) {
			fprintf(stderr, "qmap: invalid --bottom value '%s'\n", optarg);
			return EXIT_FAILURE;
		}
		min_score = b;
		break;
	}
```

**`qmap_axes_setup`:** extract the env-libs dlopen loop into
`static void qmap_axes_dlopen_env(int quiet)`; in setup, after roster
handling, extend the load loop over `roster_names` **then** `expr_names`
(both deduped by `strcmp`, combined cap `REC_QUERY_MAX_AXES` names) so an
`-X`-only invocation loads + binds its axes without `@` (D9). The sidecar
write-if-absent still uses `roster_names` **only** — expr names must never
persist into the roster.

**`--list-axes` standalone (no file):**

```c
if (list_axes && optind >= argc) {   /* right after pass-1 loop, before the argc check */
	qmap_axes_dlopen_env(1);
	qmap_axes_list();
	return EXIT_SUCCESS;
}
```

(with a file → the existing path: `gen_open` → `setup` → `list` → exit.)

**Pass-2 switch:** add

```c
case 'X': case 't': case 'b': break;             /* passive config, parsed already */
case CLIP_OPT_TOP: case CLIP_OPT_BOTTOM: break;
```

and change `case 'g':` to:

```c
case 'g':
	if (!strcmp(optarg, ".") && expr_root)
		rc = qmap_composed_get();
	else
		gen_get(optarg);
	break;
```

**`usage()` additions** (mirror in the doxygen `### Options` block):

```
        -X EXPR          composed set query (arms -g .)
                         leaves NAME[=VALUE]; operators ( ) AND OR EXCEPT NOT
                         (uppercase); A EXCEPT B = setminus; NOT X = complement;
                         unquoted VALUE ends at an operator or paren —
                         quote it to include operator words
        -t N             cap result count (0 = all)
        -b F             score floor
        --list-axes      list loaded axes and exit
```

**`main()` wiring:**

```c
int
main(int argc, char *argv[])
{
	/* -Q guard DELETED here; it now falls through to the flat CLI. */
	...
	int rc = EXIT_SUCCESS;          /* new */
	...
	while (pass-1 loop) { ... }     /* parses -X/-t/-b + long opts */
	if (list_axes && optind >= argc) {
		qmap_axes_dlopen_env(1);
		qmap_axes_list();
		return EXIT_SUCCESS;
	}
	if (optind >= argc) { usage(*argv); return EXIT_FAILURE; }
	fname = argv[optind];
	optind = 1;
	prim_hd = gen_open(fname, flags);
	srand(time(NULL));
	qmap_axes_setup();              /* load set = @ ∪ roster ∪ expr_names */
	if (list_axes) { qmap_axes_list(); return EXIT_SUCCESS; }
	while (pass-2 loop) {
		...
		case 'g':
			if (!strcmp(optarg, ".") && expr_root)
				rc = qmap_composed_get();
			else
				gen_get(optarg);
			break;
	}
	return rc;                      /* today's implicit fall-off replaced */
}
```

- `rc` is thread-local-ish file scope is unnecessary; a plain `int rc`
  shadowed nowhere, defaulting `EXIT_SUCCESS`; `qmap_composed_get` also
  returns `EXIT_SUCCESS`/`EXIT_FAILURE` (failure via its own `exit(1)` on
  named errors, per the CLI's existing style).
- Pass-2 `case 'p'/'d'/'D'` still clears `QH_RDONLY` before `gen_open` for
  the primary (today's code flips flags on the first write op; unchanged).
- Composition of `-X alpha -g . ... -p 9:nine ...` in ONE argv works because
  pass-2 runs ops in argv order and pass-1 already opened the primary
  read-write when a write op exists (the `p/d/D` flags logic runs during
  pass-1's option scan).

**`qmap_expr_parse` failure path (pass 1):** errors carry a message in
`qmap_expr_err` and pass-1 emits `qmap: -X: <msg>`, returns
`EXIT_FAILURE` — before any primary is opened, so parse errors cost
nothing.

---

## 5. `src/librec_axis_fold.c` (new)

Three by-name axes in one `.so`, loaded in tests via
`QMAP_AXIS_LIBS=$PWD/lib/librec_axis_fold.so` (the exact multi-axis-in-one-`
.so` pattern the mock + roster §5 already establish — this works around the
shared mk `LIB`-obj aggregation bug; see the comment in `librec_axis_mock.c`).
`rec_axis_open(spec)` opens the alongside-default `<dir>/<name>.db` as a
plain `:a:u` store; `fill` drains its keys (refs); `rank` scores any ref by
value; `decode` NULL (raw VALUE forwarded whole-string):

```c
#include <ttypt/rec.h>
#include <ttypt/qmap.h>
#include <stdint.h>
#include <stdlib.h>

static int
fold_fill(void *ctx, void *params, rec_set_t *out)
{
	(void) params;
	if (!ctx) return -1;
	int rc = rec_set_fill_qmap_iter(out, (uint32_t)(uintptr_t) ctx);
	rec_set_seal(out);
	return rc;
}

static int
fold_rank(void *ctx, void *params, rec_ref_t ref, float *score)
{
	(void) ctx; (void) params;
	*score = (float) ref;
	return 0;
}

__attribute__((constructor))
static void fold_init(void)
{
	static const rec_axis_t alpha = { "alpha", fold_fill, fold_rank, NULL, NULL };
	static const rec_axis_t beta  = { "beta",  fold_fill, fold_rank, NULL, NULL };
	static const rec_axis_t pure  = { "pure",  fold_fill, NULL,      NULL, NULL };
	rec_axis_register(&alpha);
	rec_axis_register(&beta);
	rec_axis_register(&pure);
}

/* Alongside-default spec: <primary-dir>/<name>.db — open as an a:u store.
 * Database name MUST be "hd" with the CLI mask: qmap files namespace
 * records by dbid = XXH32(database) and the CLI seeds via gen_open(...,
 * "hd", ...); any other name loads nothing (and exit-save would truncate). */
void *
rec_axis_open(const char *spec)
{
	if (!spec) return NULL;
	uint32_t hd = qmap_open(spec, "hd", QM_HNDL, QM_U32,
			(32768 - 1), QM_AINDEX);
	return (void *)(uintptr_t) hd;
}
```

---

## 6. `Makefile`

```make
LDLIBS-librec_axis_fold := -lqmap

lib/librec_axis_fold.${SO}: src/librec_axis_fold.c lib/libqmap.${SO} lib
	${cc} ${CFLAGS} ${CFLAGS-LIB} -shared -o $@ src/librec_axis_fold.c ${LDFLAGS} ${LDLIBS-librec_axis_fold}

all: lib/librec_axis_mock.${SO} lib/libstub.${SO} lib/libzed.${SO} lib/librec_axis_fold.${SO}
```

---

## 7. `test-cli.sh` — rewrite (RED-first acceptance spec)

Seeding (envs clean): four refs in `demo.db:a:s` (values `one..four`), and
`alpha.db` = refs 1,2,3 / `beta.db` = 2,3,4 / `pure.db` = 1,2 as `:a:u`.
Then set `QMAP_AXIS_LIBS=$PWD/lib/librec_axis_fold.so`,
`QMAP_AXIS_PATH=./lib`, `LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH`.

Rank scores = ref value; `%f` renders 6 decimals. Expected outputs:

| # | Invocation | Expected stdout |
|---|---|---|
| 1 | `--list-axes "$td/demo.db@alpha,beta,pure:a:s"` | `slot  name  …   0 alpha  y y y / 1 beta  y y y / 2 pure  y n y` |
| 2 | `--list-axes` (no file) | same 3 rows (env axes) |
| 3 | `-X alpha -g . "$td/demo.db:a:s"` (bare name, no `@`) | `3 3.000000 three\n2 2.000000 two\n1 1.000000 one` |
| 4 | `-X "alpha AND beta"` | `3 3.000000 three\n2 2.000000 two` |
| 5 | `-X "alpha OR beta"` | `4 4.000000 four\n3 3.000000 three\n2 2.000000 two\n1 1.000000 one` |
| 6 | `-X "alpha EXCEPT beta"` | `1 1.000000 one` |
| 7 | `-X "NOT alpha"` (complement) | `4 4.000000 four` |
| 8 | `-X "(alpha OR beta) AND pure"` | `2 2.000000 two\n1 1.000000 one` |
| 9 | `-X "alpha OR (beta AND pure)"` | `3 3.000000 three\n2 2.000000 two\n1 1.000000 one` |
| 10 | `-X "alpha OR beta" -g . "$td/demo.db:a:s" -t 2` | `4 4.000000 four\n3 3.000000 three` |
| 11 | `-X "alpha OR beta" -g . "$td/demo.db:a:s" -b 2.5` | `4 4.000000 four\n3 3.000000 three` |
| 12 | `-X pure -g .` (pure filter) | `1 one\n2 two` |
| 13 | `-X "alpha=hello world"` (VALUE whole-string) | `3 3.000000 three\n2 2.000000 two\n1 1.000000 one` |
| 14 | `-X "" -g .` (unarmed → classic) | `1\n2\n3\n4` (classic `-g .` on `:a:` prints refs — verified byte-identical vs pre-fold binary) |
| 15 | `-g .` / `-g 1` (no `-X`) classic | `1\n2\n3\n4` / `-1` (same verified classic behavior) |
| 16 | `-k -X alpha …` (`-k` ignored) | same as #3 |
| 17 | `-X alpha -g . "$td/demo.db:a:s" -p 9:nine "$td/demo.db:a:s"` (interleave) | #3 lines + `9` |
| 18 | `-X wonka -g . "$td/demo.db:a:s"` | exit 1; stderr contains `unknown axis name` |
| 19 | `-X "(alpha OR beta" -g . …` | exit 1; stderr contains `expected ')'` |
| 19b | `-X "alpha NOT beta" -g . …` | exit 1; stderr contains `use EXCEPT` |
| 19c | `-X "()" -g . …` | exit 1; stderr contains `expected expression` |
| 20 | `-t abc` | exit 1; `invalid --top value` |
| 21 | `-b abc` | exit 1; `invalid --bottom value` |
| 22 | `-X "alpha OR beta" -g . … -t 0` (= all) | same as #5 |
| 23 | envs unset → classic smoke (`-g 1`, `-p`, `-d`, `-r -g`) | classic byte-for-byte (test-cli.sh pins `-g .` refs incl. the interleaved ref 9) |

Error cases assert exit codes + stderr substrings. `test-roster.sh` untouched.

---

## 8. Doc-drift fixes (`mm-plan/PHASE-2-CLI.md`)

> **Status: ALREADY APPLIED in this doc pass.** The implementer does not
> need to redo them; note them for context.

- 2B-3 slice: "…pre-validated (slot valid + `ctx` non-NULL) with a named
  error **before `rec_query_run`**" → **"before running the composed query"**.
- Fold section: add a note — D10's parens/precedence/complement cannot be
  expressed in the linear `rec_query_t`; the CLI evaluates the parsed tree
  on the kernel's own `rec_set_intersect/union/subtract` +
  `rec_rank_*` (still kernel API); `rec_query_run` remains a library API for
  linear consumers and is no longer called by the CLI.

The EXCEPT decision (user, 2026-09-14) is also already reflected in
`CLI-SURFACE-EXAMPLES.md` §4/§5/§11, `PHASE-2-CLI.md` D10/§2B, and
`README.md`'s 2B-3 row; grammar here is normative.

---

## 9. Order & gate

> **Status: DONE 2026-09-14.** Implemented exactly per §§2–4; all gates
> passed on a clean rebuild (zero warnings under `-Wall -Wextra
> -Wpedantic`):
>
> - `make test` fully green: `test.sh` + `test-cli.sh` **25/25**
>   + `test-roster.sh` + rec / rec_axis / rec_axis_store suites.
> - Stale-term grep over `src/` + `include/` = **0** (mock-plugin comments
>   reworded to drop retired `-Q`/`--dl` references).
> - Classic flat CLI byte-identical vs the pre-fold binary (verified by
>   building pristine `qmap.c` from git and diffing behavior).
> - Not committed (per instruction).
>
> Bring-up fixes (deviations from the first RED cut, now reflected in the
> code and in §5/§7 above):
>
> 1. **Fold-plugin dbid.** `rec_axis_open` opened axis stores with database
>    `"fold"` + mask `0xFF` → `qmap_load_file` (dbid-filtered) loaded
>    nothing, and exit-save truncated the file. Fixed to database `"hd"`
>    + CLI mask `(32768 - 1)`, matching `gen_open` — the first file-backed
>    test axis, so this constraint is now documented in §5.
> 2. **`rec_rank_free(NULL)` guard** in the ranked branch (empty result set
>    ⇒ `board == NULL`; `rec_rank_free` dereferences unconditionally).
> 3. **Classic-row expectations** (§7 rows 14/15/23 + `-g 1`): the RED cut
>    guessed value-printing, but verified pristine behavior is ref-led
>    (`-g .` → `1..4`, `-g 1` → `-1`); rows now pin that, and row 23
>    includes the interleaved ref 9.
> 4. **Parser hardening**: `qmap_expr_eval` return type `rec_set_t *`;
>    axis-name length cap (`QMAP_AXIS_ROSTER_MAX`); `qmap_expr_free_tree`
>    clears memo sets only (never resets the arena, so a second interleaved
>    `-g .` still pre-validates).
>
> Original plan (kept for the record): `src/librec_axis_fold.c` + Makefile
> rule were written first; `test-cli.sh` rewritten to §7 and red (old
> `bin/qmap` had no `-X`/standalone `--list-axes`); then the fold in
> `src/qmap.c` (§3 + §4) until green:
>
> 1. ~~Implement the fold in `src/qmap.c`~~ DONE — parser (§2b/§2c), eval
>    (§2d), `qmap_composed_get` (§3), `-Q` + `rq_*` deletion,
>    `optstr`/long-opts/`-X`/`-t`/`-b`, load-set extension (D9), standalone
>    `--list-axes`, `main` `rc`; stale-term grep = 0.
> 2. ~~`make test` green~~ DONE — `test.sh` + `test-cli.sh` + `test-roster.sh`
>    + rec/rec_axis suites; site `make` untouched; no `/usr` installs;
>    classic flat CLI byte-for-byte (rows 14/15/23).
> 3. Docs synced (this pass + status pass). Commit only when asked.