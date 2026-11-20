# Multi-Agent Worktree & `pi-mm` Memory Rules

`pi-quest` is deactivated. Do NOT call `quest_*` tools or create `.pi/quest/` files. Work directly using standard tools, `todo` for multi-step in-session tracking, git worktrees (`scripts/worktree.sh`), and `pi-mm` (`memory_recall`, `memory_record`, `memory_review`) for persistent memory and cross-agent coordination.

## Hard Requirements
1. **Check Parallel Agents & Worktrees First.** Before mutating shared subsystems or restarting host daemons (`:8080` `axil`, `:4242` `axil-auren`, `jarvisd`), inspect active worktrees and swarm leases via `sh scripts/worktree.sh status` or `memory_recall({ scope: "session", session: "parallel", query: "<topic>" })`.
2. **Initialize Worktree Submodules Zero-Network.** Never build in a git worktree with empty `external/*` directories or missing `node_modules/@tty-pt/*` symlinks. Always initialize worktrees via `sh scripts/worktree.sh init` (or `sh scripts/worktree.sh create <branch>`).
3. **User-Space `external/*` Installs.** Every library in `external/*` using `../mk/include.mk` defaults to `PREFIX=/usr` on Linux (`external/mk/portable.mk`). Always pass `PREFIX="$HOME/.local"` when running `make install` without root (`make PREFIX="$HOME/.local" install`).
4. **Closed-Loop `pi-mm` Persistence.**
   - Recall before guessing: query `memory_recall({ scope: "persistent", query: "<topic>" })` or `memory_recall({ guardrail: "<planned command or edit>" })`.
   - Record operational traps & decisions: when you discover a build/daemon/environment trap or architectural invariant, persist it immediately via `memory_record({ ..., standing: true })`.
   - Clear epistemic drift: when `pi-mm` flags `STALE_SUSPECT` on a symbol or file you intentionally changed, verify and update it via `memory_review(ref, "update_lifecycle", ...)`.
