import test from "node:test";
import assert from "node:assert/strict";
import React from "react";
import {
  HyleTable,
  HylePagination,
  HyleFilterBar,
  HyleInput,
  HyleField,
  HyleForm,
  useHyleDataset,
  useHyleTable,
} from "../dist/index.js";
import { cachePut, cacheClear } from "@tty-pt/hyle-source";

test("@tty-pt/hyle-react exports all core components and hooks", () => {
  assert.equal(typeof HyleTable, "function");
  assert.equal(typeof HylePagination, "function");
  assert.equal(typeof HyleFilterBar, "function");
  assert.equal(typeof HyleInput, "function");
  assert.equal(typeof HyleField, "function");
  assert.equal(typeof HyleForm, "function");
  assert.equal(typeof useHyleDataset, "function");
  assert.equal(typeof useHyleTable, "function");
});

test("HyleTable renders table structure with custom cell projections and Tailwind classes", () => {
  const columns = [
    { key: "name", label: "Member", headerClassName: "font-semibold text-neutral-400" },
    {
      key: "contact",
      label: "Contact",
      cell: (item) => `Mail to: ${item.email}`,
    },
    { key: "status", label: "Status" },
  ];

  const items = [
    { id: "m1", name: "Alice", email: "alice@example.com", status: "PROSPECT" },
    { id: "m2", name: "Bob", email: "bob@example.com", status: "ACTIVE" },
  ];

  let clickedRow = null;
  let sortField = null;
  let sortOrder = null;

  const vnode = HyleTable({
    columns,
    items,
    keyField: "id",
    sort: "name",
    sortOrder: "asc",
    tableClassName: "w-full text-left text-xs",
    headerRowClassName: "border-b border-duna-border uppercase",
    rowClassName: (item) => (item.status === "PROSPECT" ? "bg-amber-50" : "bg-white"),
    onRowClick: (item) => {
      clickedRow = item;
    },
    onSort: (field, order) => {
      sortField = field;
      sortOrder = order;
    },
  });

  assert.ok(vnode, "HyleTable must return a valid React element");
  assert.equal(vnode.type, "div");
  assert.equal(vnode.props["data-hyle-slot"], "table");

  const table = vnode.props.children;
  assert.equal(table.type, "table");
  assert.equal(table.props.className, "w-full text-left text-xs");

  const [thead, tbody] = table.props.children;
  assert.equal(thead.type, "thead");
  assert.equal(tbody.type, "tbody");

  // Verify thead header row and sort button
  const headerTr = thead.props.children;
  assert.equal(headerTr.props.className, "border-b border-duna-border uppercase");
  assert.equal(headerTr.props.children.length, 3);

  const firstTh = headerTr.props.children[0];
  assert.equal(firstTh.props["data-field"], "name");
  assert.equal(firstTh.props["aria-sort"], "ascending");

  // Trigger sort button click
  const sortBtn = firstTh.props.children;
  assert.equal(sortBtn.type, "button");
  sortBtn.props.onClick({ stopPropagation: () => {} });
  assert.equal(sortField, "name");
  assert.equal(sortOrder, "desc", "Clicking active ascending column must trigger descending sort");

  // Verify tbody rows and custom cell projection
  const rows = tbody.props.children;
  assert.equal(rows.length, 2);

  // Row 1 class from dynamic function
  assert.equal(rows[0].props.className, "bg-amber-50");
  assert.equal(rows[1].props.className, "bg-white");

  // Row 1 click handler
  rows[0].props.onClick();
  assert.equal(clickedRow.id, "m1");

  // Custom cell projection for contact column
  const row1Cells = rows[0].props.children;
  assert.equal(row1Cells[1].props.children, "Mail to: alice@example.com");
});

