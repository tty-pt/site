# jail-manifest.sh -- what the axil -C chroot jail must hold.
#
# Sourced by scripts/seed-jail.sh and scripts/check-jail.sh; never run on its
# own. Emits "SRC DST" lines: SRC is relative to the build tree (the repo
# root, the caller's cwd), DST is relative to the jail root.
#
# Why there is a jail at all: axil -C under root does a real chroot(2)
# (external/axil/src/axil-posix.c:160) and then chdir("/"), both from
# init_pre_bind() -- after the -m module chain, the engine boot and the DB
# open, before the first bind. Everything loaded before that point resolves on
# the host; everything resolved after it resolves inside $JAIL. The split
# point is the on_axil_post_chroot() hook (external/axil/src/libaxil.c,
# called between init_pre_bind() and axil_bind()), and the only thing that
# runs there is the engine's persisted-region restore:
#
#     st_init()       -- persisted region modules, one xy_load() per DB row
#
# so the jail needs exactly what that, plus request-time static serving,
# resolves. Nothing else: no headers, no archives, no site or axil libraries
# (all loaded pre-chroot), no DB (corm holds the open fd across the chroot),
# no pkg-config. There is no module list: region rows are the source of
# truth, and every module the tree can build is seeded (rule 1).
#
# The topology that makes this work: **the jail IS the site root** (on prod,
# /var/www both holds the checkout and is the chroot). Every path the site and
# the engine use is site-root-relative, so var/ and htdocs/ are literally the
# same files before and after the chroot -- which is also why a jail anywhere
# else is unsupported: corm opens var/nd/std.db read-only (a missing file
# loads as an EMPTY world, silently: external/libcorm/src/libcorm.c:2759) and
# saves back to it on exit (a separate jail would write the world elsewhere).
# check-jail.sh enforces this; seed-jail.sh only ever ADDS files.
#
# The manifest:
#
#   usr/local/lib/<libnd-*.so>   every module the tree can build
#                                (external/nd-*/lib/*.so). Seeded from the
#                                tree, because the persisted regions reload
#                                from DB rows, not from a list -- a module a
#                                region names still has to exist for
#                                st_init(). Found by module_load_path() at
#                                its third system dir (cwd is "/",
#                                LD_LIBRARY_PATH is unset by the
#                                one-authority rule, /lib and /usr/lib hold
#                                nothing): external/libxylem/src/libxylem.c.
#                                A region module built outside this tree is
#                                the operator's to seed by hand.
#
#   usr/lib/<NEEDED>             the one class of file the process has NOT
#                                already mapped: the module set's DT_NEEDED
#                                union is {libc, libxylem, libm}; the axil
#                                binary links libc and libxylem itself, so a
#                                module's NEEDED for those reuses the mapped
#                                copy (verified: a dlopen of libnd-core emits
#                                no second "find library=" probe). libm is
#                                only reached through libnd-equip and
#                                libnd-spell, so glibc searches its defaults
#                                INSIDE the jail -- /lib, /usr/lib and the
#                                multiarch dirs, where /usr/local/lib is
#                                deliberately absent -- and it has to be
#                                there. Derived from readelf at seed time, so
#                                a module that gains a new dependency is
#                                caught without editing this file;
#                                check-jail.sh re-derives it.
#
#   external/axil-nd/htdocs/**    AXIL_ND_HTDOCS -- serves GET /nd
#   external/axil-tty/htdocs/**   AXIL_HTDOCS -- serves GET /tty (axil-tty's
#                                compiled default is $(PREFIX)/share/axil/
#                                htdocs, so rc.d must set the relative form)
#
# Deliberately absent: libxylem/libcorm/libqsys/libaxil-* (already mapped),
# libc and the dynamic loader (the process is running), the axil-nd store
# (opened pre-chroot), the site's own htdocs/ and var/ (they are the jail
# itself -- see the topology note above).

