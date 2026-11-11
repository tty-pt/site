import React from "react";

export interface HyleColumn<T = any> {
  key: string;
  label: string;
  sortable?: boolean;
  className?: string;
  headerClassName?: string;
  cell?: (item: T, value: any, index: number) => React.ReactNode;
}

export interface HyleTableProps<T = any> {
  columns?: HyleColumn<T>[];
  schema?: any;
  columnOverrides?: Record<string, Partial<HyleColumn<T>>>;
  items: T[];
  keyField?: string;
  sort?: string;
  sortOrder?: "asc" | "desc";
  onSort?: (field: string, order: "asc" | "desc") => void;
  onRowClick?: (item: T, index: number) => void;
  rowProps?: ((item: T, index: number) => Record<string, any>) | Record<string, any>;
  tableProps?: Record<string, any>;
  className?: string;
  tableClassName?: string;
  headerClassName?: string;
  headerRowClassName?: string;
  bodyClassName?: string;
  rowClassName?: string | ((item: T, index: number) => string);
  emptyMessage?: React.ReactNode;
  sortIndicatorAsc?: string;
  sortIndicatorDesc?: string;
}

export declare function HyleTable<T = any>(props: HyleTableProps<T>): React.ReactElement;

export interface HylePaginationLabels {
  prev?: string;
  next?: string;
  of?: string;
  rows?: string;
  page?: string;
}

export interface HylePaginationProps {
  page: number;
  perPage: number;
  total: number;
  perPageOptions?: number[];
  onPageChange: (newPage: number) => void;
  onPerPageChange?: (newPerPage: number) => void;
  className?: string;
  paginationClassName?: string;
  buttonClassName?: string;
  activeButtonClassName?: string;
  selectClassName?: string;
  countClassName?: string;
  labels?: HylePaginationLabels;
}

export declare function HylePagination(props: HylePaginationProps): React.ReactElement;

export interface HyleActiveFilter {
  key: string;
  label?: string;
  value: string;
}

export interface HyleFilterBarProps {
  searchQuery?: string;
  onSearchChange?: (query: string) => void;
  placeholder?: string;
  activeFilters?: HyleActiveFilter[];
  onRemoveFilter?: (key: string, value: string) => void;
  onReset?: () => void;
  className?: string;
  inputClassName?: string;
  badgeClassName?: string;
  clearButtonClassName?: string;
}

export declare function HyleFilterBar(props: HyleFilterBarProps): React.ReactElement;

export interface HyleInputOption {
  label: string;
  value: any;
}

export interface HyleInputProps {
  name?: string;
  label?: React.ReactNode;
  type?: "text" | "number" | "email" | "password" | "search" | "textarea" | "checkbox" | "select" | string;
  value?: any;
  onChange?: (value: any, event?: any) => void;
  placeholder?: string;
  options?: (HyleInputOption | string | number)[];
  required?: boolean;
  error?: React.ReactNode;
  disabled?: boolean;
  readOnly?: boolean;
  className?: string;
  labelClassName?: string;
  inputClassName?: string;
  errorClassName?: string;
}

export declare function HyleInput(props: HyleInputProps): React.ReactElement;

export interface HyleFieldProps extends HyleInputProps {
  schema?: any;
}

export declare function HyleField(props: HyleFieldProps): React.ReactElement;

export interface HyleFormProps {
  schema: any;
  initialValues?: Record<string, any>;
  onSubmit?: (values: Record<string, any>) => void;
  onCancel?: () => void;
  submitLabel?: string;
  cancelLabel?: string;
  fieldOverrides?: Record<string, any>;
  formClassName?: string;
  fieldClassName?: string;
  inputClassName?: string;
  labelClassName?: string;
  actionsClassName?: string;
  submitButtonClassName?: string;
  cancelButtonClassName?: string;
}

export declare function HyleForm(props: HyleFormProps): React.ReactElement;

export interface HyleDatasetResult<T = any> {
  data: any;
  total: number;
  items: T[];
  loaded: boolean;
  isLoading: boolean;
}

export declare function useHyleDataset<T = any>(
  datasetIdOrOptions: string | { resource?: string; datasetId?: string; name?: string; query?: string; initialData?: any; [key: string]: any },
  queryStr?: string | { initialData?: any; [key: string]: any },
  options?: { initialData?: any; [key: string]: any }
): HyleDatasetResult<T>;

export interface UseHyleTableOptions {
  initialPage?: number;
  initialPerPage?: number;
  initialSort?: string;
  initialOrder?: "asc" | "desc";
  initialSearch?: string;
}

export interface UseHyleTableResult {
  page: number;
  perPage: number;
  sortField: string;
  sortOrder: "asc" | "desc";
  search: string;
  queryStr: string;
  setPage: (page: number) => void;
  setPerPage: (perPage: number) => void;
  setSearch: (query: string) => void;
  onSort: (field: string, order?: "asc" | "desc") => void;
  reset: () => void;
}

export declare function useHyleTable(options?: UseHyleTableOptions): UseHyleTableResult;

declare const _default: {
  HyleTable: typeof HyleTable;
  HylePagination: typeof HylePagination;
  HyleFilterBar: typeof HyleFilterBar;
  HyleInput: typeof HyleInput;
  HyleField: typeof HyleField;
  HyleForm: typeof HyleForm;
  useHyleDataset: typeof useHyleDataset;
  useHyleTable: typeof useHyleTable;
};

export default _default;
