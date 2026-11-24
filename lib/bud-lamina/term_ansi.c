#include "term_ansi.h"

#include <ttypt/lam.h>
#include <ttypt/lam-font.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define TERM_MAX_LINES 512
#define TERM_MAX_COLS  200
#define TERM_INPUT_MAX 512
#define TERM_HIST_MAX  32

/* Standard GLFW key codes (no GLFW header required in bridge). */
#define KEY_ENTER     257
#define KEY_KP_ENTER  335
#define KEY_BACKSPACE 259
#define KEY_DELETE    261
#define KEY_RIGHT     262
#define KEY_LEFT      263
#define KEY_DOWN      264
#define KEY_UP        265
#define KEY_HOME      268
#define KEY_END       269

#define TERM_DEFAULT_FG LAM_RGB(0x93, 0xad, 0xa5)
#define TERM_DEFAULT_BG 0x00000000u

typedef struct {
	uint32_t cp;
	uint32_t fg;
	uint32_t bg;
} term_cell_t;

typedef struct {
	term_cell_t cells[TERM_MAX_COLS];
	uint16_t    len;
} term_line_t;

typedef enum {
	PS_TEXT = 0,
	PS_ESC,
	PS_CSI,
	PS_OSC,
	PS_IAC,
	PS_IAC_OPT,
	PS_IAC_SB,
	PS_IAC_SB_IAC
} parse_state_t;

struct term_ansi {
	term_line_t *lines;       /* ring buffer of TERM_MAX_LINES */
	int          head;          /* index of active bottom line */
	int          total_lines;   /* number of populated lines (1..TERM_MAX_LINES) */
	int          cur_col;
	int          scroll_off;    /* 0 = pinned at bottom, >0 = scrolled up (visual rows) */
	int          max_scroll_off;/* last computed max visual-row scroll offset */

	/* Current SGR attributes */
	uint32_t cur_fg;
	uint32_t cur_bg;
	int      bold;
	int      dim;

	/* Parser state */
	parse_state_t pstate;
	char          csi_buf[64];
	int           csi_len;
	uint8_t       utf8_buf[4];
	int           utf8_need;
	int           utf8_have;

	/* Interactive input line */
	char input[TERM_INPUT_MAX];
	int  input_len;
	int  input_cursor;
	char history[TERM_HIST_MAX][TERM_INPUT_MAX];
	int  hist_count;
	int  hist_pos;

	/* Visual backdrop & shadow styling driven by #term CSS */
	uint32_t bg_fill;
	int      text_shadow;
};

/* Matches NeverDark nd.css .xterm-fg-* / .xterm-bg-* palette */
static const uint32_t s_ansi16[16] = {
	LAM_RGB(0x11, 0x26, 0x16), /* 0  black */
	LAM_RGB(0x7f, 0x2b, 0x27), /* 1  red */
	LAM_RGB(0x2f, 0x7e, 0x25), /* 2  green */
	LAM_RGB(0x71, 0x7f, 0x24), /* 3  yellow */
	LAM_RGB(0x2f, 0x6a, 0x7f), /* 4  blue */
	LAM_RGB(0x47, 0x58, 0x7f), /* 5  magenta */
	LAM_RGB(0x32, 0x7f, 0x77), /* 6  cyan */
	LAM_RGB(0x93, 0xad, 0xa5), /* 7  white / default fg */
	LAM_RGB(0x64, 0x7d, 0x75), /* 8  bright black */
	LAM_RGB(0xe0, 0x80, 0x09), /* 9  bright red / orange */
	LAM_RGB(0x18, 0xe0, 0x00), /* 10 bright green */
	LAM_RGB(0xbd, 0xe0, 0x00), /* 11 bright yellow */
	LAM_RGB(0x00, 0xaa, 0xe0), /* 12 bright blue */
	LAM_RGB(0x00, 0x58, 0xe0), /* 13 bright magenta */
	LAM_RGB(0x00, 0xe0, 0xc4), /* 14 bright cyan */
	LAM_RGB(0x73, 0xfa, 0x91)  /* 15 bright white / cursor */
};

