# CHORDS.md — Chord syntax, transp internals, and the detection rework

> Source of truth for what counts as a chord in the site's song format, how the
> `transp` library detects and renders it, and the rework that replaced the
> ad-hoc per-character whitelists with a grammar-based token classifier and a
> parse → render pipeline.

## 1. The song format

A song's `data.txt` is a sequence of lines. There are three line kinds:

| Line kind | Rule |
|-----------|------|
| **Chord line** | Every space-separated token is a chord symbol, a special symbol (`\|` `:` `-`, repeat markers), or a lone `/` separator. |
| **Lyric line** | Any line that is not a chord line. May begin with a verse number (`1.`) which is bolded; `& < > "` are HTML-escaped on render. |
| **Comment / empty** | `%` starts a comment (bolded with `class='comment'`, or dropped with `TRANSP_REMOVE_COMMENTS`, which also drops the following empty line). Empty lines are structural. |

Chord lines and lyric lines alternate; a chord line's spacing is preserved and,
when transposition changes a chord's width, the **spacing queue** realigns the
next lyric line with space/`-` fillers.

## 2. Chord grammar (the detection contract)

A token is a **chord** iff a single left-to-right scan consumes the *entire*
token as `root` followed by zero or more **suffix atoms**.

### 2.1 Root

```
root := [A-G] ['#'|'b']?        -- English note names
      | (Do|Re|Mi|Fa|Sol|La|Si) ['#'|'b']?   -- Latin solfege (also accepted as input)
```

The initial is case-sensitive (`do` is a lyric word, `Do` is a root). Flat and
sharp spellings map to the same chromatic index (`A#` == `Bb` == 10).

### 2.2 Suffix atoms (repeatable, in any order)

| Atom | Meaning | Example |
|------|---------|---------|
| `m` `min` | minor | `Cm`, `Gmin` |
| `M` `maj` | major | `FM7`, `Gmaj7` |
| `-` | minor (Brazilian/Latin dash) | `G-`, `La-` |
| `dim` | diminished | `Gdim`, `Gdim7` |
| `º` (U+00BA) | diminished symbol | `Cº`, `Dº7`, `Gm7(5º)` |
| `h` | half-diminished | `Bh` |
| `aug` `+` | augmented | `Gaug`, `A4+` |
| `sus` `sus2` `sus4` | suspended | `Gsus4`, `G7sus4` |
| digits `5 6 7 9 11 13 …` | extensions | `C5`, `Cmaj9`, `F#m7(11)` |
| `#`/`b` + digits | altered extensions | `b5`, `#5`, `b9` |
| `add` `add9` `add11` | added tones | `Gadd9` |
| `omit` `omit3` … | omitted tones | `Gomit3` |
| `no` `no3` … | omitted tones (paren form) | `Gno3` |
| `( … )` | parenthesized group: digits \| `#` \| `b` \| `º` \| words `no add sus maj dim aug omit` | `A(no3)7`, `G(omit3)`, `Gm7(5º)` |
| `/ root` (must end the token) | slash bass (root validated like a root; bass is **not** transposed) | `G/B`, `E/G#`, `G/Do` |
| `/ digits` | extension slash | `G6/9`, `D9` |

**Whole-token consumption is the guard.** A token is rejected the moment an
unrecognized byte appears, so lyric words can never pass: `Amazing`, `miseri-`,
`córdia`, `Amada`, `C(not)`, `G/thing` are all non-chords.

### 2.3 Special tokens

A token consisting only of `|` `:` `-` (with digits attaching to a leading `|`)
is a repeat marker: `|`, `:`, `-`, `|1`, `|2`, `|10`, `|:`, `:|`. A lone `/` is
a neutral **separator** (does not force a line to be a chord line). A token that
starts with a special char is split there (`|C` → special `|` + chord `C`;
`/G` → separator `/` + chord `G`).

## 3. Transposition & notation preferences

- **Key detection:** the first chord in a song sets `ctx->key` (chromatic
  0–11); used by `transp_shift_table`. Only picked up when `key == -1`.
- **Transposition:** roots shift by semitones; suffixes are copied verbatim
  (never transposed); slash bass stays put.
- **Flat display** (`TRANSP_BEMOL`): sharp roots render with flats (`C#` → `Db`).
- **Latin display** (`TRANSP_LATIN`): roots render as solfege (`C` → `Do`) and a
  leading `m` becomes `-` (`Am` → `La-`).
- **Flags** (`transp_flags.h`): `TRANSP_HTML`, `TRANSP_BEMOL`,
  `TRANSP_BREAK_SLASH`, `TRANSP_REMOVE_COMMENTS`, `TRANSP_HIDE_CHORDS`,
  `TRANSP_HIDE_LYRICS`, `TRANSP_LATIN`.

## 4. Architecture

```
input → parse.c ──► song model ──► render.c ──► output
             │        (typed lines/tokens)      (transpose, spacing, html, flags)
             └─ token.c (grammar)
```

