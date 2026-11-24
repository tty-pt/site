#!/bin/sh
# Shared helpers for tests/unit/run-*.sh scripts.
# Source from any unit runner:
#   . "$(dirname "$0")/common.sh"

REPO_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
UNIT_CC="${CC:-clang}"
UNIT_GCC="${UNIT_GCC:-gcc}"
CX_BIN="$REPO_ROOT/external/libbud/bin/cx"
UNIT_CACHE_DIR="/tmp/site_unit_cache_$(printf '%s' "$REPO_ROOT" | cksum | cut -d' ' -f1)"

UNIT_EXT_INCLUDES=""
for _unit_inc_dir in "$REPO_ROOT"/external/*/include; do
	[ -d "$_unit_inc_dir" ] || continue
	UNIT_EXT_INCLUDES="$UNIT_EXT_INCLUDES -I$_unit_inc_dir"
done
unset _unit_inc_dir

UNIT_EXT_LDFLAGS=""
UNIT_LD_LIBRARY_PATH=""
for _unit_lib_dir in "$REPO_ROOT"/external/*/lib; do
	[ -d "$_unit_lib_dir" ] || continue
	UNIT_EXT_LDFLAGS="$UNIT_EXT_LDFLAGS -L$_unit_lib_dir"
	UNIT_LD_LIBRARY_PATH="${UNIT_LD_LIBRARY_PATH:+$UNIT_LD_LIBRARY_PATH:}$_unit_lib_dir"
done
if [ -d "${HOME:-}/.local/lib" ]; then
	UNIT_EXT_LDFLAGS="$UNIT_EXT_LDFLAGS -L$HOME/.local/lib"
	UNIT_LD_LIBRARY_PATH="${UNIT_LD_LIBRARY_PATH:+$UNIT_LD_LIBRARY_PATH:}$HOME/.local/lib"
fi
if [ -n "${LD_LIBRARY_PATH:-}" ]; then
	UNIT_LD_LIBRARY_PATH="${UNIT_LD_LIBRARY_PATH:+$UNIT_LD_LIBRARY_PATH:}$LD_LIBRARY_PATH"
fi
unset _unit_lib_dir

UNIT_TMPDIR=""
unit_tmpdir() {
	if [ -z "$UNIT_TMPDIR" ]; then
		UNIT_TMPDIR=$(mktemp -d)
		trap 'rm -rf "$UNIT_TMPDIR"' EXIT HUP INT TERM
	fi
}

