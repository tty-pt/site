/*
 * Copyright (c) 2026, tty-pt
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include <ttypt/qsys_wasm.h>

extern const qsys_module_ops_t qsys_mod_site_chrome;
extern const qsys_module_ops_t qsys_mod_song_detail;
extern const qsys_module_ops_t qsys_mod_gig_detail;
extern const qsys_module_ops_t qsys_mod_list_fe;
extern const qsys_module_ops_t qsys_mod_chat_fe;

const qsys_module_ops_t *g_site_modules[] = {
	&qsys_mod_site_chrome,
	&qsys_mod_song_detail,
	&qsys_mod_gig_detail,
	&qsys_mod_list_fe,
	&qsys_mod_chat_fe,
	NULL
};
