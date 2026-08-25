# Proposal / Future Task: lyrics

Status: **proposal**

## Goals & Scope
Add a **generalizable derived field mechanism** to the source/hyle stack so that modules can declare fields computed from other fields (artifacts). Use it to extract clean lyrics from `data.txt` (chords+lyrics) into a `lyrics.txt` artifact, making lyrics searchable via both omnisearch `q` and FTS field filters — while keeping raw `data.txt` as the single source of truth for editing/display.

**Problem**: Chord symbols (`D`, `Bm7`, `G#m`, `|:` etc.) pollute search.
**Desired State**:
- `data` → non-searchable
- `lyrics` → searchable (clean lyrics only)
- Both omnisearch `?q=lyric_word` and FTS filter `lyrics:lyric_word` work
- Chord symbols never appear in search results

## Requirements
- Derived fields are **read-only artifacts** generated on write, indexed for search, never directly edited.
- **Encapsulation rule**: hyle is a standalone library. It provides a generic derive registry with opaque context pointers. The site's source module registers providers with hyle — hyle never includes `source.h` or knows about `source_def_t`.

## Implementation Plan
1. **Core types**: `source.h` + `hyle/field.h` (type constants, struct extensions)
2. **hyle derive API**: `hyle/include/hyle/source.h` + `hyle/src/source.c` (generic registry)
3. **Source mapping**: `source.c` (derive_key passthrough, `SOURCE_FIELD_DERIVED` mapping)
4. **Macros**: `field_macros.h` (new macros `EXCL_FIELD_VF_NSEARCH`, `DERIVED_FIELD`)
5. **Song schema**: `song/fields.h` (apply macros)
6. **Song logic**: `song/song.c` (provider, artifact generation in `source_after_update`, hyle registration)
7. **Build + test**: `make`, manual verification
8. **Backfill**: Script `scripts/backfill_lyrics.c` + run on existing data
9. **Regression test**: Full test suite

## Out of scope
- (None listed explicitly, but limit scope to lyrics extraction and the generic derive registry.)

*(Refer to original LYRICS.md design notes for detailed struct layouts, algorithm details, and appendix materials if needed during implementation.)*
