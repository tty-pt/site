# Choir Module

Manages choirs (groups that own songbooks) with customizable song format categories.

## Purpose

The choir module provides:
- Create, view, edit, and delete choirs
- Track choir ownership (one owner per choir)
- Define custom song format categories for organizing songbooks
- Foundation for the songbook (sb) module

## Architecture

- **Backend:** `choir.c` - C module with ndc HTTP handlers
- **Frontend:** `ssr/index.tsx` - Deno SSR React component
- **Storage:** Filesystem + qmap index database
- **Dependencies:** auth, common, mpfd

## Storage

### Directory Structure
```
items/choir/items/
├── index.db              # qmap database: choir_id → title
└── {choir_id}/
    ├── .owner            # Plain text username
    └── data.txt          # Choir metadata (see format below)
```

### Data Format (`data.txt`)

```
title:{choir_title}
format:{format_name1}
format:{format_name2}
...
```

**Example:**
```
title:Sunday Mass Choir
format:entrada
format:aleluia
format:comunhao
format:santo
format:saida
```

## API Endpoints

### POST /api/choir/create

Create a new choir.

**Request:**
- Content-Type: `multipart/form-data`
- Authentication: Required (via session cookie)

**Parameters:**
- `id` - Choir ID (alphanumeric, used in URL)
- `title` - Choir display name
- `format` - Song format categories (one per line, default list provided)

**Response:**
- `303 See Other` → `/choir/{id}` on success
- `400 Bad Request` - Missing fields
- `401 Unauthorized` - Not logged in
- `500 Internal Server Error` - Creation failed

**Example:**
```sh
curl -X POST http://127.0.0.1:8080/api/choir/create \
  -H "Cookie: session=..." \
  -F "id=sunday-mass" \
  -F "title=Sunday Mass Choir" \
  -F "format=entrada
aleluia
comunhao"
```

### POST /api/choir/:id/edit

Edit an existing choir's title and format list.

**Request:**
- Content-Type: `multipart/form-data`
- Authentication: Required (must be choir owner)

**Parameters:**
- `title` - Updated choir title
- `format` - Updated format list (one per line)

**Response:**
- `303 See Other` → `/choir/{id}` on success
- `400 Bad Request` - Missing fields
- `401 Unauthorized` - Not logged in or not owner
- `404 Not Found` - Choir doesn't exist
- `500 Internal Server Error` - Update failed

**Example:**
```sh
curl -X POST http://127.0.0.1:8080/api/choir/sunday-mass/edit \
  -H "Cookie: session=..." \
  -F "title=Updated Title" \
  -F "format=entrada
saida"
```

### DELETE /api/choir/:id

Delete a choir.

**Request:**
- Authentication: Required (must be choir owner)

**Response:**
- `303 See Other` → `/choir` on success
- `401 Unauthorized` - Not logged in or not owner
- `404 Not Found` - Choir doesn't exist
- `500 Internal Server Error` - Deletion failed

**Example:**
```sh
curl -X DELETE http://127.0.0.1:8080/api/choir/sunday-mass \
  -H "Cookie: session=..."
```

## SSR Routes

### GET /choir

List all choirs.

**Display:**
- All choirs from index.db
- "Create New Choir" button (if logged in)
- Links to individual choir pages

### GET /choir/new

Create choir form.

**Requirements:**
- User must be logged in
- Shows login prompt if not authenticated

**Form Fields:**
- Choir ID (lowercase, no spaces)
- Choir Title
- Format List (textarea, one per line, pre-filled with defaults)

**Default Formats:**
```
entrada
aleluia
ofertorio
santo
comunhao
acao_de_gracas
saida
any
```

### GET /choir/:id

View choir details.

**Display:**
- Choir title
- Owner username
- Format list
- Songbook count (number of songbooks in this choir)
- Owner actions: Edit button, Delete button (with confirmation)

### GET /choir/:id/edit

Edit choir form.

**Requirements:**
- User must be logged in
- User must be choir owner

