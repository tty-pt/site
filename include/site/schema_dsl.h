#ifndef SITE_SCHEMA_DSL_H
#define SITE_SCHEMA_DSL_H

#include <hyle/schema.h>
#include <bud/bud.h>
#include <bud/bud_state.h>
#include <hyle-bud/hyle-bud.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Schema-driven summary and form helpers for site modules.
 */
static inline bud_node *bind_schema_summary(const hyle_schema_desc_t *schema, const void *record)
{
	if (!schema || !record)
		return NULL;
	char meta_html[2048] = { 0 };
	for (const hyle_schema_desc_t *d = schema; d->key != NULL; d++) {
		if (!d->in_meta)
			continue;
		if (strcmp(d->key, "id") == 0 || strcmp(d->key, "title") == 0 ||
		    strcmp(d->key, "yt") == 0 || strcmp(d->key, "audio") == 0 ||
		    strcmp(d->key, "pdf") == 0)
			continue;
		const char *val = (const char *)((const char *)record + d->offset);
		if (val && val[0]) {
			snprintf(meta_html + strlen(meta_html),
			         sizeof(meta_html) - strlen(meta_html),
			         "<div class='italic whitespace-pre-wrap text-xs text-muted'>%s</div>",
			         val);
		}
	}
	if (meta_html[0]) {
		return bud_tpl("<div class='flex flex-col gap-1 min-w-0'>%raw</div>", meta_html);
	}
	return bud_element("div");
}

static inline bud_node *bind_schema_form(const hyle_schema_desc_t *schema, const void *record, const char *action_url)
{
	return hyle_bud_form(schema, record, action_url, NULL, "Save", NULL, NULL, NULL);
}

/*
 * Macro definitions for SITE_DECLARE_MODULE_SCHEMA
 */
#define SITE_DECLARE_MODULE_SCHEMA(mod, SCHEMA_DEF)                            \
	typedef struct mod##_cache_s {                                         \
		char id[128];                                                  \
		char title[256];                                               \
		char author[256];                                              \
		char type[2048];                                               \
		int transpose;                                                 \
		int zoom;                                                      \
		char yt[512];                                                  \
		char audio[512];                                               \
		char pdf[512];                                                 \
		char owner[64];                                                \
	} mod##_cache_t;                                                       \
                                                                               \
	typedef struct mod##_state_s {                                         \
		mod##_cache_t cache;                                           \
		int transpose;                                                 \
		int use_latin;                                                 \
		int show_media;                                                \
		char chord_html[65536];                                        \
		int original_key;                                              \
		char save_url[512];                                            \
		int is_owner;                                                  \
		char page_user[64];                                            \
		char path[256];                                                \
		int zoom;                                                      \
		char lang[32];                                                 \
	} mod##_state_t;                                                       \
                                                                               \
	static const hyle_schema_desc_t mod##_hyle_schema[] = {                \
		FIELD_TEXT(id, mod##_cache_t),                                 \
		FIELD_TEXT(title, mod##_cache_t, .required = 1, .min_length = 1, .in_meta = 1), \
		FIELD_ARRAY(FIELD_REF, type, mod##_cache_t, #mod ".types",     \
		            .ref_inverse = #mod "s", .filter_style = "dropdown", \
		            .filter_mode = "and", .allow_add = 1, .in_meta = 1), \
		FIELD_REF(author, mod##_cache_t, #mod ".authors",              \
		          .ref_inverse = #mod "s", .filter_style = "dropdown", \
		          .allow_add = 1, .in_meta = 1),                       \
		FIELD_TEXT(yt, mod##_cache_t, .in_meta = 1),                   \
		FIELD_TEXT(audio, mod##_cache_t, .in_meta = 1),                \
		FIELD_TEXT(pdf, mod##_cache_t, .in_meta = 1),                  \
		FIELD_FILE(data, "data.txt"),                                  \
		FIELD_EXCL(owner, mod##_cache_t),                              \
		FIELD_DERIVED(lyrics, #mod ".lyrics_from_data"),               \
		FIELD_END                                                      \
	};                                                                     \
	static const hyle_schema_desc_t *const mod##_fields = mod##_hyle_schema; \
                                                                               \
	static const bud_field_desc_t mod##_bud_fields[] = {                   \
		OVERLAY_STR(save_url, mod##_state_t, save_url, 512),           \
		OVERLAY_STR(page_user, mod##_state_t, page_user, 64),          \
		OVERLAY_STR(path, mod##_state_t, path, 256),                   \
		OVERLAY_STR(lang, mod##_state_t, lang, 32),                    \
		OVERLAY_INT(transpose, mod##_state_t, transpose),              \
		OVERLAY_INT(use_latin, mod##_state_t, use_latin),              \
		OVERLAY_INT(show_media, mod##_state_t, show_media),            \
		OVERLAY_INT(zoom, mod##_state_t, zoom),                        \
		OVERLAY_INT(original_key, mod##_state_t, original_key),        \
		OVERLAY_INT(is_owner, mod##_state_t, is_owner),                \
		FIELD_END                                                      \
	};                                                                     \
                                                                               \
	static inline int mod##_wire_init(mod##_state_t *st, const char *json, size_t len) \
	{                                                                      \
		if (!st) return -1;                                            \
		wasm_state_init(json, (int)len, mod##_bud_fields, st);         \
		hyle_bud_state_apply_len(&st->cache, mod##_hyle_schema, json, len); \
		return 0;                                                      \
	}                                                                      \
                                                                               \
	static inline int mod##_wire_serialize(const mod##_state_t *st, char *out, size_t max_len) \
	{                                                                      \
		if (!st || !out || max_len == 0) return -1;                    \
		return snprintf(out, max_len,                                  \
			"{\"id\":\"%s\",\"title\":\"%s\",\"transpose\":%d,\"zoom\":%d,\"use_latin\":%d}", \
			st->cache.id, st->cache.title, st->transpose, st->zoom, st->use_latin); \
	}

#ifdef __cplusplus
}
#endif

#endif /* SITE_SCHEMA_DSL_H */
