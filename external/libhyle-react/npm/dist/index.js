import React, { useMemo, useState, useCallback, useSyncExternalStore } from "react";
import { createHyleStore, cacheHydrate, cacheGet } from "@tty-pt/hyle-source";

const e = React.createElement;

function formatFieldTitle(name) {
  if (!name) return "";
  return name
    .replace(/[_-]+/g, " ")
    .replace(/\b\w/g, (c) => c.toUpperCase());
}

/*─────────────────────────────────────────────────────────────────────────────*
 *                              <HyleTable>                                    *
 *─────────────────────────────────────────────────────────────────────────────*/

/**
 * Unopinionated, pluggable semantic table component mirroring hyle-bud.
 * Supports explicit columns OR schema-driven auto columns with columnOverrides.
 * Host applications (like Duna) command styling, class names, and custom cell projections.
 */
export function HyleTable({
  columns = [],
  schema,
  columnOverrides = {},
  items = [],
  keyField = "id",
  sort = "",
  sortOrder = "asc",
  onSort,
  onRowClick,
  rowProps,
  tableProps,
  className = "hyle-table-wrap",
  tableClassName = "hyle-table",
  headerClassName = "hyle-table-head",
  headerRowClassName = "",
  bodyClassName = "hyle-table-body",
  rowClassName = "",
  emptyMessage = "No records found",
  sortIndicatorAsc = " ▲",
  sortIndicatorDesc = " ▼",
}) {
  // Resolve columns from schema if columns is empty
  let resolvedColumns = columns;
  if ((!resolvedColumns || resolvedColumns.length === 0) && schema?.fields) {
    resolvedColumns = schema.fields.map((f) => {
      const fKey = f.name || f.key;
      const override = columnOverrides[fKey] || {};
      return {
        key: fKey,
        label: override.label || f.label || formatFieldTitle(fKey),
        sortable: override.sortable !== undefined ? override.sortable : (f.sortable !== false),
        className: override.className,
        headerClassName: override.headerClassName,
        cell: override.cell,
      };
    });
  } else if (resolvedColumns && columnOverrides && Object.keys(columnOverrides).length > 0) {
    resolvedColumns = resolvedColumns.map((col) => {
      const override = columnOverrides[col.key];
      return override ? { ...col, ...override } : col;
    });
  }

  // Normalize active sort field and direction
  let activeSortField = sort;
  let activeSortOrder = sortOrder;
  if (sort && sort.includes(":")) {
    const parts = sort.split(":");
    activeSortField = parts[0];
    activeSortOrder = parts[1] === "desc" ? "desc" : "asc";
  }

  // Sort items locally if activeSortField is present
  let sortedItems = items;
  if (items && items.length > 1 && activeSortField) {
    sortedItems = [...items].sort((a, b) => {
      let va = a ? a[activeSortField] : undefined;
      let vb = b ? b[activeSortField] : undefined;
      if (va === vb) return 0;
      if (va === null || va === undefined) return 1;
      if (vb === null || vb === undefined) return -1;
      if (typeof va === "number" && typeof vb === "number") {
        return activeSortOrder === "desc" ? vb - va : va - vb;
      }
      const sa = String(va).toLowerCase();
      const sb = String(vb).toLowerCase();
      const cmp = sa.localeCompare(sb, undefined, { numeric: true, sensitivity: "base" });
      return activeSortOrder === "desc" ? -cmp : cmp;
    });
  }

  // Header cells
  const headerCells = resolvedColumns.map((col, idx) => {
    const isSortable = col.sortable !== false;
    const isActive = activeSortField && activeSortField === col.key;
    const nextOrder = isActive ? (activeSortOrder === "asc" ? "desc" : "asc") : "asc";

    let content = col.label;
    if (isSortable) {
      const indicator = isActive
        ? activeSortOrder === "asc"
          ? sortIndicatorAsc
          : sortIndicatorDesc
        : null;

      content = e(
        "button",
        {
          type: "button",
          className: "hyle-sort-btn",
          style: {
            background: "none",
            border: "none",
            padding: 0,
            font: "inherit",
            color: "inherit",
            cursor: "pointer",
            display: "inline-flex",
            alignItems: "center",
          },
          onClick: (evt) => {
            evt.stopPropagation();
            if (onSort) onSort(col.key, nextOrder);
          },
          "data-action": "sort",
          "data-field": col.key,
          "data-order": nextOrder,
        },
        col.label,
        indicator ? e("span", { className: "hyle-sort-indicator", "aria-hidden": "true" }, indicator) : null
      );
    }

    return e(
      "th",
      {
        key: col.key || idx,
        className: col.headerClassName || undefined,
        scope: "col",
        "data-field": col.key,
        "data-sortable": isSortable ? "true" : undefined,
        "aria-sort": isActive ? (activeSortOrder === "asc" ? "ascending" : "descending") : undefined,
      },
      content
    );
  });

  const thead = e(
    "thead",
    { className: headerClassName || undefined },
    e("tr", { className: headerRowClassName || undefined }, headerCells)
  );

  // Body rows
  let bodyContent;
  if (!sortedItems || sortedItems.length === 0) {
    bodyContent = [
      e(
        "tr",
        { key: "__empty", className: "hyle-empty-row" },
        e(
          "td",
          {
            colSpan: Math.max(1, resolvedColumns.length),
            className: "hyle-empty-cell",
            style: { textAlign: "center", padding: "1.5rem" },
          },
          emptyMessage
        )
      ),
    ];
  } else {
    bodyContent = sortedItems.map((item, rowIdx) => {
      const rowKey = (item && item[keyField]) ? item[keyField] : rowIdx;
      const resolvedRowClass =
        typeof rowClassName === "function" ? rowClassName(item, rowIdx) : rowClassName;

      const cells = resolvedColumns.map((col, colIdx) => {
        const rawValue = item ? item[col.key] : undefined;
        let cellContent;
        if (col.cell) {
          cellContent = col.cell(item, rawValue, rowIdx);
        } else {
          cellContent = rawValue !== null && rawValue !== undefined ? String(rawValue) : "";
        }

        return e(
          "td",
          {
            key: col.key || colIdx,
            className: col.className || undefined,
            "data-label": col.label,
          },
          cellContent
        );
      });

      const extraProps = typeof rowProps === "function" ? (rowProps(item, rowIdx) || {}) : (rowProps || {});
      return e(
        "tr",
        {
          key: rowKey,
          className: resolvedRowClass || undefined,
          onClick: onRowClick ? () => onRowClick(item, rowIdx) : undefined,
          style: onRowClick ? { cursor: "pointer" } : undefined,
          ...extraProps,
        },
        cells
      );
    });
  }

  const tbody = e("tbody", { className: bodyClassName || undefined }, bodyContent);

  return e(
    "div",
    { className: className || undefined, "data-hyle-slot": "table" },
    e("table", { className: tableClassName || undefined, ...(tableProps || {}) }, thead, tbody)
  );
}

