# Grp Module

Manages grps (grps that own gigs) with customizable song format categories.

## Purpose

The grp module provides:
- Create, view, edit, and delete grps
- Track grp ownership (one owner per grp)
- Define custom song format categories for organizing gigs
- Foundation for the gig module

## Architecture

- **Backend:** `grp.c` - C module with axil HTTP handlers; generic Add/Edit CRUD handled via `mods/index` (`index_open`)
- **Frontend:** `ux/` directory handles detail and repertoire rendering (`detail.c`, `all.c`) using `site_ui_item_row` and `site_ui_action_form`
- **Storage:** Filesystem persistence via `libhyle-source` (`store_fs`)
- **Dependencies:** auth, common, mpfd, index, song

## Storage

### Directory Structure
```
var/grp/
└── {grp_id}/
    ├── owner             # Plain text username
    ├── title             # Group title
    ├── formats           # Song format categories
    └── songs             # Repertoire song IDs
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
