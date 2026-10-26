#!/bin/sh
set -eu

# new-nd-module.sh — Scaffolds a new NeverDark tenant module conforming to
# the two-header separation rule, declarative BCP framework, and isomorphic UI slots.

if [ "$#" -lt 1 ]; then
	echo "Usage: $0 <module-name> [description]"
	echo "Example: $0 nd-trade \"Merchant Trading and Barter Engine\""
	exit 1
fi

MOD_NAME="$1"
DESC="${2:-NeverDark extension module}"

# Strip optional "nd-" prefix for header naming
BASE_NAME="$(echo "$MOD_NAME" | sed 's/^nd-//')"
UPPER_BASE="$(echo "$BASE_NAME" | tr '[:lower:]-' '[:upper:]_')"
MOD_DIR="external/$MOD_NAME"

if [ -d "$MOD_DIR" ]; then
	echo "Error: Directory $MOD_DIR already exists."
	exit 1
fi

echo "Scaffolding new NeverDark module in $MOD_DIR..."
mkdir -p "$MOD_DIR/include/nd" "$MOD_DIR/src" "$MOD_DIR/lib"

# 1. include/nd/$BASE-types.h (Two-Header Rule: Pure types, 0 *_IMPL guards)
cat <<EOF > "$MOD_DIR/include/nd/$BASE_NAME-types.h"
#ifndef ND_${UPPER_BASE}_TYPES_H
#define ND_${UPPER_BASE}_TYPES_H

/*
 * $BASE_NAME-types.h — Shared types for $MOD_NAME.
 * Conforms to strict Two-Header Separation: 0 *_IMPL guards, 0 externs.
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	uint32_t id;
	char name[48];
	uint32_t flags;
} ${BASE_NAME}_item_t;

typedef struct {
	uint32_t count;
	${BASE_NAME}_item_t items[16];
} ${BASE_NAME}_state_t;

#ifdef __cplusplus
}
#endif

#endif /* ND_${UPPER_BASE}_TYPES_H */
EOF

# 2. include/nd/$BASE.h (Caller-facing header: XY_DECLs)
cat <<EOF > "$MOD_DIR/include/nd/$BASE_NAME.h"
#ifndef ND_${UPPER_BASE}_H
#define ND_${UPPER_BASE}_H

/*
 * $BASE_NAME.h — Caller-facing header for $MOD_NAME.
 * Implementers include <nd/$BASE_NAME-types.h>, not this header.
 */

#include <ttypt/xy.h>
#include <nd/$BASE_NAME-types.h>

#ifdef __cplusplus
extern "C" {
#endif

XY_DECL(int, ${BASE_NAME}_action, uint32_t, id, void *, ctx);

#ifdef __cplusplus
}
#endif

#endif /* ND_${UPPER_BASE}_H */
EOF

# 3. include/nd/$BASE-bcp.h (Declarative BCP channel definitions)
cat <<EOF > "$MOD_DIR/include/nd/$BASE_NAME-bcp.h"
#ifndef ND_${UPPER_BASE}_BCP_H
#define ND_${UPPER_BASE}_BCP_H

/*
 * $BASE_NAME-bcp.h — Wire protocol definitions for $MOD_NAME.
 * Uses zero-arithmetic cursor readers and writers.
 */

#include <stddef.h>
#include <stdint.h>
#include <nd/bcp-meta.h>
#include <nd/$BASE_NAME-types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ND_BCP_${UPPER_BASE} 0x20

static inline int
${BASE_NAME}_bcp_unpack(const uint8_t *buf, size_t len, ${BASE_NAME}_state_t *out)
{
	if (!buf || len == 0 || !out)
		return -1;

	nd_bcp_reader_t r;
	nd_bcp_reader_init(&r, buf, len);
	out->count = nd_bcp_read_u32_c(&r);
	if (out->count > 16) out->count = 16;

	for (size_t i = 0; i < out->count; i++) {
		out->items[i].id = nd_bcp_read_u32_c(&r);
		nd_bcp_read_str_c(&r, out->items[i].name, sizeof(out->items[i].name));
		out->items[i].flags = nd_bcp_read_u32_c(&r);
	}

	return nd_bcp_reader_ok(&r) ? 1 : -1;
}