| File | Role |
|------|------|
| `token.h` / `token.c` | Chord grammar: `transp_token_analyze()` classifies a token as CHORD / SPECIAL / SEP / NOT_CHORD and extracts the root index + root/suffix spans, the **quality** enum (first quality-bearing suffix atom wins, parens skipped) and the **slash bass** (chromatic index + byte offsets, or `bass=-1`). No qmap; roots map by letter→index arithmetic, paren words are a static list. |
| `parse.h` / `parse.c` | Splits the input into lines and space-tokens (recording leading whitespace per token), splits leading special runs, strips verse-number prefixes, classifies each line (all tokens chord/special/sep → chord line), detects `key`. Pure — no flags, no output. |
| `render.h` / `render.c` | Walks the model: transposes roots, emits one `<b>` per chord line with verbatim spacing (diff absorb/add), manages the spacing queue for lyric lines (space/`-` fillers; `not_special` resets per chord line), verse numbers, HTML escaping, comments, `TRANSP_BREAK_SLASH`, hide flags, empty-line and `skip_empty` handling. Per-song state is local. |
| `transp.c` | Public API: `transp_init` / `transp_buffer` (parse → render) / `transp_get_key` / `transp_reset_key` / `transp_shift_table` / `transp_free`. `ctx` holds only `{ key, i18n_table }`. |

The refactor removed the `libqmap` dependency entirely and retired the three
overlapping validators (`valid_modifier`, the `*eoc` first-char switch, the
slash-bass special case) plus the mid-line `goto no_chord` reset.

## 5. Behavior-change log

1. `Gm7(5º)` (any `(…º)`) is now a valid chord — previously the `º` byte pair
   was only accepted at the top level, so the whole chord line degraded to lyrics.
2. `Gaug`, `Gmaj`, `Gsus4`, `Gomit3`, `Gno3`, `Gh`, `G-` are now consistent
   outside parens (previously each suffix's first letter had to hit a small
   switch; `Gaug` and friends were silently rejected).
3. Latin solfege (`Do Re Mi Fa Sol La Si`) is accepted as **input** roots (was
   output-only).
4. `not_special` resets per chord line — one repeat marker no longer disables
   lyric alignment for the whole buffer.
5. Old whitelist false positives (`Amada`, `Amiga`, `Amina`) are no longer
   detected as chords.
6. Chord lines never start with a verse number (a leading `N.` demotes the rest
   to lyrics) — an unusual format that previously mixed `1.` + chords.

## 6. Testing

`make -C mods/song/lib/transp test` (run from `mods/song/lib/transp`; wired into
`mods/song/test.sh` line 5 — `make -C lib/transp test` — which runs under the
root `make test` → `make unit-tests` path via `mods.load`). All 58 tests pass,
ASan/UBSan-clean.

The suite in `test_transp.c` is ordered as a syntax matrix:

- **A. Roots** — plain, sharps, flats, `A#`/`Bb` equivalence.
- **B. Qualities/extensions** — minor, maj7, `7M`/`M7`, `m7b5`, `dim`, `º`,
  `4+`, paren extensions, `sus`/`add`/`omit`, power chords, slash bass
  (English + Latin).
- **C. Specials** — `|: C G :|`, `|1 … |2 … - |`, lone `| : -`.
- **D. Lines & structure** — chord/lyric interleave, numbered verses, comments,
  empty lines, lyric-syllable rejection (`córdia`, `es-tá`, `miseri-`),
  invalid paren/slash rejection.
- **E. Transpose & prefs** — negative transpose, key detection, bemol, Latin
  output, shift table, HTML escaping, break-slash, hide flags.
- **F. Desired (the rework's contract)** — `Gm7(5º)` bolded + transposed,
  consistent `aug`/`maj`/`sus`/`omit`/`no`/`h`/`-`, Latin input roots, combo
  grammar (`Gmaj7(9) F#m7b5(5º) Csus2(omit3) Bb/D`), the full user song.
- **G. Regression guards** — `Amada Amiga Amina` stays lyrics; `user_song_full`
  and `user_song_bold_exact` pin the real user song (see below).
- **H. Model (quality + bass)** — the first token-level section: direct
  `transp_token_analyze` assertions on the `quality` / `bass` / `bass_off` /
  `bass_len` fields (`model_quality_matrix`, `model_first_quality_atom_wins`,
  `model_slash_bass`). Added 2026-08-16 with the model rework; the rest of the
  suite stays pipeline-string based.

`user_song_bold_exact` pins the user song's first stanza byte-for-byte (bold
HTML + plain, transpose 0 and +1): chord lines stay one `<b>` block with
verbatim spacing, `Bbm7` renders `A#m7` (sharp default), the spacing queue
realigns the lyric lines with `-` fillers (`No Senhor-`, `có-rdia …
re--den---ção.`), the trailing space on the last lyric line is preserved, and
the detected key is F. The expected strings were captured from a probe run and
pinned — not hand-assumed.

The whole matrix is written first; sections A–E must pass against the old code
and F–G fail (the documented gap), then the refactor makes everything pass.

## 7. Related docs

- `docs/OVERVIEW.md` — stack and unbreakable rules (read first).
- `docs/DESIGN.md` — encapsulation / "evoke, don't reimplement".
- `docs/CONVENTIONS.md` — C style, handlers, memory.

## 8. Implementation status & spec (current state of the rework)

### 8.1 Status

