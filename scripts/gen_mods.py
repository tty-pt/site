#!/usr/bin/env python3
import argparse
import json
import os
import sys


def load_mod(mod_path):
    with open(mod_path, "r", encoding="utf-8") as handle:
        data = json.load(handle)
    if not isinstance(data, dict):
        raise ValueError(f"mod.json must be object: {mod_path}")

    mod_id = data.get("id") or os.path.basename(os.path.dirname(mod_path))
    title = data.get("title", mod_id)
    routes = data.get("routes", [])
    ssr = data.get("ssr", "")
    be = data.get("be") or f"mods/{mod_id}/{mod_id}.so"
    deps = data.get("deps", [])

    if isinstance(routes, str):
        routes = [routes]
    if not isinstance(routes, list):
        raise ValueError(f"routes must be list: {mod_path}")
    for route in routes:
        if not isinstance(route, str):
            raise ValueError(f"route must be string: {mod_path}")

    if isinstance(deps, str):
        deps = [deps]
    if not isinstance(deps, list):
        raise ValueError(f"deps must be list: {mod_path}")
    for dep in deps:
        if not isinstance(dep, str):
            raise ValueError(f"dep must be string: {mod_path}")

    if not isinstance(title, str):
        raise ValueError(f"title must be string: {mod_path}")
    if not isinstance(ssr, str):
        raise ValueError(f"ssr must be string: {mod_path}")
    if not isinstance(be, str):
        raise ValueError(f"be must be string: {mod_path}")

    return {
        "id": mod_id,
        "title": title,
        "routes": routes,
        "ssr": ssr,
        "be": be,
        "deps": deps,
    }


def topo_sort(mods):
    deps = {mod_id: set(mod["deps"]) for mod_id, mod in mods.items()}
    reverse = {mod_id: set() for mod_id in mods}

    missing = []
    for mod_id, mod_deps in deps.items():
        for dep in mod_deps:
            if dep not in mods:
                missing.append((mod_id, dep))
            else:
                reverse[dep].add(mod_id)

    if missing:
        for mod_id, dep in missing:
            print(f"missing dependency: {mod_id} -> {dep}", file=sys.stderr)
        raise SystemExit(1)

    indegree = {mod_id: len(mod_deps) for mod_id, mod_deps in deps.items()}
    queue = sorted([mod_id for mod_id, degree in indegree.items() if degree == 0])
    order = []

    while queue:
        mod_id = queue.pop(0)
        order.append(mod_id)
        for child in sorted(reverse[mod_id]):
            indegree[child] -= 1
            if indegree[child] == 0:
                queue.append(child)
        queue.sort()

    if len(order) != len(mods):
        remaining = sorted([mod_id for mod_id, degree in indegree.items() if degree > 0])
        print("dependency cycle detected:", ", ".join(remaining), file=sys.stderr)
        raise SystemExit(1)

    return order


def write_mods_load(path, order, mods):
    with open(path, "w", encoding="utf-8") as handle:
        for mod_id in order:
            be = mods[mod_id]["be"].strip()
            if be:
                if not be.startswith("/") and not be.startswith("./"):
                    be = f"./{be}"
                handle.write(be)
                handle.write("\n")


def format_db_line(mod):
    routes = ",".join(mod["routes"]).strip()
    return f"{mod['id']}:title={mod['title']}; routes={routes}; ssr={mod['ssr']}; be={mod['be']}"


def parse_roots(value):
    if not value:
        return []
    roots = []
    for part in value.split(","):
        part = part.strip()
        if part:
            roots.append(part)
    return roots


def collect_deps(mods, roots):
    missing = [root for root in roots if root not in mods]
    if missing:
        print("missing root modules:", ", ".join(sorted(missing)), file=sys.stderr)
        raise SystemExit(1)

    collected = set()

    def walk(mod_id):
        if mod_id in collected:
            return
        collected.add(mod_id)
        for dep in mods[mod_id]["deps"]:
            if dep not in mods:
                print(f"missing dependency: {mod_id} -> {dep}", file=sys.stderr)
                raise SystemExit(1)
            walk(dep)

    for root in roots:
        walk(root)

    return collected


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mods-dir", default="mods")
    parser.add_argument("--mods-load", default="mods.load")
    parser.add_argument("--roots", default="")
    args = parser.parse_args()

    mods_dir = os.path.abspath(args.mods_dir)
    mods = {}
    for entry in sorted(os.listdir(mods_dir)):
        mod_path = os.path.join(mods_dir, entry)
        if not os.path.isdir(mod_path):
            continue
        manifest = os.path.join(mod_path, "mod.json")
        if not os.path.exists(manifest):
            print(f"missing mod.json: {manifest}", file=sys.stderr)
            raise SystemExit(1)
        mod = load_mod(manifest)
        mods[mod["id"]] = mod

    roots = parse_roots(args.roots)
    if roots:
        selected = collect_deps(mods, roots)
        mods = {mod_id: mod for mod_id, mod in mods.items() if mod_id in selected}

    order = topo_sort(mods)
    write_mods_load(args.mods_load, order, mods)

    for mod_id in order:
        mod = mods[mod_id]
        if mod["routes"]:
            print(format_db_line(mod))


if __name__ == "__main__":
    main()
