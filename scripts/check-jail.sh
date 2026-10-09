#!/bin/sh
# check-jail.sh -- hard pre-restart gate for the axil -C chroot jail.
#
#   sh scripts/check-jail.sh [JAIL]     # JAIL defaults to $JAIL or /var/www
#
# Verifies, without needing root or a live server, everything that would make
# a restarted axil boot a hollow site: the manifest (scripts/jail-manifest.sh
# says why each entry is there) present and byte-identical to the build tree,
# nothing shadowing a module in module_load_path()'s own search order, every
# post-chroot DT_NEEDED either already mapped at startup or resolvable from a
# glibc default directory inside the jail, and the site root reachable (the
# jail IS the site root -- see the topology note in the manifest).
#
# Exits non-zero on any failure; warnings do not fail the gate.
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$root"
. scripts/jail-manifest.sh

JAIL=${1:-${JAIL:-/var/www}}
failed=0

fail() {
	printf 'check-jail: FAIL: %s\n' "$*" >&2
	failed=$((failed + 1))
}

warn() {
	printf 'check-jail: warn: %s\n' "$*" >&2
}

if ! command -v readelf >/dev/null 2>&1; then
	printf 'check-jail: readelf not found, cannot verify DT_NEEDED\n' >&2
	exit 1
fi

if [ -z "${JAIL_AXIL_BIN:-}" ]; then
	if [ -x external/axil/bin/axil ]; then
		JAIL_AXIL_BIN=external/axil/bin/axil
	else
		JAIL_AXIL_BIN=$(command -v axil 2>/dev/null || true)
	fi
fi

if [ ! -d "$JAIL" ]; then
	printf 'check-jail: %s is not a directory\n' "$JAIL" >&2
	exit 1
fi

manifest=$(jail_manifest) || {
	printf 'check-jail: manifest failed, nothing verified\n' >&2
	exit 1
}

inode_of() {
	stat -c %d:%i "$1" 2>/dev/null || true
}

# ---- 1. the manifest is present and identical to the tree ----------------
while read -r src dst; do
	[ -n "$dst" ] || continue
	if [ ! -f "$src" ]; then
		fail "$src is gone from the tree (rebuild: make)"
		continue
	fi
	if [ ! -f "$JAIL/$dst" ]; then
		fail "$dst is missing from the jail (seed: sh scripts/seed-jail.sh $JAIL)"
		continue
	fi
	if ! cmp -s "$src" "$JAIL/$dst"; then
		fail "$dst differs from $src (stale jail; reseed)"
	fi
done <<EOF
$manifest
EOF

# ---- 2. nothing shadows a seeded module ----------------------------------
# module_load_path() walks cwd ("/"), LD_LIBRARY_PATH, then /lib, /usr/lib,
# /usr/local/lib -- so a leftover copy in an earlier directory wins over the
# seeded one and the jail would boot a file nobody verified.
while read -r src dst; do
	case $dst in usr/local/lib/*) ;; *) continue ;; esac
	name=${dst##*/}
	for dir in . lib usr/lib; do
		other=$JAIL/$dir/$name
		[ "$dir" = . ] && other=$JAIL/$name
		[ -f "$other" ] || continue
		if [ "$(inode_of "$other")" = "$(inode_of "$JAIL/$dst")" ] &&
			[ -n "$(inode_of "$other")" ]; then
			continue
		fi
		if cmp -s "$other" "$JAIL/$dst"; then
			warn "$dir/$name is a duplicate of usr/local/lib/$name"
		else
			fail "$dir/$name shadows usr/local/lib/$name and differs from it (module_load_path finds it first)"
		fi
	done
done <<EOF
$manifest
EOF

# ---- 3. bare sonames resolve inside the jail -----------------------------
while IFS= read -r name; do
	# The in-tree form is addressed by path, not by soname; rule 1 already
	# checked that path inside the jail.
	[ -f "external/axil-nd/mods/$name/$name.c" ] && continue
	src=$(jail_soname_src "$name") || {
		fail "mods.load names $name, which is neither built nor installed"
		continue
	}
	resolved=
	for dir in . lib usr/lib usr/local/lib; do
		cand=$JAIL/$name.so
		[ "$dir" = . ] || cand=$JAIL/$dir/$name.so
		if [ -f "$cand" ]; then
			resolved=$cand
			break
		fi
	done
	if [ -z "$resolved" ]; then
		fail "xy_load($name) finds no $name.so anywhere in the jail"
		continue
	fi
	if ! cmp -s "$src" "$resolved"; then
		fail "xy_load($name) resolves to ${resolved#"$JAIL"/}, which differs from $src"
	fi