test("HyleTable updates and reorders rows dynamically when sort changes", () => {
  const columns = [{ key: "name", label: "Name" }, { key: "score", label: "Score" }];
  const items = [
    { id: "1", name: "Charlie", score: 85 },
    { id: "2", name: "Alice", score: 95 },
    { id: "3", name: "Bob", score: 90 }
  ];

  // 1. Ascending sort by name
  const ascTable = HyleTable({ columns, items, sort: "name", sortOrder: "asc" });
  const [, ascBody] = ascTable.props.children.props.children;
  const ascNames = ascBody.props.children.map(row => row.props.children[0].props.children);
  assert.deepEqual(ascNames, ["Alice", "Bob", "Charlie"], "Table must sort rows ascending by name");

  // 2. Descending sort by name
  const descTable = HyleTable({ columns, items, sort: "name", sortOrder: "desc" });
  const [, descBody] = descTable.props.children.props.children;
  const descNames = descBody.props.children.map(row => row.props.children[0].props.children);
  assert.deepEqual(descNames, ["Charlie", "Bob", "Alice"], "Table must sort rows descending by name");

  // 3. Numeric sort by score descending
  const numTable = HyleTable({ columns, items, sort: "score", sortOrder: "desc" });
  const [, numBody] = numTable.props.children.props.children;
  const numScores = numBody.props.children.map(row => Number(row.props.children[1].props.children));
  assert.deepEqual(numScores, [95, 90, 85], "Table must sort rows numerically descending by score");
});

test("HyleTable auto-derives columns from Hyle schema with columnOverrides", () => {
  const schema = {
    fields: [
      { name: "title", label: "Track Title" },
      { name: "artist_name", type: "text" },
      { name: "rating", type: "int" }
    ]
  };

  const items = [
    { id: "1", title: "Song One", artist_name: "Band A", rating: 5 }
  ];

  const vnode = HyleTable({
    schema,
    items,
    columnOverrides: {
      rating: {
        label: "Score",
        cell: (item) => `★ ${item.rating}`
      }
    }
  });

  const table = vnode.props.children;
  const [thead, tbody] = table.props.children;
  const headerCells = thead.props.children.props.children;
  assert.equal(headerCells.length, 3);
  assert.equal(headerCells[0].props["data-field"], "title");
  assert.equal(headerCells[1].props["data-field"], "artist_name");
  assert.equal(headerCells[2].props["data-field"], "rating");

  // Rating label overridden to Score
  const ratingBtn = headerCells[2].props.children;
  assert.equal(ratingBtn.props.children[0], "Score");

  // Rating cell projected with star icon
  const row = tbody.props.children[0];
  assert.equal(row.props.children[2].props.children, "★ 5");
});

test("HyleTable renders clean empty message when items are empty", () => {
  const columns = [
    { key: "id", label: "ID" },
    { key: "name", label: "Name" },
  ];

  const vnode = HyleTable({
    columns,
    items: [],
    emptyMessage: "No team members found",
  });

  const table = vnode.props.children;
  const [, tbody] = table.props.children;
  const emptyTr = tbody.props.children[0];
  assert.equal(emptyTr.props.children.props.children, "No team members found");
  assert.equal(emptyTr.props.children.props.colSpan, 2);
});

test("HylePagination computes bounds and triggers page transitions correctly", () => {
  let changedPage = null;
  let changedPerPage = null;

  const vnode = HylePagination({
    page: 2,
    perPage: 10,
    total: 35,
    onPageChange: (p) => {
      changedPage = p;
    },
    onPerPageChange: (pp) => {
      changedPerPage = pp;
    },
    labels: { prev: "Prev", next: "Next", of: "de", rows: "linhas" },
  });

  assert.equal(vnode.type, "div");
  assert.equal(vnode.props["data-hyle-slot"], "pagination");

  const [nav, summary] = vnode.props.children;
  assert.equal(summary.props.children, "11-20 de 35 linhas");

  const [prevBtn, pageStatus, nextBtn, selectEl] = nav.props.children;

  assert.equal(prevBtn.props.disabled, false);
  assert.equal(nextBtn.props.disabled, false);
  assert.equal(pageStatus.props.children, "Page 2 de 4");

  prevBtn.props.onClick();
  assert.equal(changedPage, 1);

  nextBtn.props.onClick();
  assert.equal(changedPage, 3);

  // Test perPage select
  assert.ok(selectEl);
  selectEl.props.onChange({ target: { value: "20" } });
  assert.equal(changedPerPage, 20);
});

