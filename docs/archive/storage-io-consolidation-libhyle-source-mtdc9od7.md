# Quest: storage-io-consolidation-libhyle-source

## Goal
Consolidate file and metadata I/O in mods/common/common_storage.c into libhyle-source

## Original request
> Goal: Consolidate file and metadata I/O in mods/common/common_storage.c into libhyle-source

## Parent Quest
[[site-simplification-deep-cleanup]]

## Current Status
- [x] in progress · not started · done

## Build & Run Commands
- Build: `make`
- Run: `./start.sh`
- Test: `sh tests/unit/run-source-utils.sh && make standalone-unit-tests && make pages-test && bash tests/pages/50-pickers.sh && AUTH_SKIP_CONFIRM=1 deno test --allow-all tests/e2e/`

## TDD & Quality Checklist
- [x] **1. Discovery**: Discovered how to build and run the project.
- [x] **2. Write Tests First**: Created unit test `tests/unit/source_utils_test.c` and runner `tests/unit/run-source-utils.sh`, added to `Makefile`.
- [x] **3. Feature Implementation**:
  - Upgraded `source_util_write_file` in `external/hyle/c/libhyle-source/src/source_utils.c` to use crash-safe atomic write (temp file + fsync + close + rename).
  - Declared and implemented `hyle_source_is_safe_id`, `hyle_source_slurp_file`, `hyle_source_write_file`, `hyle_source_remove_path_recursive`, `hyle_source_resolve_doc_root` in `<hyle-source/hyle_source.h>` and `src/source_utils.c`.
  - Refactored `mods/common/common_storage.c` to delegate `is_safe_id`, `write_file_path`, `slurp_file`, `item_remove_path_recursive`, and `resolve_doc_root` to `libhyle-source`.
  - Deleted duplicate internal `remove_path_recursive` static function in `mods/common/common_storage.c`.
- [x] **4. Build & Run**: Build clean with zero warnings or errors across native and WASM targets.
- [x] **5. Clean Code**: Zero debug artifacts, clean and consolidated storage layer.
- [x] **6. Server Restart & Full Test Suite**: Restarted server and passed full test suite (105/105 E2E tests).

## In-Depth Analysis & Findings
- `mods/common/common_storage.c` previously contained duplicate file I/O utilities (`slurp_file`, `write_file_path`, `is_safe_id`, `remove_path_recursive`, `resolve_doc_root`, etc.) which duplicate functionality that belongs to the data storage engine `libhyle-source`.
- `external/hyle/c/libhyle-source/src/source_utils.c` now provides atomic write with fsync, file slurp, recursive directory removal, ID safety checks, and document root resolution.
- `mods/common/common_storage.c` now delegates cleanly to these engine-level functions.

## Detailed Multi-Stage Execution Plan
### Stage 1: Implement & Expose File Utilities in `libhyle-source` & Develop Unit Tests (TDD)
- Upgraded `source_util_write_file` in `external/hyle/c/libhyle-source/src/source_utils.c` to use atomic temp-file creation and rename.
- Declared `hyle_source_is_safe_id`, `hyle_source_slurp_file`, `hyle_source_write_file`, `hyle_source_remove_path_recursive`, `hyle_source_resolve_doc_root` in `<hyle-source/hyle_source.h>`.
- Created unit test `tests/unit/source_utils_test.c` and runner `tests/unit/run-source-utils.sh`.
- Added to `Makefile` `standalone-unit-tests`.

### Stage 2: Refactor `mods/common/common_storage.c`
- Delegated `is_safe_id` to `hyle_source_is_safe_id`.
- Delegated `slurp_file` to `hyle_source_slurp_file`.
- Delegated `write_file_path` to `hyle_source_write_file`.
- Delegated `item_remove_path_recursive` to `hyle_source_remove_path_recursive`.
- Delegated `resolve_doc_root` to `hyle_source_resolve_doc_root`.
- Removed internal `remove_path_recursive` static function.

### Stage 3: Verification & Quality Gates
- Ran `standalone-unit-tests`, `pages-test`, `50-pickers.sh`, and `AUTH_SKIP_CONFIRM=1 deno test --allow-all tests/e2e/`.

## Acceptance Criteria & Polish Checklist
- [x] `tests/unit/source_utils_test.c` passing.
- [x] `mods/common/common_storage.c` file operations delegated to `libhyle-source`.
- [x] Zero build warnings or errors.
- [x] 105/105 E2E tests passing.

## Why this matters
Consolidating all file system operations into `libhyle-source` ensures that file read/write semantics, safety checks, and atomic commit guarantees are centrally implemented, thoroughly tested, and reusable by both server modules and engine layers.

## Decisions made
- Enhanced `libhyle-source`'s `source_util_write_file` to use atomic temp-file creation and rename (`.tmp.<pid>`), bringing crash-safe writes to the engine level.

## Files touched
- `external/hyle/c/libhyle-source/include/hyle-source/hyle_source.h`
- `external/hyle/c/libhyle-source/src/source_utils.c`
- `tests/unit/source_utils_test.c`
- `tests/unit/run-source-utils.sh`
- `Makefile`
- `mods/common/Makefile`
- `mods/common/common_storage.c`

## Remaining work
- None for Sub-Quest 3! Ready to archive sub-quest and proceed to Sub-Quest 4 of [[site-simplification-deep-cleanup]].

## Next Recommended Step
Archive sub-quest `storage-io-consolidation-libhyle-source` and return to parent quest `site-simplification-deep-cleanup`.
