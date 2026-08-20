# Grp Module

Manages grps (grps that own gigs) with customizable song format categories.

## Purpose

The grp module provides:
- Create, view, edit, and delete grps
- Track grp ownership (one owner per grp)
- Define custom song format categories for organizing gigs
- Foundation for the gig module

## Architecture

- **Backend:** `grp.c` - C module with axil HTTP handlers
- **Frontend:** `ux/` directory handles HTML rendering via `libbud` (e.g. `detail.c`, `edit.c`)
- **Storage:** Filesystem + qmap index database
- **Dependencies:** auth, common, mpfd

## Storage

### Directory Structure
```
var/grp/
├── index.db              # qmap database: grp_id → title
└── {grp_id}/
    ├── .owner            # Plain text username
    └── data.txt          # Grp metadata (title, formats)
```

## Troubleshooting

**Problem:** "Failed to create grp directory"
- **Cause:** Parent directory doesn't exist or permission denied
- **Fix:** Ensure `var/grp/` exists with correct permissions

**Problem:** "Unauthorized" when creating grp
- **Cause:** Not logged in
- **Fix:** Login via `/login` first

**Problem:** "Unauthorized" when editing grp
- **Cause:** Not the grp owner
- **Fix:** Only the grp owner can edit

## See Also

- [AGENTS.md](../../AGENTS.md) - Development guidelines
- [mods/gig/README.md](../gig/README.md) - Gig module (uses grp for organization)
