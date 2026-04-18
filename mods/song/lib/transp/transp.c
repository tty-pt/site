/*
 * transp.c - Chord transposition library implementation
 * 
 * Refactored from tty.pt/items/chords/src/transp/src/transp.c
 * Changes: wchar_t → UTF-8 char*, globals → context, library API
 */

#include "transp.h"

#include <ctype.h>
#include <locale.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>

#include <ttypt/qmap.h>

/* Spacing queue - tracks chord length differences for lyrics alignment */
struct space_queue {
	unsigned len;        /* Number of spaces to add */
	unsigned start;      /* Position in lyric line to insert */
	TAILQ_ENTRY(space_queue) entries;
};

/* Transposer context */
struct transp_ctx {
	int chord_db;        /* qmap handle for chord lookup */
	int special_db;      /* qmap handle for special symbols */
	unsigned key;        /* Detected key (0-11 or -1) */
	char **i18n_table;   /* chromatic_en or chromatic_latin */
	TAILQ_HEAD(queue_head, space_queue) queue;  /* Spacing queue */
	
	/* State variables for proc_line */
	int skip_empty;      /* Skip next empty line */
	int not_special;     /* Not a special line */
};

/* Chromatic scale tables */
static char *chromatic_en[] = {
	"C\0",
	"C#\0Db",
	"D\0",
	"D#\0Eb",
	"E\0",
	"F\0",
	"F#\0Gb",
	"G\0",
	"G#\0Ab",
	"A\0",
	"A#\0Bb",
	"B\0",
	NULL,
};

static char *chromatic_latin[] = {
	"Do\0",
	"Do#\0Reb",
	"Re\0",
	"Re#\0Mib",
	"Mi\0",
	"Fa\0",
	"Fa#\0Solb",
	"Sol\0",
	"Sol#\0Lab",
	"La\0",
	"La#\0Sib",
	"Si\0",
	NULL,
};

/* Special symbols (rhythm markers) */
static char *special[] = {
	"|",
	":",
	"-",
	NULL,
};

/* Get chord string with sharp or flat notation */
static inline char *
chord_str(transp_ctx_t *ctx, size_t chord, int flags)
{
	char *str = ctx->i18n_table[chord];
	if ((flags & TRANSP_BEMOL) && strchr(str, '#'))
		str += strlen(str) + 1;
	return str;
}

/* Safe snprintf wrapper that returns characters written */
static inline int
outprintf(char *buf, size_t bufsize, size_t offset, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	int ret = vsnprintf(buf + offset, bufsize - offset, fmt, args);
	va_end(args);
	return ret > 0 ? ret : 0;
}

/* Initialize qmap with chord table */
static void
tbl_init(int hd, char **table)
{
	for (unsigned u = 0; ; u++) {
		char *key = table[u];
		if (!key)
			break;
		qmap_put(hd, key, &u);
		key += strlen(key) + 1;
		if (*key)
			qmap_put(hd, key, &u);
	}
}

