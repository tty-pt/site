#!/usr/bin/env python3
"""
Deterministic next-issue picker for docs/issues.

Usage:
  python docs/issues/next.py            # list all ready (ready|blocked|deferred)
  python docs/issues/next.py --ready    # only ready
  python docs/issues/next.py --json     # machine readable

State machine:
  ready     -> can be started now (requires ⊆ done and children done if parent; strict phase gate via ancestors)
  blocked   -> derived: requires not yet done or children not done or ancestor blocked — never stored
  deferred  -> intentionally after verification matrix (30,31,34)
  in_progress / done -> set by editing frontmatter `state:` field

Parents require children: a parent is blocked until all its children are done.
Children inherit ancestor blocked (strict phase gate: 44 requires [43] blocks all Parent 44 leaves until drafting done). Single parent per issue (tree). `state: blocked` and `blocked_by:` are not stored — computed.

Deterministic tie-break: severity High > Medium > Low, then numeric id asc.
"""
import pathlib, re, json, sys
from collections import defaultdict

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
        inner = re.search(r"\[(.*)\]", raw)
        if not inner: return []
        vals = [v.strip() for v in inner.group(1).split(",") if v.strip()]
        return [v for v in vals if v]
    def single(k):
        raw = get(k).strip().strip('"').strip("'").strip("[]").strip()
        # parent: 43 or parent: "43" or parent: [] or empty
        if not raw or raw == "[]":
            return ""
        # handle "parent: 43" -> "43"
        return raw.split(",")[0].strip().strip('"').strip("'")
    return {
        "file": p.name,
        "id": get("id"),
        "title": get("title").strip('"').strip("'"),
        "state": get("state"),
        "severity": get("severity").strip().lower(),
        "requires": list_of("requires"),
        "validates": get("validates").strip('"').strip("'"),
        "parent": single("parent"),
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
    items = load()
    # hard fail if legacy blocked stored (no backward compat)
    legacy = [it for it in items if it["state"] == "blocked"]
    if legacy:
        print(f"ERROR: {len(legacy)} files still have state: blocked (blocked is derived, not stored): " + ", ".join(it["id"] for it in legacy), file=sys.stderr)
        sys.exit(1)
    legacy_bb = [it for it in items if "blocked_by" in open(it["path"]).read()]
    if legacy_bb:
        print(f"ERROR: {len(legacy_bb)} files still have blocked_by: field (deleted, computed): " + ", ".join(it["id"] for it in legacy_bb), file=sys.stderr)
        sys.exit(1)
    id_map = {it["id"]: it for it in items}
    done = {it["id"] for it in items if it["state"]=="done"}
    # children map (derived, not stored)
    children_map = defaultdict(list)
    for it in items:
        par = it["parent"]
        if par and par in id_map:
            children_map[par].append(it["id"])

    # own blocked parts
    own_requires = {}
    own_children_blocked = {}
    own_blocked = {}
    for it in items:
        req_blocked = [r for r in it["requires"] if r not in done]
        own_requires[it["id"]] = sorted(set(req_blocked), key=lambda cid: (SEV_ORDER.get(id_map.get(cid, {}).get("severity",""),9), cid))
        child_blocked = []
        for child_id in children_map.get(it["id"], []):
            child = id_map.get(child_id)
            if child and child["state"] != "done":
                child_blocked.append(child_id)
        child_blocked = sorted(set(child_blocked), key=lambda cid: (SEV_ORDER.get(id_map.get(cid, {}).get("severity",""),9), cid))
        own_children_blocked[it["id"]] = child_blocked
        own_blocked[it["id"]] = sorted(set(req_blocked + child_blocked), key=lambda cid: (SEV_ORDER.get(id_map.get(cid, {}).get("severity",""),9), cid))

    # ancestor requires = transitive requires of parent chain + requires chain (strict phase gate, no sibling pollution, cycle-safe)
    memo_anc = {}
    def anc_requires(iid, stack=None):
        if stack is None:
            stack = set()
        if iid in memo_anc:
            return memo_anc[iid]
        if iid in stack:
            return []
        stack.add(iid)
        own_req = list(own_requires.get(iid, []))
        result = set(own_req)
        # transitive via requires
        for r in own_req:
            if r in id_map:
                result |= set(anc_requires(r, set(stack)))
        # via parent chain
        par = id_map.get(iid, {}).get("parent")
        if par and par in id_map:
            result |= set(anc_requires(par, set(stack)))
        result = sorted(result, key=lambda cid: (SEV_ORDER.get(id_map.get(cid, {}).get("severity",""),9), cid))
        memo_anc[iid] = result
        return result

    for it in items:
        # effective blocked = own blocked + ancestor requires of parent (strict gate, no sibling pollution)
        par = it["parent"]
        anc = anc_requires(par, set()) if par and par in id_map else []
        eff = sorted(set(own_blocked[it["id"]] + anc), key=lambda cid: (SEV_ORDER.get(id_map.get(cid, {}).get("severity",""),9), cid))
        it["blocked_by_computed"] = eff
        it["blocked_by_effective"] = eff
        it["children"] = sorted(children_map.get(it["id"], []))
        # effectiveState: blocked derived, not stored
        s = it["state"]
        if s in ("done","deferred","in_progress"):
            it["effectiveState"] = s
        else:
            it["effectiveState"] = "blocked" if eff else "ready"

    if "--json" in sys.argv:
        print(json.dumps(items, indent=2))
        return
    if "--ready" in sys.argv:
        ready = [it for it in items if it["effectiveState"] == "ready"]
        ready.sort(key=lambda x: (SEV_ORDER.get(x["severity"],9), x["id"]))
        by_parent = defaultdict(list)
        for r in ready:
            by_parent[r["parent"] or "_root"].append(r)
        for par, lst in sorted(by_parent.items(), key=lambda kv: (kv[0]=="_root", kv[0])):
            label = f"Parent {par}" if par != "_root" else "Root"
            print(f"{label}:")
            for it in lst:
                print(f"  {it['id']:02} [{it['severity']}] {it['file']} — {it['title']} (state:{it['state']} effective:{it['effectiveState']})")
        return

    order = {"ready":0, "blocked":1, "deferred":2, "in_progress":3, "done":4}
    items.sort(key=lambda x: (
        order.get(x["effectiveState"],9),
        SEV_ORDER.get(x["severity"],9),
        x["id"]
    ))
    print(f"{'ID':<4} {'STATE':<10} {'SEV':<6} {'PARENT':<8} {'BLOCKED_BY':<18} FILE")
    print("-"*90)
    for it in items:
        bb = ",".join(it["blocked_by_computed"]) if it["blocked_by_computed"] else "-"
        if it["effectiveState"]=="ready":
            bb = "-"
        par = it["parent"] or "-"
        print(f"{it['id']:<4} {it['effectiveState']:<10} {it['severity']:<6} {par:<8} {bb:<18} {it['file']}")
    print()
    dag_ready = [it for it in items if it["effectiveState"] == "ready"]
    dag_ready.sort(key=lambda x: (SEV_ORDER.get(x["severity"],9), x["id"]))
    if dag_ready:
        print("NEXT (deterministic):", dag_ready[0]["id"], dag_ready[0]["file"])
        if len(dag_ready)>1:
            print("ALSO READY:", ", ".join(f"{r['id']}" for r in dag_ready[1:5]), "..." if len(dag_ready)>5 else "")

if __name__=="__main__":
    main()
