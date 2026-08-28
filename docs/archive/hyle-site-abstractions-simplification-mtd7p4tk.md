# Quest: Analyse hyle and site abstractions & simplify site module side

## Goal
Perform a comprehensive architectural audit and deep analysis of `hyle` (`libhyle`, `libhyle-source`, `libhyle-bud`), site modules (`mods/*`), `axil`, and the reference design in `../entity` (`@vinci/entity`). Formulate an ambitious, modular simplification strategy to migrate generic data resolution, picker collection/rendering, and HTTP CRUD into `external/hyle` and a generic `axil-hyle` adapter, keeping site modules strictly domain-focused and declarative.

## Current Status
- [x] done

## Completed Sub-Quests Summary

1. **Sub-Quest 1: Core Foreign-Key & Display Resolution in `libhyle-source`** ([[foreign-key-option-resolution-libhyle-source]])
   - Extracted schema display field discovery, foreign option querying, and comma/position token resolution into `external/hyle/c/libhyle-source/src/options.c`.
   - Exposed unified C APIs in `<hyle-source/hyle_source.h>`.
   - Replaced duplicate manual implementations in `mods/index/pick.c` and `mods/index/list_fill.c`.
   - Added unit test suite `tests/unit/source_options_test.c`.

2. **Sub-Quest 2: Schema-Driven Picker View Collection in `libhyle-bud`** ([[picker-view-collection-libhyle-bud]])
   - Extracted schema-driven picker view collection and query parsing into `external/hyle/c/libhyle-bud/src/picker.c`.
   - Exposed `hyle_bud_picker_view_collect_schema` and `hyle_bud_picker_view_collect_scoped` in `<hyle-bud/hyle-bud.h>`.
   - Replaced schema iteration logic in `mods/index/pick.c` with direct one-call delegation.
   - Added unit test suite `tests/unit/bud_picker_collect_test.c`.

3. **Sub-Quest 3: Generic `axil-hyle` HTTP Library** ([[generic-axil-hyle-module]])
   - Created standalone `external/axil-hyle` library providing `axil_hyle_install_routes()`.
   - Implemented full generic REST CRUD API (`/api/dataset/...`) and picker fragment hot-swap endpoint (`/pick/:id/options`).
   - Removed 1,208 LOC `mods/source/source-http.c` from site.
   - Removed fragment route registration and handlers from `mods/index/pick.c` and `mods/index/index.c`.

4. **Sub-Quest 4: Common Redundancy Cleanup** ([[cleanup-common-redundancies]])
   - Eliminated `mods/common/bud_adapter.c`, `bud_adapter.h`, and `bud_adapter_overlay_*` wrappers.
   - Updated consumers (`song.c`, `gig.c`) to call `hyle_bud_state_overlay_*` direct APIs.

## Acceptance Criteria & Quality Gates
- [x] Zero compilation warnings or errors across all modules.
- [x] Zero debug artifacts or leftover logs.
- [x] Server restarts cleanly.
- [x] Full test suite (`make standalone-unit-tests`, `make pages-test`, `tests/pages/50-pickers.sh`, and full E2E suite) passes with zero errors.

## Sub-Quests
- [x] [[foreign-key-option-resolution-libhyle-source]] — Move core foreign-key, display field, and option resolution from site mods into libhyle-source
- [x] [[picker-view-collection-libhyle-bud]] — Move schema-driven picker view collection and query parsing into libhyle-bud
- [x] [[generic-axil-hyle-module]] — Create generic axil-hyle library providing REST CRUD and picker fragment endpoints for any axil-hyle website
- [x] [[cleanup-common-redundancies]] — Eliminate redundant bud_adapter wrappers in mods/common and update consumers to use hyle_bud_state_overlay direct APIs

## Next Recommended Step
Prompt user with completion wrap-up options via `ask_questions`.
