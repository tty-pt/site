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

const stl_module_ops_t *g_stl_modules[] = {
	&stl_mod_site_chrome,
	&stl_mod_song_detail,
	&stl_mod_gig_detail,
	&stl_mod_list_fe,
	&stl_mod_chat_fe,
	NULL
};

