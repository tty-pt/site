# Mandatory Completion Report Template

**Pi MUST output this exact template before claiming any task is complete.**
Copy, fill every field, and paste in your final response. Incomplete = not done.

---

## ⚡ TORVALDS DOCTRINE COMPLIANCE REPORT

### 1. Data Layout / Invariant
**What data structure/invariant does this change affect?**
```
<exact struct name, memory layout, or invariant — one line>
```

### 2. Files & Symbols Changed
**List every file and symbol modified (from `git diff --name-only`):**
```
<file1.c: symbol1, symbol2>
<file2.h: symbol3>
```

### 3. Callers / Tests at Risk
**Inbound callers or tests that could break (trace inbound if needed):**
```
<caller1 in fileX.c>
<test_mod_Y>
```

### 4. Plan (3–5 bullets)
```
- <what changes>
- <why>
- <targeted verification command>
```

### 5. Tier 0 Verification (MANDATORY — run and report actual output)
| Check | Command | Exit Code | Output (last 5 lines) |
|-------|---------|-----------|----------------------|
| Native build | `make -C mods/<M>` | `<0 or non-zero>` | `<tail>` |
| Module tests | `make test-mod MOD=<M>` | `<0 or non-zero>` | `<tail>` |
| WASM build (if UX) | `make site-wasm` | `<0 or non-zero>` | `<tail>` |
| Boundaries (if UX) | `sh scripts/check-module-boundaries.sh` | `<0 or non-zero>` | `<tail>` |
| WASM imports (if UX) | `sh scripts/check-wasm-imports.sh` | `<0 or non-zero>` | `<tail>` |
| External lib build | `make <L>-lib` | `<0 or non-zero>` | `<tail>` |
| External lib tests | `tests/unit/run-<L>-*.sh` | `<0 or non-zero>` | `<tail>` |
| JS check (if JS) | `sh scripts/check-no-site-specific-js.sh` | `<0 or non-zero>` | `<tail>` |

**All Tier 0 commands MUST pass (exit code 0). If any fails → STOP and fix.**

### 6. Git Diff Stat
```
$ git diff --stat
<output here — must show ONLY intended changes, no debug artifacts>
```

### 7. Debug Artifacts Check
- [ ] No leftover `printf` / `console.log` / `debug` statements
- [ ] No temporary files in diff
- [ ] No unrelated formatting changes

### 8. Skill Loaded
- [ ] **torvalds-doctrine** skill read (`.pi/skills/torvalds-doctrine/SKILL.md`)
- [ ] Other relevant skills: _______________

### 9. Verdict
- [ ] **PASS** — All Tier 0 green, diff clean, skills loaded
- [ ] **FAIL** — See above (do not claim done)

---

**Pi: If you cannot fill every field with actual command output, the task is NOT complete.**