static uint32_t xterm256_color(int idx)
{
	if (idx < 0) idx = 0;
	if (idx < 16) return s_ansi16[idx];
	if (idx < 232) {
		int c = idx - 16;
		int r = (c / 36) % 6;
		int g = (c / 6) % 6;
		int b = c % 6;
		uint8_t rv = r ? (uint8_t)(55 + r * 40) : 0;
		uint8_t gv = g ? (uint8_t)(55 + g * 40) : 0;
		uint8_t bv = b ? (uint8_t)(55 + b * 40) : 0;
		return LAM_RGB(rv, gv, bv);
	}
	uint8_t gray = (uint8_t)(8 + (idx - 232) * 10);
	return LAM_RGB(gray, gray, gray);
}

static int utf8_encode(uint32_t cp, char out[4])
{
	if (cp < 0x80) {
		out[0] = (char)cp;
		return 1;
	}
	if (cp < 0x800) {
		out[0] = (char)(0xC0 | (cp >> 6));
		out[1] = (char)(0x80 | (cp & 0x3F));
		return 2;
	}
	if (cp < 0x10000) {
		out[0] = (char)(0xE0 | (cp >> 12));
		out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
		out[2] = (char)(0x80 | (cp & 0x3F));
		return 3;
	}
	out[0] = (char)(0xF0 | ((cp >> 18) & 0x07));
	out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
	out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
	out[3] = (char)(0x80 | (cp & 0x3F));
	return 4;
}

term_ansi_t *term_ansi_create(void)
{
	term_ansi_t *t = calloc(1, sizeof(*t));
	if (!t) return NULL;
	t->lines = calloc(TERM_MAX_LINES, sizeof(term_line_t));
	if (!t->lines) {
		free(t);
		return NULL;
	}
	t->total_lines = 1;
	t->cur_fg = TERM_DEFAULT_FG;
	t->cur_bg = TERM_DEFAULT_BG;
	t->hist_pos = -1;
	t->bg_fill = LAM_RGBA(0, 0, 0, 128);
	t->text_shadow = 0;
	return t;
}

void term_ansi_destroy(term_ansi_t *t)
{
	if (!t) return;
	free(t->lines);
	free(t);
}

void term_ansi_clear(term_ansi_t *t)
{
	if (!t) return;
	memset(t->lines, 0, TERM_MAX_LINES * sizeof(term_line_t));
	t->head = 0;
	t->total_lines = 1;
	t->cur_col = 0;
	t->scroll_off = 0;
	t->max_scroll_off = 0;
}

static void term_newline(term_ansi_t *t)
{
	t->head = (t->head + 1) % TERM_MAX_LINES;
	t->lines[t->head].len = 0;
	if (t->total_lines < TERM_MAX_LINES)
		t->total_lines++;
	t->cur_col = 0;
	if (t->scroll_off > 0 && t->scroll_off < t->total_lines - 1)
		t->scroll_off++;
}

static void term_put_cp(term_ansi_t *t, uint32_t cp)
{
	if (cp == '\r') {
		t->cur_col = 0;
		return;
	}
	if (cp == '\n') {
		term_newline(t);
		return;
	}
	if (cp == '\b') {
		if (t->cur_col > 0)
			t->cur_col--;
		return;
	}
	if (cp == '\t') {
		int next = (t->cur_col + 8) & ~7;
		if (next > TERM_MAX_COLS) next = TERM_MAX_COLS;
		while (t->cur_col < next)
			term_put_cp(t, ' ');
		return;
	}
	if (cp < 0x20)
		return;

	if (t->cur_col >= TERM_MAX_COLS)
		term_newline(t);

	term_line_t *ln = &t->lines[t->head];
	while (ln->len < t->cur_col) {
		ln->cells[ln->len].cp = ' ';
		ln->cells[ln->len].fg = TERM_DEFAULT_FG;
		ln->cells[ln->len].bg = TERM_DEFAULT_BG;
		ln->len++;
	}

	uint32_t fg = t->cur_fg;
	if (t->dim) {
		uint8_t r = (uint8_t)((((fg >> 16) & 0xFFu) * 2u) / 3u);
		uint8_t g = (uint8_t)((((fg >> 8)  & 0xFFu) * 2u) / 3u);
		uint8_t b = (uint8_t)(((fg         & 0xFFu) * 2u) / 3u);
		fg = LAM_RGB(r, g, b);
	}

	ln->cells[t->cur_col].cp = cp;
	ln->cells[t->cur_col].fg = fg;
	ln->cells[t->cur_col].bg = t->cur_bg;
	t->cur_col++;
	if (t->cur_col > ln->len)
		ln->len = (uint16_t)t->cur_col;
}

