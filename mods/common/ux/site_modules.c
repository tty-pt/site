/*
 * Copyright (c) 2026, tty-pt
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include <ttypt/stele_wasm.h>

extern const stl_module_ops_t stl_mod_site_chrome;
extern const stl_module_ops_t stl_mod_song_detail;
extern const stl_module_ops_t stl_mod_gig_detail;
extern const stl_module_ops_t stl_mod_list_fe;
extern const stl_module_ops_t stl_mod_chat_fe;

const stl_module_ops_t *g_site_modules[] = {
	&stl_mod_site_chrome,
	&stl_mod_song_detail,
	&stl_mod_gig_detail,
	&stl_mod_list_fe,
	&stl_mod_chat_fe,
	NULL
};

extern void (*bud_host_fetch_fn)(const char *url, size_t len, int id);
extern void (*bud_host_log_fn)(const char *msg, size_t len);
extern void (*bud_host_set_location_fn)(const char *url, size_t len);

ISO_IMPORT(void, bud, bud_host_fetch, const char *url, size_t url_len, int req_id);
ISO_IMPORT(void, bud, bud_host_log, const char *msg, size_t len);
ISO_IMPORT(void, bud, bud_host_set_location, const char *url, size_t url_len);

__attribute__((constructor)) static void site_platform_init(void)
{
	bud_host_fetch_fn = bud_host_fetch;
	bud_host_log_fn = bud_host_log;
	bud_host_set_location_fn = bud_host_set_location;
}
