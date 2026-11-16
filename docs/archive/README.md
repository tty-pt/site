# Documentation Archive (`docs/archive/`)

This directory houses completed historical design journals, point-in-time forensic audits, completed release wave plans, and internal tooling specifications. These files are preserved for historical provenance and in-code section citations (such as `ST.md §27`), but are no longer active specifications.

## Contents

### Completed Architecture & Security Journals
- **[`ST.md`](./ST.md)**: Spacetime: worlds, regions, delegation and runtime ND modules (171 KB). Cited in `external/nd-region`, `external/nd-bus`, `external/nd-obj`, `external/nd-session`.
- **[`NO_WIZ.md`](./NO_WIZ.md)**: Dropping `EF_WIZARD` in favor of region ownership. Cited in `external/nd-speech`.
- **[`FIX.md`](./FIX.md)**: Completed security bugfix record for PTY descriptor lifecycle and disconnect hook (2026-10-03).
- **[`SAFE_SH.md`](./SAFE_SH.md)**: Fail-closed terminal access across axil modules (Phase A verified 2026-10-06).

### Historical Audits & Past Release Waves (`docs/archive/audits/`)
- `BUILD_PORTABILITY_AND_ENCAPSULATION_AUDIT.md`: Pre-v1.2.0 linker and search path audit (Nov 2).
- `BUILD_PORTABILITY_AND_ENCAPSULATION_REMEDIATION_ROADMAP.md`: Pre-v1.2.0 remediation roadmap.
- `COMPREHENSIVE_ARCHITECTURAL_AUDIT_AND_NEXT_STEPS.md`: Audit following quests 1xJZan - 1xJngo (Oct 22).
- `NEVERDARK_ARCHITECTURAL_REVIEW.md`: Diff review for commits `bf0ebf2..14da0fa` (Oct 21).
- `NEVERDARK_TENANT_MODULE_ARCHITECTURAL_AUDIT.md`: Audit of 27 tenant modules (Oct 22).
- `RELEASE_PLAN.md` & `RELEASES_PLAN.md`: Release Wave 2026.10 planning documents (landed in v1.2.0/v1.2.1).

### Intermediate Task Handoffs (`docs/archive/handoffs/`)
- `handoff-field-chunk-architecture.md`: Working notes from procedural field overhaul (Nov 6).
- `handoff-recursive-noise-water.md`: Working notes from procedural worldgen pass (Nov 6).
- `water-realism-design.md`: Working design scratchpad for Seven water (Nov 6).

### Extension Rebuild & Tooling History
- **`pi-quest/`**: `HIGH_LEVEL.md`, `REBUILD_PLAN.md`, `THIS.md`, `FIX_ORDER.md`, and `issues/` (60 issue specs from early September).
- **`pi-mm/`**: `mm-plan/` (12-phase planning files from September) and `RUNNING.md`.
