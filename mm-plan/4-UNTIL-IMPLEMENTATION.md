# 4-UNTIL-IMPLEMENTATION.md — Phase 4: `--until` validity window

## Problem

All mm stores produce open-ended `[date, ∞)` intervals on joint (the
`<DATE>:<TEXT>` payload grammar). There was no way to limit the time
window to a past date — "search only before X" — without manually
crafting a joint expression.

## Design

**`--until` on `memory_scan` only** (not `memory_store`). Joint's
`A,B` atomic form rejects trailing text (`"2026-09-14,2026-09-20:text"`
→ `EINVAL`), so storing a bounded interval with text content is
impossible on the single-payload surface. The scan path has no such
limitation.

### scanExpr changes

```
scanExpr(topic, level, now, until?)
```

| level | until | expression |
|-------|-------|------------|
| 0 | absent | `stoma="..."` (pure text, current) |
| 0 | `2026-09-20` | `joint="a=0 b=2026-09-20" AND stoma="..."` |
| 1 | absent | `joint="a=today b=tomorrow" AND stoma="..."` (current) |
| 1 | `2026-09-15` | `joint="a=today b=2026-09-15" AND stoma="..."` |
| 2 | absent | `joint="a=month-start b=next-month" AND stoma="..."` (current) |
| 2 | `2026-09-20` | `joint="a=month-start b=2026-09-20" AND stoma="..."` |

`until` is the earlier of `until` and the level's natural `b`. Level 0
with `until` creates `a=0` (epoch start) to cap from the beginning.

### Tool parameter

`memory_scan(topic, level?, limit?, until?)` — `until` is optional
ISO date (YYYY-MM-DD). Parsed in scan.ts with the same regex guard as
`payloadDate`.

## Files changed

| File | Change |
|------|--------|
| `src/qmap.ts` | `scanExpr` gains optional `until` param; caps `b` at the earlier of `until` and `window.b`; level 0 + until creates `a=0 b=until` |
| `src/tools/scan.ts` | `until` parameter in tool schema + parsed before `scanExpr` call |
| `tests/args.test.ts` | 4 new tests: level 0 + until, level 1 + until same day, level 1 + until beyond window, level 2 + until |
| `tests/tools.test.ts` | 2 new tests: scan level 0 + until epoch-to-until, scan level 1 + until caps b |
| `scripts/integration-mm.sh` | Round-trip: store refs 1,2 (date 2026-09-14) + ref 3 (date 2026-09-15); scan level-0 until=2026-09-15 finds 1,2 but NOT 3 |
| `skills/pi-mm/SKILL.md` | Updated `memory_scan` signature + `until` docs |

## Rationale: why not `--until` on `memory_store`

Joint's `joint_date_parse` uses `prefix_ok=0` for the atomic `A,B`
form (both sides must be clean ISO dates with no trailing text).
The mm dialect payload `"2026-09-14,2026-09-20:Beacon Harbor lights"`
would have the right side `"2026-09-20:Beacon Harbor lights"` fail
`strptime`'s strict mode → `EINVAL`. A dual-`-p` approach (one for
joint bounded interval, one for stoma text) would create two intervals
on joint for the same ref (one bounded, one open) — the union defeats
the purpose. The scan-only approach is clean and requires zero C changes.

## Gates

- `deno test` 54/54 passed
- `deno lint` clean
- `check-complexity.ts` ok
- `integration-mm.sh` all green (level-0 + until round-trip proven)
- root `make` W06 PASS