static inline int
${BASE_NAME}_bcp_pack(uint8_t *buf, size_t cap, const ${BASE_NAME}_state_t *in)
{
	if (!buf || !in)
		return -1;

	nd_bcp_writer_t w;
	nd_bcp_writer_init(&w, buf, cap);
	nd_bcp_write_u32(&w, in->count);

	for (size_t i = 0; i < in->count && i < 16; i++) {
		nd_bcp_write_u32(&w, in->items[i].id);
		nd_bcp_write_str(&w, in->items[i].name);
		nd_bcp_write_u32(&w, in->items[i].flags);
	}

	return nd_bcp_writer_ok(&w) ? (int)nd_bcp_writer_len(&w, buf) : -1;
}

#ifdef __cplusplus
}
#endif

#endif /* ND_${UPPER_BASE}_BCP_H */
EOF

# 4. include/nd/$BASE-ui.h (Isomorphic UI slot integration)
cat <<EOF > "$MOD_DIR/include/nd/$BASE_NAME-ui.h"
#ifndef ND_${UPPER_BASE}_UI_H
#define ND_${UPPER_BASE}_UI_H

/*
 * $BASE_NAME-ui.h — Isomorphic Bud UI slot definitions for $MOD_NAME.
 */

#include <bud/bud.h>
#include <nd/ui-slot.h>
#include <nd/$BASE_NAME-types.h>

#ifdef __cplusplus
extern "C" {
#endif

static const nd_ui_action_def_t g_${BASE_NAME}_action = {
	.id = "$BASE_NAME",
	.icon = "📦",
	.label = "$BASE_NAME",
	.order = 40
};

static inline bud_node *
${BASE_NAME}_ui_render_modal(int is_active, const ${BASE_NAME}_state_t *st)
{
	bud_node *modal = bud_element("div");
	bud_set_attr(modal, "id", "modal-$BASE_NAME");
	bud_set_attr(modal, "class", is_active ? "fixed inset-0 z-50 flex items-center justify-center bg-black/75 p-4" : "hidden");

	bud_node *card = bud_element("div");
	bud_set_attr(card, "class", "w-full max-w-md bg-neutral-900 border border-neutral-700 rounded-lg p-6 shadow-2xl");

	bud_node *title = bud_element("h3");
	bud_set_attr(title, "class", "text-lg font-bold text-amber-400 mb-4");
	bud_text(title, "$DESC");
	bud_append_child(card, title);

	bud_append_child(modal, card);
	return modal;
}

#ifdef __cplusplus
}
#endif

#endif /* ND_${UPPER_BASE}_UI_H */
EOF

# 5. src/lib$MOD.c (Module implementation)
cat <<EOF > "$MOD_DIR/src/lib$MOD_NAME.c"
#include <stdio.h>
#include <string.h>
#include <ttypt/xy-mod.h>
#include <nd/$BASE_NAME-types.h>

XY_IMPL(int, ${BASE_NAME}_action, uint32_t, id, void *, ctx)
{
	(void)ctx;
	printf("[$MOD_NAME] action executed on item %u\n", id);
	return 0;
}

int xy_install(void)
{
	return 0;
}
EOF

# 6. Makefile
cat <<EOF > "$MOD_DIR/Makefile"
CC ?= cc
CFLAGS ?= -O2 -g -Wall -Wextra -fPIC -Iinclude -I/usr/include/ttypt -I/usr/include
LDFLAGS ?= -shared -L/usr/lib -lxylem

TARGET_SO = lib/lib$MOD_NAME.so
SONAME_SO = lib/$MOD_NAME.so

all: \$(TARGET_SO)

\$(TARGET_SO): src/lib$MOD_NAME.c
	@mkdir -p lib
	\$(CC) \$(CFLAGS) \$(LDFLAGS) -o \$@ src/lib$MOD_NAME.c
	@ln -sf lib$MOD_NAME.so \$(SONAME_SO)

clean:
	rm -rf lib

.PHONY: all clean
EOF

# 7. test.sh
cat <<EOF > "$MOD_DIR/test.sh"
#!/bin/sh
set -eu
make clean
make
echo "$MOD_NAME build PASS"
EOF
chmod +x "$MOD_DIR/test.sh"

# 8. README.md
cat <<EOF > "$MOD_DIR/README.md"
# $MOD_NAME

$DESC for NeverDark MUCK.

## Architecture
- **Two-Header Separation**: Pure types in \`<nd/$BASE_NAME-types.h>\`, declarations in \`<nd/$BASE_NAME.h>\`.
- **BCP Protocol**: Zero-arithmetic pack/unpack helpers in \`<nd/$BASE_NAME-bcp.h>\`.
- **Isomorphic UI**: Reactive modal and action bar dock in \`<nd/$BASE_NAME-ui.h>\`.
EOF

echo "Module $MOD_NAME successfully scaffolded in $MOD_DIR."