static void term_apply_sgr(term_ansi_t *t)
{
	int params[16];
	int n = 0;
	const char *p = t->csi_buf;
	if (*p == '\0') {
		params[n++] = 0;
	} else {
		while (*p && n < 16) {
			int val = 0;
			int any = 0;
			while (*p >= '0' && *p <= '9') {
				val = val * 10 + (*p - '0');
				p++;
				any = 1;
			}
			params[n++] = any ? val : 0;
			if (*p == ';') p++;
			else if (*p) p++;
		}
	}

	int base_fg_idx = -1;
	for (int i = 0; i < n; i++) {
		int c = params[i];
		if (c == 0) {
			t->cur_fg = TERM_DEFAULT_FG;
			t->cur_bg = TERM_DEFAULT_BG;
			t->bold = 0;
			t->dim = 0;
			base_fg_idx = -1;
		} else if (c == 1) {
			t->bold = 1;
			if (base_fg_idx >= 0 && base_fg_idx < 8)
				t->cur_fg = s_ansi16[base_fg_idx + 8];
		} else if (c == 2) {
			t->dim = 1;
		} else if (c == 22) {
			t->bold = 0;
			t->dim = 0;
		} else if (c >= 30 && c <= 37) {
			base_fg_idx = c - 30;
			t->cur_fg = s_ansi16[t->bold ? (base_fg_idx + 8) : base_fg_idx];
		} else if (c == 38 && i + 2 < n && params[i + 1] == 5) {
			t->cur_fg = xterm256_color(params[i + 2]);
			base_fg_idx = -1;
			i += 2;
		} else if (c == 38 && i + 4 < n && params[i + 1] == 2) {
			t->cur_fg = LAM_RGB(params[i + 2], params[i + 3], params[i + 4]);
			base_fg_idx = -1;
			i += 4;
		} else if (c == 39) {
			t->cur_fg = TERM_DEFAULT_FG;
			base_fg_idx = -1;
		} else if (c >= 40 && c <= 47) {
			t->cur_bg = s_ansi16[c - 40];
		} else if (c == 48 && i + 2 < n && params[i + 1] == 5) {
			t->cur_bg = xterm256_color(params[i + 2]);
			i += 2;
		} else if (c == 48 && i + 4 < n && params[i + 1] == 2) {
			t->cur_bg = LAM_RGB(params[i + 2], params[i + 3], params[i + 4]);
			i += 4;
		} else if (c == 49) {
			t->cur_bg = TERM_DEFAULT_BG;
		} else if (c >= 90 && c <= 97) {
			t->cur_fg = s_ansi16[8 + (c - 90)];
			base_fg_idx = -1;
		} else if (c >= 100 && c <= 107) {
			t->cur_bg = s_ansi16[8 + (c - 100)];
		}
	}
}

static void term_exec_csi(term_ansi_t *t, char final_ch)
{
	t->csi_buf[t->csi_len] = '\0';
	if (final_ch == 'm') {
		term_apply_sgr(t);
	} else if (final_ch == 'J') {
		int p = atoi(t->csi_buf);
		if (p == 2 || p == 3)
			term_ansi_clear(t);
	} else if (final_ch == 'K') {
		term_line_t *ln = &t->lines[t->head];
		int p = atoi(t->csi_buf);
		if (p == 0) {
			if (t->cur_col < ln->len)
				ln->len = (uint16_t)t->cur_col;
		} else if (p == 2) {
			ln->len = 0;
			t->cur_col = 0;
		}
	} else if (final_ch == 'H' || final_ch == 'f') {
		t->cur_col = 0;
	}
}

