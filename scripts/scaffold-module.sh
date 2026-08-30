#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

if [ $# -lt 1 ]; then
  echo "Usage: $0 <modulename> [DisplayName]" >&2
  echo "Example: $0 artist 'Artist'" >&2
  exit 1
fi

MOD_NAME=$(echo "$1" | tr '[:upper:]' '[:lower:]' | tr -cd 'a-z0-9_')
if [ -z "$MOD_NAME" ]; then
  echo "Error: Invalid module name '$1'." >&2
  exit 1
fi

if [ $# -ge 2 ]; then
  DISPLAY_NAME="$2"
else
  # Capitalize first letter
  DISPLAY_NAME="$(tr '[:lower:]' '[:upper:]' <<< "${MOD_NAME:0:1}")${MOD_NAME:1}"
fi

MOD_UPPER=$(echo "$MOD_NAME" | tr '[:lower:]' '[:upper:]')
MOD_DIR="${REPO_ROOT}/mods/${MOD_NAME}"
VAR_DIR="${REPO_ROOT}/var/${MOD_NAME}"

if [ -d "$MOD_DIR" ]; then
  echo "Error: Module directory '$MOD_DIR' already exists." >&2
  exit 1
fi

echo "Scaffolding module '${MOD_NAME}' (${DISPLAY_NAME})..."

mkdir -p "$MOD_DIR/ux"
mkdir -p "$VAR_DIR"

# 1. Makefile
cat <<EOF > "$MOD_DIR/Makefile"
HYLE_DIR = \$(REPO_ROOT)/external/hyle
EXTRA_CFLAGS += -I../common -I\$(HYLE_DIR)/c/libhyle-bud/include
EXTRA_LDLIBS += -lbud -laxil-auth -L\$(HYLE_DIR)/c/libhyle-bud/lib -lhyle-bud -Wl,-rpath,\$(HYLE_DIR)/c/libhyle-bud/lib
MOD_NAME = ${MOD_NAME}
DIRS = var/${MOD_NAME}
REPO_ROOT != cd ../.. && pwd
include \$(REPO_ROOT)/build.mk
EOF

# 2. fields.h
cat <<EOF > "$MOD_DIR/fields.h"
#ifndef ${MOD_UPPER}_FIELDS_H
#define ${MOD_UPPER}_FIELDS_H

#include <hyle/schema.h>
#include <stddef.h>

typedef struct {
	char id[128];
	char title[256];
	char owner[32];
} ${MOD_NAME}_cache_t;

static const hyle_schema_desc_t ${MOD_NAME}_fields[] = {
	FIELD_TEXT(id, ${MOD_NAME}_cache_t, id, .writable = 1, .in_meta = 0),
	FIELD_TEXT(title, ${MOD_NAME}_cache_t, title, .required = 1, .min_length = 1),
	FIELD_TEXT(owner, ${MOD_NAME}_cache_t, owner, .kind = BUD_EXCLUDE),
	FIELD_END
};

#define ${MOD_UPPER}_FIELD_COUNT (sizeof(${MOD_NAME}_fields) / sizeof(${MOD_NAME}_fields[0]) - 1)

#ifndef __wasm__
static const source_list_field_t ${MOD_NAME}_list_fields[] = {
	{ "title", "Title" },
	{ "owner", "Owner" },
};

static const source_list_view_t ${MOD_NAME}_list_view = {
	"${MOD_NAME}",
	${MOD_NAME}_list_fields,
	sizeof(${MOD_NAME}_list_fields) / sizeof(${MOD_NAME}_list_fields[0]),
	NULL,
	NULL,
	NULL,
	NULL,
};
#endif

#endif
EOF

# 3. <name>.c
cat <<EOF > "$MOD_DIR/${MOD_NAME}.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ttypt/xy-mod.h>
#include <ttypt/axil.h>
#include <ttypt/qmap.h>

#include "../index/index.h"
#include "../common/common.h"
#include "../source/source.h"
#include "../auth/auth.h"
#include "fields.h"

#include "ux/all.c"

static int
${MOD_NAME}_detail_auth(int fd, char *body, const item_ctx_t *ctx, void *user_data)
{
	(void)body;
	(void)user_data;
	char owner[64] = { 0 };
	${MOD_NAME}_cache_t meta;

	source_meta_read(
		ctx->item_path, ${MOD_NAME}_fields, ${MOD_UPPER}_FIELD_COUNT,
		&meta, sizeof(meta));
	if (!meta.title[0])
		return not_found(fd, "${DISPLAY_NAME} not found");

	item_owner_read(ctx->item_path, owner, sizeof(owner));

	bud_node *frag = ${MOD_NAME}_render_detail_body(owner, ctx->id);
	if (!frag)
		return server_error(fd, "OOM");

	return site_ui_respond_item_detail(fd, ctx, "${MOD_NAME}", meta.title, frag);
}

static int ${MOD_NAME}_detail_handler(int fd, char *body)
{
	return with_module_item_access(
		fd, body, "${MOD_NAME}", 0, NULL, NULL, ${MOD_NAME}_detail_auth, NULL);
}

void xy_install(void)
{
	xy_load("./mods/index/index");

	index_module_init(&(index_module_def_t){
		.name = "${MOD_NAME}",
		.display_name = "${DISPLAY_NAME}",
		.schema = ${MOD_NAME}_fields,
		.field_count = ${MOD_UPPER}_FIELD_COUNT,
		.record_size = sizeof(${MOD_NAME}_cache_t),
		.items_path = "var/${MOD_NAME}",
		.list_view = &${MOD_NAME}_list_view,
		.handlers = { .detail = ${MOD_NAME}_detail_handler },
	});
}
EOF

# 4. ux/detail.c
cat <<EOF > "$MOD_DIR/ux/detail.c"
#include "bud/bud.h"
#include "bud/bud_jsx.h"
#include "../../common/ux/site_ui.c"

bud_node *${MOD_NAME}_render_detail_body(const char *owner, const char *id)
{
	(void)id;
	return lx_el("div",
		lx_attr("class", "container mx-auto p-4"),
		lx_el("p", lx_text(owner && owner[0] ? owner : "Anonymous"))
	).data.node;
}
EOF

# 5. ux/all.c
cat <<EOF > "$MOD_DIR/ux/all.c"
#include "detail.c"
EOF

# 6. test.sh
cat <<EOF > "$MOD_DIR/test.sh"
#!/bin/sh
set -e
echo "Testing ${MOD_NAME}..."
curl -s --max-time 2 http://localhost:8080/${MOD_NAME}/ >/dev/null || exit 1
echo "OK: ${MOD_NAME}"
EOF
chmod +x "$MOD_DIR/test.sh"

# 7. Register in mods.load if not present
if ! grep -q "^${MOD_NAME}$" "${REPO_ROOT}/mods.load" 2>/dev/null; then
  echo "${MOD_NAME}" >> "${REPO_ROOT}/mods.load"
  echo "Registered '${MOD_NAME}' in mods.load."
fi

# 8. Update compile_commands.json
"${REPO_ROOT}/scripts/gen-compile-commands.sh" >/dev/null 2>&1 || true

echo "Successfully created module '${MOD_NAME}' in mods/${MOD_NAME}/"
echo "Run 'make' to compile."
