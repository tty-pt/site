# Chords Module

The chords module provides functionality for uploading, storing, and displaying chord charts with lyrics.

## Purpose

This module allows users to:
- Upload chord charts with lyrics (text format with chord symbols)
- Store optional metadata (title, type/category)
- View a listing of all uploaded chords
- Display individual chord charts in a readable format

## Architecture

**Phase 1 - Core CRUD (✓ Complete)**

The chords module follows the same pattern as the poem module:
- C backend handles file uploads via multipart/form-data
- Filesystem storage for chord data
- SSR components render the UI

**Phase 2 - Transposition (✓ Complete)**

Chord transposition to different keys:
- libtransp.so C library (UTF-8 complete rewrite from tty.pt)
- Server-side processing: C handler transposes data, sends to SSR via POST
- API endpoint: `/api/chords/transpose` (for programmatic access)
- SSR routes: `GET /chords/:id?t=N&b=1&l=1` (user-facing pages)
- Client-side JavaScript for live transposition controls
- Support for flats (♭), Latin notation (Do-Ré-Mi), and transpose range -11 to +11

**Future Phases:**
- Phase 3: Media integration (YouTube, audio, PDF)
- Phase 4: Type filtering, fork/merge workflow

## API

### POST /chords/add

Upload a new chord chart.

**Request:**
- Method: `POST`
- Content-Type: `multipart/form-data`
- Required fields:
  - `id` - Unique identifier for the chord (e.g., "amazing_grace")
  - `data` - Chord chart content (text with chord symbols and lyrics)
- Optional fields:
  - `title` - Display title (defaults to id if not provided)
  - `type` - Category/type (e.g., "Communion", "Thanksgiving")

**Response:**
- `303 See Other` - Success, redirects to `/chords/`
- `400 Bad Request` - Missing required fields
- `415 Unsupported Media Type` - Not multipart/form-data
- `500 Internal Server Error` - System error (mkdir, fopen failures)

**Example:**
```bash
curl -X POST http://localhost:8080/chords/add \
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

### GET /api/chords/transpose

Transpose a chord chart to a different key.

**Request:**
- Method: `GET`
- Query Parameters:
  - `id` - Chord identifier (required)
  - `t` - Transpose semitones from -11 to +11 (default: 0)
  - `b` - Use flats notation (1=yes, 0=no, default: 0)
  - `l` - Use Latin notation Do-Ré-Mi (1=yes, 0=no, default: 0)

**Response:**
- `200 OK` - Returns transposed chord chart as `text/plain; charset=utf-8`
- `400 Bad Request` - Missing or invalid id
- `500 Internal Server Error` - File read or transpose error

**Examples:**
```bash
# Transpose up 2 semitones (C → D)
curl "http://localhost:8080/api/chords/transpose?id=amazing_grace&t=2"

# Transpose down 3 semitones with flats (C → A♭)
curl "http://localhost:8080/api/chords/transpose?id=amazing_grace&t=-3&b=1"

# Show in Latin notation without transposing (C → Do)
curl "http://localhost:8080/api/chords/transpose?id=amazing_grace&l=1"

# Combine all options: transpose +1 with flats and Latin
curl "http://localhost:8080/api/chords/transpose?id=amazing_grace&t=1&b=1&l=1"
```

**Transpose Flags:**
- `TRANSP_HTML` (0x01) - Generate HTML markup (not used in API, for future)
- `TRANSP_BEMOL` (0x02) - Use flats instead of sharps
- `TRANSP_LATIN` (0x04) - Use Latin notation (Do, Ré, Mi, Fa, Sol, La, Si)
- `TRANSP_HIDE_CHORDS` (0x08) - Hide chord lines (not exposed in API)
- `TRANSP_HIDE_LYRICS` (0x10) - Hide lyric lines (not exposed in API)
- `TRANSP_BREAK_SLASH` (0x20) - Break slash chords (not exposed in API)
- `TRANSP_REMOVE_COMMENTS` (0x40) - Remove comment lines (not exposed in API)

## Storage

### Directory Structure

Chords are stored in `items/chords/items/{id}/`:

```
items/chords/items/
└── amazing_grace/
    ├── data.txt    # Chord chart with lyrics (required)
    ├── title       # Display title (optional)
    └── type        # Category/type (optional)
