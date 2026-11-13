# @tty-pt/hyle-react

[![npm version](https://img.shields.io/npm/v/@tty-pt/hyle-react.svg)](https://www.npmjs.com/package/@tty-pt/hyle-react)
[![License: BSD-2-Clause](https://img.shields.io/badge/License-BSD--2--Clause-blue.svg)](LICENSE)
[![React: >=18.0.0](https://img.shields.io/badge/React-%3E%3D18.0.0-61DAFB?logo=react)](#)
[![TypeScript: Ready](https://img.shields.io/badge/TypeScript-Ready-3178C6?logo=typescript)](#)

> Unopinionated, pluggable React 19 components and substrate cache hooks for Hyle datasets.

---

## 1. Installation

```bash
npm install @tty-pt/hyle-react @tty-pt/hyle @tty-pt/hyle-source
```

---

## 2. Quickstart

```tsx
import React from "react";
import { HyleTable, HylePagination, useHyleDataset } from "@tty-pt/hyle-react";

export function MemberList() {
  const { data: members, total, page, perPage, setPage } = useHyleDataset({
    dataset: "members",
    query: "active=1",
    initialPage: 1,
    perPage: 25
  });

  return (
    <div className="space-y-4">
      <HyleTable
        items={members}
        columns={[
          { key: "fullName", label: "Full Name" },
          { key: "email", label: "Email Address" },
          {
            key: "credits",
            label: "Credits",
            cell: (row) => <span className="font-mono">{row.credits}</span>
          }
        ]}
        tableClassName="w-full text-left text-sm border-collapse"
        headerRowClassName="border-b border-neutral-200 uppercase text-xs"
        rowClassName={(row) => "hover:bg-neutral-50 transition-colors"}
      />

      <HylePagination
        total={total}
        page={page}
        perPage={perPage}
        onPageChange={setPage}
      />
    </div>
  );
}
```

---

## 3. Core Components

### `<HyleTable>`
Unopinionated semantic table component mirroring `libhyle-bud`. Supports explicit columns or schema-driven auto columns with `columnOverrides`:
- `items`: Array of record objects.
- `columns`: Array of `{ key, label, cell?, sortable? }`.
- `schema`: Optional canonical Hyle schema object to auto-derive columns.
- `columnOverrides`: Map of field names to column configuration overrides.
- `onRowClick`: Callback `(item) => void` when a row is clicked.
- `onSort`: Callback `(field, order) => void` when a column sort button is clicked.

### `<HylePagination>`
Framework-agnostic page stepper and per-page selector:
- `total`: Total record count across dataset.
- `page`: Current 1-based page index.
- `perPage`: Items displayed per page.
- `onPageChange`: Callback `(pageNumber) => void`.

### `<HyleFilterBar>`
Search input and active filter badge container:
- `search`: Current query string.
- `onSearchChange`: Callback `(query) => void`.
- `activeFilters`: Map of active filter key-values.
- `onClear`: Callback to clear active filters.

### `<HyleForm>`, `<HyleField>`, `<HyleInput>`
Declarative form primitives connecting directly to Hyle schema purification rules (`hyle_purify_row`).

---

## 4. Hooks

### `useHyleDataset(options)`
React 19 native hook utilizing `useSyncExternalStore` connected directly to `@tty-pt/hyle-source`'s in-memory substrate cache:
- Real-time cache invalidation and subscription.
- Full support for Next.js SSR / Server Component `initialData` hydration.
- Optimistic update rollbacks on network mutation failures.

### `useHyleTable(options)`
Table state management hook for canonical query serialization and sort state toggling.

---

## License

BSD-2-Clause License. Copyright (c) 2026, Paulo Quirino.
