/* WASM entry for the shared list-page enhancement bundle.
 * Dual-compiled from the same sources as the native SSR render (list.c) so
 * bud node ids align on both sides. See docs/C-ISOMORPHIC-BUD.md. */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "bud/bud.h"
#include "bud/bud_jsx.h"
#include "bud/bud_app.h"

#include <hyle-bud/hyle-bud.h>

#include "../../common/viewer_zoom.h" /* VIEWER_ZOOM_* + STR for site_ui.c */
#include "site_ui.h" /* site_ui_layout, site_ui_empty_state, ... */
#include "list.c"                    /* list_state_t + list_render + ... */

#include <ttypt/stele_wasm.h>
#include "../../common/site_wasm_app.h"

static list_state_t g_state = { 0 };
static uint8_t s_bin_slice[131072];
static size_t s_bin_slice_len = 0;
static char g_pending_cache_key[512] = { 0 };
static char g_pending_browser_url[512] = { 0 };

static int list_fe_apply_bin(const void *data, size_t len)
{
	if (!data || len == 0 || len > sizeof(s_bin_slice))
		return -1;
	memcpy(s_bin_slice, data, len);
	s_bin_slice_len = len;
	return list_state_from_bin(&g_state, s_bin_slice, len);
}

#define LIST_ACT_SORT   1
#define LIST_ACT_PAGE   2
#define LIST_ACT_FILTER 3

#define REQ_TABLE_SLICE 101

static void list_fe_build_url(
        char *fetch_url, size_t fetch_sz,
        char *browser_url, size_t browser_sz,
        char *cache_key, size_t cache_sz,
        int page, int per_page,
        const char *sort_field, int sort_asc,
        const char *filter_qs)
{
	char qs[512] = { 0 };
	size_t pos = 0;
	int first = 1;

	if (filter_qs && filter_qs[0]) {
		const char *p = (*filter_qs == '?') ? filter_qs + 1 : filter_qs;
		while (*p) {
			const char *amp = strchr(p, '&');
			size_t seg_len = amp ? (size_t)(amp - p) : strlen(p);
			if (seg_len > 0) {
				int skip = (seg_len >= 5 && strncmp(p, "page=", 5) == 0) ||
				           (seg_len >= 5 && strncmp(p, "sort=", 5) == 0) ||
				           (seg_len >= 9 && strncmp(p, "per_page=", 9) == 0) ||
				           (seg_len >= 7 && strncmp(p, "format=", 7) == 0);
				if (!skip) {
					if (!first && pos + 1 < sizeof(qs) - 1)
						qs[pos++] = '&';
					size_t cp = (pos + seg_len < sizeof(qs) - 1)
					        ? seg_len
					        : (sizeof(qs) - 1 - pos);
					memcpy(qs + pos, p, cp);
					pos += cp;
					qs[pos] = '\0';
					first = 0;
				}
			}
			p += seg_len;
			if (*p == '&')
				p++;
		}
	}

	if (page > 1) {
		if (!first && pos + 1 < sizeof(qs) - 1)
			qs[pos++] = '&';
		pos += snprintf(qs + pos, sizeof(qs) - pos, "page=%d", page);
		first = 0;
	}

	if (per_page > 0 && per_page != 10) {
		if (!first && pos + 1 < sizeof(qs) - 1)
			qs[pos++] = '&';
		pos += snprintf(qs + pos, sizeof(qs) - pos, "per_page=%d", per_page);
		first = 0;
	}

	if (sort_field && sort_field[0]) {
		if (!first && pos + 1 < sizeof(qs) - 1)
			qs[pos++] = '&';
		pos += snprintf(
		        qs + pos, sizeof(qs) - pos, "sort=%s:%s",
		        sort_field, sort_asc ? "asc" : "desc");
		first = 0;
	}

	if (cache_key && cache_sz > 0)
		snprintf(cache_key, cache_sz, "%s", qs);
	if (browser_url && browser_sz > 0) {
		if (qs[0])
			snprintf(browser_url, browser_sz, "/%s?%s", g_state.module, qs);
		else
			snprintf(browser_url, browser_sz, "/%s", g_state.module);
	}
	if (fetch_url && fetch_sz > 0) {
		if (qs[0])
			snprintf(fetch_url, fetch_sz, "/%s?%s&format=bin", g_state.module, qs);
		else
			snprintf(fetch_url, fetch_sz, "/%s?format=bin", g_state.module);
	}
}

