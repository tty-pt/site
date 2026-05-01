# Gemini Project Context: NDC + Rust SSR Site

This project is a hybrid C and Rust web application built on the NDC framework. It uses C for core business logic and request handling, while Rust (via Dioxus) provides server-side rendering (SSR) and browser enhancements (WASM).

## Architecture Overview

- **Server (C):** The `ndc` binary serves as the web server. It loads modules as dynamic libraries (`.so`).
- **Business Logic (C):** Modules in `mods/` (e.g., `auth`, `song`, `poem`) handle requests, manage data in the `items/` directory, and perform logic.
- **SSR Bridge (C/Rust):** `mods/ssr/ssr.c` acts as a high-performance bridge. It passes request data and complex payloads as **JSON strings** to a Rust dynamic library (`mods/ssr/rust-renderer`). This avoids complex struct mirroring and minimizes FFI boilerplate.
- **FFI Boundary:** The C/Rust boundary is surgical. It uses a stable `RenderRequest` / `RenderResult` C-compatible ABI. Memory allocated by Rust MUST be freed by Rust via an explicit `ssr_free_result_ffi` call from the C side. **Note:** This interface is a primary candidate for further optimization and improvement to reduce boilerplate and enhance data throughput.
- **Rendering (Rust):** The Rust renderer uses Dioxus for HTML generation. It dynamically includes `ssr.rs` files from other modules. JSON is the primary data source, ensuring the C side only needs to preocupy itself with data gathering and authorization before passing a single blob to Rust.
- **Client-side (Rust/WASM):** Interactive features are implemented in Rust and compiled to WASM (e.g., `mods/song/client`).
- **Deployment:** The application is designed to run within a chroot environment for security.

## Project Structure

- `mods/`: Backend logic and SSR definitions.
  - `<module>/<module>.c`: C request handlers and logic.
  - `<module>/ssr.rs`: Rust rendering logic for the module.
  - `<module>/client/`: (Optional) Rust WASM client-side code.
- `htdocs/`: Static assets and compiled WASM/JS.
- `items/`: Data storage for application entities (songs, poems, etc.).
- `tests/`:
  - `e2e/`: Playwright end-to-end tests (run via Deno).
  - `integration/`: Shell-based integration tests.
  - `pages/`: Smoke tests for rendered pages.

## Key Development Commands

- **Build all:** `make`
- **Run server:** `./start.sh` (Starts `ndc` on port 8080 by default)
- **Run all tests:** `make test`
- **Run unit tests:** `make unit-tests` (Runs `test.sh` in each active module)
- **Run e2e tests:** `make e2e-tests` (Requires Deno and a running server)
- **Clean build:** `make clean` or `make distclean`

## Development Conventions

### C Programming
- **Indentation:** Use tabs.
- **Redirects:** Use `ndc_header_set` and `ndc_respond` for 303 redirects:
  ```c
  ndc_header_set(fd, "Location", location);
  ndc_respond(fd, 303, "");
  return 0;
  ```
- **Memory:** Never `free()` values returned by `qmap_get` or other `qmap` managed pointers.
- **Handlers:** Registered in `ndx_install()` using `ndc_register_handler`.

### Rust SSR
- Module rendering logic must be in `ssr.rs` within the module directory.
- The `rust-renderer` build script (`build.rs`) auto-discovers these files.
- Use `RequestContext` to access request details and return `ResponsePayload`.

### Data Management
- Entity data is typically stored in `items/<module_name>/items/<id>/`.
- Use the `index` module for searchable metadata.

### Testing
- Always verify changes with `make pages-test` and relevant `e2e` tests.
- New features should include a new test file in `tests/e2e/`.

## Common Pitfalls
- **Hanging Redirects:** Usually caused by missing `Connection: close` or `DF_TO_CLOSE`.
- **Module Loading:** Ensure the module is listed in `mods.load`.
- **WASM Build:** Requires `wasm-bindgen-cli` and the `wasm32-unknown-unknown` target.

## Code Reduction & Refactoring Strategy

**Core Principle: Line Count Efficiency.** Less line count with equal (or better) functionality is a primary engineering goal. Every refactor should aim to reduce boilerplate and maximize logic density without compromising readability or correctness.

To reduce boilerplate and redundancy across the C modules, the following strategies should be prioritized:

### 1. Unified TSV Indexing Logic [COMPLETED]
Common TSV management (loading, saving, rebuilding, field cleaning) has been moved to the `index` module.
- **Functions:** `index_tsv_load`, `index_tsv_save`, `index_tsv_rebuild`.
- **Status:** Integrated into `song.c` and `songbook.c`, saving ~200 lines total.

### 2. Schema-Driven Metadata Handling
Introduce a schema-driven approach for module metadata. Define field names and sizes in an array and use a generic `item_meta_respond` helper to handle reading and JSON responding in one step.
- **Goal:** Eliminate repetitive "authorized" callbacks and manual `meta_field_t` setup.

### 3. Simplified Handler Registration
Implement a `register_standard_item_handlers(module_name, callbacks_struct)` helper.
- **Goal:** Reduce the repetitive `ndc_register_handler` calls in `ndx_install` for standard routes like `GET /:id` and `POST /:id/edit`.

### 4. Choir-Centric Repertoire Reuse
The `choir` and `songbook` modules share identical repertoire management logic (collections of songs). Instead of a complete merge, the `choir` module serves as the primary provider of repertoire features, which the `songbook` module reuses.
- **Goal:** Centralize repertoire implementation in `choir` (e.g., using `repertoire_impl.inc`) and ensure `songbook` consumes these features without duplication.
- **Isolation:** Maintain distinct modules for distinct types of collections, but share the underlying logic.

### 5. Memory & Module Integrity
- **Memory Management:** Avoid manual memory management (`malloc`/`free`). Always prefer the `qmap` system and other framework-provided managed pointers to ensure safety and prevent leaks.
- **Module Lifecycle:** Never re-purpose existing modules for different functionality. Each module should have a clear, stable responsibility. If new functionality is significantly different, create a new module.
- **Shared Repertoire Logic:** Standardize the "repertoire" file name (e.g., always `data.txt`) and utilize `choir`'s core repertoire actions across all collection-style modules.

### 6. JSON as Universal Interface
Minimize C-side logic by gathering data into JSON objects as early as possible.
- **Goal:** Shift complex data structure management and rendering logic to Rust. The C side should focus on I/O, authorization, and data retrieval, passing a single JSON blob to the SSR bridge. This reduces the need for complex C structs and repetitive field mapping.