**Form Fields:**
- Title (pre-filled)
- Format List (pre-filled, one per line)

## Default Song Formats

The following default formats are provided when creating a new choir:

| Format | Portuguese Meaning | Typical Use |
|--------|-------------------|-------------|
| entrada | Entrance | Opening hymn |
| aleluia | Alleluia | Gospel acclamation |
| ofertorio | Offertory | Presentation of gifts |
| santo | Holy | Sanctus |
| comunhao | Communion | Communion hymn |
| acao_de_gracas | Thanksgiving | Post-communion |
| saida | Closing | Recessional |
| any | Any | General use |

Users can customize this list when creating or editing a choir.

## Ownership

- Ownership tracked via `.owner` file containing username
- Only the owner can edit or delete a choir
- Only the owner can create songbooks for the choir (via sb module)
- No group membership or multiple owners in current implementation

## Integration with Songbook Module

The choir module is a prerequisite for the songbook (sb) module:
- Songbooks belong to a choir
- Songbooks inherit format categories from their choir
- Only choir owner can create songbooks in that choir
- Choir's songbook counter updated when songbooks are created/deleted

## Testing

Run unit tests:
```sh
./mods/choir/test.sh
```

**Tests:**
1. List page loads (`/choir`)
2. Create page requires login (`/choir/new`)

## Code Structure

### choir.c (328 lines)

**Key Functions:**
- `choir_create()` - POST handler for creating choirs
- `choir_edit()` - POST handler for editing choirs
- `choir_delete()` - DELETE handler for deleting choirs
- `check_choir_ownership()` - Verify user owns choir
- `ndx_install()` - Register HTTP handlers
- `ndx_open()` - Initialize qmap database

**NDX API Exports:**
- None (choir is a leaf module)

**NDX API Imports:**
- `get_session_user()` from auth module
- `url_encode()`, `read_file()`, `json_escape()` from common module
- `mpfd_parse()`, `mpfd_get()` from mpfd module

### ssr/index.tsx (276 lines)

**Components:**
- `ChoirList` - Display all choirs
- `ChoirNew` - Create choir form
- `ChoirView` - View choir details
- `ChoirEdit` - Edit choir form

**Helpers:**
- `parseChoirData()` - Parse data.txt format
- `countSongbooks()` - Count songbooks in choir

## Common Development Tasks

### Add a new choir manually

```sh
CHOIR_ID="mychoir"
mkdir -p "items/choir/items/$CHOIR_ID"
echo "myusername" > "items/choir/items/$CHOIR_ID/.owner"
cat > "items/choir/items/$CHOIR_ID/data.txt" <<EOF
title:My Choir
format:entrada
format:saida
EOF
qmap -p "$CHOIR_ID:My Choir" items/choir/items/index.db
```

### Check choir ownership

```sh
cat items/choir/items/mychoir/.owner
```

### List all choirs

```sh
qmap -l items/choir/items/index.db
```

### View choir data

```sh
cat items/choir/items/mychoir/data.txt
```

## Troubleshooting

**Problem:** "Failed to create choir directory"
- **Cause:** Parent directory doesn't exist or permission denied
- **Fix:** Ensure `items/choir/items/` exists with correct permissions

**Problem:** "Unauthorized" when creating choir
- **Cause:** Not logged in
- **Fix:** Login via `/login` first

**Problem:** "Unauthorized" when editing choir
- **Cause:** Not the choir owner
- **Fix:** Only the choir owner can edit

**Problem:** Choir appears in database but not on page
- **Cause:** Corrupt data.txt or missing title line
- **Fix:** Verify `data.txt` has `title:...` as first line

## Future Enhancements

Potential improvements not yet implemented:

1. **Group Membership** - Allow multiple users to manage a choir
2. **Localization** - Translate format names and UI text
3. **Format Templates** - Pre-defined format sets (liturgical, concert, etc.)
4. **Songbook Statistics** - Track which formats are most used
5. **Archive/Unarchive** - Hide unused choirs without deleting
6. **Search/Filter** - Find choirs by name or owner
