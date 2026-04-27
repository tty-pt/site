# Songbook (sb) Module

Manages songbooks - collections of songs with individual transpose settings, organized by format categories defined in their parent choir.

## Purpose

The songbook module provides:
- Create songbooks within a choir
- Add/remove/reorder songs with per-song transpose values
- Randomize song selection by format category
- View mode: display all songs with transposed chords
- Edit mode: manage songs, transpose values, and formats

## Architecture

- **Backend:** `sb.c` - C module with ndc HTTP handlers
- **SSR:** `ssr.rs` - Rust Dioxus SSR renderer
- **Storage:** Filesystem + qmap index database
- **Dependencies:** auth, common, mpfd, choir (indirect)

## Storage

### Directory Structure
```
items/sb/items/
├── index.db              # qmap database: songbook_id → title
└── {songbook_id}/
    ├── .owner            # Plain text username
    └── data.txt          # Songbook metadata + song list
```

### Data Format (`data.txt`)

```
title:{songbook_title}
choir:{choir_id}
{chord_id}:{transpose}:{format}
{chord_id}:{transpose}:{format}
...
```

**Fields:**
- `chord_id` - ID of chord from items/chords/items/
- `transpose` - Semitones to transpose (-11 to +11, 0 = no transpose)
- `format` - Song format category (must match choir's format list)

**Example:**
```
title:Sunday Morning Set
choir:sunday-mass
AmazingGrace:0:entrada
HolyHoly:2:santo
BlessedBe:-1:comunhao
GoForth:0:saida
```

## API Endpoints

### POST /api/sb/create

Create a new songbook in a choir.

**Request:**
- Content-Type: `multipart/form-data`
- Authentication: Required (must own the choir)

**Parameters:**
- `id` - Songbook ID (alphanumeric, used in URL)
- `title` - Songbook display name
- `choir` - Choir ID (must exist, user must own)

**Response:**
- `303 See Other` → `/sb/{id}` on success
- `400 Bad Request` - Missing fields
- `401 Unauthorized` - Not logged in or don't own choir
- `500 Internal Server Error` - Creation failed

**Example:**
```sh
curl -X POST http://127.0.0.1:8080/api/sb/create \
  -H "Cookie: session=..." \
  -F "id=sunday-morning" \
  -F "title=Sunday Morning Set" \
  -F "choir=sunday-mass"
```

### POST /api/sb/:id/edit

Update entire songbook (replace all songs).

**Request:**
- Content-Type: `multipart/form-data`
- Authentication: Required (must own songbook)

**Parameters:**
- `amount` - Number of songs (integer)
- `format0`, `format1`, ... - Format for each song
- `chord0`, `chord1`, ... - Chord ID for each song
- `transpose0`, `transpose1`, ... - Transpose value for each song (-11 to +11)

**Response:**
- `303 See Other` → `/sb/{id}` on success
- `400 Bad Request` - Missing fields or invalid format
- `401 Unauthorized` - Not logged in or don't own songbook
- `500 Internal Server Error` - Update failed

**Example:**
```sh
curl -X POST http://127.0.0.1:8080/api/sb/sunday-morning/edit \
  -H "Cookie: session=..." \
  -F "amount=2" \
  -F "format0=entrada" \
  -F "chord0=AmazingGrace" \
  -F "transpose0=0" \
  -F "format1=santo" \
  -F "chord1=HolyHoly" \
  -F "transpose1=2"
```

### POST /api/sb/:id/transpose

Change transpose value for one song.

**Request:**
- Content-Type: `multipart/form-data`
- Authentication: Required (must own songbook)

**Parameters:**
- `index` - Song index (0-based)
- `transpose` - New transpose value (-11 to +11)

**Response:**
- `303 See Other` → `/sb/{id}` on success
- `400 Bad Request` - Missing fields or invalid index
- `401 Unauthorized` - Not logged in or don't own songbook
- `500 Internal Server Error` - Update failed

**Example:**
```sh
curl -X POST http://127.0.0.1:8080/api/sb/sunday-morning/transpose \
  -H "Cookie: session=..." \
  -F "index=1" \
  -F "transpose=3"
```

### POST /api/sb/:id/randomize

Replace one song with random selection by format.

**Request:**
- Content-Type: `multipart/form-data`
- Authentication: Required (must own songbook)

**Parameters:**
- `index` - Song index to replace (0-based)

**Response:**
- `303 See Other` → `/sb/{id}` on success
- `400 Bad Request` - Missing index or invalid format
- `401 Unauthorized` - Not logged in or don't own songbook
- `404 Not Found` - No chords found for format
- `500 Internal Server Error` - Update failed

**How it works:**
1. Read current song's format
2. Scan `items/chords/items/` for chords with matching `type` file
3. Select random chord from candidates
4. Replace song at index with random chord (preserve format, reset transpose to 0)

**Example:**
```sh
curl -X POST http://127.0.0.1:8080/api/sb/sunday-morning/randomize \
  -H "Cookie: session=..." \
  -F "index=2"
```

### DELETE /api/sb/:id

Delete a songbook.

**Request:**
- Authentication: Required (must own songbook)

**Response:**
- `303 See Other` → `/sb` on success
- `401 Unauthorized` - Not logged in or don't own songbook
- `404 Not Found` - Songbook doesn't exist
- `500 Internal Server Error` - Deletion failed

**Example:**
```sh
curl -X DELETE http://127.0.0.1:8080/api/sb/sunday-morning \
  -H "Cookie: session=..."
```

## SSR Routes

### GET /sb

List all songbooks.

**Display:**
- All songbooks from index.db
- "Create New Songbook" button (if logged in)
- Links to individual songbook pages

### GET /sb/new

Create songbook form.

**Requirements:**
- User must be logged in
- Shows login prompt if not authenticated

**Form Fields:**
- Choir (dropdown of user's choirs)
- Songbook ID (lowercase, no spaces)
- Songbook Title

### GET /sb/:id

View songbook with transposed chords.

**Display:**
- Songbook title and choir reference
- For each song:
  - Song title (from chord metadata)
  - Format category
  - Transpose value
  - Full chord chart (transposed)
  - Owner actions: Transpose dropdown, Randomize button (🎲)
- Jump anchors for navigation
- Owner actions: Edit button, Delete button

**Chord Transpose Integration:**
- Fetches from `/api/chords/transpose?id={chord_id}&t={transpose}`
- Strips title line from response
- Displays transposed chord chart
- Falls back to original if transpose fails

### GET /sb/:id/edit

Edit songbook form.

**Requirements:**
- User must be logged in
- User must own songbook

**Form Fields:**
- For each song slot:
  - Format (text input)
  - Chord (dropdown with all chords, showing type tags)
  - Transpose (dropdown -11 to +11)
- Amount (hidden field tracking number of songs)

**Chord Dropdown:**
- Lists all chords from `items/chords/items/`
- Shows chord type (from `type` file) as tag
- Allows adding/removing song slots dynamically

## Ownership

- Songbook ownership tracked via `.owner` file
- Only owner can edit, transpose, randomize, or delete songbook
- Songbook creation requires ownership of parent choir

## Transpose Integration

The songbook module integrates with the chords module's transpose API:

**Endpoint:** `GET /api/chords/transpose?id={chord_id}&t={transpose}`

**Response Format:**
```
Title: {song_title}

[Verse chords]
Lyrics...
```

**SSR Integration:**
```tsx
const response = await fetch(
  `http://127.0.0.1:8080/api/chords/transpose?id=${chordId}&t=${transpose}`
);
const text = await response.text();
const content = text.split('\n').slice(1).join('\n'); // Strip title line
```

## Random Song Selection

The randomize feature selects songs by format:

**Algorithm:**
1. Read current song's format (e.g., "comunhao")
2. Scan `items/chords/items/` directory
3. For each chord, check if `type` file exists
4. Read `type` file content (e.g., "Comunhao")
5. Match format (case-insensitive, strip accents)
6. Collect all matching chord IDs
7. Select random chord from candidates
8. Replace song with random chord (transpose=0)

**Type File Format:**
```
items/chords/items/AmazingGrace/type
```
Content: `entrada` (or `Entrada`, case-insensitive)

## Testing

Run integration tests:
```sh
./mods/sb/test_integration.sh
```

**Tests:**
1. Create choir
2. Create songbook
3. Add songs to songbook
4. Parse songbook data format
5. Verify ownership
6. Check qmap indices

## Code Structure

### sb.c (764 lines)

**Key Functions:**
- `sb_create()` - POST handler for creating songbooks
- `sb_edit()` - POST handler for editing songbooks
- `sb_transpose()` - POST handler for transpose changes
- `sb_randomize()` - POST handler for random song selection
- `sb_delete()` - DELETE handler for deleting songbooks
- `get_random_chord_by_type()` - Random selection algorithm
- `parse_sb_line()` - Parse `chord:transpose:format` format
- `check_sb_ownership()` - Verify songbook ownership
- `check_choir_ownership_for_sb()` - Verify choir ownership

**NDX API Exports:**
- None (songbook is a leaf module)

**NDX API Imports:**
- `get_session_user()` from auth module
- `url_encode()`, `read_file()`, `json_escape()` from common module
- `mpfd_parse()`, `mpfd_get()` from mpfd module

### ssr/index.tsx (497 lines)

**Components:**
- `SongbookList` - Display all songbooks
- `SongbookNew` - Create songbook form
- `SongbookView` - View songbook with transposed chords
- `SongbookEdit` - Edit songbook form

**Helpers:**
- `parseSongbookData()` - Parse data.txt format
- `fetchTransposedChord()` - Fetch from transpose API
- `listAllChords()` - List chords with type metadata

## Common Development Tasks

### Add a songbook manually

```sh
SB_ID="mysongbook"
CHOIR_ID="mychoir"
mkdir -p "items/sb/items/$SB_ID"
echo "myusername" > "items/sb/items/$SB_ID/.owner"
cat > "items/sb/items/$SB_ID/data.txt" <<EOF
title:My Songbook
choir:$CHOIR_ID
AmazingGrace:0:entrada
HolyHoly:2:santo
EOF
qmap -p "$SB_ID:My Songbook" items/sb/items/index.db
```

### View songbook songs

```sh
grep "^[^:]*:[^:]*:[^:]*$" items/sb/items/mysongbook/data.txt
```

### Change transpose for a song

Edit `data.txt` and change the middle value:
```sh
# Change AmazingGrace:0:entrada to AmazingGrace:2:entrada
sed -i 's/AmazingGrace:0:entrada/AmazingGrace:2:entrada/' \
  items/sb/items/mysongbook/data.txt
```

### List all songbooks

```sh
qmap -l items/sb/items/index.db
```

## Troubleshooting

**Problem:** "Failed to create songbook directory"
- **Cause:** Parent directory doesn't exist
- **Fix:** Ensure `items/sb/items/` exists

**Problem:** "Unauthorized" when creating songbook
- **Cause:** Not logged in or don't own the choir
- **Fix:** Login and ensure you own the choir

**Problem:** Transpose not working
- **Cause:** Chord ID doesn't exist or transpose API error
- **Fix:** Verify chord exists at `items/chords/items/{chord_id}/data.txt`

**Problem:** Randomize returns 404
- **Cause:** No chords found for format
- **Fix:** Add chords with matching `type` file, or use format "any"

**Problem:** Songs appear empty in view mode
- **Cause:** Chord files missing or transpose API down
- **Fix:** Check chord files exist, verify `/api/chords/transpose` works

## Future Enhancements

Potential improvements not yet implemented:

1. **Reorder Songs** - Drag-and-drop or move up/down buttons
2. **Duplicate Songbook** - Copy with new ID
3. **Print Mode** - Clean layout for printing
4. **Export** - PDF or text file generation
5. **Song History** - Track which chords were previously used
6. **Bulk Transpose** - Transpose all songs by same amount
7. **Format Validation** - Ensure formats match choir's format list
8. **Song Notes** - Add comments or instructions per song
9. **Setlist Templates** - Pre-defined song arrangements
10. **Statistics** - Track most-used chords/formats