DONE:
- `CHORDS.md` (this file).
- Full test matrix added to `mods/song/lib/transp/test_transp.c` (Sections A–G
  at the time, later joined by Section H; the existing 30 tests are
  unchanged). Tests assert **future-correct behavior** — if a
  case fails today, it must be left failing (it is the spec for the refactor).
- Baseline run against the current `transp.c` was attempted. `make -C
  mods/song/lib/transp test` aborts on the first failing `assert`; the baseline
  has multiple intentional failures. Known wrong "pass-today" assumption found
  during baseline:
  - `G6/9` is **rejected today**: the `space_after = slash_after` logic makes
    the `/` a token boundary, then the lone `9` fails the chord_db lookup and
    demotes the whole line to lyrics. `quality_sus_add_extension_slash`
    asserts `"<b>Gadd9 Gsus2 Gsus4 G6/9</b>"` → fails today, must pass after.
  - `D9/F#` **passes today** (slash-bass fallback validates `F#`).

IN PROGRESS (2026-08-16):
- `token.h`/`token.c` (grammar classifier) — DONE, spec §8.2 implemented as
  written.
- `parse.h`/`parse.c` (song model + key detection) — DONE, spec §8.3
  implemented as written.
- `render.h`/`render.c` (transform + output) — DONE, spec §8.4 implemented as
  written.
- `transp.c` rewrite (qmap-free public API, spec §8.5) and the Makefile
  (spec §8.6) — DONE.
- `make -C mods/song/lib/transp test` — GREEN (58/58, ASan/UBSan-clean).
- `make format` — DONE (reformats mods/ + external/bud; transp files were the
  only drift, still green after).
- **Model rework (quality + bass) — DONE 2026-08-16.** `transp_token_info_t`
  gained a `transp_quality_t` enum + `bass`/`bass_off`/`bass_len` fields; the
  classifier fills them (see §8.2). First token-level test section (H) added —
  the pipeline string tests (A–G) assert only rendered output, so this was the
  first direct check of the model fields. One bug surfaced and was fixed: the
  non-slash path left `bass` uninitialized stack memory, then the naive "init
  in the final writes" fix **clobbered** the slash-bass capture (bass defaults
  now initialize right after the root parse, before the suffix loop — finding
  10).
- **Hardening pass — DONE 2026-08-16.** Two robustness bugs in `render.c`
  fixed (findings 8–9): a stack overflow on the `modbuf[256]` buffer and a
  `q_push` double-free on partial realloc failure. The old
  `i18n[0][0]=='D'` Latin-detection hack was dropped in the same pass.
- Root `make test` wiring — VERIFIED: transp suite runs under `make unit-tests`
  → `mods/song/test.sh` line 5 `make -C lib/transp test` (`song` is in
  `mods.load`). Requires axil on `:8080` for the mods test.sh, but the transp
  C tests run first and are server-free.
- **Not yet done: `mods/song/lib/transp/README.md` rewrite** (it documented a
  dead API — old `transp_buffer` signature, `transp_line`, 13 tests,
  `libtransp.so`, `mods/chords`, `tty.pt` — and predated the grammar, the
  model fields, and the 58-test suite). **DONE 2026-08-16** alongside this
  update.
- Not yet verified: `make lint` (aborted mid-run), `make unit-tests` end-to-end
  (server-dependent). AGENTS.md row added 2026-08-16.

Decision log (things the original spec left ambiguous, settled during the
rework — keep these in sync with the code):
- **Verse-number demotion** (§5.6 vs §8.3): a line with a `N.` prefix whose
  remainder is all chords is demoted to a lyric line in `parse.c` (tokens
  dropped, `is_chord_line=0`). §8.3's classification text is read through
  §5.6's rule.
- **`not_special` is cleared only by SPECIAL tokens**, not by SEP (`/`) — SEP is
  a neutral separator per §2.3. The §8.4 "SPECIAL/SEP → set not_special=0"
  phrasing is a shorthand; the old code only cleared on repeat markers and the
  port keeps that.
- **Whitespace-only lines** are lyric lines (zero ptokens → not a chord line);
  this preserves the old byte-walk, which emitted the spaces verbatim.
- **Mid-line `%`** is no longer a comment: `is_comment` means `line[0]=='%'`.
  A line like `C %foo` is a lyric line and its `%` is HTML-escaped (old code
  passed mid-line comments through unescaped — the new behavior is stricter).
- **Empty result-string lines** (`strdup("")` from old `proc_line`) are not
  appended; empty line output is `<div> </div>` (HTML) / `\n` (plain), matching
  the old per-line contract. `skip_empty` is per-render local state, so a
  `TRANSP_REMOVE_COMMENTS` flag that never meets its empty line no longer leaks
  into the next song.
- **Trailing newline**: parse drops the empty segment after a final `\n`
  (`"C G\n"` is one line; `"C G\n\n"` is two), matching the old
  `while (line_start && *line_start)` splitter.
- **Spacing queue is a growable array, not a TAILQ**: §8.4's "TAILQ spacing
  queue" is a means to FIFO drain semantics, which the array preserves. It
  also removes the `sys/queue.h` dependency.
- **`transp_render`'s `int *key` is accepted but unused**: key detection
  happens in `transp_song_parse` (`*key` set only when it is `-1`), so the
  parameter exists for interface fidelity (mirrors the `transp_ctx` fields)
  and is `(void)`-cast.
