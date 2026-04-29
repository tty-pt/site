#include <stdlib.h>
#include <string.h>

#include <ttypt/ndx-mod.h>

#include "common_internal.h"
#include "../ssr/ssr.h"

NDX_LISTENER(int, respond_with_item_json, int, fd, const item_ctx_t *, ctx,
	const meta_field_t *, fields, size_t, count, const char *, extra_json)
{
	json_object_t *jo = json_object_new(0);
	char *json;
	int owner;
	int result;
	char module[64] = {0};
	char uri[512] = {0};
	const char *action = "detail";

	if (!jo)
		return respond_error(fd, 500, "OOM");

	json_object_kv_str(jo, "id", ctx->id);
	for (size_t i = 0; i < count; i++)
		json_object_kv_str(jo, fields[i].name, fields[i].buf);

	owner = (ctx->username && ctx->username[0])
		? item_check_ownership(ctx->item_path, ctx->username) : 0;
	json_object_kv_bool(jo, "owner", owner);

	if (extra_json && extra_json[0])
		json_object_append_fragment(jo, extra_json, strlen(extra_json));

	json = json_object_finish(jo);
	if (!json)
		return respond_error(fd, 500, "Failed to finish JSON");

	/* Extract module from items/<module>/items/... or from DOCUMENT_URI */
	if (strncmp(ctx->item_path, "items/", 6) == 0) {
		const char *m = ctx->item_path + 6;
		const char *slash = strchr(m, '/');
		if (slash) {
			size_t len = (size_t)(slash - m);
			if (len < sizeof(module)) {
				memcpy(module, m, len);
				module[len] = '\0';
			}
		}
	}

	if (!module[0]) {
		ndc_env_get(fd, uri, "DOCUMENT_URI");
		const char *m = uri;
		if (*m == '/') m++;
		const char *slash = strchr(m, '/');
		if (slash) {
			size_t len = (size_t)(slash - m);
			if (len < sizeof(module)) {
				memcpy(module, m, len);
				module[len] = '\0';
			}
		} else {
			strncpy(module, m, sizeof(module) - 1);
		}
	}

	ndc_env_get(fd, uri, "DOCUMENT_URI");
	if (strstr(uri, "/edit"))
		action = "edit";

	result = ssr_render_item(fd, module, ctx->id, action, json);
	free(json);
	return result;
}
