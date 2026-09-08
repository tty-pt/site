# pi-early-compact

A standalone Pi extension that decides **when** to compact context early:
window-adaptive tiers, percentage or token thresholds, and a warning margin —
configurable from a single `/early-compact` command.

It never trims context itself. It calls pi's standard `ctx.compact()`; whatever
handles `session_before_compact` performs the cut. That means:

- With **pi-vcc** (`overrideDefaultCompaction: true`), the trim is deterministic
  and instant.
- Without pi-vcc, pi's built-in compactor takes over.

Either way works. pi-early-compact is **compatible with pi-vcc but not
dependent on it**.

## Install

Project-local extension auto-discovery loads `index.ts` from
`.pi/extensions/pi-early-compact/`.

If you have `pi-auto-compact` installed, remove it so the two preflights never
double-fire:

```bash
pi uninstall pi-auto-compact   # or remove "npm:pi-auto-compact" from settings.json packages
```

Then reload pi (`/reload`).

## How it works

On `input` (agent idle), the extension projects `current usage + new prompt`
against the resolved threshold. At or above it, `ctx.compact()` runs once before
the prompt is sent. Soft errors ("Nothing to compact" / "Already compacted") let
the prompt through; hard errors fail closed. `turn_end` shows a colored dot
(red = compacting, yellow = approaching, dim = healthy).

### Mid-run compaction (during agent turns)

While an agent run is active, the extension also watches the `context` event (
emitted before each LLM call — the same boundary pi's own auto-compaction uses,
where no tool is mid-flight and tool results are already persisted). Once usage
crosses the threshold **mid-run**, it compacts and the run continues on its own:

1. `ctx.compact()` fires without blocking the handler (manual compaction aborts
   the running turn, so awaiting it would deadlock).
2. The turn settles cleanly at the boundary.
3. On success, the extension queues a standalone continuation
   (`sendMessage(..., { triggerTurn: true, deliverAs: "steer" })`) — an idle
   trigger — so the task **resumes on the fresh compacted context** without a
   new prompt.

One compact per crossing: a hysteresis flag re-arms only once usage falls below
the warning threshold. Soft compaction errors also resume the run (the abort
already happened); hard errors leave the run paused and notify. Toggle with
`midRunCompact` (default on); disable the auto-continue with `continueAfterCompact`
(default on) to compact at the boundary and wait for the next prompt instead.

## Threshold resolution (first match wins)

1. **Explicit** — `thresholdPct` or `thresholdTokens` set via `/early-compact`
2. **Env** — `PI_EARLY_COMPACT_THRESHOLD` (accepts `75%` or `300k`),
   `PI_EARLY_COMPACT_WARNING_MARGIN_TOKENS`, `PI_EARLY_COMPACT_WARNING_PCT`
3. **Settings** — `.pi/settings.json` / `~/.pi/agent/settings.json` keys under
   `pi-early-compact` (threshold, warning) or the legacy `compaction.economy*`
4. **Adaptive curve (default)** — interpolate the compact percentage linearly
   between the window anchors, capped at 90%, no absolute ceiling

### Adaptive curve anchors — default

| context window | compact at |
|----------------|-----------|
| ≤ 250k         | 75%       |
| ≤ 500k         | 60%       |
| ≤ 1M           | 40%       |
| > 1M           | 40% (clamped percentage) |

The curve runs **linearly between anchors**: the percentage for a window between
two anchors interpolates `pct` between them (`375k → 67.5%`). Below the first
anchor the first percentage applies; above the last anchor the last percentage
applies and the threshold simply scales with the window. There is no absolute
token ceiling.

### Project overrides via `.pi/settings.json`

Declare your own anchors under `pi-early-compact`, each with a window cap and
either a percentage **or** a token amount:

```json
"pi-early-compact": {
  "tiers": [
    { "upTo": 200000, "pct": 80 },
    { "upTo": 1000000, "pct": 50 }
  ]
}
```

A 200k window compacts at 80% (160k tokens); a 1M window at 50% (500k tokens);
a 600k window interpolates to 65% (390k). `tokens` anchors are allowed instead
of `pct` (converted to a percentage at their own window, e.g.
`{ "upTo": 200000, "tokens": 160000 }` ≡ 80%). Malformed tier arrays are
ignored and fall back to the defaults above.

The warning threshold defaults to the compact percent minus 10 points (e.g.
compact 75% → warn at 65% of the window).

## Configuration

```text
/early-compact                     status
/early-compact 75%                 set percentage threshold
/early-compact 300k 30k            set token threshold + warning margin
/early-compact manual              disable adaptive tiers (use explicit threshold)
/early-compact adaptive            re-enable adaptive tiers
/early-compact default             reset to defaults
/early-compact off | on            disable / enable early compaction
/early-compact midrun on | off     toggle mid-run compaction during agent turns
/early-compact midrun show         show mid-run state
```

Config persists to `~/.pi/agent/pi-early-compact.json` (merge-safe, atomic
rename). Override the path with `PI_EARLY_COMPACT_CONFIG_PATH`. The optional
flags `midRunCompact` and `continueAfterCompact` (both default `true`) live in
the same file.

Pi's own built-in auto-compaction stays enabled as the final safety net.

## Development

```bash
npm test          # or: deno test --allow-all --sloppy-imports --node-modules-dir=none tests/
npm run zip       # writes .pi/extensions/pi-early-compact-bundle.zip
```

`src/policy.ts` is pure and dependency-free; `src/trigger.ts` capability-detects
`getContextUsage` / `compact` at runtime and never imports a pi package;
`src/midrun.ts` adds mid-run compaction over the same structural types and
capability-detects `sendMessage` before auto-continuing.

## Design notes

- **Not quest-coupled.** pi-quest v2 already guarantees state survival via
  custom snapshots; early compaction only reduces context pressure and never
  touches that machinery.
- **Graceful degradation.** If `getContextUsage` or the model window is unknown
  (e.g. right after a compaction), the preflight is skipped and pi's own
  compaction covers it. If a host has no `compact()` at all, prompts flow through
  untouched.