/*─────────────────────────────────────────────────────────────────────────────*
 *                            <HylePagination>                                 *
 *─────────────────────────────────────────────────────────────────────────────*/

/**
 * Pagination stepper matching hyle_bud_table_footer contracts.
 */
export function HylePagination({
  page = 1,
  perPage = 10,
  total = 0,
  perPageOptions = [5, 10, 20, 50, 100],
  onPageChange,
  onPerPageChange,
  className = "hyle-table-footer",
  paginationClassName = "hyle-pagination",
  buttonClassName = "hyle-page-btn",
  activeButtonClassName = "active",
  selectClassName = "hyle-per-page-select",
  countClassName = "hyle-row-count",
  labels = {},
}) {
  const totalPages = Math.max(1, Math.ceil(total / perPage));
  const prevLabel = labels.prev || "← Prev";
  const nextLabel = labels.next || "Next →";
  const ofLabel = labels.of || "of";
  const rowsLabel = labels.rows || "rows";
  const pageLabel = labels.page || "Page";

  const startRow = total === 0 ? 0 : (page - 1) * perPage + 1;
  const endRow = Math.min(total, page * perPage);

  const prevBtn = e(
    "button",
    {
      type: "button",
      className: buttonClassName,
      disabled: page <= 1,
      onClick: () => {
        if (page > 1 && onPageChange) onPageChange(page - 1);
      },
      "data-action": "page",
      "data-page": Math.max(1, page - 1),
    },
    prevLabel
  );

  const nextBtn = e(
    "button",
    {
      type: "button",
      className: buttonClassName,
      disabled: page >= totalPages,
      onClick: () => {
        if (page < totalPages && onPageChange) onPageChange(page + 1);
      },
      "data-action": "page",
      "data-page": Math.min(totalPages, page + 1),
    },
    nextLabel
  );

  const pageStatus = e(
    "span",
    { className: "hyle-page-status", style: { margin: "0 0.5rem" } },
    `${pageLabel} ${page} ${ofLabel} ${totalPages}`
  );

  let selectEl = null;
  if (perPageOptions && perPageOptions.length > 0 && onPerPageChange) {
    selectEl = e(
      "select",
      {
        className: selectClassName,
        value: perPage,
        onChange: (evt) => {
          const val = parseInt(evt.target.value, 10);
          if (val > 0 && onPerPageChange) onPerPageChange(val);
        },
        "aria-label": "Rows per page",
      },
      perPageOptions.map((opt) =>
        e("option", { key: opt, value: opt }, `${opt} / page`)
      )
    );
  }

  const paginationNav = e(
    "div",
    { className: paginationClassName },
    prevBtn,
    pageStatus,
    nextBtn,
    selectEl
  );

  const countSummary = e(
    "span",
    { className: countClassName },
    `${startRow}-${endRow} ${ofLabel} ${total} ${rowsLabel}`
  );

  return e(
    "div",
    { className: className, "data-hyle-slot": "pagination" },
    paginationNav,
    countSummary
  );
}

