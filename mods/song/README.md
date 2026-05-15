# Song Module

Upload and display songs with chord charts via HTTP endpoints and SSR pages.

## Purpose

The song module provides functionality for:
- Uploading songs with chord charts (text format with chord symbols above lyrics)
- Storing metadata (title, type/category, author, media links)
- Displaying a listing of all uploaded songs
- Transposing songs to different keys (client-side and server-side)

## Architecture

- **Backend:** `song.c` - C module with ndc HTTP handlers
- **SSR:** `ssr/src/main.rs` - Rust Dioxus SSR renderer
- **Dataset:** Uses the common dataset system for loading song data
- **Storage:** Filesystem + qmap index database
- **Transposition:** C library (`lib/transp/`) for chord transposition
- **WASM:** Client-side enhancements via Rust/WASM

### SSR Route Pattern

The module uses a pattern-based routing system:
- `GET /song/` - List all songs (via hyle SSR)
- `GET /song/add` - Upload form (via hyle SSR)
- `GET /song/:id` - Song detail with optional transposition
- `GET /song/:id/edit` - Edit song form

## API

### POST /song/add

Upload a new song.

**Request:**
- Method: `POST`
- Content-Type: `multipart/form-data`
- Required fields:
  - `id` - Unique identifier for the song (e.g., "amazing_grace")
  - `data` - Chord chart content (text with chord symbols and lyrics)
- Optional fields:
  - `title` - Display title (defaults to id if not provided)
  - `type` - Category/type (e.g., "Communion", "Entrance")
  - `author` - Author name
  - `yt` - YouTube URL
  - `audio` - Audio URL
  - `pdf` - PDF URL

**Response:**
- `303 See Other` - Success, redirects to `/song/`
- `400 Bad Request` - Missing required fields
- `415 Unsupported Media Type` - Not multipart/form-data
- `500 Internal Server Error` - System error (mkdir, fopen failures)

**Example:**
```bash
curl -X POST http://localhost:8080/song/add \
  -F "id=amazing_grace" \
  -F "title=Amazing Grace" \
  -F "type=Communion" \
  -F "data=@chord.txt"
```

Where `chord.txt` contains:
```
C       G       Am      F
Amazing Grace, how sweet the sound
    F       C       G
That saved a wretch like me
```

### GET /api/song/:id/transpose

Transpose a song to a different key.

**Request:**
- Method: `GET`
- Query Parameters:
  - `id` - Song identifier (required)
  - `t` - Transpose semitones from -11 to +11 (default: 0)
  - `b` - Use flats notation (1=yes, 0=no, default: 0)
  - `l` - Use Latin notation Do-Ré-Mi (1=yes, 0=no, default: 0)

**Response:**
- `200 OK` - Returns transposed song as `text/plain; charset=utf-8`
- `400 Bad Request` - Missing or invalid id
- `500 Internal Server Error` - File read or transpose error

**Examples:**
```bash
# Transpose up 2 semitones (C → D)
curl "http://localhost:8080/api/song/transpose?id=amazing_grace&t=2"

# Transpose down 3 semitones with flats (C → A♭)
curl "http://localhost:8080/api/song/transpose?id=amazing_grace&t=-3&b=1"

# Show in Latin notation without transposing (C → Do)
curl "http://localhost:8080/api/song/transpose?id=amazing_grace&l=1"

# Combine all options: transpose +1 with flats and Latin
curl "http://localhost:8080/api/song/transpose?id=amazing_grace&t=1&b=1&l=1"
```

**Transpose Flags:**
- `TRANSP_BEMOL` (0x02) - Use flats instead of sharps
- `TRANSP_LATIN` (0x04) - Use Latin notation (Do, Ré, Mi, Fa, Sol, La, Si)

## Storage

### Directory Structure

Songs are stored in `items/song/items/{id}/`:

```
items/song/items/
└── amazing_grace/
    ├── data.txt    # Song with chords and lyrics (required)
    ├── title       # Display title (optional)
    ├── type        # Category/type (optional)
    ├── author      # Author name (optional)
    ├── yt          # YouTube URL (optional)
    ├── audio       # Audio URL (optional)
    ├── pdf         # PDF URL (optional)
    └── owner       # Owner username (optional)
```