```

**Required Directory:**
- `items/chords/items/` must exist before uploads will work
- Created automatically by `make` or manually: `mkdir -p items/chords/items`

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

## SSR Routes

The module provides these SSR routes via two mechanisms:

### Pattern Handlers (C → SSR)

**GET /chords/:id** - Displays individual chord with optional transposition

Registered in `chords.c` as pattern handlers:
- `GET:/chords/` - List view (proxies to SSR)
- `GET:/chords/:id` - Detail view with server-side transposition

**Query Parameters:**
- `t=N` - Transpose by N semitones (-11 to +11, default: 0)
- `b=1` - Use flats (♭) notation
- `l=1` - Use Latin notation (Do-Ré-Mi)
- `C=1` - Hide chords (lyrics only)
- `L=1` - Hide lyrics (chords only)

**Server-Side Processing Flow:**
1. C handler extracts `:id` from URL pattern
2. Reads query parameters (t, b, l, C, L)
3. If transposition needed:
   - Reads chord file from filesystem
   - Calls `transp_buffer()` from libtransp.so
   - Sends transposed data to Deno SSR via POST
4. If no transposition (no query params):
   - Proxies GET request to Deno SSR
   - SSR reads file directly
5. SSR component receives either:
   - `body` parameter (pre-transposed data from C)
   - No body (reads file normally)

**Benefits:**
- Performant C-based transposition
- No FFI complexity in Deno
- Clean memory management (C frees after POST)
- SSR focuses on rendering only

### Direct SSR Routes

The module's SSR component also registers routes in `ssr/index.tsx`:
- `GET /chords/` - List all chords
- `GET /chords/add` - Upload form
- `GET /chords/:id` - Display individual chord (fallback if no query params)

### Components

**ChordList** (`ssr/components/ChordList.tsx`)
- Displays all chords as clickable buttons
- Shows "No chords yet" if none exist
- Provides "Add Chord" button

**ChordAdd** (`ssr/components/ChordAdd.tsx`)
- Form with fields: id, title, type, data (textarea)
- Posts to `/chords/add`

**ChordDetail** (`ssr/components/ChordDetail.tsx`)
- Displays chord chart in monospace font with pre-wrap
- Shows title (or id if no title)
- **Transpose controls:**
  - Dropdown selector for -11 to +11 semitones
  - Checkbox for flats (♭) notation
  - Checkbox for Latin (Do-Ré-Mi) notation
  - Client-side JavaScript fetches transposed version from `/api/chords/transpose`
- Provides "Back to Chords" link

## Dependencies

### C Module Dependencies
- `mods/ssr/ssr.so` - SSR rendering and proxy APIs
  - `call_ssr_proxy_get(fd, path)` - Proxy GET to Deno
  - `call_ssr_proxy_post(fd, path, body, len)` - Proxy POST with processed data
- `mods/mpfd/mpfd.so` - Multipart form data parsing
- `lib/transp/libtransp.so` - Chord transposition library (C linkage)

Loaded via `ndx_load()` and NDX_DEF declarations in `chords.c`

### SSR Dependencies
- React 18
- Deno std library (path utilities)
- Layout component from `mods/ssr/ui.tsx`

### Transp Library
- Location: `lib/transp/`
- Language: C (shared library)
- UTF-8 native (complete rewrite from tty.pt wchar_t version)
- 13 unit tests (all passing)
- See: `lib/transp/README.md` for library documentation

## Testing

### Unit Tests

**Transp Library Tests:**
```bash
cd lib/transp && make test
```

13 unit tests covering:
- Chord recognition and transposition
- Flat notation (b flag)
- Latin notation (l flag)
- Numbered verses ("1. Verse")
- Line-by-line processing
- Context state management

**Chords Module Tests:**
```bash
./mods/chords/test.sh
```

Test coverage (11 tests):
1. POST with wrong content-type (415)
2. POST missing id field (400)
3. POST missing data field (400)
4. POST empty data (400)
5. POST valid multipart (303 redirect)
6. Verify data.txt created
7. Verify content matches
8. Verify title file created
9. Verify type file created
10. POST with minimal fields (id + data only)
11. Verify minimal chord created

### Integration Tests

**Transpose API Tests:**
```bash
./mods/chords/test_integration.sh
```

21 tests covering:
- Transpose range (-11 to +11)
- Flat notation (b=1)
- Latin notation (l=1)
- Combined flags (t+b+l)
- Chord name transposition (C→D, Am→Bm)
- Lyrics preservation

### System-Wide Tests

The module is included in system-wide tests:
```bash
make test              # All tests
make unit-tests        # Module unit tests only
make pages-test        # SSR page smoke tests
```

## Usage Examples

### Upload a Simple Chord

```bash
# Create chord file
cat > mychord.txt << 'EOF'
C       G       Am      F
This is a test chord
EOF

