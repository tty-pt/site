#ifndef COMMON_LIST_BIN_C
#define COMMON_LIST_BIN_C

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "list_state.h"

static uint32_t
hylt_put_arena_str(char *arena, size_t arena_cap, size_t *arena_len, const char *s)
{
	size_t slen;
	uint32_t off;

	if (!s || !*s)
		return 0; /* offset 0 is always "" */

	slen = strlen(s);
	if (*arena_len + slen + 1 > arena_cap)
		return (uint32_t)0xFFFFFFFF;

	off = (uint32_t)*arena_len;
	memcpy(arena + off, s, slen + 1);
	*arena_len += slen + 1;
	return off;
}

static const char *
hylt_get_arena_str(const char *arena, size_t arena_sz, uint32_t offset)
{
	if (offset >= arena_sz)
		return "";
	return arena + offset;
}

/* Serialize list_state_t into canonical HYLT binary wire format.
 * Returns the written byte length (> 0) on success, or -1 on error. */
int list_state_to_bin(const list_state_t *state, void *out, size_t out_sz)
{
	hylt_header_t *hdr;
	hylt_meta_t *meta;
	hylt_col_wire_t *col_wire;
	hylt_opt_wire_t *opt_wire;
	uint32_t *id_offsets;
	uint32_t *cell_offsets;
	char *arena;
	size_t fixed_hdr;
	size_t cols_sz;
	size_t opts_sz;
	size_t ids_sz;
	size_t cells_sz;
	size_t fixed_total;
	size_t arena_offset;
	size_t arena_cap;
	size_t arena_len;
	int i, j;

	if (!state || !out)
		return -1;

	if (state->ncols < 0 || state->ncols > LIST_MAX_COLS ||
	    state->nids < 0 || state->nids > LIST_MAX_ROWS ||
	    state->nopts < 0 || state->nopts > LIST_MAX_OPTS)
		return -1;

	fixed_hdr = sizeof(hylt_header_t) + sizeof(hylt_meta_t);
	cols_sz = (size_t)state->ncols * sizeof(hylt_col_wire_t);
	opts_sz = (size_t)state->nopts * sizeof(hylt_opt_wire_t);
	ids_sz = (size_t)state->nids * sizeof(uint32_t);
	cells_sz = (size_t)state->nids * (size_t)state->ncols * sizeof(uint32_t);
	fixed_total = fixed_hdr + cols_sz + opts_sz + ids_sz + cells_sz;

	/* 4-byte align arena start */
	arena_offset = (fixed_total + 3) & ~((size_t)3);
	if (out_sz < arena_offset + 1)
		return -1;

	arena = (char *)out + arena_offset;
	arena_cap = out_sz - arena_offset;
	arena[0] = '\0';
	arena_len = 1;

	hdr = (hylt_header_t *)out;
	meta = (hylt_meta_t *)((uint8_t *)out + sizeof(hylt_header_t));
	col_wire = (hylt_col_wire_t *)((uint8_t *)meta + sizeof(hylt_meta_t));
	opt_wire = (hylt_opt_wire_t *)((uint8_t *)col_wire + cols_sz);
	id_offsets = (uint32_t *)((uint8_t *)opt_wire + opts_sz);
	cell_offsets = (uint32_t *)((uint8_t *)id_offsets + ids_sz);

	/* Populate table string metadata */
	meta->module_offset = hylt_put_arena_str(arena, arena_cap, &arena_len, state->module);
	meta->display_name_offset = hylt_put_arena_str(arena, arena_cap, &arena_len, state->display_name);
	meta->content_field_offset = hylt_put_arena_str(arena, arena_cap, &arena_len, state->content_field);
	meta->content_label_offset = hylt_put_arena_str(arena, arena_cap, &arena_len, state->content_label);
	meta->content_placeholder_offset = hylt_put_arena_str(arena, arena_cap, &arena_len, state->content_placeholder);
	meta->username_offset = hylt_put_arena_str(arena, arena_cap, &arena_len, state->username);
	meta->query_offset = hylt_put_arena_str(arena, arena_cap, &arena_len, state->query);
	meta->q_offset = hylt_put_arena_str(arena, arena_cap, &arena_len, state->q);
	meta->sort_field_offset = hylt_put_arena_str(arena, arena_cap, &arena_len, state->sort_field);
	meta->lang_offset = hylt_put_arena_str(arena, arena_cap, &arena_len, state->lang);

	if (meta->module_offset == (uint32_t)0xFFFFFFFF ||
	    meta->display_name_offset == (uint32_t)0xFFFFFFFF ||
	    meta->content_field_offset == (uint32_t)0xFFFFFFFF ||
	    meta->content_label_offset == (uint32_t)0xFFFFFFFF ||
	    meta->content_placeholder_offset == (uint32_t)0xFFFFFFFF ||
	    meta->username_offset == (uint32_t)0xFFFFFFFF ||
	    meta->query_offset == (uint32_t)0xFFFFFFFF ||
	    meta->q_offset == (uint32_t)0xFFFFFFFF ||
	    meta->sort_field_offset == (uint32_t)0xFFFFFFFF ||
	    meta->lang_offset == (uint32_t)0xFFFFFFFF)
		return -1;

	/* Populate columns */
	for (i = 0; i < state->ncols; i++) {
		const list_col_t *c = &state->cols[i];
		hylt_col_wire_t *w = &col_wire[i];

		w->key_offset = hylt_put_arena_str(arena, arena_cap, &arena_len, c->key);
		w->label_offset = hylt_put_arena_str(arena, arena_cap, &arena_len, c->label);
		w->target_source_offset = hylt_put_arena_str(arena, arena_cap, &arena_len, c->target_source);
		w->filter_offset = hylt_put_arena_str(arena, arena_cap, &arena_len, c->filter);
		w->current_offset = hylt_put_arena_str(arena, arena_cap, &arena_len, c->current);
		w->type = (uint16_t)c->type;
		w->opt_start = (uint16_t)c->opt_start;
		w->opt_count = (uint16_t)c->opt_count;
		w->reserved = 0;

		if (w->key_offset == (uint32_t)0xFFFFFFFF ||
		    w->label_offset == (uint32_t)0xFFFFFFFF ||
		    w->target_source_offset == (uint32_t)0xFFFFFFFF ||
		    w->filter_offset == (uint32_t)0xFFFFFFFF ||
		    w->current_offset == (uint32_t)0xFFFFFFFF)
			return -1;
	}

	/* Populate options */
	for (i = 0; i < state->nopts; i++) {
		const list_opt_t *o = &state->opts[i];
		hylt_opt_wire_t *w = &opt_wire[i];

		w->id_offset = hylt_put_arena_str(arena, arena_cap, &arena_len, o->id);
		w->label_offset = hylt_put_arena_str(arena, arena_cap, &arena_len, o->label);
		if (w->id_offset == (uint32_t)0xFFFFFFFF ||
		    w->label_offset == (uint32_t)0xFFFFFFFF)
			return -1;
	}

	/* Populate row IDs */
	for (i = 0; i < state->nids; i++) {
		id_offsets[i] = hylt_put_arena_str(arena, arena_cap, &arena_len, state->ids[i]);
		if (id_offsets[i] == (uint32_t)0xFFFFFFFF)
			return -1;
	}

	/* Populate cells */
	for (i = 0; i < state->nids; i++) {
		for (j = 0; j < state->ncols; j++) {
			int idx = i * state->ncols + j;
			cell_offsets[idx] = hylt_put_arena_str(arena, arena_cap, &arena_len, state->values[idx]);
			if (cell_offsets[idx] == (uint32_t)0xFFFFFFFF)
				return -1;
		}
	}

	/* Populate header */
	hdr->magic = HYLT_MAGIC;
	hdr->version = HYLT_VERSION;
	hdr->flags = 0;
	if (state->sort_asc)
		hdr->flags |= HYLT_FLAG_SORT_ASC;
	if (state->custom)
		hdr->flags |= HYLT_FLAG_CUSTOM;
	if (state->has_page)
		hdr->flags |= HYLT_FLAG_HAS_PAGE;

	hdr->total = (uint32_t)(state->total >= 0 ? state->total : 0);
	hdr->page = (uint16_t)(state->page >= 0 ? state->page : 1);
	hdr->per_page = (uint16_t)(state->per_page >= 0 ? state->per_page : 10);
	hdr->ncols = (uint16_t)state->ncols;
	hdr->nids = (uint16_t)state->nids;
	hdr->nopts = (uint16_t)state->nopts;
	hdr->reserved = 0;
	hdr->arena_offset = (uint32_t)arena_offset;
	hdr->arena_size = (uint32_t)arena_len;

	return (int)(arena_offset + arena_len);
}

