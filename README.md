# NDC + Fresh SSR Site

> For developers, see [AGENTS.md](./AGENTS.md) for development guidelines.

Hybrid web application combining NDC (C HTTP server) with Fresh (Deno/Preact SSR).

## Architecture

- **NDC** (port 8080): HTTP server, auth, sessions, file uploads
- **Fresh** (port 3000): Renders UI with Preact components
- **Proxy**: NDC forwards SSR requests to Fresh

```
Client → NDC:8080 → proxy.c → Fresh:3000 → Preact
```

## Quick Start

```bash
# Create required directories
mkdir -p items/poem/items items/song/items items/songbook/items items/choir/items

# Build and start
make run

# Visit http://localhost:8080
```

## Modules

| Module | Path | Description |
|--------|------|-------------|
| auth | `/login`, `/register` | User authentication |
| poem | `/poem` | Poem upload & listing |
| song | `/song` | Chord charts with transpose |
| choir | `/choir` | Choir management |
| songbook | `/songbook` | Song collections |

## Fresh Migration

Migrated from legacy SSR to Fresh routes:

- **Route files**: `mods/<module>/routes/` → hard linked to `routes/<module>/`
- **Island files**: `mods/<module>/(_islands)/` → hard linked to `islands/`
- **Auto-generated**: Run `deno task start` to create hard links

### Edit Pages Pattern

Edit pages use C → Fresh integration:

1. **C handler** reads files, POSTs to Fresh
2. **Fresh** renders form with existing data
3. **Form** submits to C for saving

```c
// C: Read files, proxy to Fresh
ndc_register_handler("GET:/song/:id/edit", song_edit_get_handler);
ndc_register_handler("POST:/song/:id/edit", song_edit_post_handler);
```

## Commands

```bash
make              # Build all C modules + CSS
make run          # Build and start servers
make test         # Run tests
deno task start   # Start Fresh (creates hard links)
```

## Requirements

- C compiler (clang/gcc)
- Deno
- ndc, ndx, qmap libraries

## Testing

```bash
make test               # All tests
make unit-tests         # Module tests
cd mods/poem && ./test.sh  # Single module
```

## Troubleshooting

- **502 errors**: Check `lsof -i :3000` - Fresh may not be running
- **Missing routes**: Run `deno task setup-routes`
- **Module not loading**: Check `cat mods.load`

## License

MIT