/*─────────────────────────────────────────────────────────────────────────────*
 *                            <HyleFilterBar>                                  *
 *─────────────────────────────────────────────────────────────────────────────*/

/**
 * Filter and search input control for query filtering.
 */
export function HyleFilterBar({
  searchQuery = "",
  onSearchChange,
  placeholder = "Search...",
  activeFilters = [],
  onRemoveFilter,
  onReset,
  className = "hyle-filter-bar",
  inputClassName = "hyle-filter-input",
  badgeClassName = "hyle-filter-badge",
  clearButtonClassName = "hyle-filter-clear",
}) {
  const searchInput = e("input", {
    type: "search",
    className: inputClassName,
    placeholder,
    value: searchQuery,
    onChange: (evt) => {
      if (onSearchChange) onSearchChange(evt.target.value);
    },
    "aria-label": placeholder,
  });

  const filterBadges = (activeFilters || []).map((f) =>
    e(
      "span",
      {
        key: `${f.key}:${f.value}`,
        className: badgeClassName,
        style: { display: "inline-flex", alignItems: "center", gap: "0.25rem" },
      },
      `${f.label || f.key}: ${f.value}`,
      onRemoveFilter
        ? e(
            "button",
            {
              type: "button",
              onClick: () => onRemoveFilter(f.key, f.value),
              style: { background: "none", border: "none", cursor: "pointer", padding: 0 },
              "aria-label": `Remove ${f.label || f.key} filter`,
            },
            "×"
          )
        : null
    )
  );

  const resetButton =
    (searchQuery || (activeFilters && activeFilters.length > 0)) && onReset
      ? e(
          "button",
          {
            type: "button",
            className: clearButtonClassName,
            onClick: onReset,
          },
          "Reset"
        )
      : null;

  return e(
    "div",
    { className, "data-hyle-slot": "filter" },
    searchInput,
    filterBadges.length > 0 ? e("div", { className: "hyle-active-filters" }, filterBadges) : null,
    resetButton
  );
}

/*─────────────────────────────────────────────────────────────────────────────*
 *                    <HyleInput>, <HyleField>, <HyleForm>                     *
 *─────────────────────────────────────────────────────────────────────────────*/

/**
 * Flexible standalone input control for custom layouts (grids, modals, cards, tabs).
 */