static bud_node *list_stl_render(void)
{
	return list_render(&g_state);
}

static int list_stl_init(const char *json, int len)
{
	char cache_key[512] = { 0 };

	if (json && len >= 4 && memcmp(json, "HYLT", 4) == 0)
		list_fe_apply_bin(json, len >= 0 ? (size_t)len : 0);
	else
		list_state_from_json_len(&g_state, json, len >= 0 ? (size_t)len : 0);
	site_ui_set_locale(g_state.lang);
	hyle_bud_set_translator(ui_t);

	if (json && len > 0) {
		list_fe_build_url(
		        NULL, 0, NULL, 0, cache_key, sizeof(cache_key),
		        g_state.page, g_state.per_page,
		        g_state.sort_field, g_state.sort_asc,
		        g_state.query);
		hyle_bud_cache_put(g_state.module, cache_key, json, (size_t)len);
	}
	return 0;
}

static void list_stl_action(int action_id, const char *data)
{
	char fetch_url[512] = { 0 };
	char browser_url[512] = { 0 };
	char cache_key[512] = { 0 };
	size_t cached_len = 0;
	const char *cached = NULL;

	if (action_id == LIST_ACT_SORT && data && data[0]) {
		int new_asc = (strcmp(g_state.sort_field, data) == 0)
		        ? !g_state.sort_asc
		        : 1;

		list_fe_build_url(
		        fetch_url, sizeof(fetch_url),
		        browser_url, sizeof(browser_url),
		        cache_key, sizeof(cache_key),
		        1, g_state.per_page,
		        data, new_asc,
		        g_state.query);

		cached = hyle_bud_cache_get(g_state.module, cache_key, &cached_len);
		if (cached) {
			if (cached_len >= 4 && memcmp(cached, "HYLT", 4) == 0)
				list_fe_apply_bin(cached, cached_len);
			else
				list_state_from_json_len(&g_state, cached, cached_len);
			bud_slot_update(STL_SLOT_PAGE, list_stl_render);
			stl_host_set_location(browser_url, strlen(browser_url));
			return;
		}

		/* Sort in-memory rows immediately (optimistic sort response) */
		if (g_state.nids > 1) {
			list_state_sort(&g_state, data);
			bud_slot_update(STL_SLOT_PAGE, list_stl_render);
			stl_host_set_location(browser_url, strlen(browser_url));
		}

		/* If whole dataset is already in memory, cache and return */
		if (g_state.total <= g_state.per_page && g_state.nids > 1) {
			char sbuf[16384];
			int bsz = list_state_to_bin(&g_state, sbuf, sizeof(sbuf));
			if (bsz > 0) {
				hyle_bud_cache_put(
				        g_state.module, cache_key, sbuf,
				        (size_t)bsz);
			} else if (list_state_to_json(&g_state, sbuf, sizeof(sbuf)) == 0) {
				hyle_bud_cache_put(
				        g_state.module, cache_key, sbuf,
				        strlen(sbuf));
			}
			return;
		}

		/* Server fetch required for full dataset sort */
		snprintf(g_pending_cache_key, sizeof(g_pending_cache_key), "%s", cache_key);
		snprintf(g_pending_browser_url, sizeof(g_pending_browser_url), "%s", browser_url);
		if (bud_host_fetch_fn)
			bud_host_fetch_fn(fetch_url, strlen(fetch_url), REQ_TABLE_SLICE);
		else
			stl_host_fetch(fetch_url, strlen(fetch_url), REQ_TABLE_SLICE);

	} else if (action_id == LIST_ACT_PAGE && data && data[0]) {
		int new_page = atoi(data);
		if (new_page <= 0)
			new_page = 1;

		list_fe_build_url(
		        fetch_url, sizeof(fetch_url),
		        browser_url, sizeof(browser_url),
		        cache_key, sizeof(cache_key),
		        new_page, g_state.per_page,
		        g_state.sort_field, g_state.sort_asc,
		        g_state.query);

		cached = hyle_bud_cache_get(g_state.module, cache_key, &cached_len);
		if (cached) {
			if (cached_len >= 4 && memcmp(cached, "HYLT", 4) == 0)
				list_fe_apply_bin(cached, cached_len);
			else
				list_state_from_json_len(&g_state, cached, cached_len);
			bud_slot_update(STL_SLOT_PAGE, list_stl_render);
			stl_host_set_location(browser_url, strlen(browser_url));
			return;
		}

		snprintf(g_pending_cache_key, sizeof(g_pending_cache_key), "%s", cache_key);
		snprintf(g_pending_browser_url, sizeof(g_pending_browser_url), "%s", browser_url);
		if (bud_host_fetch_fn)
			bud_host_fetch_fn(fetch_url, strlen(fetch_url), REQ_TABLE_SLICE);
		else
			stl_host_fetch(fetch_url, strlen(fetch_url), REQ_TABLE_SLICE);

	} else if (action_id == LIST_ACT_FILTER && data) {
		char sort_field[64] = { 0 };
		int sort_asc = 1;
		char page_buf[32] = { 0 };
		char pp_buf[32] = { 0 };
		int page = 1;
		int pp = g_state.per_page > 0 ? g_state.per_page : 10;

		idx_parse_sort(data, sort_field, sizeof(sort_field), &sort_asc);
		if (!sort_field[0] && g_state.sort_field[0]) {
			snprintf(sort_field, sizeof(sort_field), "%s", g_state.sort_field);
			sort_asc = g_state.sort_asc;
		}

		if (idx_query_param(data, "page", page_buf, sizeof(page_buf))) {
			page = atoi(page_buf);
			if (page <= 0)
				page = 1;
		}

		if (idx_query_param(data, "per_page", pp_buf, sizeof(pp_buf))) {
			int parsed_pp = atoi(pp_buf);
			if (parsed_pp > 0)
				pp = parsed_pp;
		}

		list_fe_build_url(
		        fetch_url, sizeof(fetch_url),
		        browser_url, sizeof(browser_url),
		        cache_key, sizeof(cache_key),
		        page, pp,
		        sort_field, sort_asc,
		        data);

		cached = hyle_bud_cache_get(g_state.module, cache_key, &cached_len);
		if (cached) {
			if (cached_len >= 4 && memcmp(cached, "HYLT", 4) == 0)
				list_fe_apply_bin(cached, cached_len);
			else
				list_state_from_json_len(&g_state, cached, cached_len);
			bud_slot_update(STL_SLOT_PAGE, list_stl_render);
			stl_host_set_location(browser_url, strlen(browser_url));
			return;
		}

		snprintf(g_pending_cache_key, sizeof(g_pending_cache_key), "%s", cache_key);
		snprintf(g_pending_browser_url, sizeof(g_pending_browser_url), "%s", browser_url);
		if (bud_host_fetch_fn)
			bud_host_fetch_fn(fetch_url, strlen(fetch_url), REQ_TABLE_SLICE);
		else
			stl_host_fetch(fetch_url, strlen(fetch_url), REQ_TABLE_SLICE);
	}
}

static void list_fetch_callback(int req_id, const char *data, int len)
{
	(void)req_id;
	if (data && len > 0) {
		if (g_pending_cache_key[0]) {
			hyle_bud_cache_put(
			        g_state.module, g_pending_cache_key, data,
			        (size_t)len);
		}
		if (len >= 4 && memcmp(data, "HYLT", 4) == 0)
			list_fe_apply_bin(data, (size_t)len);
		else
			list_state_from_json_len(&g_state, data, (size_t)len);
		bud_slot_update(STL_SLOT_PAGE, list_stl_render);
		if (g_pending_browser_url[0]) {
			stl_host_set_location(
			        g_pending_browser_url,
			        strlen(g_pending_browser_url));
			g_pending_browser_url[0] = '\0';
		}
	}
}

static void list_stl_cleanup(void)
{
	memset(&g_state, 0, sizeof(g_state));
	s_bin_slice_len = 0;
	g_pending_cache_key[0] = '\0';
	g_pending_browser_url[0] = '\0';
}

BUD_WASM_MODULE_EXT(list_fe, list_stl_init, list_stl_render, list_stl_action, NULL, list_stl_cleanup, list_fetch_callback);