- **Result buffer**: `transp_render` sizes its output from the summed line
  lengths (≥ the old `strlen(input)*8+64` bound) and keeps the 8192-byte
  per-line outbuf floor (bumped to `max(8192, line_len*8+64)` so single long
  lines cannot overflow the historical fixed buffer).
- **`j` counter semantics**: ported exactly from `proc_line` — `j` counts
  lead + root + suffix + absorbed spaces + residual on chord lines, and lyric
  bytes on lyric lines; the "add one space" path deliberately does **not**
  increment `j` (matches the original queue-start arithmetic).
- **Quality model** (2026-08-16): the first quality-bearing suffix atom wins
  (`Gm7(5º)`→MINOR from the `m`, `Fm7b5`→MINOR, `Cmaj9`→MAJOR, `Bh`→HALF_DIM,
  `C5`→POWER, `Gno3`/`Gomit3`→UNDEFINED, no qualifier→MAJOR). Parenthesized
  groups are skipped (locked scope: quality does not consult parens this pass).
  The heuristic mirrors `word_atom`'s longest-first matching so the enum can
  never disagree with the grammar's acceptance.
- **Bass model** (2026-08-16): a trailing `/root` bass is recorded as a
  chromatic index + `bass_off`/`bass_len` byte offsets (render never touches
  it). No slash → `bass=-1, bass_off=0, bass_len=0`. An extension slash
  (`G6/9`) is not a bass — `bass` stays −1. `G#/Bb`-style respelling
  (accidentals) and interval-aware key handling are explicitly future work
  (§9) and were NOT designed in.
- **Bass defaults must precede the suffix loop**: initializing
  `bass=-1/off=0/len=0` inside the final writes block (after the loop) is a
  bug — it overwrites a slash-bass capture. The defaults are set right after
  the root parse.

REMAINING (verification/wiring):
- `make lint` (was aborted mid-run) → `make unit-tests` end-to-end (axil on
  :8080; pre-existing `songbook` step-6 failure unrelated). AGENTS.md row added
  and README rewritten as part of the 2026-08-16 model/hardening pass.

### 8.8 Research findings (2026-08-16 — bugs found while tracing tests to code)

Bugs in the then-unstaged code, confirmed by tracing the A–G matrix and the 30
pre-existing tests against `token.c`/`parse.c`/`render.c`:

1. **`token.c` `parse_root` Latin bug.** The `for` loop breaks with `i`
   holding the **array index** (0–6) into `latin_roots`, not the matched name's
   byte length. `nbytes` is wrong for every Latin root (`Do`→0, `Fa`→3,
   `Sol`→4, `La`→5, `Si`→6) and the `#`/`b` accidental check reads the wrong
   byte. Consequences: `latin_input_roots` / `latin_input_roots_transposed`
   fail, `slash_chord_latin_bass` (`G/Do C/Sol D/Re`) fails because the
   slash-bass guard `pos+1+blen == len` never holds, and `mod_len` underflows
   (size_t) → OOB read on the mod pointer. Fix: set the byte length of the
   matched root name before the accidental check.
2. **`render.c` drops `TRANSP_HIDE_LYRICS` on lyric lines.** `render_lyric_line`
   never checks the flag (§8.4 requires `<div></div>` / nothing) and always
   drains the queue; old `proc_line` returned early and left the queue alone.
   Breaks `hide_lyrics`.
3. **`render.c` strips the verse prefix in plain mode.** It walks from
   `text + verse_len` always; old code only stripped `N.` under HTML. The
   production plain endpoint `/api/song/:id/transpose` would drop verse numbers.
   Fix: offset only in HTML mode.
4. **`Bbm7` test expectations vs sharp-default rendering.** `paren_diminished_fifth`
   and `user_song_full` asserted `Bbm7`, but the grammar maps `Bb`→chromatic 10
   and `chord_str` renders `A#` without `TRANSP_BEMOL` — contradicting
   `roots_flat_display_as_sharp` (`A# Bb` → `A# A#`) and the `Bbm7b5→A#m7b5`
   precedent. **Decision: amended the tests to `A#m7`** (keeps the grammar).
 5. **Makefile `$(OBJ)` rule breaks with multiple sources.** The old
    `$(OBJ): $(SRC) transp.h` + `$(CC) -c -o $@ $(SRC)` only worked because
    `SRC` was a single file; it must become a `%.o: %.c` pattern rule.

Findings from the implementation run (2026-08-16, after §8.8 was first
written — the "DONE" files had **never compiled**; the old Makefile built only
`transp.c`, so `token/parse/render` were dead code until now):

6. **`render.c:365` latent `q_push` arity bug.** The call was
   `q_push(st, &st->q, j, diff - i_absorb)` against
   `static int q_push(transp_queue_t *q, size_t start, size_t len)` — a
   compile error (`-Wincompatible-pointer-types` + "too many arguments").
   Fixed to `q_push(&st->q, j, diff - i_absorb)`. This is why "DONE" status was
   unreliable: nothing had built the new files.