static void term_feed_utf8_byte(term_ansi_t *t, uint8_t b)
{
	if (t->utf8_need == 0) {
		if (b < 0x80) {
			term_put_cp(t, b);
		} else if ((b & 0xE0) == 0xC0) {
			t->utf8_buf[0] = b;
			t->utf8_have = 1;
			t->utf8_need = 2;
		} else if ((b & 0xF0) == 0xE0) {
			t->utf8_buf[0] = b;
			t->utf8_have = 1;
			t->utf8_need = 3;
		} else if ((b & 0xF8) == 0xF0) {
			t->utf8_buf[0] = b;
			t->utf8_have = 1;
			t->utf8_need = 4;
		}
		return;
	}

	if ((b & 0xC0) != 0x80) {
		t->utf8_need = 0;
		t->utf8_have = 0;
		term_feed_utf8_byte(t, b);
		return;
	}

	t->utf8_buf[t->utf8_have++] = b;
	if (t->utf8_have == t->utf8_need) {
		uint32_t cp = 0;
		if (t->utf8_need == 2) {
			cp = ((uint32_t)(t->utf8_buf[0] & 0x1F) << 6) |
			     (uint32_t)(t->utf8_buf[1] & 0x3F);
		} else if (t->utf8_need == 3) {
			cp = ((uint32_t)(t->utf8_buf[0] & 0x0F) << 12) |
			     ((uint32_t)(t->utf8_buf[1] & 0x3F) << 6) |
			     (uint32_t)(t->utf8_buf[2] & 0x3F);
		} else {
			cp = ((uint32_t)(t->utf8_buf[0] & 0x07) << 18) |
			     ((uint32_t)(t->utf8_buf[1] & 0x3F) << 12) |
			     ((uint32_t)(t->utf8_buf[2] & 0x3F) << 6) |
			     (uint32_t)(t->utf8_buf[3] & 0x3F);
		}
		t->utf8_need = 0;
		t->utf8_have = 0;
		term_put_cp(t, cp);
	}
}

void term_ansi_write(term_ansi_t *t, const char *data, size_t len)
{
	if (!t || !data || len == 0)
		return;

	for (size_t i = 0; i < len; i++) {
		uint8_t b = (uint8_t)data[i];
		switch (t->pstate) {
		case PS_TEXT:
			if (b == 0xFF) {
				t->pstate = PS_IAC;
			} else if (b == 0x1B) {
				t->pstate = PS_ESC;
			} else {
				term_feed_utf8_byte(t, b);
			}
			break;
		case PS_ESC:
			if (b == '[') {
				t->pstate = PS_CSI;
				t->csi_len = 0;
			} else if (b == ']') {
				t->pstate = PS_OSC;
			} else {
				t->pstate = PS_TEXT;
			}
			break;
		case PS_CSI:
			if (b >= 0x40 && b <= 0x7E) {
				term_exec_csi(t, (char)b);
				t->pstate = PS_TEXT;
			} else if (t->csi_len + 1 < (int)sizeof(t->csi_buf)) {
				t->csi_buf[t->csi_len++] = (char)b;
			}
			break;
		case PS_OSC:
			if (b == 0x07 || b == 0x9C) {
				t->pstate = PS_TEXT;
			} else if (b == 0x1B) {
				t->pstate = PS_ESC;
			}
			break;
		case PS_IAC:
			if (b == 0xFF) {
				t->pstate = PS_TEXT;
			} else if (b == 0xFA) {
				t->pstate = PS_IAC_SB;
			} else if (b >= 0xFB && b <= 0xFE) {
				t->pstate = PS_IAC_OPT;
			} else {
				t->pstate = PS_TEXT;
			}
			break;
		case PS_IAC_OPT:
			t->pstate = PS_TEXT;
			break;
		case PS_IAC_SB:
			if (b == 0xFF)
				t->pstate = PS_IAC_SB_IAC;
			break;
		case PS_IAC_SB_IAC:
			if (b == 0xF0)
				t->pstate = PS_TEXT;
			else if (b != 0xFF)
				t->pstate = PS_IAC_SB;
			break;
		}
	}
}

void term_ansi_scroll(term_ansi_t *t, int delta_lines)
{
	if (!t) return;
	t->scroll_off += delta_lines;
	if (t->scroll_off < 0)
		t->scroll_off = 0;
	int limit = t->max_scroll_off > 0 ? t->max_scroll_off
	                                  : (t->total_lines > 0 ? t->total_lines - 1 : 0);
	if (t->scroll_off > limit)
		t->scroll_off = limit;
}

int term_ansi_input_char(term_ansi_t *t, uint32_t cp)
{
	if (!t || cp < 0x20 || cp == 0x7F)
		return 0;
	char enc[4];
	int elen = utf8_encode(cp, enc);
	if (t->input_len + elen >= TERM_INPUT_MAX - 1)
		return 0;
	memmove(t->input + t->input_cursor + elen,
	        t->input + t->input_cursor,
	        (size_t)(t->input_len - t->input_cursor + 1));
	memcpy(t->input + t->input_cursor, enc, (size_t)elen);
	t->input_len += elen;
	t->input_cursor += elen;
	t->scroll_off = 0;
	return 1;
}