# A library stem to a file in the tree: external/<pkg>/lib/<stem>.so. Exactly
# one match or none -- two matches would be two authorities to choose from.
jail_tree_lib() {
	_jl_stem=$1
	_jl_n=0
	_jl_hit=
	for _jl_f in external/*/lib/"$_jl_stem".so; do
		[ -f "$_jl_f" ] || continue
		_jl_n=$((_jl_n + 1))
		_jl_hit=$_jl_f
	done
	[ "$_jl_n" -eq 1 ] || return 1
	printf '%s\n' "$_jl_hit"
	unset _jl_stem _jl_n _jl_hit _jl_f
}

# module_load_path()'s system dirs, jail-relative, in the order it walks them
# (external/libxylem/src/libxylem.c). Replicated by check-jail.sh.
jail_loader_dirs() {
	printf '%s\n' lib usr/lib usr/local/lib
}

# Where glibc itself looks for a DT_NEEDED name when the ld.so cache is absent
# (a jail has no /etc/ld.so.cache): measured with LD_DEBUG=libs against a
# missing NEEDED -- /lib, /lib/<multiarch>, /usr/lib, /usr/lib/<multiarch>,
# plus a glibc-hwcaps subdir of each. /usr/lib is the seed target because it
# is in every spelling and needs no multiarch name.
jail_loader_defaults() {
	printf '%s\n' lib lib/x86_64-linux-gnu usr/lib usr/lib/x86_64-linux-gnu
}

# Where the host keeps a runtime library: the same defaults glibc walks.
jail_host_lib() {
	for _jh_d in /lib/x86_64-linux-gnu /usr/lib/x86_64-linux-gnu /lib \
		/usr/lib /usr/local/lib; do
		if [ -f "$_jh_d/$1" ]; then
			printf '%s\n' "$_jh_d/$1"
			unset _jh_d
			return 0
		fi
	done
	unset _jh_d
	return 1
}

# Resolve a bare soname to its source: the tree first (that is the authority
# check-jail.sh compares the jail against), then the host's own search order
# for a module built outside the tree. Neither -> failure: an unresolvable
# stem must stop the seed rather than be skipped silently.
jail_soname_src() {
	_js_stem=$1
	if _js_src=$(jail_tree_lib "$_js_stem"); then
		printf '%s\n' "$_js_src"
		unset _js_stem _js_src
		return 0
	fi
	for _js_d in /lib /usr/lib /usr/local/lib; do
		if [ -f "$_js_d/$_js_stem.so" ]; then
			printf '%s\n' "$_js_d/$_js_stem.so"
			unset _js_stem _js_src _js_d
			return 0
		fi
	done
	unset _js_stem _js_src _js_d
	return 1
}

# DT_NEEDED names of one object, one per line.
jail_needed() {
	readelf -d "$1" 2>/dev/null |
		sed -n 's/.*Shared library: \[\(.*\)\].*/\1/p'
}

# The axil binary's DT_NEEDED closure: everything listed here is mapped by the
# time the process starts, so a module's NEEDED for one of these reuses the
# mapped copy and needs no file in the jail.
jail_startup_libs() {
	[ -n "$1" ] && [ -x "$1" ] || return 0
	ldd "$1" 2>/dev/null |
		awk '{ for (i = 1; i <= NF; i++) if ($i ~ /^\//) { n = $i; sub(/.*\//, "", n); print n } }' |
		sort -u
}

# The module sources of rule 1 (see jail_manifest): every .so the manifest
# installs under usr/local/lib, from the tree side.
jail_manifest_sos() {
	for _jf in external/nd-*/lib/*.so; do
		[ -f "$_jf" ] || continue
		printf '%s\n' "$_jf"
	done
	unset _jf
}

# Every module stem the manifest seeds, in rule-1 order (used by
# check-jail.sh to re-run module_load_path()'s search inside the jail).
jail_manifest_bare_names() {
	for _jf in external/nd-*/lib/*.so; do
		[ -f "$_jf" ] || continue
		_jf_stem=${_jf##*/}
		printf '%s\n' "${_jf_stem%.so}"
	done
	unset _jf _jf_stem
}

jail_manifest() {
	# ---- 1. installed game modules, from the tree -----------------------
	for _jf in external/nd-*/lib/*.so; do
		[ -f "$_jf" ] || continue
		printf '%s usr/local/lib/%s\n' "$_jf" "${_jf##*/}"
	done

	# ---- 2. the two static trees the relative AXIL_* envs point at -------
	for _jf_dir in external/axil-nd/htdocs external/axil-tty/htdocs; do
		if [ ! -d "$_jf_dir" ]; then
			printf 'jail-manifest: missing %s\n' "$_jf_dir" >&2
			unset _jf_src _jf _jf_dir _jf_needed _jf_mapped
			return 1
		fi
		for _jf in "$_jf_dir"/*; do
			[ -f "$_jf" ] || continue
			printf '%s %s\n' "$_jf" "$_jf"
		done
	done

	# ---- 3. DT_NEEDED the process will not already hold ------------------
	# Everything else in the module graph resolves by reuse; this is the
	# remainder, placed where glibc's own default search will find it.
	_jf_needed=$(jail_manifest_sos | while IFS= read -r _jf; do
		jail_needed "$_jf"
	done | sort -u)
	if [ -n "$_jf_needed" ]; then
		_jf_mapped=$(jail_startup_libs "${JAIL_AXIL_BIN:-}" | tr '\n' ' ')
		for _jf_need in $_jf_needed; do
			case " $_jf_mapped " in
			*" $_jf_need "*) continue ;;
			esac
			if ! _jf_src=$(jail_host_lib "$_jf_need"); then
				printf 'jail-manifest: %s is needed after the chroot but is in neither the axil startup closure nor the host libraries\n' \
					"$_jf_need" >&2
				unset _jf_src _jf _jf_dir _jf_needed _jf_mapped _jf_need
				return 1
			fi
			printf '%s usr/lib/%s\n' "$_jf_src" "$_jf_need"
		done
	fi
	unset _jf_src _jf _jf_dir _jf_needed _jf_mapped _jf_need
	return 0
}
