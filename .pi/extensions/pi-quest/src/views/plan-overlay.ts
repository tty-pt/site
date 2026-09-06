// HIGH_LEVEL: #surface — on-demand floating plan viewer, zero inference.
// HIGH_LEVEL: #commands — /quest plan opens the viewer.
// SPEC: B1.3 (draft stays inspectable), B1.8 (amendments stay inspectable).
//
// Composition over reimplementation, per pi's TUI docs (Pattern 1:
// Selection Dialog): the viewer body is vanilla SelectList, which owns
// navigation, windowing, and its scroll indicator. This module only maps
// draft lines to items, draws title/hint chrome, and falls back to a plain
// box when pi-tui is absent. Long draft lines are word-wrapped to visual
// rows before either renderer sees them (SelectList truncates by design),
// so nothing is silently clipped; every emitted row is then padded to
// exactly the frame width so no row can overflow.
import type { PiOverlayComponent, PiTheme } from "../hooks/events";
import { classifyKey, normalizeKey } from "./plan-keys";
import { wrapLine, wrapMarkdown } from "./plan-wrap";

const MIN_WIDTH = 20;
const MAX_WIDTH = 80;
export const LIST_VISIBLE = 14;

// One shared key legend, used verbatim by both viewers. Deliberately
// minimal: vim bindings (jk, ^U/^D, g/G), Space, and q keep working but are
// unlisted, so their handling must stay even though the legend hides them.
export const HINT = "↑↓/PgUp/PgDn/Home/End to navigate · Esc to close";

export interface SelectItem {
  value: string;
  label: string;
  description?: string;
}

export interface SelectListTheme {
  selectedPrefix(text: string): string;
  selectedText(text: string): string;
  description(text: string): string;
  scrollInfo(text: string): string;
  noMatch(text: string): string;
}

export interface SelectableList extends PiOverlayComponent {
  onSelect?: (item: SelectItem) => void;
  onCancel?: () => void;
  getSelectedItem?: () => SelectItem | null;
  setSelectedIndex?: (index: number) => void;
}

export interface ViewerKit {
  SelectList: new (items: SelectItem[], maxVisible: number, theme: SelectListTheme) => SelectableList;
  visibleWidth(text: string): number;
  truncateToWidth(text: string, width: number): string;
}

const VANILLA_SPECIFIER = "@earendil-works/pi-tui";

export class PlanOverlayComponent implements PiOverlayComponent {
  private offset = 0;
  // Pre-wrapped once so scrolling and rendering share one width-stable row
  // model (the overlay is opened at a fixed width). Narrower renders pad or
  // truncate each pre-wrapped row via fitRow.
  private readonly rows: string[];

  constructor(
    private readonly title: string,
    private readonly lines: string[],
    private readonly theme: PiTheme,
    private readonly onClose: () => void,
    private readonly height = 16,
  ) {
    this.rows = this.wrapAll();
  }

  handleInput(data: string): void {
    switch (classifyKey(data, null)) {
      case "close":
        this.onClose();
        return;
      case "up":
        this.offset = Math.max(0, this.offset - 1);
        return;
      case "down":
        this.offset = Math.min(this.maxOffset(), this.offset + 1);
        return;
      case "pgup":
        this.offset = Math.max(0, this.offset - this.height);
        return;
      case "pgdn":
        this.offset = Math.min(this.maxOffset(), this.offset + this.height);
        return;
      case "halfup":
        this.offset = Math.max(0, this.offset - Math.ceil(this.height / 2));
        return;
      case "halfdown":
        this.offset = Math.min(this.maxOffset(), this.offset + Math.ceil(this.height / 2));
        return;
      case "top":
        this.offset = 0;
        return;
      case "bottom":
        this.offset = this.maxOffset();
        return;
      default:
        return;
    }
  }

  render(width: number): string[] {
    const box = Math.min(Math.max(width, MIN_WIDTH), MAX_WIDTH);
    const { inner, border, row } = frame(this.theme, box);
    const slice = this.rows.slice(this.offset, this.offset + this.height);
    const first = this.rows.length === 0 ? 0 : this.offset + 1;
    const last = this.offset + slice.length;
    const foot = `${first}-${last}/${this.rows.length} · ${HINT}`;
    return [
      border("╭", "─", "╮"),
      row(fitRow(` ${this.title}`, inner)),
      border("├", "─", "┤"),
      ...slice.map((line) => row(fitRow(` ${line}`, inner))),
      border("├", "─", "┤"),
      row(fitRow(` ${foot}`, inner)),
      border("╰", "─", "╯"),
    ];
  }

  // Canonical wrap width: the overlay's max box (80) leaves an inner of 78,
  // and each row carries a leading space, so content fits in 77 cells.
  private wrapAll(): string[] {
    const width = MAX_WIDTH - 3;
    const rows: string[] = [];
    for (const line of this.lines) {
      if (line.trim() === "") {
        rows.push(" ");
        continue;
      }
      const wrapped = wrapLine(line, width);
      rows.push(wrapped[0] ?? " ");
      for (const cont of wrapped.slice(1)) rows.push("  " + cont);
    }
    return rows;
  }

  private maxOffset(): number {
    return Math.max(0, this.rows.length - this.height);
  }
}

export function toSelectItems(markdown: string): SelectItem[] {
  return markdown.split("\n").map((line, index) => ({
    value: String(index),
    label: line === "" ? " " : line,
  }));
}

