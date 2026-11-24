#ifndef BUD_LAMINA_TERM_ANSI_H
#define BUD_LAMINA_TERM_ANSI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct term_ansi term_ansi_t;

term_ansi_t *term_ansi_create(void);
void         term_ansi_destroy(term_ansi_t *t);

/* Feed raw bytes (UTF-8 + ANSI SGR CSI + Telnet IAC sequences). */
void term_ansi_write(term_ansi_t *t, const char *data, size_t len);

/* Clear scrollback buffer. */
void term_ansi_clear(term_ansi_t *t);

/* Scrollback navigation (positive = scroll up into history). */
void term_ansi_scroll(term_ansi_t *t, int delta_lines);

/* Interactive command line editing (returns 1 if buffer changed). */
int  term_ansi_input_char(term_ansi_t *t, uint32_t cp);
int  term_ansi_input_key(term_ansi_t *t, int key, int mods,
                         char *submit_out, size_t submit_cap);

/* Configure terminal backdrop fill (0 = transparent) and text-shadow flag. */
void term_ansi_set_style(term_ansi_t *t, uint32_t bg_fill, int text_shadow);

/* Render terminal scrollback + optional command line inside [x,y,w,h]. */
void term_ansi_render(term_ansi_t *t, uint32_t mono_font, int font_px,
                      int x, int y, int w, int h, int focused);

/* Introspection (for unit tests). */
int         term_ansi_line_count(const term_ansi_t *t);
const char *term_ansi_get_line(const term_ansi_t *t, int idx_from_bottom,
                               char *buf, size_t cap);
uint32_t    term_ansi_get_cell_fg(const term_ansi_t *t, int idx_from_bottom,
                                  int col);

#ifdef __cplusplus
}
#endif

#endif /* BUD_LAMINA_TERM_ANSI_H */