unit_cx_transpile() {
	for _cx_src in "$@"; do
		case "$_cx_src" in
			/*) _cx_abs="$_cx_src" ;;
			*)  _cx_abs="$REPO_ROOT/$_cx_src" ;;
		esac
		_cx_out="${_cx_abs}.c"
		if [ ! -f "$_cx_out" ] || [ "$_cx_abs" -nt "$_cx_out" ] || [ "$CX_BIN" -nt "$_cx_out" ]; then
			"$CX_BIN" "$_cx_abs" -o "$_cx_out"
		fi
	done
	unset _cx_src _cx_abs _cx_out
}

unit_obj_stale() {
	_obj="$1"
	_src="$2"
	[ ! -f "$_obj" ] && return 0
	[ "$_src" -nt "$_obj" ] && return 0
	[ ! -f "$_obj.d" ] && return 0
	# -MP emits one '<header>:' line per header prerequisite.
	_stale=0
	for _dep in $(sed -n 's/:$//p' "$_obj.d"); do
		if [ ! -e "$_dep" ] || [ "$_dep" -nt "$_obj" ]; then
			_stale=1
			break
		fi
	done
	[ "$_stale" -eq 1 ] && return 0
	return 1
}

# Build/update a cached static archive in $UNIT_CACHE_DIR.
# Usage: unit_build_archive <name.a> <compiler> <cflags> -- <src...>
# Sets UNIT_ARCHIVE to the resulting archive path.
unit_build_archive() {
	_arch_name="$1"; shift
	_cc="$1"; shift
	_cflags="$1"; shift
	[ "${1:-}" = "--" ] && shift

	_flag_hash=$(printf '%s|%s' "$_cc" "$_cflags" | cksum | cut -d' ' -f1)
	_obj_dir="$UNIT_CACHE_DIR/obj_${_flag_hash}"
	_archive="$UNIT_CACHE_DIR/${_arch_name%.a}_${_flag_hash}.a"
	mkdir -p "$_obj_dir"

	_objs=""
	_dirty=0
	for _src in "$@"; do
		case "$_src" in
			/*) _abs="$_src" ;;
			*)  _abs="$REPO_ROOT/$_src" ;;
		esac
		_key=$(printf '%s' "$_src" | tr '/.' '__')
		_obj="$_obj_dir/${_key}.o"
		_objs="$_objs $_obj"
		if unit_obj_stale "$_obj" "$_abs"; then
			# shellcheck disable=SC2086
			"$_cc" $_cflags -MMD -MP -MT "$_obj" -MF "$_obj.d.tmp.$$" -c "$_abs" -o "$_obj.tmp.$$" || {
				rm -f "$_obj.tmp.$$" "$_obj.d.tmp.$$"
				return 1
			}
			mv -f "$_obj.tmp.$$" "$_obj"
			mv -f "$_obj.d.tmp.$$" "$_obj.d"
			_dirty=1
		fi
	done

	if [ "$_dirty" -eq 1 ] || [ ! -f "$_archive" ]; then
		# shellcheck disable=SC2086
		ar rcs "$_archive.tmp.$$" $_objs || {
			rm -f "$_archive.tmp.$$"
			return 1
		}
		mv -f "$_archive.tmp.$$" "$_archive"
	fi

	UNIT_ARCHIVE="$_archive"
	unset _arch_name _cc _cflags _flag_hash _obj_dir _archive _objs _dirty _src _abs _key _obj
}

ND_UX_INCLUDES="\
	-I$REPO_ROOT/include \
	-I$REPO_ROOT/external/libbud/include \
	-I$REPO_ROOT/external/axil-nd/include \
	-I$REPO_ROOT/external/nd-seven/include \
	-I$REPO_ROOT/external/axil-tty/include \
	-I$REPO_ROOT/external/liblamina/include \
	-I$REPO_ROOT/external/nd-attr/include \
	-I$REPO_ROOT/external/nd-equip/include \
	-I$REPO_ROOT/external/nd-spell/include \
	-I$REPO_ROOT/external/nd-fight/include \
	-I$REPO_ROOT/external/nd-mortal/include \
	-I$REPO_ROOT/external/libareole/include \
	-I$REPO_ROOT/external/libcorolla/include \
	-I$REPO_ROOT/external/libxylem/include \
	-I$REPO_ROOT/external/libchaff/include \
	-I$REPO_ROOT/external/nd-world/include \
	-I$REPO_ROOT/external/nd-plant/include \
	-I$REPO_ROOT/mods/common/ux \
	-I$REPO_ROOT/mods/nd/ux"

ND_UX_CX_SRCS="\
	mods/common/ux/site_layout.cx \
	mods/common/ux/site_forms.cx \
	mods/common/ux/site_media.cx \
	mods/nd/ux/nd_settings_ui.cx \
	mods/nd/ux/nd_hud_ui.cx \
	mods/nd/ux/nd_shell_ui.cx \
	mods/nd/ux/nd_map_ui.cx \
	mods/nd/ux/nd_dock_ui.cx \
	mods/nd/ux/glue/equip_ui.cx \
	mods/nd/ux/glue/attr_ui.cx"

ND_SEVEN_SRCS="\
	mods/nd/ux/nd_scene.c \
	mods/nd/ux/nd_lamina.c \
	mods/nd/ux/nd_seven.c \
	mods/nd/ux/nd_seven_gl_shaders.c \
	mods/nd/ux/nd_seven_gl_math.c \
	mods/nd/ux/nd_seven_gl_billboard.c \
	mods/nd/ux/nd_seven_gl_occ.c \
	mods/nd/ux/nd_seven_gl_clouds.c \
	mods/nd/ux/nd_seven_gl.c \
	mods/nd/ux/nd_seven_atlas_draw.c \
	mods/nd/ux/nd_seven_ground.c \
	mods/nd/ux/nd_seven_motion.c \
	mods/nd/ux/nd_seven_objects.c \
	external/libareole/src/areole.c \
	external/liblamina/src/particle.c \
	external/libstele/src/stele_wq.c"

ND_UX_SRCS="\
	mods/common/ux/site_ui.c \
	mods/nd/ux/nd_ui.c \
	mods/nd/ux/nd_win.c \
	mods/nd/ux/nd_settings_ui.cx.c \
	mods/nd/ux/nd_hud_ui.cx.c \
	mods/nd/ux/nd_shell_ui.cx.c \
	mods/nd/ux/nd_map_ui.cx.c \
	mods/nd/ux/nd_dock_ui.cx.c \
	mods/nd/ux/nd_state_io.c \
	mods/nd/ux/glue/equip_ui.cx.c \
	mods/nd/ux/glue/attr_ui.cx.c \
	mods/nd/ux/glue/spell_ui.c \
	mods/nd/ux/glue/fight_ui.c \
	mods/nd/ux/glue/mortal_ui.c \
	$ND_SEVEN_SRCS"

# Compile and run a full ND UX C unit test linked against cached libnd_ux_test.a.
# Usage: unit_run_nd_ux_test <test_file.c> [extra_cflags...]
unit_run_nd_ux_test() {
	_test_src="$1"; shift
	case "$_test_src" in
		/*) _test_abs="$_test_src" ;;
		*)  _test_abs="$REPO_ROOT/$_test_src" ;;
	esac
	unit_tmpdir
	# shellcheck disable=SC2086
	unit_cx_transpile $ND_UX_CX_SRCS
	# shellcheck disable=SC2086
	unit_build_archive libnd_ux_test.a "$UNIT_GCC" "-g -O0 $ND_UX_INCLUDES" -- $ND_UX_SRCS
	_bin="$UNIT_TMPDIR/$(basename "$_test_abs" .c)"
	if [ "$(uname -s)" = "Darwin" ]; then
		# shellcheck disable=SC2086
		"$UNIT_GCC" -g -O0 $ND_UX_INCLUDES "$@" \
			-o "$_bin" "$_test_abs" -Wl,-force_load,"$UNIT_ARCHIVE" \
			$UNIT_EXT_LDFLAGS \
			-lbud -laxil -lxylem -lhyle-bud -lstele -lxxhash -lm -lpthread
	else
		# shellcheck disable=SC2086
		"$UNIT_GCC" -g -O0 $ND_UX_INCLUDES "$@" \
			-o "$_bin" "$_test_abs" -Wl,--whole-archive "$UNIT_ARCHIVE" -Wl,--no-whole-archive \
			$UNIT_EXT_LDFLAGS \
			-lbud -laxil -lxylem -lhyle-bud -lstele -lxxhash -lm -lpthread
	fi
	LD_LIBRARY_PATH="$UNIT_LD_LIBRARY_PATH" "$_bin"
}

# Compile and run a Seven-engine C unit test linked against cached libnd_seven_test.a.
# Usage: unit_run_nd_seven_test <test_file.c> <cflags> <libs>
unit_run_nd_seven_test() {
	_test_src="$1"
	_extra_cflags="${2:--O1}"
	_libs="${3:--lxxhash -lm -lpthread}"
	case "$_test_src" in
		/*) _test_abs="$_test_src" ;;
		*)  _test_abs="$REPO_ROOT/$_test_src" ;;
	esac
	unit_tmpdir
	# shellcheck disable=SC2086
	unit_build_archive libnd_seven_test.a "$UNIT_GCC" "-O1 $ND_UX_INCLUDES" -- $ND_SEVEN_SRCS
	_bin="$UNIT_TMPDIR/$(basename "$_test_abs" .c)"
	# shellcheck disable=SC2086
	"$UNIT_GCC" $_extra_cflags $ND_UX_INCLUDES \
		-o "$_bin" "$_test_abs" "$UNIT_ARCHIVE" \
		$UNIT_EXT_LDFLAGS $_libs
	LD_LIBRARY_PATH="$UNIT_LD_LIBRARY_PATH" "$_bin"
}