done <<EOF
$(jail_manifest_bare_names)
EOF

startup=$(jail_startup_libs "$JAIL_AXIL_BIN" | tr '\n' ' ')
need_fails() {
	jail_manifest_sos | while IFS= read -r so; do
		[ -f "$so" ] || continue
		for need in $(jail_needed "$so"); do
			case " $startup " in
			*" $need "*) continue ;;
			esac
			have=
			for dir in $(jail_loader_defaults); do
				[ -f "$JAIL/$dir/$need" ] && {
					have=1
					break
				}
			done
			[ -n "$have" ] || printf '%s needs %s\n' "${so##*/}" "$need"
		done
	done
}

# ---- 4. every DT_NEEDED is reachable after the chroot --------------------
# Collected first: the loop runs in a pipeline, hence in a subshell, so its
# verdict has to come back as data rather than as shell state.
need_fails=$(need_fails)
if [ -n "$need_fails" ]; then
	while IFS= read -r line; do
		[ -n "$line" ] || continue
		fail "$line: not in the axil startup closure and not in any glibc default directory of the jail"
	done <<-EOF
		$need_fails
	EOF
fi

# ---- 5. the jail is the site root ----------------------------------------
# corm opens var/nd/std.db with O_RDONLY and mmaps it: a missing file loads
# as an empty world with no error, and the exit save would then write the
# world into the jail. Both halves have to be the same files.
for dir in var htdocs; do
	if [ -d "$root/$dir" ] && [ ! -d "$JAIL/$dir" ]; then
		fail "$dir/ is in the site tree but not in the jail: the jail must be the site root itself (a separate jail loads an empty world and saves back into it)"
	fi
done
if [ -f "$root/var/nd/std.db" ]; then
	if [ ! -f "$JAIL/var/nd/std.db" ]; then
		fail "var/nd/std.db is in the tree but not in the jail"
	elif [ -n "$(inode_of "$root/var/nd/std.db")" ] &&
		[ "$(inode_of "$root/var/nd/std.db")" != "$(inode_of "$JAIL/var/nd/std.db")" ]; then
		fail "var/nd/std.db in the jail is a different file from the tree's"
	fi
fi

# ---- 6. the pre-chroot half is not stale ---------------------------------
# Everything the axil process maps before the hook comes from the host; if a
# library the tree also builds has drifted, a restart would run old code with
# a freshly seeded jail. Only names the tree builds are checked -- libc and
# friends have no tree counterpart.
if [ -n "$JAIL_AXIL_BIN" ]; then
	for lib in $(jail_startup_libs "$JAIL_AXIL_BIN"); do
		src=$(jail_tree_lib "$lib" 2>/dev/null) || continue
		host=$(jail_host_lib "$lib") || {
			fail "$lib is linked by axil but absent from the host libraries"
			continue
		}
		if ! cmp -s "$src" "$host"; then
			fail "$host differs from $src: stale install (run make install-libs) before restarting"
		fi
	done
fi

# ---- 7. non-fatal observations -------------------------------------------
if [ -f "$root/serve.allow" ] && [ ! -f "$JAIL/serve.allow" ]; then
	warn "serve.allow is in the tree but not in the jail (axil reads it post-chroot)"
fi
if [ -x "$root/bin/sh" ] && [ ! -x "$JAIL/bin/sh" ]; then
	warn "bin/sh is in the tree but not in the jail (axil-tty's shell exec will fail; see docs/BUILD.md chroot prerequisites)"
elif [ ! -e "$JAIL/bin/sh" ]; then
	warn "no bin/sh in the jail (axil-tty's shell exec will fail; see docs/BUILD.md chroot prerequisites)"
fi

if [ "$failed" -ne 0 ]; then
	printf 'check-jail: %s failure(s) in %s\n' "$failed" "$JAIL" >&2
	exit 1
fi
printf 'check-jail: ok (%s)\n' "$JAIL"