7. **`basic_transpose` fails after the compile fix: "C G" +2 renders as a
   lyric line** — plain output `C G\n`, no transpose, no `<b>` in HTML, and
   `transp_get_key` stays `-1`. Debug (`transp_song_parse` + token dump):
   `is_chord_line=0` and `ntok` is reset to 0 (parse frees tokens on non-chord
   lines), yet `transp_token_analyze("C")` / `("G")` return CHORD in isolation.
   So the defect is in the `parse.c` path (`tokenize_line` / `tokenize_piece` /
   classification), **not** `token.c`. STATUS: under investigation.

Status of the implementation run (updated 2026-08-16, as fixes land):

- **Root cause of finding 7: `pl->ntok` was never incremented.** `grow_tokens`
  returns `&pl->toks[pl->ntok]`, but `tokenize_piece` never bumped `ntok`, so
  every line ended with `ntok==0`, the `pl->ntok > 0` classification guard was
  always false, and every line was demoted to lyrics (tokens freed, `key=-1`).
  **Fixed:** `pl->ntok++` in both the special-split branch and the tail branch
  of `tokenize_piece`. `basic_transpose` then passes.
- After the `ntok` fix the suite passed tests 1–19 but failed
  `no_stray_close_bold_on_lyric_line` (test 20): `"Amazing Grace"` rendered
  `<div><b>Amazing Grace</b></div>` in the suite and `<div>Amazing Grace</div>`
  in isolation. **Root cause: uninitialized `t->info.kind` in `parse.c`.**
  `transp_token_analyze` returns `TRANSP_TOK_NOT_CHORD` **without writing
  `*out`** (token.h's contract is "fills *out only for CHORD"), but the tail
  branch discarded the return value, so `t->info.kind` held realloc garbage —
  usually 0 (accidentally correct), sometimes non-zero (line kept as a chord
  line with garbage roots). The garbage-root read then produced the ASan
  `chord_str` global-buffer-overflow (index −4, `(size_t)(negative % 12)` wraps
  pointer arithmetic back to a negative index) during `test_multiline_input`.
  **One bug, two symptoms.** **Fixed:** `t->info.kind = transp_token_analyze(...)`
  in the tail branch (use the return value, per the header contract).
  Verified: 54/54 PASS, and the suite is clean under
  `-fsanitize=address,undefined`.
- **`render.c` comment tag length bug:** `out_append(..., "<b class='comment'>", 18)`
  passed **18** but the literal is **19** chars — the closing `>` was dropped
  (output showed `<b class='comment'%&lt;b&gt;…`). **Fixed:** 18 → 19.
- **`render.c` did not strip the `%` in HTML comment output.** The test expects
  `<b class='comment'>Intro comment</b>`; the renderer emitted
  `%Intro comment`. **Fixed:** `render_comment_line` advances past a leading
  `%` before escaping (HTML mode only; plain mode still passes the line
  through verbatim). `comment_line_html` then passes.

All 58 tests in `test_transp.c` pass; `make -C mods/song/lib/transp test` is
green and ASan/UBSan-clean; `make format` done; root-`make test` wiring
verified; transp README rewritten. Remaining: `make lint` (aborted mid-run),
`make unit-tests` end-to-end (server on :8080), AGENTS.md row.

Findings from the model + hardening pass (2026-08-16):

8. **`render.c` stack overflow on `modbuf[256]`.** The old code built a
   `char modbuf[256]` with a bare `strcat` loop over suffix atoms. An input
   like `G` followed by 256+ copies of `m` (each 1 byte, e.g. from a
   hand-typed `Gmmmmm…`) overflows it. It was reachable under
   `TRANSP_LATIN`, which re-renders the mod. **Fixed:** no stack buffer — the
   mod is emitted in two pieces (`'-'` + `mod+1`) when the Latin `m`→`-`
   substitution fires, and `latin_m` is tracked alongside. The
   `i18n[0][0]=='D'` Latin-detection hack went away in the same edit (the
   `latin` flag is passed down explicitly now).
9. **`render.c` `q_push` double-free on partial realloc failure.** The
   spacing queue stored `start[]` and `len[]` in two separately-realloc'd
   arrays: `realloc(start)` could succeed while `realloc(len)` failed, leaving
   `q->start` pointing at freed memory that `q_free` then freed again. (Same
   latent bug class as the finding-6 arity bug — untriggered by normal tests,
   found by reading the code.) **Fixed:** a single interleaved
   `{start,len}` array; on realloc failure both pointers stay valid and
   `lyric_fill` was updated to drain the new layout.
10. **`transp_token_analyze` left `bass`/`quality` uninitialized for chords
    without a slash bass.** Section H caught it: `assert_chord_info`'s
    `info` is plain stack memory, so `info.bass == -1` compared against
    garbage. The first fix (initialize in the final writes block) then made
    `model_slash_bass` fail with `info.bass == 8` because the defaults
    overwrote the slash-bass capture. **Fixed:** defaults initialize right
    after the root parse, before the suffix loop; the slash-bass branch
    overrides them.

Minor/known-verbatim port deltas (accepted, mostly untested):
- SEP `/` sets `has_chords`, so `/G` renders `<b>/G</b>` (old:
  `<div>/<b>G</b></div>`).
- `TRANSP_HIDE_CHORDS` HTML emits `<div> </div>` (old: `<div></div>`; spec §8.4
  says the space form).
- Trailing spaces after the last chord token of a chord line are dropped
  (old byte-walk emitted them); no queue effect (EOL diffs push nothing).
- Latin `m`→`-` is applied blindly to any mod starting with `m`, so `Gmaj7`
  renders `Sol-aj7` under Latin — pre-existing quirk, ported verbatim.
- `diff` is clamped to ≥0 (old code let it go negative; identical net effect:
  no absorb, no queue push, no space).
- Negative semitones normalization is ported verbatim
  (`+= (1+(s/12))*12`); values below -12 can still produce negative `%` results
  (pre-existing, out of scope).

Production consumer: `mods/song/song.c::song_transpose_root` calls
`transp_reset_key` → `transp_buffer` → `transp_get_key`; both plain (`t=…`) and
HTML (`h=1`) flag paths exist, so the plain-mode verse fix (finding 3) matters.

### 8.2 token.h / token.c — the grammar

```c
/* token.h */
#ifndef TRANSP_TOKEN_H
#define TRANSP_TOKEN_H
#include <stddef.h>

typedef enum {
    TRANSP_TOK_NOT_CHORD = 0, /* lyric word / unparseable */
    TRANSP_TOK_CHORD = 1,     /* root + suffix consumes the whole token */
    TRANSP_TOK_SPECIAL = 2,   /* repeat marker: | : - and |digits variants */
    TRANSP_TOK_SEP = 3,       /* lone '/' */
} transp_tok_kind_t;

typedef enum {
    TRANSP_QUAL_MAJOR = 0,    /* no qualifier, or M / maj / sus / add / aug */
    TRANSP_QUAL_MINOR = 1,    /* m / min / '-' */
    TRANSP_QUAL_HALF_DIM = 2, /* h */
    TRANSP_QUAL_POWER = 3,    /* 5 (no third) */
    TRANSP_QUAL_DIMINISHED = 4, /* dim / º */
    TRANSP_QUAL_UNDEFINED = 5,   /* omit3 / no3 — no third, no usable tag */
} transp_quality_t;

typedef struct {
    transp_tok_kind_t kind;
    int root;            /* chromatic 0-11 (C=0 … B=11), or -1 */
    size_t root_off;     /* byte offset of the root within the token */
    size_t root_len;     /* bytes of the root (1-3: "C", "A#", "Sol") */
    size_t mod_off;      /* == root_off + root_len */
    size_t mod_len;      /* bytes of the suffix (0 for bare roots) */
    transp_quality_t quality; /* first quality-bearing atom (§8.2) */
    int bass;            /* chromatic of slash bass, or -1 if none */
    size_t bass_off;     /* byte offset of the bass within the token */
    size_t bass_len;     /* bytes of the bass ("F#", "Do") */
} transp_token_info_t;

int transp_token_analyze(const char *tok, size_t len, transp_token_info_t *out);
/* returns TRANSP_TOK_CHORD/SPECIAL/SEP/NOT_CHORD; fills *out only for CHORD */
#endif
```

`token.c` details (all static):
- **Root match** `parse_root(s, len, &root, &nbytes)`: returns 1 and fills
  `root` (0-11) + `nbytes`. English: `[A-G]` + optional `#`/`b`. Latin:
  `Do|Re|Mi|Fa|Sol|La|Si` + optional `#`/`b`. Base index: C/Do=0, D/Re=2,
  E/Mi=4, F/Fa=5, G/Sol=7, A/La=9, B/Si=11; `#`→+1, `b`→−1 (mod 12, +12). Case
  sensitive initial (lowercase is lyric). No qmap — plain arithmetic.
- **`transp_token_analyze`**:
  1. If `len` is all chars from `{ '|', ':', '-', digit }` and contains at
     least one of `|: -` → SPECIAL.
  2. Else `parse_root` at offset 0; on failure → NOT_CHORD.
  3. Loop `pos = root_len` over suffix atoms until `pos == len` (→ CHORD):
     - `'/'`: if `parse_root` at `pos+1` consumes to `len` → CHORD (slash bass,
       bass NOT transposed later); else if next is a digit → consume digit run
       (extension slash, e.g. `G6/9`); else NOT_CHORD.
     - `'('`: paren group — scan to `')'`; content may be digits, `#`, `b`,
       `º` (0xC2 0xBA), or words from `{no add sus maj dim aug omit}`; else
       NOT_CHORD.
     - `'#','b'`: consume + following digit run (altered ext, `b5` `#9`).
     - digit: consume digit run.
     - `0xC2 0xBA` (`º`): consume 2 bytes.
     - `'+'`: consume 1.
      - atoms longest-first: `maj min dim aug sus add omit no` (allow optional
        trailing digits for `sus`/`add`/`omit`; bare `sus`/`add`/`dim`/`aug`/`no`
        are valid), then `m M h`, then `'-'` (minor dash, Brazilian/Latin).
      - anything else → NOT_CHORD.
  4. **Defaults first**: `bass=-1, bass_off=0, bass_len=0` are set right after
     the root parse (before the loop) — never in the final writes block, or a
     slash-bass capture is overwritten (finding 10).
- **Bass capture**: in the `'/'` branch, when `parse_root` consumes the tail
  (`pos+1+blen == len`), record `out->bass = b`, `bass_off = pos+1`,
  `bass_len = blen`. Extension slash (`G6/9`) does not touch the bass fields.
- **Quality** `chord_quality(mod, mod_len)`: a static longest-first walk over
  the suffix mirrors `word_atom`. First quality-bearing atom wins:
  `m`/`min`/`-`→MINOR, `dim`/`º`→DIMINISHED, `h`→HALF_DIM, `5`→POWER
  (`omit3`/`no3`→UNDEFINED, checked before the `5`-as-extension rule),
  `M`/`maj`/`sus`/`add`/`aug`→MAJOR, nothing→MAJOR. Paren groups (`(` … `)`)
  are skipped. The scan only sets a field the token already accepts — it can
  never turn a NOT_CHORD into a CHORD.

### 8.3 parse.h / parse.c — the song model

```c
/* parse.h */
#ifndef TRANSP_PARSE_H
#define TRANSP_PARSE_H
#include "token.h"

typedef struct {
    const char *text;            /* borrowed, points at first non-space byte */
    size_t len;
    size_t lead;                 /* bytes of leading whitespace before it */
    transp_token_info_t info;    /* CHORD only (kind TRANSP_TOK_CHORD) */
} transp_ptoken_t;

typedef struct {
    int is_chord_line;   /* all tokens CHORD/SPECIAL/SEP */
    int is_comment;      /* first byte '%' */
    int is_empty;
    int has_verse;       /* lyric/chord line begins "N." */
    size_t verse_len;    /* bytes of "N." prefix */
    const char *text;    /* borrowed; line with \r\n trimmed, verse NOT stripped */
    size_t len;
    transp_ptoken_t *toks; /* malloc'd; chord lines only, else NULL */
    size_t ntok;
} transp_pline_t;

typedef struct {
    transp_pline_t *lines;
    size_t nlines;
} transp_song_t;

int transp_song_parse(const char *input, transp_song_t *song, int *key);
/* 0 on success, -1 on OOM. *key = chromatic of first chord token, or -1. */
void transp_song_free(transp_song_t *song);
#endif
```

`parse.c` details:
- Works on a **borrowed copy**: `transp_buffer` strdups the input first; parse
  records `const char *` pointers into it; render runs before the free.
- Split on `\n`, trim trailing `\r`. `is_empty` if `len==0`; `is_comment` if
  `line[0]=='%'`.
- **Verse prefix**: if `isdigit(line[0])` and `strchr(line,'.')`, then
  `verse_len = dot+1 - line` (matches today's `sim` semantics). Tokenize the
  text *after* the prefix.
- **Tokenize** a line's remainder on runs of `' '`; record `lead` per token.
  Then **split leading special runs** off each space-token (so `|:`, `|1`,
  `|C`, `/G` behave like today's byte walk): while first char is `|` consume it
  plus following digits as one SPECIAL token; while it is `:` or `-` consume one
  char as SPECIAL; while it is `/` consume one char as SEP. Classify the tail
  with `transp_token_analyze`.
- **Classify** the line: all tokens CHORD/SPECIAL/SEP → `is_chord_line=1`
  (tokens stored). Else lyric line (`toks=NULL`). Lone `/` is SEP → neutral.
- **Key**: first token with `kind==TRANSP_TOK_CHORD && root>=0` sets `*key`
  only when `*key==-1`.

### 8.4 render.h / render.c — transform + output

```c
/* render.h */
#ifndef TRANSP_RENDER_H
#define TRANSP_RENDER_H
#include "parse.h"
#include "transp_flags.h"

char *transp_render(const transp_song_t *song, int semitones, int flags,
                    char **i18n_table, int *key);
/* malloc'd string; NULL on OOM. i18n_table + key live in transp_ctx (above). */
#endif
```

`render.c` details — port the current `proc_line` semantics **verbatim** onto
the model; keep the `strcat`-append pattern and the per-song local state:
`skip_empty`, `not_special` (reset to 1 at each chord line, set 0 if the line
has a SPECIAL token), and the spacing queue (drain at end). **Hardening
(2026-08-16):** the queue is a **single interleaved `{start,len}` growable
array** (not two arrays — a partial realloc failure used to double-free, and
not the spec's TAILQ — `sys/queue.h` is gone); the per-line outbuf is
`max(8192, line_len*8+64)`; and the **`modbuf[256]` stack buffer is removed**
(fixed overflow, findings 8–9).
- **Empty line**: if `skip_empty` pending → consume it, emit nothing. Else HTML
  `<div> </div>` (space guard), plain `\n`.
- **Comment** (`%`): `TRANSP_REMOVE_COMMENTS` → set `skip_empty=1`, still emit
  `<div> </div>`. Else HTML: `<div><b class='comment'>…escaped…</b></div>`;
  plain: passthrough.
- **Chord line**: emit `<div>` (HTML); lazy single `<b>` opened before the first
  token, closed at line end. For each token: emit `lead` spaces verbatim
  (`TRANSP_HIDE_LYRICS`: collapse to a single space after the first chord, i.e.
  `no_space && has_chords` logic); SPECIAL/SEP → emit raw text, `j += len`,
  set `not_special=0`; CHORD → `newroot=(root+semitones)%12`,
  `new_cstr=chord_str` (bemol: `#` spelling → flat; Latin: `m`→`-`), emit
  `new_cstr` then the mod **without a stack buffer** (Latin `m`→`-` emits
  `'-'` + `mod+1`; `latin_m` tracked per chord). **Diff/absorb** (port
  exactly): `diff = strlen(new_cstr) - root_len`; if `diff>0` absorb
  `i=min(diff, next_lead)` spaces from the following token's lead (`j+=i`); if
  at EOL `j+=diff`; if the following char is not space/slash/NUL add one `' '`
  (`diff++`); then if `i<diff` push `{start=j, len=diff-i}` to the queue and
  `j+=len`. Slash bass and `º`/`-` suffixes are copied verbatim (never
  transposed). `TRANSP_HIDE_CHORDS`: emit nothing but `<div> </div>` (no queue
  entries). `TRANSP_HIDE_LYRICS` is fine (normal chord rendering).
- **Lyric line**: `TRANSP_HIDE_LYRICS` → `<div></div>` (no space — match today).
  HTML `<div>`; if `has_verse` emit `<b>…N.…</b>` (escaped). Walk bytes from
  `text+verse_len`, `j` from 0: consume queue entries while `j>=start` (if
  `not_special`: fill `start..start+len` with `' '` if previous byte was space
  else `'-'`); `'<'` → emit rest raw, `j=0`, end line (today's passthrough);
  `TRANSP_BREAK_SLASH` `/ ` → `\n`, `j=0`; else HTML-escape `& < > "`, `j++`.
  Close `</div>`.
- Plain mode: every line ends `\n`.

### 8.5 transp.c — public API (rewrite)

```c
struct transp_ctx { int key; char **i18n_table; };  /* opaque in header */
```
- `transp_init`: alloc ctx, `setlocale(LC_ALL, "en_US.UTF-8")`, `key=-1`,
  table = `chromatic_en`. `transp_free`: free ctx.
- `transp_buffer(ctx, input, semitones, flags)`: normalize negative semitones
  (existing `+= (1+(s/12))*12`); set `i18n_table` from `TRANSP_LATIN`; strdup
  input; `transp_song_parse`; `transp_render`; free song + copy. Keep the 8×
  result buffer. Keep the static `chromatic_en`/`chromatic_latin` tables (same
  `"C\0", "C#\0Db", …` encoding, `chord_str` unchanged). **Delete** `chord_db`,
  `special_db`, `paren_db`, `qmap` includes, and `valid_modifier` entirely.
- `transp_get_key` / `transp_reset_key` (`key=-1`) / `transp_shift_table`
  unchanged in behavior.

### 8.6 Makefile

```
SRC = token.c parse.c render.c transp.c
OBJ = $(SRC:.c=.o)
$(OBJ): $(SRC) transp.h transp_flags.h token.h parse.h render.h
# remove: LDLIBS += -lqmap
```

### 8.7 Porting pitfalls (learned during analysis)

- `G6/9` must be valid (extension slash) — the old `space_after = slash_after`
  made it fail; the grammar handles `'/'` by trying a root first, digits second.
- `D9/F#` must stay valid (root after `/`).
- Chord line spacing is emitted verbatim; only the *diff* (root width change)
  alters it, and only up to the following whitespace; the queue then realigns
  the lyric line with space/`-` fillers. `complex_song` pins this exactly
  (`"D       A       Bm      G"`).
- `not_special` is per chord line (fixes today's never-reset quirk), and its
  `0` state only suppresses filler insertion, not queue removal.
- Repeat-bracket digits (`|1`, `|10`) belong to the SPECIAL token.
- Key detection only fires when `key == -1` (callers use `transp_reset_key`
  between songs).
- The `user_song_full` test asserts `transp_get_key(ctx) == 5` (first chord F).

## 9. Future features & seams (model rework, 2026-08-16)

Scoped out by the user for this pass; the model was shaped so they land without
churning the token contract:

- **Accidental-aware / key-aware respelling** — "later, not now". The model
  already carries the *chromatic* root and bass (`G#/Bb`-equivalence), but the
  renderer picks spellings by preference flag, not by key. A future step would
  pick `Bbm7` vs `A#m7` from the song key; the tests keep the current
  sharp-default (`Bbm7` renders `A#m7`) until then.
- **Intervals / music-theory reasoning** — explicitly future. The `quality`
  enum + `bass` are the seam; the renderer still copies suffixes verbatim.
  Nothing key- or theory-aware is designed in (§8.1 decision log).
- **`bass_off`/`bass_len`** let a future renderer re-emit or transpose the bass
  spelling without re-parsing the token string.
- **Quality heuristics that must keep matching the grammar**: `chord_quality`
  mirrors `word_atom` longest-first; `omit3`/`no3` are checked before the `5`
  power-chord rule so `Gomit3` stays UNDEFINED, not POWER. Parens are skipped —
  revisiting that is the natural next refinement (e.g. `G(no3)` could consult
  parens for quality).
- Hardening made the renderer **overflow/leak-safe by construction** (findings
  8–9): no fixed stack buffer for mods, single-array queue, per-line outbuf
  floor `max(8192, line_len*8+64)`.
- Still open, unchanged from §8.1: `make lint`, end-to-end `make unit-tests`
  (axil on :8080). AGENTS.md row + README rewrite are done.