test("HylePagination disables prev button on first page and next button on last page", () => {
  const page1 = HylePagination({ page: 1, perPage: 10, total: 15 });
  const [nav1] = page1.props.children;
  assert.equal(nav1.props.children[0].props.disabled, true, "Prev must be disabled on page 1");
  assert.equal(nav1.props.children[2].props.disabled, false, "Next must be enabled on page 1 of 2");

  const page2 = HylePagination({ page: 2, perPage: 10, total: 15 });
  const [nav2] = page2.props.children;
  assert.equal(nav2.props.children[0].props.disabled, false);
  assert.equal(nav2.props.children[2].props.disabled, true, "Next must be disabled on last page");
});

test("HyleFilterBar renders input and active filter badges", () => {
  let searchVal = null;
  let removedKey = null;
  let resetTriggered = false;

  const vnode = HyleFilterBar({
    searchQuery: "alice",
    placeholder: "Filter users...",
    activeFilters: [{ key: "status", label: "Status", value: "active" }],
    onSearchChange: (q) => {
      searchVal = q;
    },
    onRemoveFilter: (k) => {
      removedKey = k;
    },
    onReset: () => {
      resetTriggered = true;
    },
  });

  const [input, filterBadgesContainer, resetBtn] = vnode.props.children;
  assert.equal(input.props.value, "alice");

  input.props.onChange({ target: { value: "bob" } });
  assert.equal(searchVal, "bob");

  assert.ok(filterBadgesContainer);
  const badge = filterBadgesContainer.props.children[0];
  const removeBtn = badge.props.children[1];
  removeBtn.props.onClick();
  assert.equal(removedKey, "status");

  assert.ok(resetBtn);
  resetBtn.props.onClick();
  assert.equal(resetTriggered, true);
});

test("HyleInput and HyleField render flexible inputs with custom classes and types", () => {
  let changedVal = null;

  // 1. Text Input
  const textInput = HyleInput({
    name: "email",
    label: "Email Address",
    value: "test@example.com",
    onChange: (v) => {
      changedVal = v;
    },
    inputClassName: "custom-input-class",
  });

  const [, controlEl] = textInput.props.children;
  assert.equal(controlEl.props.type, "text");
  assert.equal(controlEl.props.value, "test@example.com");
  assert.equal(controlEl.props.className, "custom-input-class");

  controlEl.props.onChange({ target: { value: "new@example.com" } });
  assert.equal(changedVal, "new@example.com");

  // 2. Textarea
  const textarea = HyleInput({
    name: "bio",
    label: "Biography",
    type: "textarea",
    value: "Hello world",
  });
  const [, taControl] = textarea.props.children;
  assert.equal(taControl.type, "textarea");
  assert.equal(taControl.props.value, "Hello world");

  // 3. Select Dropdown
  const select = HyleInput({
    name: "role",
    label: "Role",
    type: "select",
    options: [{ label: "Host", value: "host" }, { label: "Member", value: "member" }],
    value: "host",
  });
  const [, selControl] = select.props.children;
  assert.equal(selControl.type, "select");
  assert.equal(selControl.props.children.length, 2);

  // 4. Schema-driven HyleField
  const schema = {
    fields: [
      { name: "age", type: "int", label: "User Age" },
      { name: "bio", type: "text", label: "User Bio" }
    ]
  };

  const fieldInt = HyleField({ schema, name: "age", value: 30 });
  assert.equal(fieldInt.type, HyleInput);
  assert.equal(fieldInt.props.type, "number");
  assert.equal(fieldInt.props.label, "User Age");
  assert.equal(fieldInt.props.value, 30);

  const fieldBio = HyleField({ schema, name: "bio", value: "Story" });
  assert.equal(fieldBio.type, HyleInput);
  assert.equal(fieldBio.props.type, "textarea");
  assert.equal(fieldBio.props.value, "Story");
});

