# Advanced Git Recovery — How the Pi-Quest Plan Was Recovered and How to Recover Anything

> **Context:** `2026-09-01` `pi-quest` `docs/HIGH_LEVEL_PLAN_CONFIGURABLE_REVIEWER.md` (`743 lines` `B3 1788269285`) + `src/logging` `src/hooks` `src/persistence` B2/B3 edits were lost after `git reset --hard HEAD 2719b52 pi-quest refactor`. Plan was recovered byte-identical via `git fsck --lost-found`, src was not — this doc explains why and how to recover/prevent any case.

## 1. How we recovered the situation

### 1.1 What you saw
- `git status` showed `?? docs/HIGH_LEVEL_PLAN…` missing, `M src/logging/types.ts 44 lines` `M src/persistence.ts 15 lines` unstaged, `git ls-files --stage 7a22bf…` == `HEAD` (313 lines, no `INITIAL_PROMPT`).
- `git log --all --oneline --name-status -- docs/HIGH_LEVEL_PLAN…` `0 hits` — file never committed.

### 1.2 How the plan came back
- `git fsck --lost-found --no-reflogs` (`man git-fsck --lost-found Write dangling objects into .git/lost-found/other/`) found `1446` dangling `blob` (`ls .git/lost-found/other | wc -l`).
- `grep -r intentHash .git/lost-found/other` → single `da871cb1fd5fb73cda525585f409d3bfc80133f0 121054 B 743 lines` `head -1 # High-Level Plan — Configurable Reviewer` `Status BUILD B3` — the plan had been `git hash-object -w`/`git add`’d as a new untracked file before the reset, so it existed as a loose `blob` in `.git/objects/da/871cb…`.
- `git cat-file -p da871cb1 > docs/HIGH_LEVEL_PLAN_CONFIGURABLE_REVIEWER.md && wc -l 743` — byte-identical, no reconstruction.

### 1.3 Why the rest did not come back the same way
- `src/logging/types.ts:172` `c5c020923… INITIAL_PROMPT/intentHash` + `src/persistence.ts bef6633… future_draft_exists` were **unstaged worktree** (`git diff --stat 2 files`, `git diff --cached --stat (empty)`, `git hash-object types.ts → c5c0209… missing` `git cat-file -e` fails, not in `.git/objects`, not in `verify-pack` 4048 packed blobs, not in `lost-found/other` — `grep intentHash` across `1446` blobs `0 src hits`, only `da871cb1` plan line).
- Git object model (`man git-add`, `man git-hash-object`): **only** `git hash-object -w` / `git add` / `git update-index` creates a `blob` oid and an index entry (`DIRC` `100644 <oid> 0 path`). Unstaged `M` lives only as filesystem bytes — `git reset --hard` discards them by design (`man git-reset --hard Any changes ... are discarded`).
- The `9 zip` blobs `PK 189k` in `lost-found/other` are `Aug 30` `pi-quest-bundle.zip` monoliths (`src/types.ts` not `src/logging/types.ts`), not the `Sep 01` modular `B3` worktree.

## 2. Git recovery cheat sheet

| Situation | Recover with | Needs prior hash? | Notes |
|---|---|---|---|
| Staged (`git add`’d, in index) but not committed, then `reset --hard` | `git fsck --lost-found` → `other/<oid>` `git cat-file -p <oid>` `git show :0:path` before reset | YES | Also `--cache` adds index as head. Survives until `gc --prune 2.weeks.ago` |
| Unstaged worktree only | **Not in git** — use outside git | NO | Use below |
| Committed (any ref) | `git reflog` `HEAD@{1}:path` `git show HEAD@{1}:path` `git log -g` | NO | `gc.reflogExpire 90d` |
| Stashed then dropped | `git stash list` `git fsck --lost-found --no-reflogs` `git show stash@{0}` `git log --merges --grep=WIP` | YES (stash created blob) | Stash is `H-I-W` merge commit |

**Useful commands (read-only):**
```bash
git fsck --lost-found --no-reflogs; ls .git/lost-found/other | wc -l
git ls-files --stage <path>; git hash-object <path>; git cat-file -e <oid> && echo exists || echo missing
git diff --cached --stat; git diff --stat; git status --porcelain
git show HEAD:path | head; git show :0:path | head
git verify-pack -v .git/objects/pack/*.idx | grep <oid>
grep -r <string> .git/lost-found/other
```

## 3. When git cannot help — recover outside git

1. **Opencode transcript** `~/.local/share/opencode/log/opencode.log` `57M` + `/tmp/opencode/tool-output/` + `.pi/quest/current/*/execution.log` — often echoes the diff verbatim.
2. **Editor local history** `~/.config/Code/Backups/` JetBrains `Local History` `~/.local/state/nvim/backup/` `.swp`.
3. **Filesystem undelete** `extundelete /dev/nvme0n1p2 --restore-file <path>` (needs `sudo`, low odds after `reset` on `97%` full, blocks overwritten).
4. **No btrfs/zfs snapshot** on this host (`df -T ext4` `mountinfo` no `lvm`).

## 4. Prevention — make staged the default

- `git add -A .pi/extensions/pi-quest/` + `git stash push -m autosave` before any `reset/checkout/clean`.
- Hook: `git config alias.auto '!git add -A && git stash push -m autosave'` or `watch -n 30 git add -A`.
- `git hash-object -w <file>` in opencode `before_agent_start` for `B3`-style multi-file edits.
- `git worktree add ../pi-quest-worktree` for risky experiments.
- Keep `da871cb1` as `RECOVERY_SOURCE` in plan `§8` footnote; never `git gc --prune=now` before extracting `lost-found/other`.

## 5. Facts that might be useful

- `HT 2` `plan 743 lines` survives as `dangling blob` until `gc.pruneExpire 2.weeks` + `gc.reflogExpireUnreachable 30d` — `git gc --no-prune` is safe.
- `pack-*.idx` `4048` blobs are compressed commits, not working-tree.
- `git show :path` reads **index**, `git show HEAD:path` reads **commit**, `git hash-object` reads **working-tree** — three different oids for one path.
- `TEST-PROMPT.md:1` consumer complexity goal + `AGENTS.md:35 Uniform §37 Pure-C` + `DAG 130 files 688 edges 1 known cycle` are the invariants the recovered plan protects.

---
*Teams using pi-quest: after this recovery, re-apply `da871cb1 §11.7 6 file edits` + `§11.8 Phase L` then `A/B/C/D` (`56→57 passed DAG 130/688 zip 47bf83a1 → 57 passed 323 steps`) and `git add -A` before the next reset.*