export function HyleInput({
  name,
  label,
  type = "text",
  value = "",
  onChange,
  placeholder,
  options = [],
  required = false,
  error,
  disabled = false,
  readOnly = false,
  className = "hyle-input-group",
  labelClassName = "hyle-input-label",
  inputClassName = "hyle-input-control",
  errorClassName = "hyle-input-error",
}) {
  const inputId = name ? `hyle-input-${name}` : undefined;

  let controlEl = null;

  if (type === "textarea") {
    controlEl = e("textarea", {
      id: inputId,
      name,
      value: value ?? "",
      placeholder,
      required,
      disabled,
      readOnly,
      className: inputClassName,
      onChange: (evt) => onChange && onChange(evt.target.value, evt),
    });
  } else if (type === "select") {
    const optionElements = (options || []).map((opt) => {
      const optVal = typeof opt === "object" ? opt.value : opt;
      const optLabel = typeof opt === "object" ? opt.label : opt;
      return e("option", { key: String(optVal), value: optVal }, optLabel);
    });

    controlEl = e(
      "select",
      {
        id: inputId,
        name,
        value: value ?? "",
        required,
        disabled,
        className: inputClassName,
        onChange: (evt) => onChange && onChange(evt.target.value, evt),
      },
      optionElements
    );
  } else if (type === "checkbox") {
    const isChecked = Boolean(
      value === true || value === 1 || value === "1" || value === "true" || value === "on"
    );

    return e(
      "div",
      { className },
      e(
        "label",
        {
          htmlFor: inputId,
          className: labelClassName,
          style: { display: "inline-flex", alignItems: "center", gap: "0.5rem", cursor: "pointer" },
        },
        e("input", {
          id: inputId,
          type: "checkbox",
          name,
          checked: isChecked,
          required,
          disabled,
          className: inputClassName,
          onChange: (evt) => onChange && onChange(evt.target.checked, evt),
        }),
        label ? e("span", null, label) : null
      ),
      error ? e("span", { className: errorClassName }, error) : null
    );
  } else {
    controlEl = e("input", {
      id: inputId,
      type,
      name,
      value: value ?? "",
      placeholder,
      required,
      disabled,
      readOnly,
      className: inputClassName,
      onChange: (evt) => onChange && onChange(evt.target.value, evt),
    });
  }

  return e(
    "div",
    { className },
    label
      ? e(
          "label",
          { htmlFor: inputId, className: labelClassName },
          label,
          required ? e("span", { "aria-hidden": "true", style: { color: "red", marginLeft: "2px" } }, "*") : null
        )
      : null,
    controlEl,
    error ? e("span", { className: errorClassName }, error) : null
  );
}

/**
 * Schema-driven individual form field that derives field constraints
 * from a Hyle schema descriptor, while allowing custom overrides.
 */
export function HyleField({
  schema,
  name,
  label,
  type,
  required,
  options,
  ...rest
}) {
  const fieldDesc = schema?.fields?.find((f) => f.name === name || f.key === name);

  let derivedType = type;
  if (!derivedType && fieldDesc) {
    if (fieldDesc.type === "text" || fieldDesc.type === 2) {
      derivedType = "textarea";
    } else if (fieldDesc.type === "int" || fieldDesc.type === "integer" || fieldDesc.type === 1 || fieldDesc.type === "number") {
      derivedType = "number";
    } else if (fieldDesc.type === "bool" || fieldDesc.type === "boolean" || fieldDesc.type === 3) {
      derivedType = "checkbox";
    } else if (fieldDesc.options || fieldDesc.type === "enum") {
      derivedType = "select";
    } else {
      derivedType = "text";
    }
  }

  const derivedLabel =
    label !== undefined
      ? label
      : fieldDesc?.label || formatFieldTitle(name);

  const derivedRequired =
    required !== undefined
      ? required
      : Boolean(fieldDesc?.required);

  const derivedOptions = options || fieldDesc?.options || [];

  return e(HyleInput, {
    name,
    label: derivedLabel,
    type: derivedType || "text",
    required: derivedRequired,
    options: derivedOptions,
    ...rest,
  });
}

/**
 * Schema-driven automated form component mirroring hyle_bud_form.
 */
export function HyleForm({
  schema,
  initialValues = {},
  onSubmit,
  onCancel,
  submitLabel = "Submit",
  cancelLabel = "Cancel",
  fieldOverrides = {},
  formClassName = "hyle-form space-y-4",
  fieldClassName = "hyle-form-field",
  inputClassName = "hyle-input-control",
  labelClassName = "hyle-input-label",
  actionsClassName = "hyle-form-actions flex gap-2 pt-2",
  submitButtonClassName = "hyle-btn-submit",
  cancelButtonClassName = "hyle-btn-cancel",
}) {
  const [values, setValues] = useState(() => ({ ...initialValues }));

  const handleChange = (name, val) => {
    setValues((prev) => ({ ...prev, [name]: val }));
  };

  const handleSubmit = (evt) => {
    if (evt && evt.preventDefault) evt.preventDefault();
    if (onSubmit) onSubmit(values);
  };

  const fields = (schema?.fields || []).map((f) => {
    const fName = f.name || f.key;
    const override = fieldOverrides[fName] || {};

    if (typeof override === "function") {
      return override(values[fName], (newVal) => handleChange(fName, newVal), values);
    }

    return e(HyleField, {
      key: fName,
      schema,
      name: fName,
      value: values[fName],
      onChange: (val) => handleChange(fName, val),
      className: override.className || fieldClassName,
      inputClassName: override.inputClassName || inputClassName,
      labelClassName: override.labelClassName || labelClassName,
      ...override,
    });
  });

  const actions = e(
    "div",
    { className: actionsClassName },
    e(
      "button",
      {
        type: "submit",
        className: submitButtonClassName,
      },
      submitLabel
    ),
    onCancel
      ? e(
          "button",
          {
            type: "button",
            className: cancelButtonClassName,
            onClick: onCancel,
          },
          cancelLabel
        )
      : null
  );

  return e(
    "form",
    {
      className: formClassName,
      onSubmit: handleSubmit,
      "data-hyle-slot": "form",
    },
    ...fields,
    actions
  );
}

