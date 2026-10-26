# nd-kernel-mods.sh -- the NeverDark kernel modules, passed to axil via -m.
#
# These are the engine itself (region governance provides `loadmod`, so it
# cannot be loaded by it). libaxil-nd does not xy_load them and mods.load does
# not list them. Every *content* module (biome, plant, attr, ...) is loaded
# in-game with `loadmod` and persisted in region rows, restored by st_init().
ND_KERNEL_MODS="libnd-region:libnd-bus:libnd-obj:libnd-world:libnd-view:libnd-mcp:libnd-speech:libnd-session"

# Resolve every nd library from this tree. Without this the bare soname
# `libnd-session` resolves to an installed /usr/lib copy while `./lib/axil-nd`
# is the local build: two copies of law_io.c with separate fd->player tables,
# so fd_player() answers 0 and every command is refused. Callers that run the
# installed set (start.sh after `make install-libs`) simply do not source this.
nd_local_ld_path() {
	_root="$1"; _p=""
	for _d in "$_root"/external/*/lib; do _p="$_p:$_d"; done
	printf '%s' "${_p#:}"
}
