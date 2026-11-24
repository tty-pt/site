---
name: advanced-git-recovery
description: "Recover lost git state after reset --hard, failed rebase, dangling blobs, lost commits. Use when git status shows missing files, git log misses history, or fsck/reflog/recovery needed. Triggers on: git recovery, lost commits, dangling blobs, git fsck, git reflog, reset hard lost files, advanced git recovery"
---

# Advanced Git Recovery

Recover `docs/HIGH_LEVEL_PLAN_CONFIGURABLE_REVIEWER.md 743 lines` style losses after `git reset --hard HEAD 2719b52` — when `git status` shows `M`/`??` but `git log` misses history and `git fsck` is the only source.

## How we recovered Pi-Quest

1. Plan `da871cb1fd5fb73cda525585f409d3bfc80133f0 121054 B 743 lines Status BUILD B3` had been `git hash-object -w`/`git add`’d before reset → loose `blob` in `.git/objects/da/871cb…` and `.git/lost-found/other/da871cb1…` (`git fsck --lost-found` found `1446` dangling `blob`). Recovered via `git cat-file -p da871cb1 > docs/HIGH_LEVEL_PLAN_CONFIGURABLE_REVIEWER.md && wc -l 743`.

2. Src `src/logging/types.ts c5c0209… +INITIAL_PROMPT/intentHash` + `src/persistence.ts bef6633… future_draft_exists` were **unstaged worktree** (`git diff --stat 2 files`, `git diff --cached --stat empty`, `git ls-files --stage 7a22bf… == HEAD`, `git hash-object → missing`, `verify-pack 4048` 0 hits, `grep intentHash` across `1446` blobs 0 src hits). `git reset --hard` discards them by design (`man git-reset --hard`).

## Git object model

```
Working Tree (filesystem) --[hash-object -w / add]--> Index (.git/index DIRC) + Object DB (.git/objects/[ab]/, pack/*.pack) --[write-tree/commit]--> Tree/Commit (refs/heads/*)
```

Only `hash-object -w`/`add`/`update-index` creates a `blob` oid. Unstaged `M` lives only on disk. See `references/recovery-phases.md` for full Phase L → A → B → C → D detail (`da871cb1 §11.7`).

## Cheat sheet

| Situation | Recover with | Needs prior hash? |
|---|---|---|
| Staged (`git add`’d) but not committed, then `reset --hard` | `git fsck --lost-found` → `other/<oid>` `git cat-file -p <oid>` `git show :0:path` before reset | YES |
| Unstaged worktree only | **Not in git** — use Opencode transcript / editor history / extundelete | NO |
| Committed | `git reflog` `HEAD@{1}:path` `git show HEAD@{1}:path` | NO |
| Stashed then dropped | `git stash list` `git fsck --no-reflogs` `git log --merges --grep=WIP` | YES |

```bash
git fsck --lost-found --no-reflogs; ls .git/lost-found/other | wc -l
git ls-files --stage <path>; git hash-object <path>; git cat-file -e <oid> && echo exists || echo missing
git diff --cached --stat; git diff --stat; git status --porcelain
git show HEAD:path | head; git show :0:path | head
git verify-pack -v .git/objects/pack/*.idx | grep <oid>
grep -r <string> .git/lost-found/other
```

## When git cannot help

1. **Opencode transcript** `~/.local/share/opencode/log/opencode.log` `57M` + `/tmp/opencode/tool-output/` + `.pi/quest/current/*/execution.log`
2. **Editor history** `~/.config/Code/Backups/` JetBrains `Local History` `~/.local/state/nvim/backup/` `.swp`
3. **Filesystem** `extundelete /dev/nvme0n1p2 --restore-file <path>` (needs `sudo`, low odds on full disk)
4. No `btrfs`/`zfs` snapshot on this host (`df -T ext4`).

## Prevention

- `git add -A .pi/extensions/pi-quest/` + `git stash push -m autosave` before any `reset/checkout/clean`
- `git config alias.auto '!git add -A && git stash push -m autosave'`
- `git hash-object -w <file>` in `before_agent_start` for multi-file edits
- `git worktree add ../pi-quest-worktree` for risky experiments
- Never `git gc --prune=now` before extracting `lost-found/other`; `gc.pruneExpire 2.weeks`, `gc.reflogExpireUnreachable 30d`

## Facts

- `pack-*.idx` `4048` blobs are packed commits, not working-tree
- `git show :path` = index, `git show HEAD:path` = commit, `git hash-object` = working-tree — three oids for one path
- `TEST-PROMPT.md:1` consumer complexity + `AGENTS.md:35 Uniform §37 Pure-C` + `DAG 130/688` are the invariants the recovered plan protects

See `docs/ADVANCED_GIT_RECOVERY.md` for human-readable long form and `references/recovery-phases.md` for full `da871cb1` edit list.
