#include <stdlib.h>
#include <string.h>

#include <ttypt/ndx-mod.h>

#include "common_internal.h"

NDX_LISTENER(int, respond_with_item_json, int, fd, const item_ctx_t *, ctx,
	const meta_field_t *, fields, size_t, count, const char *, extra_json)
{
	json_object_t *jo = json_object_new(0);
	char *json;
	int owner;
	int result;

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

	result = core_post_json(fd, json);
	free(json);
	return result;
}