# Upload with minimal fields
curl -X POST http://localhost:8080/chords/add \
  -F "id=test_chord" \
  -F "data=@mychord.txt"
```

### Upload with All Fields

```bash
curl -X POST http://localhost:8080/chords/add \
  -F "id=holy_god" \
  -F "title=Holy God We Praise Thy Name" \
  -F "type=Thanksgiving" \
  -F "data=@holy_god.txt"
```

### View in Browser

1. Start the site: `./start.sh`
2. Navigate to: `http://localhost:8080/chords/`
3. Click on a chord to view it
4. Click "Add Chord" to upload a new one

## Error Handling

The module implements comprehensive error handling:

### System Call Checks
- `mkdir()` - Checks for errors, allows EEXIST (chords.c:83-88)
- `fopen()` - Checks for NULL return (chords.c:94-101, 109-117, 124-132)

### HTTP Error Codes
- `400 Bad Request` - Missing or empty id/data fields
- `415 Unsupported Media Type` - Wrong Content-Type
- `500 Internal Server Error` - mkdir/fopen failures with descriptive messages

### Memory Management
- Allocates buffer for chord data (chords.c:68)
- Frees on all error paths (chords.c:71, 76, 87, 100)
- Frees after successful write (chords.c:104)

## Troubleshooting

### Upload fails with HTTP 500

**Symptom:** POST returns "Failed to create chord directory"

**Cause:** `items/chords/items/` directory doesn't exist

**Fix:**
```bash
mkdir -p items/chords/items
```

### Upload fails with HTTP 415

**Symptom:** POST returns "Expected multipart/form-data"

**Cause:** Wrong Content-Type header

**Fix:** Use `-F` with curl (sets multipart/form-data automatically):
```bash
curl -F "id=test" -F "data=@file.txt" http://localhost:8080/chords/add
```

### Chord doesn't appear in listing

**Symptom:** Upload succeeds but chord not visible at `/chords/`

**Cause:** SSR server might need restart, or directory has dot prefix

**Fix:**
1. Check directory: `ls items/chords/items/`
2. Restart Deno SSR: `pkill -f deno && cd mods/ssr && deno run --allow-all server.ts &`
3. Ensure id doesn't start with dot (hidden files are skipped)

### Tests fail with "Connection refused"

**Symptom:** `./mods/chords/test.sh` fails to connect

**Cause:** Port conflict or previous test didn't clean up

**Fix:**
```bash
# Kill any existing test servers
pkill -f "ndc.*3002"

# Re-run tests
./mods/chords/test.sh
```

## Future Enhancements

### Phase 3: Media Integration
- YouTube video embedding (yt field)
- Audio player (audio field)
- PDF links (pdf field)
- MusicXML sheet music rendering

### Phase 4: Advanced Features
- Type filtering/categorization with qmap databases
- Fork/merge workflow (personal workspaces)
- Zoom preferences
- Edit existing chords

## Code References

### C Implementation
- Main handler: `handle_chords_add()` (chords.c:14-134)
- POST handler: `chords_handler()` (chords.c:136-166)
- Module registration: `ndx_install()` (chords.c:175-186)

### SSR Implementation
- Main routes: `render()` (ssr/index.tsx:43-62)
- Chord listing: `getChords()` (ssr/index.tsx:10-23)
- Data retrieval: `getChordData()` (ssr/index.tsx:25-41)

### Tests
- Unit tests: `test.sh:30-91` (11 test cases)
- Error handling: `test.sh:34-54` (4 error scenarios)
- Success paths: `test.sh:62-89` (7 verification tests)

## Related Modules

- **poem** - Similar file upload pattern, reference implementation
- **mpfd** - Multipart form data parsing library
- **ssr** - SSR rendering and layout components
- **auth** - Future: user authentication for edit/delete operations
