# @tty-pt/hyle-react

Unopinionated, pluggable React 19 components and substrate cache hooks for Hyle datasets.

## Architecture

`@tty-pt/hyle-react` is the React counterpart to `libhyle-bud`. It binds structured Hyle datasets to standard React DOM components without imposing hardcoded styling or rigid CSS.

- **`<HyleTable>`**: Renders semantic HTML tables with sortable headers, default text cells or custom cell projections, and row click handlers.
- **`<HylePagination>`**: Unopinionated page stepper, per-page selector, and row count indicators.
- **`<HyleFilterBar>`**: Search input, active filter tags, and clear actions.
- **`useHyleDataset`**: React 19 native hook utilizing `useSyncExternalStore` connected directly to `@tty-pt/hyle-source`'s substrate cache.