export function buildSelectListTheme(theme: PiTheme): SelectListTheme {
  return {
    selectedPrefix: (s) => theme.fg("accent", s),
    selectedText: (s) => theme.fg("accent", s),
    description: (s) => theme.fg("muted", s),
    scrollInfo: (s) => theme.fg("dim", s),
    noMatch: (s) => theme.fg("dim", s),
  };
}

export function isKit(value: unknown): value is ViewerKit {
  if (typeof value !== "object" || value === null) return false;
  const kit = value as Record<string, unknown>;
  return typeof kit["SelectList"] === "function" &&
    typeof kit["visibleWidth"] === "function" &&
    typeof kit["truncateToWidth"] === "function";
}

export async function loadVanilla(specifier = VANILLA_SPECIFIER): Promise<ViewerKit | null> {
  try {
    const mod = await import(specifier) as unknown;
    return isKit(mod) ? (mod as ViewerKit) : null;
  } catch {
    return null;
  }
}

function frame(theme: PiTheme, box: number): {
  inner: number;
  border(l: string, fill: string, r: string): string;
  row(text: string): string;
} {
  const inner = box - 2;
  return {
    inner,
    border: (l, fill, r) => theme.fg("border", l + fill.repeat(inner) + r),
    row: (text) => theme.fg("border", "│") + text + theme.fg("border", "│"),
  };
}

export interface RowMeasure {
  visibleWidth(text: string): number;
  truncateToWidth(text: string, width: number): string;
}

const plainMeasure: RowMeasure = {
  visibleWidth: (text) => Array.from(text).length,
  truncateToWidth: (text, width) => Array.from(text).slice(0, width).join(""),
};

// Every emitted row ends exactly `width` visible cells wide — padded when
// short, ANSI-aware truncated when long — so no row can ever overflow or
// underflow the frame, whatever produced it (styled list output, wide
// chars, tabs, theme wrappers).
export function fitRow(text: string, width: number, measure: RowMeasure = plainMeasure): string {
  let seen: number;
  try {
    seen = measure.visibleWidth(text);
  } catch {
    return text;
  }
  if (seen === width) return text;
  if (seen < width) return text + " ".repeat(width - seen);
  try {
    return measure.truncateToWidth(text, width);
  } catch {
    return text;
  }
}

export function chooseViewer(
  kit: ViewerKit | null,
  title: string,
  markdown: string,
  theme: PiTheme,
  onClose: () => void,
): PiOverlayComponent {
  if (kit === null) return new PlanOverlayComponent(title, markdown.split("\n"), theme, onClose);
  // SelectList adds `  ` + `→ `/`  ` prefixes (inner−4 cells of label space),
  // so wide-wrap draft lines at the canonical max instead of letting them clip.
  const items = toSelectItems(wrapMarkdown(markdown, MAX_WIDTH - 6).join("\n"));
  const list = new kit.SelectList(items, LIST_VISIBLE, buildSelectListTheme(theme));
  list.onSelect = () => {};
  list.onCancel = () => onClose();
  // SelectList.handleInput only knows up/down/confirm/cancel (pi-tui
  // registers pageUp/pageDown bindings but never checks them), so paging
  // and home/end go through the public index API instead of key synthesis.
  const half = Math.max(1, Math.floor(LIST_VISIBLE / 2));
  const jump = (target: number): void => {
    try {
      list.setSelectedIndex?.(Math.max(0, Math.min(items.length - 1, target)));
    } catch {
      // The viewer is best-effort; the draft file remains the source.
    }
  };
  const current = (): number => {
    try {
      const value = list.getSelectedItem?.()?.value;
      const index = typeof value === "string" ? parseInt(value, 10) : NaN;
      return Number.isNaN(index) ? 0 : index;
    } catch {
      return 0;
    }
  };
  const forward = (data: string): void => {
    try {
      list.handleInput?.(data);
    } catch {
      // The viewer is best-effort; the draft file remains the source.
    }
  };
  return {
    render: (width: number) => {
      const { inner, border, row } = frame(theme, Math.min(Math.max(width, MIN_WIDTH), MAX_WIDTH));
      return [
        border("╭", "─", "╮"),
        row(fitRow(` ${title}`, inner, kit)),
        border("├", "─", "┤"),
        ...list.render(inner).map((line) => row(fitRow(line, inner, kit))),
        border("├", "─", "┤"),
        row(fitRow(` ${HINT} `, inner, kit)),
        border("╰", "─", "╯"),
      ];
    },
    handleInput: (data: string) => {
      const token = normalizeKey(data);
      if (token === "q" || token === "Q") {
        onClose();
        return;
      }
      if (token === "j") return forward("\x1b[B");
      if (token === "k") return forward("\x1b[A");
      if (token === "pageDown" || token === " ") return jump(current() + LIST_VISIBLE);
      if (token === "pageUp") return jump(current() - LIST_VISIBLE);
      if (token === "ctrlU") return jump(current() - half);
      if (token === "ctrlD") return jump(current() + half);
      if (token === "home" || token === "g") return jump(0);
      if (token === "end" || token === "G") return jump(items.length - 1);
      forward(data);
    },
    handleMouse: (event: unknown) => {
      try {
        return list.handleMouse?.(event as never) as unknown;
      } catch {
        return undefined;
      }
    },
  };
}

export async function openPlanViewer(
  title: string,
  markdown: string,
  theme: PiTheme,
  onClose: () => void,
): Promise<PiOverlayComponent> {
  return chooseViewer(await loadVanilla(), title, markdown, theme, onClose);
}
