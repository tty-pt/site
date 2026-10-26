#include "nd_ui_internal.h"

static void nd_ansi_to_html(const char *src, char *dst, size_t cap)
{
	if (!src || !dst || cap == 0) return;
	size_t r = 0, w = 0;
	int fg = 7, bg = 0, bold = 0;
	int cur_fg = 7, cur_bg = 0;
	int span_open = 0;

	while (src[r] && w + 64 < cap) {
		if (src[r] == '\033' && src[r + 1] == '[') {
			r += 2;
			while (src[r] && src[r] != 'm') {
				int code = 0;
				while (src[r] >= '0' && src[r] <= '9') {
					code = code * 10 + (src[r] - '0');
					r++;
				}
				if (code == 0) {
					fg = 7;
					bg = 0;
					bold = 0;
				} else if (code == 1) {
					bold = 1;
				} else if (code == 2 || code == 22) {
					bold = 0;
				} else if (code >= 30 && code <= 37) {
					fg = code - 30;
				} else if (code == 39) {
					fg = 7;
				} else if (code >= 40 && code <= 47) {
					bg = code - 40;
				} else if (code == 49) {
					bg = 0;
				}
				if (src[r] == ';') r++;
			}
			if (src[r] == 'm') r++;

			int eff_fg = (bold && fg < 8) ? (fg + 8) : fg;
			if (span_open && (eff_fg != cur_fg || bg != cur_bg)) {
				memcpy(dst + w, "</span>", 7);
				w += 7;
				span_open = 0;
			}

			if (!span_open && (eff_fg != 7 || bg != 0)) {
				char cls[64] = {0};
				size_t clen = 0;
				if (eff_fg != 7)
					clen += snprintf(cls + clen, sizeof(cls) - clen, "cf%d", eff_fg);
				if (bg != 0)
					clen += snprintf(cls + clen, sizeof(cls) - clen, "%sc%d", clen ? " " : "", bg);

				int n = snprintf(dst + w, cap - w, "<span class=\"%s\">", cls);
				if (n > 0) {
					w += (size_t)n;
					span_open = 1;
					cur_fg = eff_fg;
					cur_bg = bg;
				}
			}
		} else if (src[r] == '<') {
			if (w + 4 < cap) { memcpy(dst + w, "&lt;", 4); w += 4; }
			r++;
		} else if (src[r] == '>') {
			if (w + 4 < cap) { memcpy(dst + w, "&gt;", 4); w += 4; }
			r++;
		} else if (src[r] == '&') {
			if (w + 5 < cap) { memcpy(dst + w, "&amp;", 5); w += 5; }
			r++;
		} else {
			dst[w++] = src[r++];
		}
	}

	if (span_open && w + 7 < cap) {
		memcpy(dst + w, "</span>", 7);
		w += 7;
	}
	dst[w < cap ? w : cap - 1] = '\0';
}

/* #room-map always holds exactly one RAW_HTML child. A TEXT child that later
 * becomes RAW_HTML (first BCP view frame) is not a clean DOM patch: the
 * hydrator reported a SSR/WASM mismatch and left one stray coloured cell.
 * RAW -> RAW is a single patch-innerhtml on the element. */
static bud_node *raw_escaped(const char *text)
{
	size_t n = text ? strlen(text) : 0;
	char *esc = malloc(n * 5 + 1);
	size_t w = 0;
	bud_node *node;

	if (!esc)
		return bud_raw("");
	for (size_t i = 0; i < n; i++) {
		switch (text[i]) {
		case '<': memcpy(esc + w, "&lt;", 4); w += 4; break;
		case '>': memcpy(esc + w, "&gt;", 4); w += 4; break;
		case '&': memcpy(esc + w, "&amp;", 5); w += 5; break;
		default: esc[w++] = text[i]; break;
		}
	}
	esc[w] = '\0';
	node = bud_raw(esc);
	free(esc);
	return node;
}