/* Process a single line of chord chart */
static char *
proc_line(transp_ctx_t *ctx, const char *line, int t, int flags, int *skip_empty_out)
{
	char outbuf[8192];  /* Output buffer */
	char buf[8];
	const char *s = line;
	char *o = outbuf;
	int not_bolded = 1, is_special = 0;
	unsigned j = 0;
	int sim = 0, si = 0;  /* sim = offset in input, si = offset in output */
	
	/* Remove trailing newline */
	size_t linelen = strlen(line);
	char *line_copy = strdup(line);
	if (linelen > 0 && line_copy[linelen - 1] == '\n')
		line_copy[linelen - 1] = '\0';
	linelen = strlen(line_copy);
	if (linelen > 0 && line_copy[linelen - 1] == '\r')
		line_copy[linelen - 1] = '\0';
	
	/* Handle skip empty */
	if (ctx->skip_empty && strcmp(line_copy, "") == 0) {
		ctx->skip_empty = 0;
		free(line_copy);
		if (skip_empty_out) *skip_empty_out = 1;
		return strdup("");
	}
	
	s = line_copy;
	
	/* HTML opening tag */
	if (flags & TRANSP_HTML) {
		si += outprintf(outbuf, sizeof(outbuf), si, "<div>");
		
		/* Handle numbered lines (verses) */
		if (isdigit(*line_copy)) {
			char *dot = strchr(s, '.');
			if (!dot)
				goto end;
			
			size_t len = dot + 1 - s;
			sim += len;
			si += outprintf(outbuf, sizeof(outbuf), si, "<b>%.*s</b>", (int)len, s);
			s += len;
		}
	}
	
	o = outbuf + si;
	
	int no_space = 1, has_chords = 0;
	for (; *s;) {
		/* Handle spaces and slashes */
		if (*s == ' ' || *s == '/') {
			char what = *s;
			if (flags & TRANSP_HIDE_LYRICS) {
				if (no_space && has_chords) {
					*o++ = what;
					no_space = 0;
					s++;
					j++;
					continue;
				}
			} else if (!(flags & TRANSP_HIDE_CHORDS))
				*o++ = what;
			s++;
			j++;
			continue;
		}
		
		/* Handle comments */
		if (*s == '%') {
			if (flags & TRANSP_REMOVE_COMMENTS)
				ctx->skip_empty = 1;
			else if (not_bolded && (flags & TRANSP_HTML))
				o += outprintf(outbuf, sizeof(outbuf), o - outbuf, "<b class='comment'>%s</b>", s);
			else
				o += outprintf(outbuf, sizeof(outbuf), o - outbuf, "%s", s);
			s += strlen(s);
			continue;
		}
		
		no_space = 1;
		
		/* Check for chord */
		int notflat = s[1] != '#' && s[1] != 'b';
		char *eoc, *space_after, *slash_after;
		
		eoc = (char *)s + (!notflat ? 2 : 1);
		space_after = strchr(eoc, ' ');
		slash_after = strchr(eoc, '/');
		
		const unsigned *chord_r;
		unsigned chord = -1;
		
		memset(buf, 0, sizeof(buf));
		strncpy(buf, s, 1);
		
		/* Check if special symbol */
		chord_r = qmap_get(ctx->special_db, buf);
		if ((is_special = !!chord_r)) {
			strncpy(buf, s, space_after ? space_after - s : strlen(s));
			ctx->not_special = 0;
			chord = -1;
		} else {
			/* Validate chord modifiers */
			switch (*eoc) {
				case ' ':
				case '\n':
				case '\0':
				case '/':
				case 'm': break;
				default:
					if (isdigit(*eoc) || !strncmp(eoc, "sus", 3)
							|| !strncmp(eoc, "add", 3)
							|| !strncmp(eoc, "maj", 3)
							|| !strncmp(eoc, "dim", 3))
						break;
					goto no_chord;
			}
		}
		
		if (slash_after && (!space_after || space_after > slash_after))
			space_after = slash_after;
		
		/* Lookup chord in database */
		if (!is_special) {
			memset(buf, 0, sizeof(buf));
			strncpy(buf, s, eoc - s);
			chord_r = qmap_get(ctx->chord_db, buf);
			if (!chord_r)
				goto no_chord;
			chord = *chord_r;
		}
		
		char *new_cstr;
		int len, diff, modlen, i;
		
		has_chords = 1;
		
		if (is_special) {
			new_cstr = (char *)s;
			diff = 0;
			modlen = strlen(buf);
		} else {
			/* Transpose chord */
			if (ctx->key == (unsigned) -1) {
				ctx->key = chord;
			}
			chord = (chord + t) % 12;
			new_cstr = chord_str(ctx, chord, flags);
			len = strlen(buf);
			diff = strlen(new_cstr) - len;
			modlen = space_after ? space_after - eoc : strlen(eoc);
		}
		
		if (flags & TRANSP_HIDE_CHORDS) {
			s = eoc + modlen;
			continue;
		}
		
		/* HTML bold opening tag */
		if (not_bolded && (flags & TRANSP_HTML)) {
			o += outprintf(outbuf, sizeof(outbuf), o - outbuf, "<b>");
			not_bolded = 0;
		}
		
		if (is_special) {
			s = eoc + modlen;
			o += outprintf(outbuf, sizeof(outbuf), o - outbuf, "%s ", buf);
			continue;
		}
		
		/* Copy chord modifiers */
		memset(buf, 0, sizeof(buf));
		strncpy(buf, eoc, modlen);
		j += eoc - s + modlen;
		s = eoc + modlen;
		
		/* Skip absorbed spaces */
		for (i = 0; i < diff && *s == ' '; i++, s++, j++) ;
		
		if (*s == '\0')
			for (i = 0; i < diff; i++, j++) ;
		
		/* Latin notation: 'm' → '-' for minor chords */
		if (buf[0] == 'm' && ctx->i18n_table == chromatic_latin)
			buf[0] = '-';
		
		/* Output transposed chord */
		o += outprintf(outbuf, sizeof(outbuf), o - outbuf, "%s%s", new_cstr, buf);
		
		/* Add space if needed */
		if (*s != ' ' && *s != '/' && *s != '\0') {
			*o++ = ' ';
			diff++;
		}
		
		/* Queue spacing adjustment for lyrics */
		if (i < diff) {
			struct space_queue *new_element = malloc(sizeof(*new_element));
			new_element->len = diff - i;
			new_element->start = j;
			TAILQ_INSERT_TAIL(&ctx->queue, new_element, entries);
			j += diff - i;
		}
	}
	
	goto end;

no_chord:
	/* Line contains lyrics, not chords */
	j = 0;
	o = outbuf + si;  /* Continue from where we left off */
	if (flags & TRANSP_HTML && si == 0) {
		o += outprintf(outbuf, sizeof(outbuf), 0, "<div>");
	}
	if (flags & TRANSP_HIDE_LYRICS) {
		free(line_copy);
		if (flags & TRANSP_HTML)
			return strdup("<div></div>");
		return strdup("");
	}
	
	s = line_copy + sim;
	
	/* Close bold tag if needed */
	if ((flags & TRANSP_HTML) && !not_bolded) {
		o += outprintf(outbuf, sizeof(outbuf), o - outbuf, "</b>");
		not_bolded = 1;
	}
	
	/* Process lyrics with spacing adjustments */
	for (; *s;) {
		/* Apply spacing adjustments from chord line */
		if (!TAILQ_EMPTY(&ctx->queue)) {
			struct space_queue *first = TAILQ_FIRST(&ctx->queue);
			if (j >= first->start) {
				if (ctx->not_special) {
					while (j < first->start + first->len) {
						char c = (o > outbuf && *(o - 1) == ' ') ? ' ' : '-';
						*o++ = c;
						j++;
					}
				}
				TAILQ_REMOVE(&ctx->queue, first, entries);
				free(first);
			}
		}
		
		/* HTML passthrough */
		if (*s == '<') {
			o += outprintf(outbuf, sizeof(outbuf), o - outbuf, "%s", s);
			j = 0;
			goto end;
		}
		
		/* Break on slash */
		if (flags & TRANSP_BREAK_SLASH) {
			if (*s == '/' && *(s + 1) == ' ') {
				s += 2;
				*o++ = '\n';
				j = 0;
				continue;
			}
		}
		
		*o++ = *s++;
		j++;
	}

end:
	/* Close HTML tags */
	if (flags & TRANSP_HTML) {
		if (!not_bolded) {
			o += outprintf(outbuf, sizeof(outbuf), o - outbuf, "</b>");
		}
		/* Add space to prevent empty div */
		if (o - outbuf < 6 && !has_chords)
			*o++ = ' ';
		o += outprintf(outbuf, sizeof(outbuf), o - outbuf, "</div>");
	} else {
		*o++ = '\n';
	}
	*o = '\0';
	
	free(line_copy);
	return strdup(outbuf);
}