**Required Directory:**
- `items/song/items/` must exist before uploads will work
- Created automatically by `make test-data-dirs` or manually: `mkdir -p items/song/items`

### File Formats

**data.txt** - Plain text with chords above lyrics:
```
Bm      G            A      Bm
Anima   Christi, sanctifica me
Em       Bm     G       F#
Corpus Christi, salva me
```

**title** - Plain text (optional):
```
Amazing Grace
```

**type** - Plain text (optional):
```
Communion
```

### Song Types

Song types are categories used for organizing songs and filtering. Default types include:
- entrada (Entrance)
- aleluia (Alleluia)
- ofertorio (Offertory)
- santo (Holy)
- comunhao (Communion)
- acao_de_gracas (Thanksgiving)
- saida (Closing)
- any (General use)

## Dataset System

The song module uses the common dataset system for loading song data across the application.

### Data Flow

```
index_hd (qmap) ← populated at startup by index_open
       ↓
dataset_def.source_hd = index_hd
       ↓
dataset_rows_json_build iterates over source_hd
       ↓
For each ID, looks up JSON from source_hd
       ↓
If JSON empty, calls dataset_scan_item to read from disk
```

### Dataset Definition (song.c)

```c
static const dataset_field_t fields[] = {
    { "id", NULL, DATASET_FIELD_STRING, 0 },
    { "title", "title", DATASET_FIELD_STRING, 1 },
    { "type", "type", DATASET_FIELD_STRING, 1 },
    { "author", "author", DATASET_FIELD_STRING, 1 },
    { "yt", "yt", DATASET_FIELD_STRING, 1 },
    { "audio", "audio", DATASET_FIELD_STRING, 1 },
    { "pdf", "pdf", DATASET_FIELD_STRING, 1 },
    { "data", "data.txt", DATASET_FIELD_STRING, 1 },
    { "owner", "owner", DATASET_FIELD_STRING, 0 },
};

dataset_def_t def = {
    .id = "song.items",
    .items_path = "items/song/items",
    .source_hd = index_hd,
    // ...
};
dataset_register(&def);
```

## Testing

### Unit Tests

```bash
make unit-tests
# or
cd mods/song && ./test.sh
```

### E2E Tests

```bash
make test-single-capture TEST=song-list.test.ts
make test-single-capture TEST=song-add.test.ts
make test-single-capture TEST=song-edit.test.ts
make test-single-capture TEST=song-detail-transpose.test.ts
```

For debugging test failures, see [debug/README.md](../../debug/README.md).

## Dependencies

- `mods/ssr/ssr` - SSR rendering
- `mods/mpfd/mpfd` - Multipart form data parsing
- `mods/auth/auth` - Session management
- `mods/index/index` - Index management
- `mods/common/common` - Dataset system

## Troubleshooting

### Upload fails with HTTP 500

**Cause:** Directory `items/song/items/` doesn't exist.

**Fix:**
```bash
mkdir -p items/song/items
```

### Songs don't appear in listing

**Causes:**
1. Directory doesn't exist (see above)
2. Server not restarted after changes
3. Song files missing or corrupted

**Debug:**
```bash
# Check if directory exists
ls -la items/song/items/

# Check if ndc is running
pgrep -a ndc

# Check server logs
tail -100 debug/runtime/ndc.log
```

### Transpose not working

**Cause:** Song ID doesn't exist or data.txt is empty.

**Debug:**
```bash
# Check song exists
ls -la items/song/items/{song_id}/

# Check data.txt has content
cat items/song/items/{song_id}/data.txt
```

## See Also

- [AGENTS.md](../../AGENTS.md) - Development guidelines
- [debug/README.md](../../debug/README.md) - Debug logging system
- [mods/common/README.md](../common/README.md) - Dataset system documentation
- [mods/song/lib/transp/README.md](lib/transp/README.md) - Transposition library