static bud_node *render_room_map_ascii(const nd_client_state_t *st)
{
	bud_node *map = lx_n("nd-map",
		lx_attr("id", "room-map"),
		lx_attr("class", "font-mono whitespace-pre text-[10px] leading-tight bg-black/80 p-2 rounded border border-neutral-700/60 overflow-hidden select-none text-center shadow-inner"));

	if (st && st->map_buf[0]) {
		/* Each ANSI escape (>= 3 bytes) can open a ~30 byte <span>. */
		size_t cap = strlen(st->map_buf) * 12 + 256;
		char *html = malloc(cap);
		if (html) {
			nd_ansi_to_html(st->map_buf, html, cap);
			bud_append(map, bud_raw(html));
			free(html);
		}
		return map;
	}

	if (st && st->map_data.width > 0 && st->map_data.height > 0) {
		char out[2048];
		size_t pos = 0;
		uint8_t w = st->map_data.width;
		uint8_t h = st->map_data.height;
		if (w > 7) w = 7;
		if (h > 7) h = 7;

		for (uint8_t y = 0; y < h; y++) {
			/* Top boundary of row y */
			for (uint8_t x = 0; x < w; x++) {
				const nd_map_tile_t *t = &st->map_data.tiles[y * w + x];
				int n_open = (t->exits & 2); /* E_NORTH */
				int n_door = (t->doors & 2);
				if (x == 0) out[pos++] = '+';
				if (n_door) {
					out[pos++] = '#'; out[pos++] = '#'; out[pos++] = '#';
				} else if (n_open) {
					out[pos++] = ' '; out[pos++] = ' '; out[pos++] = ' ';
				} else {
					out[pos++] = '-'; out[pos++] = '-'; out[pos++] = '-';
				}
				out[pos++] = '+';
			}
			out[pos++] = '\n';

			/* Room interior of row y */
			for (uint8_t x = 0; x < w; x++) {
				const nd_map_tile_t *t = &st->map_data.tiles[y * w + x];
				int w_open = (t->exits & 1); /* E_WEST */
				int w_door = (t->doors & 1);
				if (w_door) out[pos++] = '#';
				else if (w_open) out[pos++] = ' ';
				else out[pos++] = '|';

				out[pos++] = ' ';
				out[pos++] = t->symbol ? t->symbol : '.';
				out[pos++] = ' ';

				if (x == w - 1) {
					int e_open = (t->exits & 8); /* E_EAST */
					int e_door = (t->doors & 8);
					if (e_door) out[pos++] = '#';
					else if (e_open) out[pos++] = ' ';
					else out[pos++] = '|';
				}
			}
			out[pos++] = '\n';
		}

		/* Bottom boundary of last row */
		for (uint8_t x = 0; x < w; x++) {
			const nd_map_tile_t *t = &st->map_data.tiles[(h - 1) * w + x];
			int s_open = (t->exits & 16); /* E_SOUTH */
			int s_door = (t->doors & 16);
			if (x == 0) out[pos++] = '+';
			if (s_door) {
				out[pos++] = '#'; out[pos++] = '#'; out[pos++] = '#';
			} else if (s_open) {
				out[pos++] = ' '; out[pos++] = ' '; out[pos++] = ' ';
			} else {
				out[pos++] = '-'; out[pos++] = '-'; out[pos++] = '-';
			}
			out[pos++] = '+';
		}
		out[pos] = '\0';

		bud_append(map, raw_escaped(out));
		return map;
	}

	bud_append(map, raw_escaped(st ? st->room.desc : ""));
	return map;
}

static bud_node *render_room_map_canvas(const nd_client_state_t *st)
{
	(void)st;
	return lx_n("canvas",
		lx_attr("id", "room-map-canvas"),
		lx_attr("data-bud-canvas", "1"),
		lx_attr("width", "160"),
		lx_attr("height", "160"),
		lx_attr("class", "w-[160px] h-[160px] bg-black/80 rounded border border-neutral-700/60 shadow-inner block mx-auto"));
}

bud_node *render_room_map(const nd_client_state_t *st)
{
	const char *map_drv = (st && st->preferred_map_driver[0]) ? st->preferred_map_driver : "ascii-bud";

	if (strcmp(map_drv, "none") == 0)
		return NULL;

	if (strcmp(map_drv, "canvas-lamina") == 0 || strcmp(map_drv, "canvas") == 0)
		return render_room_map_canvas(st);

	return render_room_map_ascii(st);
}
