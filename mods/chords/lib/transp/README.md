# libtransp - Chord Transposition Library

A UTF-8 native C library for transposing musical chord charts.

## Overview

libtransp provides functions to transpose chord charts (chords + lyrics) to different musical keys. It's a complete UTF-8 rewrite of the original `transp.c` from tty.pt (which used `wchar_t`).

## Features

- **Transpose chords** from -11 to +11 semitones
- **Flat notation** (♭) - use flats instead of sharps
- **Latin notation** - Do, Ré, Mi, Fa, Sol, La, Si instead of C, D, E, F, G, A, B
- **Preserve lyrics** - only chord lines are transposed
- **Numbered verses** - handles "1. Verse", "2. Chorus", etc.
- **UTF-8 native** - all string operations use UTF-8 `char*`

## API

### Context Management

```c
transp_ctx_t *transp_init(void);
void transp_free(transp_ctx_t *ctx);
void transp_reset(transp_ctx_t *ctx);
```

- `transp_init()` - Create and initialize a new transpose context
- `transp_free()` - Free context and release resources
- `transp_reset()` - Reset context state between operations

### Transposition

```c
int transp_buffer(
    transp_ctx_t *ctx,
    const char *input,
    int semitones,
    int flags,
    char **output
);

int transp_line(
    transp_ctx_t *ctx,
    const char *line,
    int semitones,
    int flags,
    char *out,
    size_t out_size
);
```

**Parameters:**
- `ctx` - Transpose context (must call `transp_init()` first)
- `input` / `line` - Input chord chart text
- `semitones` - Transpose amount (-11 to +11, 0 = no change)
- `flags` - Bitwise OR of transpose flags (see below)
- `output` / `out` - Output buffer (caller must free for `transp_buffer()`)
- `out_size` - Output buffer size (for `transp_line()`)

**Returns:** 0 on success, -1 on error

### Flags

```c
#define TRANSP_HTML             0x01  // Generate HTML markup
#define TRANSP_BEMOL            0x02  // Use flats (♭) instead of sharps
#define TRANSP_LATIN            0x04  // Use Latin notation (Do-Ré-Mi)
#define TRANSP_HIDE_CHORDS      0x08  // Hide chord lines
#define TRANSP_HIDE_LYRICS      0x10  // Hide lyric lines
#define TRANSP_BREAK_SLASH      0x20  // Break slash chords
#define TRANSP_REMOVE_COMMENTS  0x40  // Remove comment lines
```

## Usage Examples

### Simple Transpose

```c
#include "transp.h"

transp_ctx_t *ctx = transp_init();
if (!ctx) return -1;

const char *input = "C       G       Am      F\n"
                    "Amazing Grace, how sweet the sound\n";

char *output = NULL;
int ret = transp_buffer(ctx, input, 2, 0, &output);
if (ret == 0) {
    printf("%s", output);
    // Output:
    // D       A       Bm      G
    // Amazing Grace, how sweet the sound
    free(output);
}

transp_free(ctx);
```

### Transpose with Flats

```c
transp_ctx_t *ctx = transp_init();
char *output = NULL;

transp_buffer(ctx, "C G Am F\n", 1, TRANSP_BEMOL, &output);
// Output: Db Ab Bbm Gb

free(output);
transp_free(ctx);
```

### Latin Notation

```c
transp_ctx_t *ctx = transp_init();
char *output = NULL;

transp_buffer(ctx, "C G Am F\n", 0, TRANSP_LATIN, &output);
// Output: Do Sol La- Fa

free(output);
transp_free(ctx);
```

### Line-by-Line Processing

```c
transp_ctx_t *ctx = transp_init();
char output[256];

const char *lines[] = {
    "C       G       Am      F",
    "Amazing Grace, how sweet the sound",
    NULL
};

for (int i = 0; lines[i]; i++) {
    transp_line(ctx, lines[i], 2, 0, output, sizeof(output));
    printf("%s\n", output);
}

transp_free(ctx);
```

## Building

### As Shared Library

```bash
make
```

Produces: `libtransp.so`

### With Tests

```bash
make test
```