int term_ansi_input_key(term_ansi_t *t, int key, int mods,
                        char *submit_out, size_t submit_cap)
{
	(void)mods;
	if (!t) return 0;
	if (submit_out && submit_cap > 0)
		submit_out[0] = '\0';

	if (key == KEY_ENTER || key == KEY_KP_ENTER) {
		if (submit_out && submit_cap > 1) {
			size_t n = (size_t)t->input_len;
			if (n + 2 > submit_cap) n = submit_cap - 2;
			memcpy(submit_out, t->input, n);
			submit_out[n] = '\n';
			submit_out[n + 1] = '\0';
		}
		if (t->input_len > 0) {
			if (t->hist_count < TERM_HIST_MAX) {
				snprintf(t->history[t->hist_count++], TERM_INPUT_MAX,
				         "%s", t->input);
			} else {
				memmove(t->history[0], t->history[1],
				        (TERM_HIST_MAX - 1) * TERM_INPUT_MAX);
				snprintf(t->history[TERM_HIST_MAX - 1], TERM_INPUT_MAX,
				         "%s", t->input);
			}
		}
		/* Local echo into scrollback */
		term_ansi_write(t, "\033[92m> \033[0m", 11);
		if (t->input_len > 0)
			term_ansi_write(t, t->input, (size_t)t->input_len);
		term_ansi_write(t, "\n", 1);

		t->input[0] = '\0';
		t->input_len = 0;
		t->input_cursor = 0;
		t->hist_pos = -1;
		t->scroll_off = 0;
		return 1;
	}

	if (key == KEY_BACKSPACE) {
		if (t->input_cursor <= 0) return 0;
		int prev = t->input_cursor - 1;
		while (prev > 0 && ((unsigned char)t->input[prev] & 0xC0) == 0x80)
			prev--;
		int diff = t->input_cursor - prev;
		memmove(t->input + prev, t->input + t->input_cursor,
		        (size_t)(t->input_len - t->input_cursor + 1));
		t->input_len -= diff;
		t->input_cursor = prev;
		return 1;
	}

	if (key == KEY_DELETE) {
		if (t->input_cursor >= t->input_len) return 0;
		int next = t->input_cursor + 1;
		while (next < t->input_len && ((unsigned char)t->input[next] & 0xC0) == 0x80)
			next++;
		int diff = next - t->input_cursor;
		memmove(t->input + t->input_cursor, t->input + next,
		        (size_t)(t->input_len - next + 1));
		t->input_len -= diff;
		return 1;
	}

	if (key == KEY_LEFT) {
		if (t->input_cursor > 0) {
			t->input_cursor--;
			while (t->input_cursor > 0 &&
			       ((unsigned char)t->input[t->input_cursor] & 0xC0) == 0x80)
				t->input_cursor--;
			return 1;
		}
		return 0;
	}

	if (key == KEY_RIGHT) {
		if (t->input_cursor < t->input_len) {
			t->input_cursor++;
			while (t->input_cursor < t->input_len &&
			       ((unsigned char)t->input[t->input_cursor] & 0xC0) == 0x80)
				t->input_cursor++;
			return 1;
		}
		return 0;
	}

	if (key == KEY_HOME) {
		t->input_cursor = 0;
		return 1;
	}

	if (key == KEY_END) {
		t->input_cursor = t->input_len;
		return 1;
	}

	if (key == KEY_UP && t->hist_count > 0) {
		if (t->hist_pos < 0)
			t->hist_pos = t->hist_count - 1;
		else if (t->hist_pos > 0)
			t->hist_pos--;
		snprintf(t->input, sizeof(t->input), "%s", t->history[t->hist_pos]);
		t->input_len = (int)strlen(t->input);
		t->input_cursor = t->input_len;
		return 1;
	}

	if (key == KEY_DOWN && t->hist_pos >= 0) {
		if (t->hist_pos < t->hist_count - 1) {
			t->hist_pos++;
			snprintf(t->input, sizeof(t->input), "%s", t->history[t->hist_pos]);
			t->input_len = (int)strlen(t->input);
			t->input_cursor = t->input_len;
		} else {
			t->hist_pos = -1;
			t->input[0] = '\0';
			t->input_len = 0;
			t->input_cursor = 0;
		}
		return 1;
	}

	return 0;
}