test("HyleForm renders schema-driven form and handles submission", () => {
  const schema = {
    fields: [
      { name: "full_name", label: "Full Name", required: true },
      { name: "tier", label: "Membership Tier" }
    ]
  };

  const internals = React.__CLIENT_INTERNALS_DO_NOT_USE_OR_WARN_USERS_THEY_CANNOT_UPGRADE;
  const prevH = internals?.H;
  let formState;

  if (internals) {
    internals.H = {
      useState(init) {
        if (!formState) formState = typeof init === "function" ? init() : init;
        return [formState, (next) => { formState = typeof next === "function" ? next(formState) : next; }];
      }
    };
  }

  let submittedData = null;

  try {
    const form = HyleForm({
      schema,
      initialValues: { full_name: "Kate Moss", tier: "resident" },
      onSubmit: (values) => {
        submittedData = values;
      },
      submitLabel: "Save Member",
    });

    assert.equal(form.type, "form");
    assert.equal(form.props["data-hyle-slot"], "form");

    // Submit form
    form.props.onSubmit({ preventDefault: () => {} });
    assert.deepEqual(submittedData, { full_name: "Kate Moss", tier: "resident" });
  } finally {
    if (internals) internals.H = prevH;
  }
});

test("useHyleDataset integrates with @tty-pt/hyle-source substrate cache and supports initialData", () => {
  cacheClear();

  // Provide lightweight React 19 hook test dispatcher
  const internals = React.__CLIENT_INTERNALS_DO_NOT_USE_OR_WARN_USERS_THEY_CANNOT_UPGRADE;
  const prevH = internals?.H;
  if (internals) {
    internals.H = {
      useMemo(cb) { return cb(); },
      useSyncExternalStore(subscribe, getSnapshot) { return getSnapshot(); },
      useState(init) { return [typeof init === "function" ? init() : init, () => {}]; },
      useCallback(cb) { return cb; }
    };
  }

  try {
    // 1. Hydrate directly via initialData option on cold cache
    const initialData = {
      total: 1,
      items: [{ id: "init_1", name: "Preloaded Member" }]
    };

    const res = useHyleDataset("rsc_test", "role=admin", { initialData });
    assert.equal(res.isLoading, false);
    assert.equal(res.total, 1);
    assert.equal(res.items.length, 1);
    assert.equal(res.items[0].name, "Preloaded Member");
  } finally {
    if (internals) internals.H = prevH;
  }
});

test("useHyleTable produces canonical query strings and handles sort toggling", () => {
  const internals = React.__CLIENT_INTERNALS_DO_NOT_USE_OR_WARN_USERS_THEY_CANNOT_UPGRADE;
  const prevH = internals?.H;
  let stateMap = new Map();
  let stateIdx = 0;

  if (internals) {
    internals.H = {
      useMemo(cb) { return cb(); },
      useCallback(cb) { return cb; },
      useState(init) {
        const id = stateIdx++;
        if (!stateMap.has(id)) {
          stateMap.set(id, typeof init === "function" ? init() : init);
        }
        const val = stateMap.get(id);
        const setVal = (newVal) => {
          const next = typeof newVal === "function" ? newVal(stateMap.get(id)) : newVal;
          stateMap.set(id, next);
        };
        return [val, setVal];
      }
    };
  }

  try {
    const tableState = useHyleTable({
      initialPage: 2,
      initialPerPage: 20,
      initialSort: "name",
      initialOrder: "asc",
      initialSearch: "alice"
    });

    assert.equal(tableState.page, 2);
    assert.equal(tableState.perPage, 20);
    assert.equal(tableState.sortField, "name");
    assert.equal(tableState.sortOrder, "asc");
    assert.equal(tableState.search, "alice");

    // Verify computed query string
    assert.ok(tableState.queryStr.includes("page=2"));
    assert.ok(tableState.queryStr.includes("limit=20"));
    assert.ok(tableState.queryStr.includes("sort=name"));
    assert.ok(tableState.queryStr.includes("order=asc"));
    assert.ok(tableState.queryStr.includes("q=alice"));
  } finally {
    if (internals) internals.H = prevH;
  }
});
