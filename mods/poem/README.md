# poem - Poem Upload and Listing

Upload and display poems via HTTP endpoints and SSR pages.

## Overview

The poem module provides functionality to upload poems as text files and display them in a listing. Poems are stored in the filesystem with support for comments.

## Endpoints

### POST /poem/add

Upload a poem via multipart form data.

**Parameters:**
- `id` (required) - Poem identifier (alphanumeric, used as directory name)
- `file` (required) - Poem content file (text)

**Responses:**
- `303 See Other` - Success, redirects to `/poem/`
- `400 Bad Request` - Missing id or file, or empty file
- `415 Unsupported Media Type` - Wrong content type (expected `multipart/form-data`)
- `500 Internal Server Error` - Failed to create directory or write file

**Example:**
```sh
curl -X POST http://localhost:8080/poem/add \
  -F "id=my-poem" \
  -F "file=@poem.txt"
```

### GET /poem/add

Returns `405 Method Not Allowed` (use POST instead).

## SSR Routes

The module provides server-side rendered pages:

- `/poem` or `/poem/` - List all uploaded poems
- `/poem/add` - Upload form (SSR renders the form)
- `/poem/:id` - View individual poem detail

## Storage Structure

Poems are stored in the filesystem:

```
items/poem/items/{id}/
  ├── pt_PT.html      # Poem content
  └── comments.txt    # Comments file (empty on creation)
```

**Example:**
```
items/poem/items/my-poem/
  ├── pt_PT.html
  └── comments.txt
```

## Setup Requirements

⚠️ **CRITICAL:** The directory `items/poem/items/` must exist before poems can be uploaded.

```sh
mkdir -p items/poem/items
```

If this directory doesn't exist, all uploads will fail with:
```
HTTP 500 - Failed to create poem directory
```

## Error Handling

The module includes comprehensive error checking (added March 2026):

- Checks `mkdir()` return value for directory creation
- Checks `fopen()` return value for file writing
- Returns HTTP 500 with descriptive error messages on failure
- Properly frees allocated memory before returning on error paths

## Dependencies

This module depends on:
- `mods/ssr/ssr` - For SSR rendering
- `mods/mpfd/mpfd` - For parsing multipart form data uploads

Declared in `ndx_deps[]` and loaded via `ndx_load()` in `ndx_install()`.

## Implementation Details

**Backend:** `poem.c` (lines 14-99: `handle_poem_add()`)
- Parses multipart form data using mpfd
- Creates directory at `{DOCUMENT_ROOT}/items/poem/items/{id}/`
- Writes poem content to `pt_PT.html`
- Creates empty `comments.txt` file
- Redirects to listing page on success

**SSR:** `ssr/index.tsx`
- `getPoems()` - Reads directory and returns poem IDs
- `getPoemContent()` - Reads poem HTML file
- Components: `PoemList`, `PoemAdd`, `PoemDetail`

## Testing

**Unit Tests:** `./test.sh` or `make -C mods/poem test`

Covers:
1. Wrong content-type (expects 415)
2. Missing id field (expects 400)
3. Missing file field (expects 400)
4. Empty file (expects 400)
5. Valid upload (expects 303)
6. File creation verification
7. Content verification
8. Comments file creation
9. GET request rejection (expects 405)

**Integration Tests:** `tests/integration/02-poem-listing-update.sh`

Verifies:
- Empty listing shows "No poems yet"
- First poem appears after upload
- Second poem appears after upload
- Both poems visible in listing
- Correct poem count

## Usage Example

From another module or handler:

```c
// Upload is handled via HTTP POST to /poem/add
// Listing is rendered via SSR at /poem/

// Files are accessible at:
// {DOCUMENT_ROOT}/items/poem/items/{id}/pt_PT.html
```

## Troubleshooting

### Uploads fail with "Failed to create poem directory"

**Cause:** Parent directory `items/poem/items/` doesn't exist.

**Solution:**
```sh
mkdir -p /home/quirinpa/site/items/poem/items
```

### Poems don't appear in listing

**Causes:**
1. Directory doesn't exist (see above)
2. Site server not running
3. Permissions issue on items directory

**Debug:**
```sh
# Check if directory exists
ls -la items/poem/items/

# Check if ndc is running
pgrep -a ndc

# Check directory permissions
ls -ld items/poem/items/
```

### Test cleanup deletes poems

**Issue:** Running integration tests deletes `items/poem/items/` directory.

**Workaround:** Recreate directory after tests:
```sh
make test
mkdir -p items/poem/items
```

## See Also

- [mods/mpfd/README.md](../mpfd/README.md) - Multipart form data parsing
- [mods/ssr/README.md](../ssr/README.md) - SSR architecture
- [AGENTS.md](../../AGENTS.md) - Development guidelines