Runs 13 unit tests covering:
- Chord recognition (C, Cm, C#m, etc.)
- Transpose range (-11 to +11)
- Flat notation
- Latin notation
- Numbered verses
- Context state management

### Clean

```bash
make clean
```

## Integration

### Linking

```makefile
# In your Makefile
LDFLAGS += -L/path/to/lib/transp -ltransp
CFLAGS += -I/path/to/lib/transp
```

### Runtime

Ensure `libtransp.so` is in `LD_LIBRARY_PATH`:

```bash
export LD_LIBRARY_PATH=/path/to/lib/transp:$LD_LIBRARY_PATH
```

Or use RPATH:

```makefile
LDFLAGS += -Wl,-rpath,/path/to/lib/transp
```

## Implementation Details

### Chord Recognition

The library recognizes chords by analyzing the first non-whitespace character:
- Must be uppercase letter (A-G)
- Followed by optional modifiers: #, b, m, 7, 9, sus, dim, aug, etc.
- Whitespace or punctuation after chord name ends the chord

**Examples:**
- `C` → chord
- `Cm` → chord
- `C#m7` → chord
- `Chorus` → not a chord (lowercase)
- `CHORUS` → not a chord (all caps > 4 chars)

### Transposition Algorithm

1. Normalize chord to base note (C=0, C#=1, ..., B=11)
2. Add semitones (modulo 12)
3. Apply sharp/flat rules based on flags
4. Replace original chord with transposed version

### Latin Notation Mapping

| English | Latin | Minor |
|---------|-------|-------|
| C | Do | Do- |
| C# | Do# | Do#- |
| D | Ré | Ré- |
| D# | Ré# | Ré#- |
| E | Mi | Mi- |
| F | Fa | Fa- |
| F# | Fa# | Fa#- |
| G | Sol | Sol- |
| G# | Sol# | Sol#- |
| A | La | La- |
| A# | La# | La#- |
| B | Si | Si- |

### Memory Management

- `transp_buffer()` allocates output - **caller must free()**
- `transp_line()` uses caller-provided buffer - no free needed
- Context holds internal buffers - call `transp_free()` when done

### Thread Safety

- Each context is independent
- Safe to use multiple contexts in different threads
- **NOT safe** to share a single context between threads

## Testing

Run all tests:

```bash
make test
```

Test coverage (13 tests):
1. Simple transpose up (+2 semitones)
2. Transpose with flats (Db, Eb, etc.)
3. Transpose with Latin notation
4. Minor chords (Am, Bm, etc.)
5. Sharp chords (C#m, F#, etc.)
6. Numbered verses ("1. Verse")
7. Non-chords (lowercase, all-caps words)
8. Transpose down (-3 semitones)
9. Transpose full range (-11 to +11)
10. Context reset between operations
11. Lyrics preservation
12. Empty lines
13. Mixed chord and lyric lines

## Known Issues

### Pre-existing Bug (from tty.pt)

Lyrics starting with uppercase letters that look like chords (e.g., "Amazing Grace" starts with "A") may get an extra `</b>` tag in HTML output. This bug exists in the production tty.pt version and is considered acceptable for now.

**Example:**
```
Input:  Amazing Grace
Output: Amazing </b>Grace  (extra </b> tag)
```

**Workaround:** Don't use TRANSP_HTML flag, or accept the minor HTML quirk.

## Files

| File | Purpose |
|------|---------|
| `transp.h` | Public API header |
| `transp.c` | Implementation (547 lines) |
| `test_transp.c` | Unit tests (13 tests) |
| `Makefile` | Build configuration |
| `libtransp.so` | Compiled shared library |

## Version History

- **2026-03-02** - UTF-8 rewrite, 13 unit tests passing
- **Original** - tty.pt transp.c (wchar_t based, 409 lines)

## License

Same as parent project (tty.pt/site)

## See Also

- [mods/chords/README.md](../../mods/chords/README.md) - Chords module that uses this library
- [mods/chords/chords.c](../../mods/chords/chords.c) - Integration example
- [../tty.pt/items/chords/src/transp/](../../tty.pt/items/chords/src/transp/) - Original implementation
