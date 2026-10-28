#include "nd_ui_internal.h"
#include <ttypt/axil-ws.h>

void nd_state_serialize(const nd_client_state_t *st, char *buf, size_t cap)
{
	if (!st || !buf || cap == 0)
		return;

	/* Worst case every byte of map_buf is escaped to two. */
	const size_t esc_cap = sizeof(st->map_buf) * 2 + 4;
	char *esc_map = calloc(1, esc_cap);
	if (!esc_map)
		return;
	size_t em = 0;
	for (size_t i = 0; st->map_buf[i] && em + 4 < esc_cap; i++) {
		if (st->map_buf[i] == '\n') {
			esc_map[em++] = '\\';
			esc_map[em++] = 'n';
		} else if (st->map_buf[i] == '"' || st->map_buf[i] == '\\') {
			esc_map[em++] = '\\';
			esc_map[em++] = st->map_buf[i];
		} else {
			esc_map[em++] = st->map_buf[i];
		}
	}

	char map_hex[ND_MAP_HEX_CAP];
	map_hex[0] = '\0';
	static const char hex_chars[] = "0123456789abcdef";
	if (st->map_data.width > 0) {
		const uint8_t *raw = (const uint8_t *)&st->map_data;
		for (size_t i = 0; i < sizeof(nd_map_data_t); i++) {
			map_hex[i * 2]     = hex_chars[(raw[i] >> 4) & 0x0F];
			map_hex[i * 2 + 1] = hex_chars[raw[i] & 0x0F];
		}
		map_hex[sizeof(nd_map_data_t) * 2] = '\0';
	}

	int offset = snprintf(buf, cap,
		"{\"me\":%u,\"auth_failed\":%d,\"user\":\"%s\",\"map_buf\":\"%s\","
		"\"map_hex\":\"%s\","
		"\"active_modal\":\"%s\","
		"\"selected_item_ref\":%u,"
		"\"target_ref\":%u,"
		"\"preferred_driver\":\"%s\","
		"\"preferred_map_driver\":\"%s\","
		"\"layout_mode\":%u,"
		"\"pres_flags\":%u,"
		"\"tod\":%u,"
		"\"room\":{\"ref\":%u,\"name\":\"%s\",\"desc\":\"%s\",\"exits\":%u},"
		"\"room_ref\":%u,\"room_name\":\"%s\",\"room_desc\":\"%s\",\"room_exits\":%u,\"room_art\":\"%s\","
		"\"gauges\":[",
		st->me, st->auth_failed, st->user, esc_map,
		map_hex,
		st->active_modal,
		st->selected_item_ref,
		st->target_ref,
		st->preferred_driver,
		st->preferred_map_driver[0] ? st->preferred_map_driver : "ascii-bud",
		(unsigned)st->layout_mode,
		(unsigned)st->pres_flags,
		st->tod,
		st->room.ref, st->room.name, st->room.desc, st->room.exits,
		st->room.ref, st->room.name, st->room.desc, st->room.exits, st->room.art);
	free(esc_map);

	if (offset > 0 && (size_t)offset < cap) {
		size_t gcur = (size_t)offset;
		int first = 1;
		for (size_t i = 0; i < ND_MAX_GAUGES && gcur + 96 < cap; i++) {
			if (!st->gauges[i].name[0])
				continue;
			int gn = snprintf(buf + gcur, cap - gcur,
				"%s{\"n\":\"%s\",\"val\":%u,\"max\":%u}",
				first ? "" : ",", st->gauges[i].name,
				st->gauges[i].bar.val, st->gauges[i].bar.max);
			if (gn > 0) gcur += (size_t)gn;
			first = 0;
		}
		int en = snprintf(buf + gcur, cap - gcur, "],\"entities\":[");
		if (en > 0) offset = (int)(gcur + (size_t)en);
	}

	if (offset < 0 || (size_t)offset >= cap)
		return;

	size_t cur = (size_t)offset;
	for (size_t i = 0; i < st->entity_count && cur + 160 < cap; i++) {
		const nd_entity_desc_t *e = &st->entities[i];
		int n = snprintf(buf + cur, cap - cur,
			"%s{\"ref\":%u,\"name\":\"%s\",\"ch\":\"%c\",\"fg\":%u,\"loc\":%u,\"art\":\"%s\"}",
			(i > 0) ? "," : "",
			e->ref, e->name, e->ch ? e->ch : '?', e->fg, e->location, e->art);
		if (n > 0) cur += (size_t)n;
	}

	if (cur + 3 < cap) {
		buf[cur++] = ']';

		/* Actions array */
		if (cur + 16 < cap) {
			int an = snprintf(buf + cur, cap - cur, ",\"actions\":[");
			if (an > 0) cur += (size_t)an;
			for (size_t i = 0; i < st->action_count && cur + 128 < cap; i++) {
				const nd_action_desc_t *a = &st->actions[i];
				int n = snprintf(buf + cur, cap - cur,
					"%s{\"id\":%u,\"label\":\"%s\",\"icon\":\"%s\"}",
					(i > 0) ? "," : "",
					a->id, a->label, a->icon);
				if (n > 0) cur += (size_t)n;
			}
			if (cur + 1 < cap) {
				buf[cur++] = ']';
			}
		}

		/* Allow tenant modules to contribute state slices */
		char slice_buf[1024] = {0};
		nd_ui_glue_state_serialize(st, slice_buf, sizeof(slice_buf));
		size_t slen = strlen(slice_buf);
		if (slen > 0 && cur + slen + 16 < cap) {
			int n = snprintf(buf + cur, cap - cur, ",\"slices\":{%s}", slice_buf);
			if (n > 0) cur += (size_t)n;
		}

		buf[cur++] = '}';
		buf[cur] = '\0';
	}
}

