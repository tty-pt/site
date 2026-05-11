/* Unified field definition for song module.
 * One table serves both server (source generators) and WASM (bud_state_apply).
 *
 * Kinds:
 *   0 = SF_RECORD     — record field, include as-is in state JSON
 *   1 = SF_EXCLUDE    — record field, exclude from state JSON
 *   2 = SF_REF_DISPLAY — record field, resolve multi-ref IDs to display names
 *   3 = SONG_OVERLAY_INT  — computed int, overlay onto state JSON
 *   4 = SONG_OVERLAY_STR  — computed string, overlay onto state JSON
 */

#ifndef SONG_FIELDS_H
#define SONG_FIELDS_H

#include "bud/bud.h"
#include <stddef.h>
#include "../common/field_macros.h"

typedef struct {
	char id[128];
	char title[256];
	char type[2048];
	char author[256];
	char yt[512];
	char audio[512];
	char pdf[512];
	char owner[32];
} song_cache_t;

typedef struct {
	song_cache_t cache;
	int transpose;
	int use_bemol;
	int use_latin;
	int show_media;
	char chord_html[65536];
	int original_key;
	char save_url[512];
	int is_owner;
	char page_user[64];
	char path[256];
	int zoom;
} app_state_t;

/* ── Song type entity ────────────────────────────────────────── */

typedef struct {
	char id[64];
	char name[256];
} song_type_cache_t;

static const bud_field_desc_t song_type_fields[] = {
	REC_FIELD(id, song_type_cache_t, id, 64, 1, 0, 0, 0),
	REC_FIELD(name, song_type_cache_t, name, 256, 1, 0, 0, 1),
	INVERSE_FIELD(songs, "song.items", "type"),
	FIELD_END
};

#define SONG_TYPE_FIELD_COUNT                                                  \
	(sizeof(song_type_fields) / sizeof(song_type_fields[0]) - 1)

/* ── Song record fields ──────────────────────────────────────── */

static const bud_field_desc_t song_fields[] = {
	REC_FIELD(id, song_cache_t, id, 128, 1, 0, 0, 0),
	REC_FIELD(title, song_cache_t, title, 256, 1, 1, 1, 1),
	MULTI_REF_FIELD(
	        type, song_cache_t, type, 2048, "song.types", "songs", 1),
	REC_FIELD(author, song_cache_t, author, 256, 1, 0, 0, 1),
	REC_FIELD(yt, song_cache_t, yt, 512, 1, 0, 0, 1),
	REC_FIELD(audio, song_cache_t, audio, 512, 1, 0, 0, 1),
	REC_FIELD(pdf, song_cache_t, pdf, 512, 1, 0, 0, 1),
	EXCL_FIELD_V(data, BUD_QM_VSTR, 1, 0),
	EXCL_FIELD(owner, song_cache_t, owner, 32, BUD_QM_STR, 0),
	OVERLAY_INT(t, app_state_t, transpose),
	OVERLAY_INT(b, app_state_t, use_bemol),
	OVERLAY_INT(l, app_state_t, use_latin),
	OVERLAY_INT(m, app_state_t, show_media),
	OVERLAY_INT(zoom, app_state_t, zoom),
	OVERLAY_INT(key, app_state_t, original_key),
	OVERLAY_INT(owner, app_state_t, is_owner),
	OVERLAY_STR(user, app_state_t, page_user, 64),
	OVERLAY_STR(path, app_state_t, path, 256),
	OVERLAY_STR(save, app_state_t, save_url, 512),
	FIELD_END
};

#define SONG_FIELD_COUNT (sizeof(song_fields) / sizeof(song_fields[0]) - 1)

#endif
