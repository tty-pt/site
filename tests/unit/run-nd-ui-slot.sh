#!/bin/sh
set -e

SRC_DIR=$(pwd)
BUILD_DIR=$(mktemp -d /tmp/nd_ui_slot_test_XXXXXX)
trap 'rm -rf "$BUILD_DIR"' EXIT

clang -g -Wall -Wextra \
	-I"$SRC_DIR" \
	-I"$SRC_DIR/external/axil-nd/include" \
	-I"$SRC_DIR/external/libbud/include" \
	-I"$SRC_DIR/external/libxylem/include" \
	-I"$SRC_DIR/external/nd-attr/include" \
	-I"$SRC_DIR/external/nd-level/include" \
	-I"$SRC_DIR/external/nd-mortal/include" \
	-I"$SRC_DIR/external/nd-plant/include" \
	-I"$SRC_DIR/external/nd-race/include" \
	-I"$SRC_DIR/external/nd-spell/include" \
	-I"$SRC_DIR/external/nd-fight/include" \
	-I"$SRC_DIR/external/nd-obj/include" \
	-I"$SRC_DIR/external/nd-region/include" \
	-I"$SRC_DIR/external/nd-speech/include" \
	-I"$SRC_DIR/external/nd-view/include" \
	-I"$SRC_DIR/external/nd-world/include" \
	-I"$SRC_DIR/external/nd-mcp/include" \
	-I/usr/include \
	"$SRC_DIR/tests/unit/nd_ui_slot_test.c" \
	-L"$SRC_DIR/external/libbud/lib" \
	-lbud -lxylem \
	-o "$BUILD_DIR/nd_ui_slot_test"

"$BUILD_DIR/nd_ui_slot_test"