/*─────────────────────────────────────────────────────────────────────────────*
 *                          REACT 19 NATIVE HOOKS                              *
 *─────────────────────────────────────────────────────────────────────────────*/

/**
 * React 19 hook subscribing to @tty-pt/hyle-source substrate cache via useSyncExternalStore.
 */
export function useHyleDataset(arg1, arg2 = "", arg3 = {}) {
  let datasetId = arg1;
  let queryStr = typeof arg2 === "string" ? arg2 : "";
  let options = typeof arg2 === "object" && arg2 !== null ? arg2 : (arg3 || {});

  if (typeof arg1 === "object" && arg1 !== null) {
    datasetId = arg1.resource || arg1.datasetId || arg1.name;
    queryStr = arg1.query || arg1.queryStr || "";
    options = arg1;
  }

  const store = useMemo(() => createHyleStore(datasetId, options), [datasetId]);

  // Synchronously hydrate initial data from SSR / RSC if provided and cache is unprimed
  if (options.initialData) {
    const cached = cacheGet(datasetId, queryStr, options);
    if (!cached) {
      cacheHydrate(datasetId, queryStr, options.initialData, options);
    }
  }

  const snapshot = useSyncExternalStore(
    store.subscribe,
    () => store.getSnapshot(queryStr),
    () => store.getSnapshot(queryStr)
  );

  const total = snapshot?.total ?? (Array.isArray(snapshot) ? snapshot.length : 0);
  const items = snapshot?.items ?? (Array.isArray(snapshot) ? snapshot : []);

  return {
    data: snapshot,
    total,
    items,
    loaded: snapshot !== null && snapshot !== undefined,
    isLoading: snapshot === null || snapshot === undefined,
  };
}

/**
 * Hook for managing table state (page, perPage, sort, search) and producing canonical query strings.
 */
export function useHyleTable({
  initialPage = 1,
  initialPerPage = 10,
  initialSort = "",
  initialOrder = "asc",
  initialSearch = "",
} = {}) {
  const [page, setPage] = useState(initialPage);
  const [perPage, setPerPage] = useState(initialPerPage);
  const [sortField, setSortField] = useState(initialSort);
  const [sortOrder, setSortOrder] = useState(initialOrder);
  const [search, setSearch] = useState(initialSearch);

  const handleSort = useCallback((field, order) => {
    if (order) {
      setSortField(field);
      setSortOrder(order);
    } else {
      if (sortField === field) {
        setSortOrder((prev) => (prev === "asc" ? "desc" : "asc"));
      } else {
        setSortField(field);
        setSortOrder("asc");
      }
    }
    setPage(1);
  }, [sortField]);

  const handleSearch = useCallback((newSearch) => {
    setSearch(newSearch);
    setPage(1);
  }, []);

  const queryStr = useMemo(() => {
    const params = new URLSearchParams();
    if (page > 1) params.set("page", String(page));
    if (perPage !== 10) params.set("limit", String(perPage));
    if (sortField) {
      params.set("sort", sortField);
      params.set("order", sortOrder);
    }
    if (search) params.set("q", search);
    return params.toString();
  }, [page, perPage, sortField, sortOrder, search]);

  return {
    page,
    perPage,
    sortField,
    sortOrder,
    search,
    queryStr,
    setPage,
    setPerPage,
    setSearch: handleSearch,
    onSort: handleSort,
    reset: () => {
      setPage(initialPage);
      setPerPage(initialPerPage);
      setSortField(initialSort);
      setSortOrder(initialOrder);
      setSearch(initialSearch);
    },
  };
}

export default {
  HyleTable,
  HylePagination,
  HyleFilterBar,
  HyleInput,
  HyleField,
  HyleForm,
  useHyleDataset,
  useHyleTable,
};
