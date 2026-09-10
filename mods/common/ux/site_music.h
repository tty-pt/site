#ifndef SITE_MUSIC_H
#define SITE_MUSIC_H

#include "bud/bud.h"
#include <transp/music.h>

static inline bud_node *
site_ui_render_key_options(int cur_key, int orig_key, int latin)
{
	bud_node *opts = bud_fragment();
	int norm_key = ((cur_key % 12) + 12) % 12;
	for (int i = 0; i < 12; i++) {
		bud_node *o = bud_tpl(
		        "<option value='%d' %b>%s</option>", i,
		        (i == norm_key) ? "selected" : NULL,
		        key_name(i, orig_key, latin));
		if (o)
			bud_append(opts, o);
	}
	return opts;
}

#endif