/* Public API */

transp_ctx_t *
transp_init(void)
{
	transp_ctx_t *ctx = calloc(1, sizeof(*ctx));
	if (!ctx)
		return NULL;
	
	/* Register qmap type for unsigned */
	unsigned unsigned_type = qmap_reg(sizeof(unsigned));
	
	/* Create in-memory qmap databases */
	ctx->chord_db = qmap_open(NULL, NULL, QM_STR, unsigned_type, 0x1F, 0);
	ctx->special_db = qmap_open(NULL, NULL, QM_STR, unsigned_type, 0xF, 0);
	
	if (ctx->chord_db < 0 || ctx->special_db < 0) {
		free(ctx);
		return NULL;
	}
	
	/* Initialize locale for UTF-8 */
	setlocale(LC_ALL, "en_US.UTF-8");
	
	/* Populate chord and special databases */
	tbl_init(ctx->chord_db, chromatic_en);
	tbl_init(ctx->chord_db, chromatic_latin);
	tbl_init(ctx->special_db, special);
	
	/* Initialize context */
	TAILQ_INIT(&ctx->queue);
	ctx->key = -1;
	ctx->skip_empty = 0;
	ctx->not_special = 1;  /* Default to adding dashes for alignment */
	ctx->i18n_table = chromatic_en;
	
	return ctx;
}

