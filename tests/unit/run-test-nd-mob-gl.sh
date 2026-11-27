#!/bin/sh
set -eu

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TMPDIR_WORK="$(mktemp -d)"
trap 'rm -rf "$TMPDIR_WORK"' EXIT

BIN="$TMPDIR_WORK/test_nd_mob_gl"

cc -O2 -Wall -Wextra -Werror -DLAM_WEBGL_NATIVE \
	-I"$REPO_ROOT/external/nd-mob-gl/include" \
	-I"$REPO_ROOT/external/nd-mob/include" \
	-I"$REPO_ROOT/external/liblamina/include" \
	-I"$REPO_ROOT/external/libxylem/include" \
	-I"$REPO_ROOT/external/libstele/include" \
	"$REPO_ROOT/tests/unit/test_nd_mob_gl.c" \
	"$REPO_ROOT/external/nd-mob-gl/src/libnd-mob-gl.c" \
	-L"$REPO_ROOT/external/libxylem/lib" \
	-L"$REPO_ROOT/external/libstele/lib" \
	-Wl,-rpath,"$REPO_ROOT/external/libxylem/lib" \
	-Wl,-rpath,"$REPO_ROOT/external/libstele/lib" \
	-lxylem -lstele -lm \
	-o "$BIN"

"$BIN"

if command -v glslangValidator >/dev/null 2>&1; then
	"$BIN" --dump-shaders "$TMPDIR_WORK"
	glslangValidator -l "$TMPDIR_WORK/mob_shadow.vert" "$TMPDIR_WORK/mob_shadow.frag"
	glslangValidator -l "$TMPDIR_WORK/mob_color.vert" "$TMPDIR_WORK/mob_color.frag"
	echo "PASS glslangValidator (mob_shadow + mob_color linked cleanly)"
else
	echo "WARN: glslangValidator not found; skipped GLSL compile+link check"
fi
