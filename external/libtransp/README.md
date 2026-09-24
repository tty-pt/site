# libtransp

[![C99](https://img.shields.io/badge/C-C99-555?logo=c)](#)
[![BSD-2-Clause](https://img.shields.io/badge/License-BSD--2--Clause-blue)](#)
[![chord-transpose](https://img.shields.io/badge/chord-transpose-C4841A)](#)

> Chord detection and transposition library.

Grammar-based chord detection + transpose/render for the site's song charts.
Read [`CHORDS.md`](./CHORDS.md) (repo root) for the full grammar, pipeline
internals, and the detection rework.

## Contents

- [Features](#features)
- [Install](#install)
- [Build from source](#build-from-source)
- [Quickstart](#quickstart)
- [API overview](#api-overview)
- [What counts as a chord](#what-counts-as-a-chord)
- [Source layout](#source-layout)
- [Documentation](#documentation)
- [Testing](#testing)
- [License](#license)

## Features

- **Grammar-based detection**: a token is a chord iff a single left-to-right
  scan consumes it entirely as `root` + suffix atoms — no character
  whitelists, no chord database, no corm.
- **Typed song model**: chord / lyric / comment / empty lines become typed
  tokens with per-token quality (`transp_quality_t`) and slash-bass spans.
- **Transpose + render**: shifts chord roots by semitones and renders plain
  text or HTML while preserving the original spacing.
- **Preference flags**: flats vs sharps (`TRANSP_BEMOL`), Latin solfege
  (`TRANSP_LATIN`), show/hide sections, comment removal.
- **Key detection**: latches the key from the first chord token until reset.

## Install

libtransp is a static (`lib/libtransp.a`) library consumed in-tree by the
site's song-chart pipeline; it is not distributed as a separate binary
package. Prebuilt packages for the other tty.pt libraries are documented in
the [installation instructions](
https://github.com/tty-pt/ci/blob/main/docs/install.md); build libtransp
from source below.

## Build from source

```sh
make          # builds lib/libtransp.a (static)
make test     # builds + runs the 58-test suite
make clean
```

**Dependencies:** none — pure C, grammar-based (the grammar lives in
`src/token.c`; no corm, no xxhash, no sockets).

## Quickstart

```c
#include "transp.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    transp_ctx_t *ctx = transp_init();
    if (!ctx) return 1;
    const char *input =
        "C       G       Am      F\n"
        "Amazing Grace, how sweet the sound\n";
    char *out = transp_buffer(ctx, input, 2, TRANSP_HTML);
    printf("%s\n", out ? out : "(error)");
    /* <div><b>D A Bm G</b></div><div>Amazing Grace, how sweet the sound</div> */
    free(out);
    transp_free(ctx);
    return 0;
}
```

Notes on output:

- Without `TRANSP_HTML` every line ends with `\n`; with it, lines become
  `<div>…</div>` and chord lines one `<b>…</b>` block.
- Chord roots render with sharps by default; `TRANSP_BEMOL` switches to flats
  (`C#` → `Db`), `TRANSP_LATIN` to solfege (`C` → `Do`, `Am` → `La-`). Flat/sharp
  spelling is a preference flag, not key-aware.
- Slash basses (`G/B`, `E/G#`) stay fixed — never transposed. Suffixes
  (`m7`, `(5º)`, …) are copied verbatim.

## API overview

```c
typedef struct transp_ctx transp_ctx_t;

transp_ctx_t *transp_init(void);                       /* NULL on error */
char *transp_buffer(transp_ctx_t *ctx, const char *input,
                    int semitones, int flags);         /* malloc'd; caller frees */
int transp_get_key(transp_ctx_t *ctx);                 /* 0-11 chromatic, or -1 */
void transp_reset_key(transp_ctx_t *ctx);              /* clears detected key */
char *transp_shift_table(transp_ctx_t *ctx, int latin);/* malloc'd; caller frees */
void transp_free(transp_ctx_t *ctx);                   /* NULL-safe */
```

- `transp_buffer` parses → transposes → renders the whole input and returns a
  newly allocated string (NULL on error). `semitones` is -11..+11 (0 = no
  change). Key is detected from the first chord token and latches in the ctx
  until `transp_reset_key` (call it between songs).
- `transp_shift_table` needs a detected key; it emits the 12 keys with their
  semitone offset from that key (English, or Latin with `latin=1`).
- Flags (`transp_flags.h`): `TRANSP_HTML 0x04`, `TRANSP_BEMOL 0x08`,
  `TRANSP_BREAK_SLASH 0x20`, `TRANSP_REMOVE_COMMENTS 0x10`,
  `TRANSP_HIDE_CHORDS 0x01`, `TRANSP_HIDE_LYRICS 0x02`, `TRANSP_LATIN 0x80`.

## What counts as a chord

A token is a chord iff a single left-to-right scan consumes the whole token as
`root` + zero or more suffix atoms:

- **Root:** `[A-G]` (or `Do|Re|Mi|Fa|Sol|La|Si`) + optional `#`/`b`. Case
  sensitive: `do` is a lyric word.
- **Suffix atoms:** `m`/`min`/`-`, `M`/`maj`, `dim`/`º`, `h`, `aug`/`+`,
  `sus`/`add`/`omit`/`no`, digits (`5 7 9 11 …`), altered extensions (`b5 #9`),
  paren groups (`(no3)`, `(5º)`), extension slashes (`G6/9`), trailing
  `/root` slash basses.
- **Rejected** the moment an unrecognized byte appears: `Amazing`, `miseri-`,
  `córdia`, `Amada`, `C(not)`, `G/thing` are non-chords.
- Repeat markers `| : -` (+`|digits`) and a lone `/` are special/separator
  tokens that still keep a line a chord line.

The model also exposes per-token **quality** (`transp_quality_t`: MAJOR/MINOR/
HALF_DIM/POWER/DIMINISHED/UNDEFINED) and the **slash bass** (chromatic index +
`bass_off`/`bass_len`). These are not yet consumed by the renderer (suffixes
stay verbatim); they are the seam for future key-aware spelling and
music-theory features. Full semantics: `CHORDS.md` §8.2.

## Source layout

| File | Role |
|------|------|
| `include/transp/transp.h` | Public API, ctx, chromatic tables, shift table |
| `src/token.h` / `src/token.c` | Chord grammar: `transp_token_analyze` → CHORD/SPECIAL/SEP/NOT_CHORD + root/suffix spans, quality, slash bass |
| `src/parse.h` / `src/parse.c` | Input → song model (typed lines/tokens), key detection |
| `src/render.h` / `src/render.c` | Model → output (transpose, spacing queue, HTML/plain, flags) |
| `src/test_transp.c` | 58-test syntax matrix |

History: the original was a `wchar_t` `transp.c` from tty.pt, then a UTF-8
rewrite with ad-hoc per-character whitelists (the `libcorm` `chord_db`
approach). The 2026-08-16 rework replaced the whitelists with the grammar-based
classifier and parse → render pipeline, added the model fields, and fixed
robustness bugs (a stack overflow in mod rendering and a queue double-free on
partial realloc failure).

## Documentation

- [`CHORDS.md`](./CHORDS.md) — the grammar and pipeline internals
- [include/transp/](./include/transp/) — public headers (`transp.h`,
  `transp_flags.h`, `parse.h`, `token.h`, `spelling.h`, `music.h`)

## Testing

```sh
make test     # builds + runs the 58-test suite
```

The suite runs the ordered syntax matrix in `test_transp.c` (roots,
qualities/extensions, specials, lines & structure, transpose/prefs, the
rework contract, regression guards, and the model field assertions) and is
clean under ASan/UBSan. The transp tests are wired into the root build via
`mods/song/test.sh` (`make -C lib/transp test`).

## License

BSD 2-Clause License. Copyright (c) 2026, tty-pt. See `LICENSE`.