import test from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { writeFileSync, unlinkSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

function parseHyltBuffer(arrayBuffer) {
  const view = new DataView(arrayBuffer);
  const bytes = new Uint8Array(arrayBuffer);

  const magic = view.getUint32(0, true);
  assert.equal(magic, 0x544C5948, "Magic must be HYLT (0x544C5948)");

  const version = view.getUint16(4, true);
  assert.equal(version, 1, "Version must be 1");

  const flags = view.getUint16(6, true);
  const total = view.getUint32(8, true);
  const page = view.getUint16(12, true);
  const per_page = view.getUint16(14, true);
  const ncols = view.getUint16(16, true);
  const nids = view.getUint16(18, true);
  const nopts = view.getUint16(20, true);
  const arena_offset = view.getUint32(24, true);
  const arena_size = view.getUint32(28, true);

  const decoder = new TextDecoder("utf-8");
  function getArenaStr(offset) {
    if (offset >= arena_size) return "";
    let end = arena_offset + offset;
    while (end < bytes.length && bytes[end] !== 0) {
      end++;
    }
    return decoder.decode(bytes.subarray(arena_offset + offset, end));
  }

  // Meta offsets (40 bytes at offset 32)
  const meta_base = 32;
  const module_name = getArenaStr(view.getUint32(meta_base + 0, true));
  const display_name = getArenaStr(view.getUint32(meta_base + 4, true));
  const content_field = getArenaStr(view.getUint32(meta_base + 8, true));
  const content_label = getArenaStr(view.getUint32(meta_base + 12, true));
  const content_placeholder = getArenaStr(view.getUint32(meta_base + 16, true));
  const username = getArenaStr(view.getUint32(meta_base + 20, true));
  const query = getArenaStr(view.getUint32(meta_base + 24, true));
  const q = getArenaStr(view.getUint32(meta_base + 28, true));
  const sort_field = getArenaStr(view.getUint32(meta_base + 32, true));
  const lang = getArenaStr(view.getUint32(meta_base + 36, true));

  // Columns (ncols * 28 bytes)
  let cur = meta_base + 40;
  const cols = [];
  for (let i = 0; i < ncols; i++) {
    const key = getArenaStr(view.getUint32(cur + 0, true));
    const label = getArenaStr(view.getUint32(cur + 4, true));
    const target_source = getArenaStr(view.getUint32(cur + 8, true));
    const filter = getArenaStr(view.getUint32(cur + 12, true));
    const current = getArenaStr(view.getUint32(cur + 16, true));
    const type = view.getUint16(cur + 20, true);
    const opt_start = view.getUint16(cur + 22, true);
    const opt_count = view.getUint16(cur + 24, true);
    cols.push({ key, label, target_source, filter, current, type, opt_start, opt_count });
    cur += 28;
  }

  // Options (nopts * 8 bytes)
  const opts = [];
  for (let i = 0; i < nopts; i++) {
    const id = getArenaStr(view.getUint32(cur + 0, true));
    const label = getArenaStr(view.getUint32(cur + 4, true));
    opts.push({ id, label });
    cur += 8;
  }

  // Row IDs (nids * 4 bytes)
  const ids = [];
  for (let i = 0; i < nids; i++) {
    ids.push(getArenaStr(view.getUint32(cur, true)));
    cur += 4;
  }

  // Cells (nids * ncols * 4 bytes)
  const rows = [];
  for (let i = 0; i < nids; i++) {
    const row = [];
    for (let j = 0; j < ncols; j++) {
      row.push(getArenaStr(view.getUint32(cur, true)));
      cur += 4;
    }
    rows.push(row);
  }

  return {
    module: module_name,
    display_name,
    content_field,
    content_label,
    content_placeholder,
    username,
    query,
    q,
    sort_field,
    lang,
    sort_asc: (flags & 0x0001) !== 0,
    custom: (flags & 0x0002) !== 0,
    has_page: (flags & 0x0004) !== 0,
    total,
    page,
    per_page,
    cols,
    opts,
    ids,
    rows,
  };
}

test("HYLT binary protocol JavaScript decoder parses C-serialized buffer", () => {
  // Use a small C helper to serialize a test state to stdout and capture raw bytes
  const cSource = `
#include <stdio.h>
#include <string.h>
#include "mods/common/ux/list_state.h"
#include "mods/common/ux/list_bin.c"

int main(void) {
    list_state_t st;
    char buf[16384];
    memset(&st, 0, sizeof(st));
    snprintf(st.module, sizeof(st.module), "song");
    snprintf(st.display_name, sizeof(st.display_name), "Songs");
    st.page = 1;
    st.per_page = 10;
    st.total = 42;
    st.sort_asc = 1;
    snprintf(st.sort_field, sizeof(st.sort_field), "title");

    st.ncols = 2;
    snprintf(st.cols[0].key, sizeof(st.cols[0].key), "id");
    snprintf(st.cols[0].label, sizeof(st.cols[0].label), "ID");
    snprintf(st.cols[1].key, sizeof(st.cols[1].key), "title");
    snprintf(st.cols[1].label, sizeof(st.cols[1].label), "Title");

    st.nids = 2;
    st.ids[0] = "s1";
    st.ids[1] = "s2";
    st.values[0] = "1";
    st.values[1] = "Canção 1";
    st.values[2] = "2";
    st.values[3] = "Canção 2";

    int len = list_state_to_bin(&st, buf, sizeof(buf));
    if (len > 0) {
        fwrite(buf, 1, len, stdout);
    }
    return 0;
}
`;
  const tmpC = join(tmpdir(), `test_hylt_${Date.now()}.c`);
  const tmpBin = join(tmpdir(), `test_hylt_${Date.now()}.bin`);
  try {
    writeFileSync(tmpC, cSource);
    execFileSync("gcc", ["-O2", "-I.", tmpC, "-o", tmpBin]);
    const rawBuffer = execFileSync(tmpBin);

    const ab = rawBuffer.buffer.slice(rawBuffer.byteOffset, rawBuffer.byteOffset + rawBuffer.byteLength);
    const parsed = parseHyltBuffer(ab);

    assert.equal(parsed.module, "song");
    assert.equal(parsed.display_name, "Songs");
    assert.equal(parsed.page, 1);
    assert.equal(parsed.total, 42);
    assert.equal(parsed.sort_field, "title");
    assert.equal(parsed.sort_asc, true);
    assert.equal(parsed.cols.length, 2);
    assert.equal(parsed.cols[0].key, "id");
    assert.equal(parsed.cols[1].key, "title");
    assert.deepEqual(parsed.ids, ["s1", "s2"]);
    assert.deepEqual(parsed.rows, [["1", "Canção 1"], ["2", "Canção 2"]]);
  } finally {
    try { unlinkSync(tmpC); } catch (_) {}
    try { unlinkSync(tmpBin); } catch (_) {}
  }
});
