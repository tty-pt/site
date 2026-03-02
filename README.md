# NDC Site (ndc + Deno SSR)

> For developers, see [AGENTS.md](./AGENTS.md) for development guidelines.

Small web site built as an ndc application with a Deno-based SSR backend. The repository contains the ndc core, several modules (auth, poem, chords, mpfd, index_api) and an SSR proxy module that forwards requests to a Deno SSR server.

## Architecture

This site uses ndc as the HTTP server, with ndx for module loading and qmap for data storage. SSR pages are rendered via a Deno SSR backend.

Request flow: Client → ndc (8080) → ssr.so → Deno SSR (3000) → React

## Setup Requirements

Before running the site, create required directories:

```sh
mkdir -p items/poem/items
mkdir -p items/chords/items
```

These directories are used by the poem and chords modules to store uploaded content. If they don't exist, uploads will fail with HTTP 500 errors.

## Quick Start

1. **Setup directories:**
   ```sh
   mkdir -p items/poem/items items/chords/items
   ```

2. **Build and test:**
   ```sh
   make test
   ```

3. **Start the server:**
   ```sh
   ./start.sh
   ```
   Server runs on http://localhost:8080

4. **Run page smoke tests:**
   ```sh
   make pages-test
   ```

Requirements
- A C compiler (clang/gcc) and development headers for ndc/ndx/qmap (used when building modules)
- `deno` installed at the path used by `start.sh` (or edit `start.sh` to point to your deno)
- `ndc` and `qmap` libs available on your system (the Makefile expects them under `/home/quirinpa` in this tree; adapt `Makefile`/env as needed)

Library dependencies
- **qmap** v0.6.0 — Key-value map library with file persistence
- **ndx** v0.2.0 — Plugin/module system with dependency loading
- **ndc** v1.0.0 — HTTP(S) + WebSocket server framework

Repository layout
- `mods/` — ndc modules (each has a C source file, Makefile, and optional SSR directory)
- `mods/ssr/ssr.c` — SSR proxy implementation (parses upstream response and forwards via ndc APIs)
- `mods/ssr/server.ts` — Deno SSR server (optional local file; not tracked by default)
- `tests/pages/` — small page smoke tests and helpers
- `tests/integration/` — integration tests
- `docs/` — additional documentation
- `AGENTS.md` — guidelines for agents to run tests on each change
- `.githooks/pre-commit` — sample pre-commit hook that runs `make test` (not installed automatically)

## Module Documentation

Each module has detailed documentation:

- [mods/auth/](mods/auth/README.md) - Authentication and session management
- [mods/chords/](mods/chords/README.md) - Chord chart upload and display
- [mods/common/](mods/common/README.md) - Shared utility functions
- [mods/mpfd/](mods/mpfd/README.md) - Multipart form data parser
- [mods/poem/](mods/poem/README.md) - Poem upload and listing
- [mods/ssr/](mods/ssr/README.md) - SSR proxy architecture

For creating new modules, see [docs/MODULE_DEVELOPMENT.md](docs/MODULE_DEVELOPMENT.md).

Testing rules (agents and humans)
1. Run `make test` after every change and before committing.
2. For end-to-end checks, run `make pages-test` (this starts the services then runs smoke tests).
3. Use `git status` and `git ls-files` to avoid committing build artifacts (see `.gitignore`).

Enable the pre-commit hook locally
```sh
# link the provided hook into Git hooks so it runs automatically
ln -s ../../.githooks/pre-commit .git/hooks/pre-commit
chmod +x .githooks/pre-commit
```

Development notes
- The SSR proxy was hardened to parse upstream status/headers and forward them via `ndc_head`/`ndc_header` to avoid duplicated headers and injection risks — see `mods/ssr/ssr.c`.
- Avoid committing generated binaries (`*.so`, `module.db`, `mods.load`) or editor swap files. These are ignored by `.gitignore`.

## Recent Fixes

### mpfd Memory Management (March 2026)

Fixed critical use-after-free bug in the multipart form data parser:

**Problem:** Code called `free()` on values retrieved from qmap before calling `qmap_put()`, causing crashes and memory corruption.

**Solution:**
- Registered custom variable-size qmap type via `qmap_mreg()`
- Removed all manual `free()` calls on qmap-managed values
- qmap now properly manages all memory for uploaded field data

See `mods/mpfd/mpfd.c:315-316` for the type registration implementation.

### Poem Upload Error Handling (March 2026)

Added comprehensive error checking to poem upload handler:

**Problem:** Silent failures when directory didn't exist or file writes failed. Users saw HTTP 303 redirect but poems weren't saved.

**Solution:**
- Added `errno.h` for proper error detection
- Check `mkdir()` return value, return HTTP 500 on failure (except EEXIST)
- Check `fopen()` return value, return HTTP 500 if file write fails
- Clean up allocated memory before returning on error paths
- Return descriptive error messages ("Failed to create poem directory", "Failed to write poem file")

See `mods/poem/poem.c:66-90` for the updated error handling.

If something breaks
- Re-run `make test` to get failing module output.
- Check logs in `/tmp` if you use `start.sh` (or update `start.sh` to redirect logs where you want).

## Troubleshooting

### Poem uploads fail with HTTP 500

**Symptom:** Uploading a poem returns "Failed to create poem directory" or "Failed to write poem file"

**Cause:** The `items/poem/items/` directory doesn't exist.

**Solution:**
```sh
mkdir -p items/poem/items
```

### Tests delete production data

**Issue:** Running integration tests deletes the `items/poem/items/` directory, breaking production uploads.

**Workaround:** Recreate the directory after running tests:
```sh
make test
mkdir -p items/poem/items
```

### SSR not responding / 502 errors

**Causes:**
1. Deno SSR server not running
2. Deno crashed
3. Port 3000 already in use

**Debug:**
```sh
# Check if Deno is running
pgrep -a deno

# Check port 3000
lsof -i :3000

# Restart Deno
cd mods/ssr
deno run --allow-net --allow-read --allow-env server.ts
```

### Module not loading

**Symptom:** Module doesn't respond to requests

**Debug:**
```sh
# Check if module is in mods.load
cat mods.load | grep mymodule

# Check module.db entry
qmap -g mymodule module.db

# Check ndc logs for load errors
tail -f /tmp/ndc.log
```

License
- MIT
