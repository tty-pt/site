---
name: worktree-and-memory
description: "Multi-agent worktree coordination, zero-network submodule init, external/* PREFIX=$HOME/.local builds, user systemd daemons (jarvisd, axil, axil-auren), and closed-loop pi-mm memory discipline (without pi-quest). Triggers on: worktree, parallel session, subagent, swarm, lease, external/lib*, make install, PREFIX, jarvisd, systemd-run, memory_record, memory_review, epistemic drift, STALE_SUSPECT"
---

# Worktree Coordination, External Builds & `pi-mm` Memory Discipline

`pi-quest` is deactivated. Do **not** call `quest_*` tools or touch `.pi/quest/`. All multi-agent coordination, worktree isolation, and persistent operational memory use `scripts/worktree.sh` and `pi-mm` (`memory_recall`, `memory_record`, `memory_review`).

## 1. Cross-Agent & Worktree Discovery (Before Mutating)

Before editing shared subsystems, installing libraries to `~/.local`, or restarting host daemons:

```bash
sh scripts/worktree.sh status
```

This reports in one shot:
1. **All active git worktrees** (path, branch, HEAD SHA, dirty files, dirty submodules).
2. **Live Swarm Attention Leases** (`.pi/cache/code-index/swarm-leases.json`, 15-min TTL) showing which session ID + worktree is currently `MUTATING` or `inspecting` which file/symbol.
3. **Host-shared daemons** (`axil` on `:8080`, `jarvisd` user systemd service, `axil-auren` on `:4242`).

You can also query sibling agent transcripts directly via `pi-mm`:
```ts
memory_recall({ scope: "session", session: "parallel", query: "<module or symbol>" })
```

**Rule**: One writer per worktree (`cwd`). Never have two agents mutate the same worktree concurrently.

## 2. Git Worktree Lifecycle (Zero-Network Submodules & Unified `pi-mm`)

Bare `git worktree add` or `/worktree` leaves all 50+ `external/*` submodule directories empty and `node_modules/@tty-pt/*` unlinked, which breaks builds or causes silent fallback to stale `/usr/include` headers.

Always manage or bootstrap worktrees with [`scripts/worktree.sh`](../../scripts/worktree.sh):

```bash
# Create a new worktree at .worktrees/<branch> with zero-network submodule links & unified .pi/mm:
sh scripts/worktree.sh create <branch_name>

# Or initialize the current worktree (e.g. after /worktree or subagent({ worktree: true })):
sh scripts/worktree.sh init

# Cleanly remove a worktree and prune root + submodule worktree registries:
sh scripts/worktree.sh remove <branch_or_path>
```

### Worktree vs. Host Isolation Boundaries
| Resource | Scope | Rule |
|---|---|---|
| `mods/*`, `external/*` sources | **Per-worktree** | Commit inside `external/<lib>` first, then stage the gitlink in the parent worktree. |
| `/tmp/site_wasm_build_<cksum>` | **Per-worktree** | Keyed by `pwd | cksum`; safe across worktrees, unsafe if two agents share the same `cwd`. |
| `.pi/mm/` & `.pi/cache/code-index/` | **Unified across worktrees** | `pi-mm` resolves `.worktrees/*` and `.pi/scratch/slot-*` to the primary repo's `mem.db`, `standing_directives.json`, `swarm-leases.json`, and `lore-bindings.json`. |
| `~/.local/{bin,lib,include}` | **Host-global** | Installing from a worktree overwrites `~/.local` for the entire machine. Check `sh scripts/worktree.sh status` first. |
| `:8080` (`axil`), `:4242` (`axil-auren`), `jarvisd` | **Host-global** | Only one daemon binds each port/service host-wide. |

## 3. `external/*` Build & User Daemon Traps

1. **`make PREFIX="$HOME/.local" install` (Never bare `make install`)**:
   - Every library in `external/*` includes `../mk/include.mk` $\rightarrow$ `external/mk/portable.mk`, which sets `prefix-Linux := /usr`.
   - Running bare `make install` without root fails with `Permission denied` on `/usr/include/ttypt/*`.
   - Always install to the user prefix:
     ```bash
     make -C external/lib<name> PREFIX="$HOME/.local" all install
     ```
2. **`jarvisd` Build & Transient User Systemd Service**:
   - `external/libjarvis/Makefile` builds `bin/jarvisd` from `src/jarvisd.c` (with `-j` Jarvis mode and PipeWire EOF backoff).
   - `jarvisd` runs as a **transient** user systemd service (`systemd-run --user --unit=jarvisd`).
   - Stopping or killing a transient unit causes systemd to garbage-collect it immediately (`systemctl --user start jarvisd` will fail with `Unit jarvisd.service not found`).
   - Always restart/relaunch `jarvisd` via:
     ```bash
     systemctl --user stop jarvisd 2>/dev/null || true
     systemctl --user reset-failed jarvisd 2>/dev/null || true
     systemd-run --user --unit=jarvisd "$HOME/.local/bin/jarvisd" -j
     sleep 1
     "$HOME/.local/bin/jarvisd" -c "STATUS" && "$HOME/.local/bin/jarvisd" -c "JARVIS_STATUS"
     ```

## 4. Closed-Loop `pi-mm` Memory Discipline

1. **Pre-Flight Before Guessing**:
   - Run `memory_recall({ guardrail: "<planned command or edit>", file: "<path>" })` before risky builds, installs, or architectural edits.
2. **Persist Operational Traps Immediately (`standing: true`)**:
   - Whenever you hit and solve a build trap, environment quirk, or user preference, record it before ending your turn so no future agent repeats the error:
     ```ts
     memory_record({
       problem: "<concise rule or invariant>",
       approaches: [
         { approach: "<what failed>", outcome: "failed", reason: "<why>" },
         { approach: "<what worked>", outcome: "worked", reason: "<why>" }
       ],
       constraints: ["<mandatory rule>"],
       standing: true,
       anchor: true,
       project: "site",
       subsystem: "<subsystem>"
     })
     ```
3. **Clear Epistemic Drift (`STALE_SUSPECT`) Immediately**:
   - When `pi-mm` emits `⚠️ [pi-mm Epistemic Drift Detected] (Ref N, STALE_SUSPECT)` after an edit, inspect it with `memory_review(N, "inspect")` and either re-verify or supersede it with `memory_review(N, "update_lifecycle", { lifecycle: "superseded", reason: "..." })`.
