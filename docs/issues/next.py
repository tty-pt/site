#!/usr/bin/env python3
"""
Deterministic next-issue picker for docs/issues.

Usage:
  python docs/issues/next.py            # list all ready (ready|blocked|deferred)
  python docs/issues/next.py --ready    # only ready
  python docs/issues/next.py --json     # machine readable

State machine:
  ready     -> can be started now (requires == [])
  blocked   -> requires not yet done
  deferred  -> intentionally after verification matrix (30,31,34)
  in_progress / done -> set by editing frontmatter `state:` field

Deterministic tie-break: severity High > Medium > Low, then numeric id asc (FIX_ORDER step 1 = 36 is High+ready, so it sorts first among ready).
"""
import pathlib, re, json, sys

root = pathlib.Path(__file__).parent
SEV_ORDER = {"high":0, "medium":1, "low":2}

def parse(p):
    text = p.read_text(encoding="utf-8")
    m = re.search(r"^---\n(.*?)\n---\n", text, flags=re.S)
    if not m:
        return None
    fm = m.group(1)
    def get(k):
        mm = re.search(rf"^{k}:\s*(.*)$", fm, flags=re.M)
        return mm.group(1).strip() if mm else ""
    def list_of(k):
        raw = get(k)
        # "[01, 04]" or "[]"
        inner = re.search(r"\[(.*)\]", raw)
        if not inner: return []
        vals = [v.strip() for v in inner.group(1).split(",") if v.strip()]
        return [v for v in vals if v]
    return {
        "file": p.name,
        "id": get("id"),
        "title": get("title").strip('"').strip("'"),
        "state": get("state"),
        "severity": get("severity").strip().lower(),
        "requires": list_of("requires"),
        "blocked_by": list_of("blocked_by"),
        "validates": get("validates").strip('"').strip("'"),
        "path": str(p),
    }

def load():
    items = []
    for p in sorted(root.glob("[0-9][0-9]-*.md")):
        data = parse(p)
        if data:
            items.append(data)
    return items

def main():
    FIX_ORDER_RANK = {"36":0, "37":1, "38":2, "39":3}
    items = load()
    # done set = ids where state==done
    done = {it["id"] for it in items if it["state"]=="done"}
    # recompute blocked_by as requires - done (for display)
    for it in items:
        it["blocked_by_computed"] = [r for r in it["requires"] if r not in done]

    if "--json" in sys.argv:
        print(json.dumps(items, indent=2))
        return
    if "--ready" in sys.argv:
        ready = [it for it in items if it["state"]=="ready"]
        # same FIX_ORDER prioritization for --ready
        ready.sort(key=lambda x: (FIX_ORDER_RANK.get(x["id"], 9), SEV_ORDER.get(x["severity"],9), x["id"]))
        for it in ready:
            print(f"{it['id']} [{it['severity']}] {it['file']} — {it['title']}")
        return

    # default: full table sorted deterministically: ready first (FIX_ORDER priority), then blocked, then deferred, then done
    order = {"ready":0, "blocked":1, "deferred":2, "in_progress":3, "done":4}
    items.sort(key=lambda x: (
        order.get(x["state"],9),
        FIX_ORDER_RANK.get(x["id"], 9) if x["state"]=="ready" else 9,
        SEV_ORDER.get(x["severity"],9),
        x["id"]
    ))
    print(f"{'ID':<4} {'STATE':<10} {'SEV':<6} {'BLOCKED_BY':<18} FILE")
    print("-"*90)
    for it in items:
        bb = ",".join(it["blocked_by_computed"]) if it["blocked_by_computed"] else "-"
        if it["state"]=="ready":
            bb = "-"
        print(f"{it['id']:<4} {it['state']:<10} {it['severity']:<6} {bb:<18} {it['file']}")
    print()
    ready = [it for it in items if it["state"]=="ready"]
    ready.sort(key=lambda x: (FIX_ORDER_RANK.get(x["id"], 9), SEV_ORDER.get(x["severity"],9), x["id"]))
    if ready:
        print("NEXT (deterministic):", ready[0]["id"], ready[0]["file"])
        if not done and ready[0]["id"]!="36":
            print("WARN: expected 36 as first ready per FIX_ORDER.md step 1 — ensure 04,06,08,10,11,16 are ready but 36 has FIX_ORDER priority")
        if len(ready)>1:
            print("ALSO READY:", ", ".join(f"{r['id']}" for r in ready[1:5]), "..." if len(ready)>5 else "")

if __name__=="__main__":
    main()
