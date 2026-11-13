# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2026-11-13

### Added
- **Unopinionated React 19 Components (`@tty-pt/hyle-react`)**:
  - `<HyleTable>`: Semantic table component mirroring `libhyle-bud` with sortable headers, auto-derived schema columns, `columnOverrides`, and custom cell projections.
  - `<HylePagination>`: Page stepper, per-page selector, and bounds calculations.
  - `<HyleFilterBar>`: Search input, filter tags, and clear actions.
  - `<HyleForm>`, `<HyleField>`, `<HyleInput>`: Schema-driven declarative form primitives.
- **Substrate Cache & Hooks**:
  - `useHyleDataset`: React 19 native hook utilizing `useSyncExternalStore` connected directly to `@tty-pt/hyle-source`'s in-memory substrate cache with optimistic updates and SSR `initialData` support.
  - `useHyleTable`: Table state management hook for canonical query serialization and sort state toggling.
- **TypeScript First**: Full TypeScript declarations in `dist/index.d.ts` and automated types integration.
