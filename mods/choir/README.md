# Choir Module

Manages choirs (groups that own songbooks) with customizable song format categories.

## Purpose

The choir module provides:
- Create, view, edit, and delete choirs
- Track choir ownership (one owner per choir)
- Define custom song format categories for organizing songbooks
- Foundation for the songbook module

## Architecture

- **Backend:** `choir.c` - C module with axil HTTP handlers
- **Frontend:** `ux/` directory handles HTML rendering via `libbud` (e.g. `detail.c`, `edit.c`)
- **Storage:** Filesystem + qmap index database
- **Dependencies:** auth, common, mpfd

## Storage

### Directory Structure
```
items/choir/items/
├── index.db              # qmap database: choir_id → title
└── {choir_id}/
    ├── .owner            # Plain text username
    └── data.txt          # Choir metadata (title, formats)
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

## See Also

- [AGENTS.md](../../AGENTS.md) - Development guidelines
- [mods/songbook/README.md](../songbook/README.md) - Songbook module (uses choir for organization)