/* Zero-copy deserialize HYLT binary buffer into list_state_t.
 * Directly maps ids[i] and values[...] to string arena offsets inside buf.
 * Zero string allocations, zero parsing.
 * Returns 0 on success, or -1 on malformed/invalid buffer. */
int list_state_from_bin(list_state_t *state, const void *buf, size_t len)
{
	const hylt_header_t *hdr;
	const hylt_meta_t *meta;
	const hylt_col_wire_t *cols;
	const hylt_opt_wire_t *opts;
	const uint32_t *id_offsets;
	const uint32_t *cell_offsets;
	const char *arena;
	size_t min_fixed;
	size_t arena_sz;
	int i, j;

	if (!state || !buf)
		return -1;

	if (len < sizeof(hylt_header_t) + sizeof(hylt_meta_t))
		return -1;

	hdr = (const hylt_header_t *)buf;
	if (hdr->magic != HYLT_MAGIC || hdr->version != HYLT_VERSION)
		return -1;

	if (hdr->ncols > LIST_MAX_COLS ||
	    hdr->nids > LIST_MAX_ROWS ||
	    hdr->nopts > LIST_MAX_OPTS)
		return -1;

	min_fixed = sizeof(hylt_header_t) + sizeof(hylt_meta_t) +
	            (size_t)hdr->ncols * sizeof(hylt_col_wire_t) +
	            (size_t)hdr->nopts * sizeof(hylt_opt_wire_t) +
	            (size_t)hdr->nids * sizeof(uint32_t) +
	            (size_t)hdr->nids * (size_t)hdr->ncols * sizeof(uint32_t);

	if (hdr->arena_offset < min_fixed || hdr->arena_offset >= len)
		return -1;

	arena_sz = (size_t)hdr->arena_size;
	if (arena_sz == 0 || arena_sz > len - hdr->arena_offset)
		return -1;

	arena = (const char *)buf + hdr->arena_offset;
	if (arena[arena_sz - 1] != '\0')
		return -1; /* arena must end with null terminator */

	memset(state, 0, sizeof(*state));

	state->sort_asc = (hdr->flags & HYLT_FLAG_SORT_ASC) ? 1 : 0;
	state->custom = (hdr->flags & HYLT_FLAG_CUSTOM) ? 1 : 0;
	state->has_page = (hdr->flags & HYLT_FLAG_HAS_PAGE) ? 1 : 0;
	state->total = (int)hdr->total;
	state->page = (int)hdr->page;
	state->per_page = (int)hdr->per_page;
	state->ncols = (int)hdr->ncols;
	state->nids = (int)hdr->nids;
	state->nopts = (int)hdr->nopts;

	meta = (const hylt_meta_t *)((const uint8_t *)buf + sizeof(hylt_header_t));
	strncpy(state->module, hylt_get_arena_str(arena, arena_sz, meta->module_offset), sizeof(state->module) - 1);
	strncpy(state->display_name, hylt_get_arena_str(arena, arena_sz, meta->display_name_offset), sizeof(state->display_name) - 1);
	strncpy(state->content_field, hylt_get_arena_str(arena, arena_sz, meta->content_field_offset), sizeof(state->content_field) - 1);
	strncpy(state->content_label, hylt_get_arena_str(arena, arena_sz, meta->content_label_offset), sizeof(state->content_label) - 1);
	strncpy(state->content_placeholder, hylt_get_arena_str(arena, arena_sz, meta->content_placeholder_offset), sizeof(state->content_placeholder) - 1);
	strncpy(state->username, hylt_get_arena_str(arena, arena_sz, meta->username_offset), sizeof(state->username) - 1);
	strncpy(state->query, hylt_get_arena_str(arena, arena_sz, meta->query_offset), sizeof(state->query) - 1);
	strncpy(state->q, hylt_get_arena_str(arena, arena_sz, meta->q_offset), sizeof(state->q) - 1);
	strncpy(state->sort_field, hylt_get_arena_str(arena, arena_sz, meta->sort_field_offset), sizeof(state->sort_field) - 1);
	strncpy(state->lang, hylt_get_arena_str(arena, arena_sz, meta->lang_offset), sizeof(state->lang) - 1);

	cols = (const hylt_col_wire_t *)((const uint8_t *)meta + sizeof(hylt_meta_t));
	for (i = 0; i < state->ncols; i++) {
		list_col_t *c = &state->cols[i];
		const hylt_col_wire_t *w = &cols[i];

		strncpy(c->key, hylt_get_arena_str(arena, arena_sz, w->key_offset), sizeof(c->key) - 1);
		strncpy(c->label, hylt_get_arena_str(arena, arena_sz, w->label_offset), sizeof(c->label) - 1);
		strncpy(c->target_source, hylt_get_arena_str(arena, arena_sz, w->target_source_offset), sizeof(c->target_source) - 1);
		strncpy(c->filter, hylt_get_arena_str(arena, arena_sz, w->filter_offset), sizeof(c->filter) - 1);
		strncpy(c->current, hylt_get_arena_str(arena, arena_sz, w->current_offset), sizeof(c->current) - 1);
		c->type = (int)w->type;
		c->opt_start = (int)w->opt_start;
		c->opt_count = (int)w->opt_count;
	}

	opts = (const hylt_opt_wire_t *)((const uint8_t *)cols + (size_t)state->ncols * sizeof(hylt_col_wire_t));
	for (i = 0; i < state->nopts; i++) {
		list_opt_t *o = &state->opts[i];
		const hylt_opt_wire_t *w = &opts[i];

		strncpy(o->id, hylt_get_arena_str(arena, arena_sz, w->id_offset), sizeof(o->id) - 1);
		strncpy(o->label, hylt_get_arena_str(arena, arena_sz, w->label_offset), sizeof(o->label) - 1);
	}

	id_offsets = (const uint32_t *)((const uint8_t *)opts + (size_t)state->nopts * sizeof(hylt_opt_wire_t));
	for (i = 0; i < state->nids; i++) {
		state->ids[i] = hylt_get_arena_str(arena, arena_sz, id_offsets[i]);
	}

	cell_offsets = id_offsets + state->nids;
	for (i = 0; i < state->nids; i++) {
		for (j = 0; j < state->ncols; j++) {
			int idx = i * state->ncols + j;
			state->values[idx] = hylt_get_arena_str(arena, arena_sz, cell_offsets[idx]);
		}
	}

	return 0;
}

#endif