/* Compact JSON sequential scanner for entities array */
void parse_json_entities(const char *json, nd_client_state_t *st)
{
	if (!json || !st)
		return;
	const char *arr = strstr(json, "\"entities\":");
	if (!arr)
		return;
	arr = strchr(arr, '[');
	if (!arr)
		return;
	arr++;

	size_t count = 0;
	while (*arr && *arr != ']' && count < ND_MAX_ENTITIES) {
		const char *obj = strchr(arr, '{');
		if (!obj)
			break;
		const char *obj_end = strchr(obj, '}');
		if (!obj_end)
			break;

		/* Extract ref */
		const char *p = strstr(obj, "\"ref\":");
		if (p && p < obj_end) {
			st->entities[count].ref = (uint32_t)strtoul(p + 6, NULL, 10);
		}
		/* Extract name */
		p = strstr(obj, "\"name\":\"");
		if (p && p < obj_end) {
			p += 8;
			size_t nlen = 0;
			while (p + nlen < obj_end && p[nlen] != '"' && nlen < sizeof(st->entities[count].name) - 1) {
				st->entities[count].name[nlen] = p[nlen];
				nlen++;
			}
			st->entities[count].name[nlen] = '\0';
		}
		/* Extract ch */
		p = strstr(obj, "\"ch\":\"");
		if (p && p < obj_end) {
			st->entities[count].ch = p[6];
		}
		/* Extract fg */
		p = strstr(obj, "\"fg\":");
		if (p && p < obj_end) {
			st->entities[count].fg = (uint16_t)strtoul(p + 5, NULL, 10);
		}
		/* Extract loc */
		p = strstr(obj, "\"loc\":");
		if (p && p < obj_end) {
			st->entities[count].location = (uint32_t)strtoul(p + 6, NULL, 10);
		}
		/* Extract art */
		p = strstr(obj, "\"art\":\"");
		if (p && p < obj_end) {
			p += 7;
			size_t alen = 0;
			while (p + alen < obj_end && p[alen] != '"' && alen < sizeof(st->entities[count].art) - 1) {
				st->entities[count].art[alen] = p[alen];
				alen++;
			}
			st->entities[count].art[alen] = '\0';
		}

		count++;
		arr = obj_end + 1;
	}
	st->entity_count = count;
}

void parse_json_actions(const char *json, nd_client_state_t *st)
{
	if (!json || !st)
		return;
	const char *arr = strstr(json, "\"actions\":");
	if (!arr)
		return;
	arr = strchr(arr, '[');
	if (!arr)
		return;
	arr++;

	size_t count = 0;
	while (*arr && *arr != ']' && count < ND_MAX_ACTIONS) {
		const char *obj = strchr(arr, '{');
		if (!obj)
			break;
		const char *obj_end = strchr(obj, '}');
		if (!obj_end)
			break;

		const char *p = strstr(obj, "\"id\":");
		if (p && p < obj_end) {
			st->actions[count].id = (uint32_t)strtoul(p + 5, NULL, 10);
		}
		p = strstr(obj, "\"label\":\"");
		if (p && p < obj_end) {
			p += 9;
			size_t nlen = 0;
			while (p + nlen < obj_end && p[nlen] != '"' && nlen < sizeof(st->actions[count].label) - 1) {
				st->actions[count].label[nlen] = p[nlen];
				nlen++;
			}
			st->actions[count].label[nlen] = '\0';
		}
		p = strstr(obj, "\"icon\":\"");
		if (p && p < obj_end) {
			p += 8;
			size_t nlen = 0;
			while (p + nlen < obj_end && p[nlen] != '"' && nlen < sizeof(st->actions[count].icon) - 1) {
				st->actions[count].icon[nlen] = p[nlen];
				nlen++;
			}
			st->actions[count].icon[nlen] = '\0';
		}

		count++;
		arr = obj_end + 1;
	}
	st->action_count = count;
}

static void parse_gauge_elem(const char *elem, size_t elen, void *user)
{
	nd_client_state_t *st = (nd_client_state_t *)user;
	char name[ND_GAUGE_NAME_MAX] = {0};
	bud_json_str_len(elem, elen, "n", name, sizeof(name));
	if (!name[0])
		return;
	nd_client_gauge_set(st, name,
		(uint16_t)bud_json_int_len(elem, elen, "val", 0),
		(uint16_t)bud_json_int_len(elem, elen, "max", 0));
}