char *
transp_buffer(transp_ctx_t *ctx, const char *input, int semitones, int flags)
{
    if (!ctx || !input)
        return NULL;

    /* Normalize negative transpose */
    if (semitones < 0)
        semitones += (1 + (semitones / 12)) * 12;

    /* Set i18n table based on flags */
    if (flags & TRANSP_LATIN)
        ctx->i18n_table = chromatic_latin;
    else
        ctx->i18n_table = chromatic_en;

    /* Split input by lines manually to respect empty lines (essential for HTML) */
    char *input_copy = strdup(input);
    char *result = malloc(strlen(input) * 8); /* Increased for HTML tags & entities */
    if (!result) {
        free(input_copy);
        return NULL;
    }
    result[0] = '\0';

    char *line_start = input_copy;
    char *line_end;

    while (line_start && *line_start) {
        line_end = strchr(line_start, '\n');
        if (line_end) *line_end = '\0';

        int skip_empty = 0;
        char *transposed = proc_line(ctx, line_start, semitones, flags, &skip_empty);

        if (transposed) {
            if (!skip_empty) {
                strcat(result, transposed);
            }
            free(transposed);
        }

        line_start = line_end ? line_end + 1 : NULL;
    }

    free(input_copy);

    /* Clean up any remaining queue entries (Standard C99 logic) */
    struct space_queue *elem;
    while (!TAILQ_EMPTY(&ctx->queue)) {
        elem = TAILQ_FIRST(&ctx->queue);
        TAILQ_REMOVE(&ctx->queue, elem, entries);
        free(elem);
    }

    return result;
}

int
transp_get_key(transp_ctx_t *ctx)
{
	if (!ctx)
		return -1;
	return ctx->key;
}

void
transp_reset_key(transp_ctx_t *ctx)
{
	if (!ctx)
		return;
	ctx->key = -1;
}

char *
transp_shift_table(transp_ctx_t *ctx, int latin)
{
	if (!ctx || ctx->key == (unsigned) -1)
		return NULL;
	
	char **table = latin ? chromatic_latin : chromatic_en;
	char *result = malloc(512);
	if (!result)
		return NULL;
	
	result[0] = '\0';
	for (unsigned i = 0; i < 12; i++) {
		char *name = table[i];
		long t = (long) i - ctx->key;
		if (t < 0)
			t += 12;
		char line[64];
		snprintf(line, sizeof(line), "%s %ld\n", name, t);
		strcat(result, line);
	}
	
	return result;
}

void
transp_free(transp_ctx_t *ctx)
{
	if (!ctx)
		return;
	
	/* Close qmap databases */
	if (ctx->chord_db >= 0)
		qmap_close(ctx->chord_db);
	if (ctx->special_db >= 0)
		qmap_close(ctx->special_db);
	
	/* Free queue entries */
	while (!TAILQ_EMPTY(&ctx->queue)) {
		struct space_queue *elem = TAILQ_FIRST(&ctx->queue);
		TAILQ_REMOVE(&ctx->queue, elem, entries);
		free(elem);
	}
	
	free(ctx);
}
