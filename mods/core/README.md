# core — Application Bootstrap and Module Loader

The root module responsible for initializing the module dependency DAG, setting up host foundations, configuring core routing/redirects, and booting the application.

## Overview

`mods/core` is the entry point specified when launching the Axil HTTP server (`-m mods/core/core`). It runs **before** the server process chroots to the repository root.

## Responsibilities

1. **Bootstrap Foundation Modules:**
   - Loads `mods/common/common` (page helpers, CSRF, storage)
   - Loads `mods/source/source` (dataset registry and query interface)
2. **Load Module Dependency Graph (`mods.load`):**
   - Reads `mods.load` and loads site modules in DAG order (`poem`, `song`, `grp`, `gig`).
   - Module dependencies are resolved and deduplicated via `xy_load()`.
3. **Core Handlers & Root Routing:**
   - Registers root route `GET:/` delegating to `mods/index`.
   - Registers legacy redirects (`/sb` $\rightarrow$ `/song`, `/chords` $\rightarrow$ `/song`).
   - Registers the dynamic reload endpoint (`GET:/reload`) for development.

## Lifecycle

```c
void xy_install(void)
{
    xy_load("./mods/common/common");
    xy_load("./mods/source/source");
    // Load modules configured in mods.load
    load_modules_from_file("mods.load");
    
    axil_register_handler("GET:/", handle_root);
    axil_register_handler("GET:/sb", handle_sb_redir);
    axil_register_handler("GET:/chords", handle_chords_redir);
    axil_register_handler("GET:/reload", handle_reload);
}
```

## Related Docs

- `docs/ARCHITECTURE.md` — Module load order and XY contract.
- `docs/BUILD.md` — Server startup commands and prerequisites.