static const term_line_t *get_line_from_bottom(const term_ansi_t *t, int idx_from_bottom)
{
	if (!t || idx_from_bottom < 0 || idx_from_bottom >= t->total_lines)
		return NULL;
	int slot = (t->head - idx_from_bottom) % TERM_MAX_LINES;
	if (slot < 0) slot += TERM_MAX_LINES;
	return &t->lines[slot];
}

void term_ansi_set_style(term_ansi_t *t, uint32_t bg_fill, int text_shadow)
{
	if (!t) return;
	t->bg_fill = bg_fill;
	t->text_shadow = text_shadow ? 1 : 0;
}

typedef struct {
	const term_line_t *ln;
	int col_start;
	int col_len;
} term_vis_slice_t;

void term_ansi_render(term_ansi_t *t, uint32_t mono_font, int font_px,
                      int x, int y, int w, int h, int focused)
{
	if (!t || w <= 4 || h <= 4)
		return;

	/* Paint terminal backdrop only when non-transparent (matches .xterm-viewport vs #rpg-chat #term) */
	if (t->bg_fill & 0xFF000000u)
		lam_fill(x, y, w, h, t->bg_fill);

	if (mono_font == UINT32_MAX)
		return;

	if (font_px < 10) font_px = 13;
	uint32_t lh = lam_font_line_height(mono_font, (uint32_t)font_px);
	if (lh < 12) lh = (uint32_t)(font_px + 3);

	uint32_t cell_w = 0, tmp_h = 0;
	lam_font_measure_px(&cell_w, &tmp_h, mono_font, "M", 0, 0, 1000, 1000,
	                    (uint32_t)font_px,
	                    LAM_UI_WS_NOWRAP, LAM_UI_WB_NORMAL);
	if (cell_w < 6) cell_w = 8;

	int pad = 6;
	int inner_w = w - pad * 2;
	if (inner_w < (int)cell_w)
		inner_w = (int)cell_w;
	int vis_cols = inner_w / (int)cell_w;
	if (vis_cols < 1) vis_cols = 1;
	if (vis_cols > TERM_MAX_COLS) vis_cols = TERM_MAX_COLS;

	int show_input = (focused || t->input_len > 0);
	int input_bar_h = show_input ? ((int)lh + 8) : 0;
	if (input_bar_h > h)
		input_bar_h = h;

	int scroll_h = h - pad * 2 - input_bar_h;
	int vis_rows = (scroll_h >= (int)lh) ? (scroll_h / (int)lh) : 0;
	if (vis_rows > 256)
		vis_rows = 256;

	/* Skip empty trailing current line if nothing has been written on it yet */
	int logical_skip = (t->total_lines > 1 && t->lines[t->head].len == 0) ? 1 : 0;
	int n_logical = t->total_lines - logical_skip;
	if (n_logical < 0) n_logical = 0;

	/* Soft-wrap logical lines into visual rows and clamp scroll_off strictly */
	int total_vis = 0;
	for (int i = 0; i < n_logical; i++) {
		const term_line_t *ln = get_line_from_bottom(t, logical_skip + i);
		int len = ln ? (int)ln->len : 0;
		total_vis += (len > 0) ? ((len + vis_cols - 1) / vis_cols) : 1;
	}

	int max_off = (total_vis > vis_rows && vis_rows > 0) ? (total_vis - vis_rows) : 0;
	t->max_scroll_off = max_off;
	if (t->scroll_off > max_off)
		t->scroll_off = max_off;
	if (t->scroll_off < 0)
		t->scroll_off = 0;

	term_vis_slice_t slices[256];
	int n_slices = 0;
	int skip = t->scroll_off;
	for (int i = 0; i < n_logical && n_slices < vis_rows; i++) {
		const term_line_t *ln = get_line_from_bottom(t, logical_skip + i);
		int len = ln ? (int)ln->len : 0;
		int nrows = (len > 0) ? ((len + vis_cols - 1) / vis_cols) : 1;
		for (int sr = nrows - 1; sr >= 0 && n_slices < vis_rows; sr--) {
			if (skip > 0) {
				skip--;
				continue;
			}
			int cs = sr * vis_cols;
			int cl = len - cs;
			if (cl < 0) cl = 0;
			if (cl > vis_cols) cl = vis_cols;
			slices[n_slices].ln = ln;
			slices[n_slices].col_start = cs;
			slices[n_slices].col_len = cl;
			n_slices++;
		}
	}

	int start_y = y + pad;
	int max_scroll_y = y + h - input_bar_h;
	int right_edge = x + w - pad;
	char run_buf[TERM_MAX_COLS * 4 + 4];
	uint32_t clip_x1 = (uint32_t)(right_edge > 0 ? right_edge : 0);
	uint32_t clip_y1 = (uint32_t)(max_scroll_y > 0 ? max_scroll_y : 0);

	for (int r = 0; r < n_slices; r++) {
		const term_vis_slice_t *sl = &slices[n_slices - 1 - r];
		if (!sl->ln || sl->col_len <= 0)
			continue;

		int draw_y = start_y + r * (int)lh;
		if (draw_y < y || draw_y + (int)lh > max_scroll_y)
			break;

		int col = sl->col_start;
		int col_end = sl->col_start + sl->col_len;
		int pen_x = x + pad;

		while (col < col_end && pen_x < right_edge) {
			uint32_t fg = sl->ln->cells[col].fg;
			uint32_t bg = sl->ln->cells[col].bg;
			int run_start = col;
			size_t rlen = 0;
			while (col < col_end &&
			       sl->ln->cells[col].fg == fg &&
			       sl->ln->cells[col].bg == bg &&
			       rlen + 4 < sizeof(run_buf)) {
				char enc[4];
				int n = utf8_encode(sl->ln->cells[col].cp, enc);
				memcpy(run_buf + rlen, enc, (size_t)n);
				rlen += (size_t)n;
				col++;
			}
			run_buf[rlen] = '\0';

			uint32_t run_w = 0, run_h = 0;
			lam_font_measure_px(&run_w, &run_h, mono_font, run_buf,
			                    0, 0, (uint32_t)(right_edge - pen_x), lh,
			                    (uint32_t)font_px,
			                    LAM_UI_WS_NOWRAP, LAM_UI_WB_NORMAL);
			int adv_w = (run_w > 0) ? (int)run_w : (col - run_start) * (int)cell_w;

			if (((bg >> 24) & 0xFFu) > 0) {
				int rw = adv_w;
				if (pen_x + rw > right_edge) rw = right_edge - pen_x;
				if (rw > 0)
					lam_fill(pen_x, draw_y, rw, (int)lh, bg);
			}
			if (t->text_shadow) {
				lam_tint(LAM_RGBA(0, 0, 0, 150));
				lam_font_draw_px(mono_font, run_buf,
				                 (uint32_t)(pen_x > 0 ? pen_x : 0),
				                 (uint32_t)(draw_y + 1),
				                 clip_x1, clip_y1 + 1,
				                 (uint32_t)font_px,
				                 LAM_UI_WS_NOWRAP, LAM_UI_WB_NORMAL);
			}
			lam_tint(fg);
			lam_font_draw_px(mono_font, run_buf,
			                 (uint32_t)(pen_x > 0 ? pen_x : 0),
			                 (uint32_t)draw_y,
			                 clip_x1, clip_y1,
			                 (uint32_t)font_px,
			                 LAM_UI_WS_NOWRAP, LAM_UI_WB_NORMAL);
			pen_x += adv_w;
		}
	}

	/* Subtle scrollbar indicator when scrollback exceeds visible rows */
	if (total_vis > vis_rows && vis_rows > 0 && scroll_h >= 12 && max_off > 0) {
		int sb_x = x + w - 4;
		int sb_y = y + pad;
		int sb_h = scroll_h;
		int thumb_h = (sb_h * vis_rows) / total_vis;
		if (thumb_h < 12) thumb_h = 12;
		if (thumb_h > sb_h) thumb_h = sb_h;
		int thumb_y = sb_y + (int)((int64_t)(sb_h - thumb_h) * (max_off - t->scroll_off) / max_off);
		lam_fill(sb_x, sb_y, 3, sb_h, LAM_RGBA(255, 255, 255, 20));
		lam_fill(sb_x, thumb_y, 3, thumb_h, LAM_RGBA(147, 173, 165, 110));
	}

	/* Interactive input bar at bottom (only visible when focused or editing) */
	if (show_input && input_bar_h >= (int)lh + 4) {
		int iy = y + h - input_bar_h;
		uint32_t full_y1 = (uint32_t)(y + h > 0 ? y + h : 0);
		lam_fill(x + 1, iy, w - 2, input_bar_h - 1,
		         focused ? LAM_RGBA(16, 24, 22, 180) : LAM_RGBA(12, 16, 18, 120));
		lam_fill(x + 1, iy, w - 2, 1,
		         focused ? LAM_RGB(0x73, 0xfa, 0x91) : LAM_RGBA(149, 137, 197, 90));

		int text_y = iy + 4;
		uint32_t ty_u = (uint32_t)(text_y > 0 ? text_y : 0);
		lam_tint(focused ? LAM_RGB(0x73, 0xfa, 0x91) : LAM_RGB(0x64, 0x7d, 0x75));
		lam_font_draw_px(mono_font, "> ",
		                 (uint32_t)(x + pad > 0 ? x + pad : 0), ty_u,
		                 clip_x1, full_y1, (uint32_t)font_px,
		                 LAM_UI_WS_NOWRAP, LAM_UI_WB_NORMAL);

		int prompt_x = x + pad + (int)cell_w * 2;
		int avail_in_w = right_edge - prompt_x;
		if (avail_in_w > 4) {
			int clen = t->input_cursor;
			if (clen < 0) clen = 0;
			if (clen > t->input_len) clen = t->input_len;
			if (clen >= TERM_INPUT_MAX) clen = TERM_INPUT_MAX - 1;

			/* Pan input horizontally so the cursor always stays inside [prompt_x, right_edge - 4] */
			int view_start = 0;
			uint32_t vis_cursor_px = 0;
			char prefix[TERM_INPUT_MAX];
			if (clen > 0) {
				memcpy(prefix, t->input, (size_t)clen);
				prefix[clen] = '\0';
				uint32_t ph = 0;
				lam_font_measure_px(&vis_cursor_px, &ph, mono_font, prefix,
				                    0, 0, 100000, 1000, (uint32_t)font_px,
				                    LAM_UI_WS_NOWRAP, LAM_UI_WB_NORMAL);
				while ((int)vis_cursor_px > avail_in_w - 4 && view_start < clen) {
					view_start++;
					while (view_start < clen &&
					       ((unsigned char)t->input[view_start] & 0xC0) == 0x80)
						view_start++;
					vis_cursor_px = 0;
					if (clen > view_start) {
						lam_font_measure_px(&vis_cursor_px, &ph, mono_font,
						                    prefix + view_start,
						                    0, 0, 100000, 1000, (uint32_t)font_px,
						                    LAM_UI_WS_NOWRAP, LAM_UI_WB_NORMAL);
					}
				}
			}

			uint32_t px_u = (uint32_t)(prompt_x > 0 ? prompt_x : 0);
			if (t->input_len > view_start) {
				lam_tint(LAM_RGB(0xe8, 0xf2, 0xee));
				lam_font_draw_px(mono_font, t->input + view_start, px_u, ty_u,
				                 clip_x1, full_y1, (uint32_t)font_px,
				                 LAM_UI_WS_NOWRAP, LAM_UI_WB_NORMAL);
			}

			if (focused) {
				int caret_x = prompt_x + (int)vis_cursor_px;
				if (caret_x > right_edge - 2) caret_x = right_edge - 2;
				if (caret_x < prompt_x) caret_x = prompt_x;
				lam_fill(caret_x, text_y, 2, (int)lh,
				         LAM_RGB(0x73, 0xfa, 0x91));
			}
		}
	}

	lam_tint(0xFFFFFFFFu);
}

int term_ansi_line_count(const term_ansi_t *t)
{
	return t ? t->total_lines : 0;
}

const char *term_ansi_get_line(const term_ansi_t *t, int idx_from_bottom,
                               char *buf, size_t cap)
{
	if (!buf || cap == 0) return "";
	buf[0] = '\0';
	const term_line_t *ln = get_line_from_bottom(t, idx_from_bottom);
	if (!ln) return buf;
	size_t out = 0;
	for (uint16_t i = 0; i < ln->len && out + 4 < cap; i++) {
		char enc[4];
		int n = utf8_encode(ln->cells[i].cp, enc);
		memcpy(buf + out, enc, (size_t)n);
		out += (size_t)n;
	}
	buf[out] = '\0';
	return buf;
}

uint32_t term_ansi_get_cell_fg(const term_ansi_t *t, int idx_from_bottom, int col)
{
	const term_line_t *ln = get_line_from_bottom(t, idx_from_bottom);
	if (!ln || col < 0 || col >= (int)ln->len)
		return 0;
	return ln->cells[col].fg;
}