void nd_negotiate_regional_driver(const nd_client_state_t *st)
{
	(void)st;
}

ISO_EXPORT(void, wasm_init, const char *json, int len)
{
	nd_ui_init_defaults();
	memset(&client_state, 0, sizeof(client_state));
	client_state.pres_flags = ND_REGION_ALLOW_CHOICE;
	snprintf(client_state.preferred_map_driver, sizeof(client_state.preferred_map_driver), "ascii-bud");
	nd_ui_glue_state_default(&client_state);

	if (json && len > 0) {
		client_state.me = (uint32_t)bud_json_int(json, "me", 0);
		client_state.auth_failed = (int)bud_json_int(json, "auth_failed", 0);
		bud_json_str(json, "user", client_state.user, sizeof(client_state.user));
		bud_json_str(json, "map_buf", client_state.map_buf, sizeof(client_state.map_buf));

		char map_hex[ND_MAP_HEX_CAP] = {0};
		bud_json_str(json, "map_hex", map_hex, sizeof(map_hex));
		if (map_hex[0]) {
			uint8_t *raw = (uint8_t *)&client_state.map_data;
			size_t hex_len = strlen(map_hex);
			if (hex_len == sizeof(nd_map_data_t) * 2) {
				for (size_t i = 0; i < sizeof(nd_map_data_t); i++) {
					char h1 = map_hex[i * 2];
					char h2 = map_hex[i * 2 + 1];
					uint8_t b1 = (h1 >= 'a') ? (h1 - 'a' + 10) : (h1 >= 'A') ? (h1 - 'A' + 10) : (h1 - '0');
					uint8_t b2 = (h2 >= 'a') ? (h2 - 'a' + 10) : (h2 >= 'A') ? (h2 - 'A' + 10) : (h2 - '0');
					raw[i] = (uint8_t)((b1 << 4) | b2);
				}
			}
		}

		client_state.tod = (uint32_t)bud_json_int(json, "tod", 0);
		client_state.selected_item_ref = (uint32_t)bud_json_int(json, "selected_item_ref", 0);
		client_state.target_ref = (uint32_t)bud_json_int(json, "target_ref", 0);
		bud_json_str(json, "active_modal", client_state.active_modal, sizeof(client_state.active_modal));
		nd_ui_set_active_modal(client_state.active_modal);
		bud_json_str(json, "preferred_driver", client_state.preferred_driver, sizeof(client_state.preferred_driver));
		bud_json_str(json, "preferred_map_driver", client_state.preferred_map_driver, sizeof(client_state.preferred_map_driver));
		client_state.layout_mode = (uint8_t)bud_json_int(json, "layout_mode", 0);
		client_state.pres_flags = (uint8_t)bud_json_int(json, "pres_flags", ND_REGION_ALLOW_CHOICE);

		/* Rehydrate room info */
		client_state.room.ref = (uint32_t)bud_json_int(json, "room_ref", 0);
		client_state.room.exits = (uint32_t)bud_json_int(json, "room_exits", 0);
		bud_json_str(json, "room_name", client_state.room.name, sizeof(client_state.room.name));
		bud_json_str(json, "room_desc", client_state.room.desc, sizeof(client_state.room.desc));
		bud_json_str(json, "room_art", client_state.room.art, sizeof(client_state.room.art));

		/* Rehydrate generic gauges, then let each module restore its own state */
		bud_json_array_for_each_key_len(json, (size_t)len, "gauges", parse_gauge_elem, &client_state);
		on_nd_state_deserialize(json, &client_state);

		/* Rehydrate entities and actions lists */
		parse_json_entities(json, &client_state);
		parse_json_actions(json, &client_state);
	}

	/* Immediately synchronize scene graph before initial render frame */
	nd_scene_sync(&g_scene, &client_state);

	ws_connect("/nd", 3);
}

ISO_EXPORT(void, wasm_term_on_input, const char *buf, size_t len)
{
	if (buf && len > 0)
		ws_send(buf, len);
}

ISO_EXPORT(void, wasm_ws_on_open, void)
{
}

ISO_EXPORT(void, wasm_ws_on_close, int code)
{
	(void)code;
}

ISO_EXPORT(void, wasm_ws_on_channel, uint8_t channel, const uint8_t *buf, size_t len)
{
	if (!buf || len == 0)
		return;

	if (channel == 1 || (buf[0] == ND_BCP_MAGIC_0 && buf[1] == ND_BCP_MAGIC_1)) {
		int ret = nd_bcp_decode(buf, len, &client_state);
		if (ret > 0) {
			nd_scene_sync(&g_scene, &client_state);
			bud_app_set_state();
			nd_map_canvas_redraw(&client_state);
		}
		return;
	}
}

ISO_EXPORT(void, wasm_ws_on_message, const uint8_t *buf, size_t len)
{
	if (!buf || len == 0)
		return;

	if (buf[0] == ND_BCP_MAGIC_0 && buf[1] == ND_BCP_MAGIC_1) {
		wasm_ws_on_channel(1, buf, len);
	} else {
		wasm_ws_on_channel(0, buf, len);
	}
}
