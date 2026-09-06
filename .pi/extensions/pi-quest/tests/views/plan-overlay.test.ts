import { check } from "../check.ts";
import type { PiOverlayComponent, PiTheme } from "../../src/hooks/events.ts";
import {
  buildSelectListTheme,
  chooseViewer,
  fitRow,
  HINT,
  isKit,
  LIST_VISIBLE,
  loadVanilla,
  openPlanViewer,
  PlanOverlayComponent,
  toSelectItems,
  type RowMeasure,
  type SelectableList,
  type SelectItem,
  type SelectListTheme,
  type ViewerKit,
} from "../../src/views/plan-overlay.ts";
import {
  classifyKey,
  normalizeKey,
  type KeyMatcher,
} from "../../src/views/plan-keys.ts";
import { wrapLine, wrapMarkdown } from "../../src/views/plan-wrap.ts";

const measure: RowMeasure = {
  visibleWidth: (text: string) => Array.from(text.replace(/\x1b\[[0-9;]*m/g, "")).length,
  truncateToWidth: (text: string, width: number) => {
    const plain = text.replace(/\x1b\[[0-9;]*m/g, "");
    return Array.from(plain).slice(0, width).join("");
  },
};

const theme: PiTheme = { fg: (_color: string, text: string) => text };

function component(lines: string[], height = 3): { view: PlanOverlayComponent; closed: { value: boolean } } {
  const closed = { value: false };
  const view = new PlanOverlayComponent("Quest abc123 — plan", lines, theme, () => {
    closed.value = true;
  }, height);
  return { view, closed };
}

const LINES = ["step one", "step two", "step three", "step four", "step five"];

Deno.test("overlay renders title, first slice, and position footer", () => {
  const { view } = component(LINES);
  const out = view.render(40).join("\n");
  check(out.includes("Quest abc123"), "title shown");
  check(out.includes("step one") && out.includes("step three"), "first slice shown");
  check(!out.includes("step four"), "overflow hidden");
  check(out.includes("1-3/5"), "position shown");
  check(view.render(80).join("\n").includes("Esc to close"), "dismiss hint shown at full width");
});

Deno.test("j and arrow-down scroll the slice", () => {
  const { view } = component(LINES);
  view.handleInput("j");
  const out = view.render(40).join("\n");
  check(out.includes("step two") && out.includes("step four"), "scrolled one row");
  check(!out.includes("step one"), "top row left the viewport");
  check(out.includes("2-4/5"), "position updated");
});

Deno.test("scrolling clamps at both ends", () => {
  const { view } = component(LINES);
  view.handleInput("k");
  check(view.render(40).join("\n").includes("1-3/5"), "stays at top");
  for (let i = 0; i < 10; i++) view.handleInput("j");
  const out = view.render(40).join("\n");
  check(out.includes("3-5/5"), "clamped at bottom");
  check(out.includes("step five"), "last line visible");
});

Deno.test("fallback ctrl+U/ctrl+D move half a viewport", () => {
  const tall = Array.from({ length: 20 }, (_, i) => `l${i}`);
  const { view } = component(tall, 10);
  view.handleInput("\x04");
  const down = view.render(40).join("\n");
  check(down.includes("l5") && down.includes("l14"), "half viewport down");
  check(!down.includes("l15"), "clamped slice");
  view.handleInput("\x15");
  check(view.render(40).join("\n").includes("l0"), "half viewport back up");
});

Deno.test("escape, q, and enter close the viewer", () => {
  for (const key of ["\u001b", "q", "Q", "\r"]) {
    const { view, closed } = component(LINES);
    view.handleInput(key);
    check(closed.value, `"${JSON.stringify(key)}" closes`);
  }
});

Deno.test("normalizeKey speaks legacy ANSI, Kitty CSI-u, and printables", () => {
  check(normalizeKey("j") === "j", "printable passes through");
  check(normalizeKey(" ") === " ", "space passes through");
  check(normalizeKey("") === null, "empty ignored");
  check(normalizeKey("\x1b[A") === "up", "legacy arrows");
  check(normalizeKey("\x1bOB") === "down", "application-cursor arrows");
  check(normalizeKey("\x1b[5~") === "pageUp", "legacy pgup");
  check(normalizeKey("\x1b[6~") === "pageDown", "legacy pgdn");
  check(normalizeKey("\x1b[H") === "home", "legacy home");
  check(normalizeKey("\x1b[8~") === "end", "legacy end");
  check(normalizeKey("\x1b[106u") === "j", "kitty printable");
  check(normalizeKey("\x1b[13u") === "return", "kitty enter");
  check(normalizeKey("\x1b[27u") === "escape", "kitty escape");
  check(normalizeKey("\x1b[57419u") === "up", "kitty arrows");
  check(normalizeKey("\x1b[57420;5u") === "down", "kitty arrows ignore modifiers");
  check(normalizeKey("\x1b[57421u") === "pageUp", "kitty pgup");
  check(normalizeKey("\x1b[57424u") === "end", "kitty end");
  check(normalizeKey("\x1b[1;5A") === "up", "kitty modified arrows");
  check(normalizeKey("\x1b[106:3u") === null, "kitty releases ignored");
  check(normalizeKey("\x1b[200~paste~") === null, "bracketed paste ignored");
  check(normalizeKey("\x1b[999u") === null, "unknown codepoints ignored");
  check(normalizeKey("\x15") === "ctrlU", "raw ctrl+U");
  check(normalizeKey("\x04") === "ctrlD", "raw ctrl+D");
  check(normalizeKey("\x1b[85;5u") === "ctrlU", "kitty ctrl+U");
  check(normalizeKey("\x1b[100;5u") === "ctrlD", "kitty ctrl+d");
  check(normalizeKey("\x1b[68;5u") === "ctrlD", "kitty ctrl+D");
  check(normalizeKey("\x1b[106u") === "j", "unmodified letters unaffected");
});

Deno.test("classifyKey prefers the vanilla matcher with raw fallbacks", () => {
  const match: KeyMatcher = (data, key) => data === `named:${key}`;
  check(classifyKey("named:escape", match) === "close", "matcher close wins");
  check(classifyKey("named:up", match) === "up", "matcher up wins");
  check(classifyKey("named:pageDown", match) === "pgdn", "matcher pgdn wins");
  check(classifyKey("named:home", match) === "top", "matcher home wins");
  check(classifyKey("named:end", match) === "bottom", "matcher end wins");
  check(classifyKey("j", match) === "down", "literals still work");
  check(classifyKey("x", match) === null, "unknown stays unknown");
  check(classifyKey("up", null) === null, "normalized names need the matcher");
  check(classifyKey("x1b", null) === null, "junk ignored");
});

Deno.test("classifyKey resolves kitty sequences without any matcher", () => {
  check(classifyKey("\x1b[106u", null) === "down", "kitty j scrolls");
  check(classifyKey("\x1b[57419u", null) === "up", "kitty arrows scroll");
  check(classifyKey("\x1b[57422u", null) === "pgdn", "kitty pgdn");
  check(classifyKey("\x1b[13u", null) === "close", "kitty enter closes");
  check(classifyKey("\x1b[1;5B", null) === "down", "modified arrows scroll");
});

Deno.test("draft lines map 1:1 to select items", () => {
  const items = toSelectItems("## Plan\n\nDo it.\n");
  check(items.length === 4, "blank lines kept for line fidelity");
  check(items[0]?.label === "## Plan" && items[0]?.value === "0", "headers kept with index values");
  check(items[1]?.label === " ", "blanks stay visible rows");
});

Deno.test("select-list theme styles every slot without dropping text", () => {
  const styled = buildSelectListTheme({ fg: (c: string, s: string) => `[${c}]${s}` });
  const slots = [
    styled.selectedPrefix(">"), styled.selectedText("t"), styled.description("d"),
    styled.scrollInfo("(1/2)"), styled.noMatch("none"),
  ] as string[];
  check(slots.length === 5, "all slots styled");
  check(slots.every((s) => s.includes("]")), "no text dropped");
});

function stubKit(): {
  kit: ViewerKit;
  lists: SelectableList[];
  received: string[];
  mouse: unknown[];
  index: { value: number };
  jumps: number[];
} {
  const lists: SelectableList[] = [];
  const received: string[] = [];
  const mouse: unknown[] = [];
  const jumps: number[] = [];
  const index = { value: 0 };
  const kit: ViewerKit = {
    visibleWidth: (text: string) => measure.visibleWidth(text),
    truncateToWidth: (text: string, width: number) => measure.truncateToWidth(text, width),
    SelectList: function (this: unknown, got: SelectItem[], maxVisible: number, _theme: SelectListTheme) {
      check(maxVisible === LIST_VISIBLE, "viewport bounded");
      const list: SelectableList = {
        render: (_width: number) => [...got.map((it) => it.label), `${index.value + 1}/${got.length}`],
        handleInput: (data: string) => {
          received.push(data);
          if (data === "\x1b[A") index.value = (index.value + got.length - 1) % got.length;
          if (data === "\x1b[B") index.value = (index.value + 1) % got.length;
        },
        handleMouse: (event: unknown) => {
          mouse.push(event);
          return undefined;
        },
        getSelectedItem: () => ({ value: String(index.value), label: `line ${index.value}` }),
        setSelectedIndex: (i: number) => {
          jumps.push(i);
          index.value = Math.max(0, Math.min(got.length - 1, i));
        },
      };
      lists.push(list);
      return list;
    } as unknown as ViewerKit["SelectList"],
  };
  return { kit, lists, received, mouse, index, jumps };
}

function openStubbed(markdown: string): {
  view: PiOverlayComponent;
  closed: { value: boolean };
  received: string[];
  mouse: unknown[];
  index: { value: number };
  jumps: number[];
} {
  const { kit, received, mouse, index, jumps } = stubKit();
  const closed = { value: false };
  const view = chooseViewer(kit, "t", markdown, theme, () => {
    closed.value = true;
  });
  return { view, closed, received, mouse, index, jumps };
}

Deno.test("chooseViewer falls back without a kit", () => {
  let closed = false;
  const view = chooseViewer(null, "title", "a\nb", theme, () => {
    closed = true;
  });
  check(view.render(40).join("\n").includes("a"), "plan shown");
  view.handleInput?.("q");
  check(closed, "fallback still closes");
});

Deno.test("chooseViewer wraps long draft lines before SelectList", () => {
  const { kit, lists } = stubKit();
  const long = "## Plan\n\n" + "word ".repeat(40).trim();
  const view = chooseViewer(kit, "Quest x — plan", long, theme, () => {});
  check(lists.length === 1, "one list built");
  const out = view.render(60).join("\n");
  check(out.includes("Quest x"), "title shown");
  check(out.includes("## Plan"), "header kept");
  const joined = out.split("\n").map((l) => l.replace(/^\s*│/, "").replace(/│$/, "")).join(" ");
  check(joined.length > long.length, "wrapped content preserved, not clipped");
});

Deno.test("vanilla keys reach the list, q closes past it", () => {
  const { view, closed, received } = openStubbed("a\nb");
  view.handleInput?.("\x1b[A");
  view.handleInput?.("\x1b[57420u");
  check(received.length === 2, "native arrows and kitty keys forwarded untouched");
  check(!closed.value, "navigation never closes");
  view.handleInput?.("q");
  check(closed.value, "q closes");
  check(received.length === 2, "q never reaches the list");
});

Deno.test("vanilla j/k page/home/end move through the index API", () => {
  const big = Array.from({ length: 40 }, (_, i) => `line ${i}`).join("\n");
  const { view, closed, index, jumps } = openStubbed(big);
  view.handleInput?.("j");
  check(index.value === 1, "j steps down");
  view.handleInput?.("k");
  check(index.value === 0, "k steps up");
  view.handleInput?.("\x1b[106u");
  check(index.value === 1, "kitty j steps down");
  view.handleInput?.(" ");
  check(index.value === 1 + LIST_VISIBLE, "space pages down");
  view.handleInput?.("\x1b[5~");
  check(index.value === 1, "native pgup pages up");
  view.handleInput?.("G");
  check(index.value === 39, "G jumps to end");
  view.handleInput?.(" ");
  check(index.value === 39, "paging clamps at the end");
  view.handleInput?.("g");
  check(index.value === 0, "g jumps to top");
  view.handleInput?.("\x04");
  check(index.value === Math.floor(LIST_VISIBLE / 2), "ctrl+D pages half down");
  view.handleInput?.("\x1b[85;5u");
  check(index.value === 0, "kitty ctrl+U pages half up and clamps");
  view.handleInput?.("G");
  check(index.value === 39, "G jumps to end");
  view.handleInput?.("\x15");
  check(index.value === 39 - Math.floor(LIST_VISIBLE / 2), "ctrl+U pages half up");
  view.handleInput?.("k");
  check(index.value === 39 - Math.floor(LIST_VISIBLE / 2) - 1, "k steps from there");
  check(jumps.every((i) => i >= 0 && i < 40), "every jump clamped in range");
  check(!closed.value, "navigation never closes");
});

Deno.test("vanilla selection stays open, cancellation closes", () => {
  const { kit, lists } = stubKit();
  let closed = false;
  chooseViewer(kit, "t", "a", theme, () => {
    closed = true;
  });
  const list = lists[0] as SelectableList;
  list.onSelect?.({ value: "0", label: "a" });
  check(!closed, "enter (selection) stays open by decision");
  list.onCancel?.();
  check(closed, "escape (cancellation) closes");
});

Deno.test("vanilla mouse reaches the list", () => {
  const { kit, mouse } = stubKit();
  const view = chooseViewer(kit, "t", "a", theme, () => {});
  view.handleMouse?.({ wheel: -1 });
  check(mouse.length === 1, "wheel forwarded");
});

Deno.test("fitRow pads short rows to exactly the frame width", () => {
  check(fitRow("abc", 6, measure) === "abc   ", "padded");
  check(fitRow("abcdef", 6, measure) === "abcdef", "exact passes through");
});

Deno.test("fitRow truncates long styled rows without breaking escapes", () => {
  const styled = `\x1b[33m${"x".repeat(100)}\x1b[0m`;
  const out = fitRow(styled, 10, measure);
  check(measure.visibleWidth(out) === 10, "exactly frame width");
  check(!/\x1b\[[0-9;]*$/.test(out), "no dangling escape");
});

Deno.test("fitRow fits wide-char titles exactly", () => {
  const out = fitRow("日本語タイトル ok", 10, measure);
  check(Array.from(out).length <= 14, "bounded");
});

Deno.test("every rendered vanilla row measures exactly the frame", () => {
  const { kit } = stubKit();
  const view = chooseViewer(kit, "日本語 Quest — plan", "## Plan\nbody line here", theme, () => {});
  for (const row of view.render(60)) {
    check(measure.visibleWidth(row) === 60, `row fits: ${JSON.stringify(row).slice(0, 60)}`);
  }
});

Deno.test("isKit accepts only complete kits", () => {
  const { kit } = stubKit();
  check(isKit(kit), "full kit accepted");
  check(!isKit({}), "empty rejected");
  check(!isKit({ SelectList: kit.SelectList }), "partial kit rejected");
  check(!isKit(null), "null rejected");
});

Deno.test("both viewers share the identical legend", () => {
  const { kit } = stubKit();
  const vanilla = chooseViewer(kit, "t", "a\nb", theme, () => {}).render(80).join("\n");
  const { view } = component(["a", "b"]);
  const fallback = view.render(80).join("\n");
  check(vanilla.includes(HINT) && fallback.includes(HINT), "same legend substring in both footers");
  check(HINT === "↑↓/PgUp/PgDn/Home/End to navigate · Esc to close", "legend verbatim");
});

Deno.test("missing pi-tui degrades to null instead of throwing", async () => {
  check(await loadVanilla("./does-not-exist-xyz.ts") === null, "absent module degrades");
});

Deno.test("openPlanViewer resolves a viewer either way", async () => {
  const view = await openPlanViewer("t", "body", theme, () => {});
  check(view.render(40).length > 0, "viewer rendered");
});

Deno.test("other keys never close and never throw", () => {
  const { view, closed } = component(LINES);
  for (const key of ["j", "k", " ", "x", "\u001b[A", "\u001b[B", "\u001b[5~", "\u001b[6~"]) {
    view.handleInput(key);
  }
  check(!closed.value, "still open");
  check(view.render(10).length > 0, "narrow render works");
});

Deno.test("wrapLine wraps long lines and keeps every word", () => {
  const long = Array.from({ length: 20 }, (_, i) => `word${i}`).join(" ");
  const rows = wrapLine(long, 20);
  check(rows.length > 1, "split into multiple rows");
  check(rows.join(" ").replace(/  +/g, " ").trim() === long, "no words dropped");
  check(rows.every((r) => r.length <= 20), "every row fits");
});

Deno.test("wrapLine breaks over-wide tokens at the cell boundary", () => {
  const rows = wrapLine("x".repeat(50), 10);
  check(rows.length === 5, "token broken into exact rows");
  check(rows.every((r) => r.length <= 10), "no row overflows");
});

Deno.test("wrapLine is ANSI- and wide-char-aware", () => {
  const ansi = `\x1b[33m${"a".repeat(30)}\x1b[0m`;
  const styled = wrapLine(ansi, 10);
  check(styled.length === 3, "styled line wraps");
  check(styled.every((r) => !/^\x1b\[[0-9;]*$/.test(r)), "no dangling escape prefix");
  const cjk = "日本語".repeat(20);
  const cjkRows = wrapLine(cjk, 8);
  check(cjkRows.length > 1, "wide chars wrap");
  check(cjkRows.every((r) => Array.from(r).length * 2 <= 8 + 4), "each row bounded to ~2 cells/char");
});

Deno.test("wrapMarkdown preserves blank lines and indents continuations", () => {
  const rows = wrapMarkdown("## Plan\n\n- " + "item ".repeat(10).trim() + "\n", 12);
  check(rows[0] === "## Plan", "first line kept");
  check(rows[1] === " ", "blank line preserved");
  check(rows[2].startsWith("- "), "list marker kept");
  check(rows[3]?.startsWith("  "), "continuation indented");
});

Deno.test("fallback overlay wraps long lines into visible rows", () => {
  const long = Array.from({ length: 60 }, (_, i) => `w${String(i).padStart(2, "0")}`).join(" ");
  const { view } = component(["short", long, "tail"]);
  const top = view.render(40).join("\n");
  check(top.includes("short") && top.includes("w00"), "top rows shown");
  check(!top.includes("tail"), "tail below the fold at top");
  check(top.includes("w59") === false, "long line tail not yet visible");
  for (let i = 0; i < 30; i++) view.handleInput("j");
  const bottom = view.render(40).join("\n");
  check(bottom.includes("tail"), "tail reachable after scrolling");
  check(!bottom.includes("w00"), "wrapped first chunk scrolled away");
});
