/*
 * render.c - Transform the song model into transposed output
 *
 * Ports the historical proc_line semantics onto the parse model: one <b> per
 * chord line, verbatim spacing with diff absorb/add, a spacing queue that
 * realigns lyric lines with space/'-' fillers, verse numbers, comments,
 * hide flags, and TRANSP_BREAK_SLASH. All per-song state (skip_empty,
 * not_special, the queue) is local to transp_render. See CHORDS.md §8.4.
 */

#include "render.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "spelling.h"

#define OUTBUF_MIN 8192

/* Spacing queue — FIFO of { start, len } alignment inserts. A single
 * interleaved array so a grow is one realloc (no partial-failure window). */
typedef struct {
	size_t *pair; /* {start,len} interleaved */
	size_t n;
	size_t cap;
} transp_queue_t;

static int q_push(transp_queue_t *q, size_t start, size_t len)
{
	if (q->n == q->cap) {
		size_t ncap = q->cap ? q->cap * 2 : 8;
		size_t *p = realloc(q->pair, ncap * 2 * sizeof(*p));
		if (!p)
			return 0;
		q->pair = p;
		q->cap = ncap;
	}
	q->pair[2 * q->n] = start;
	q->pair[2 * q->n + 1] = len;
	q->n++;
	return 1;
}

static void q_free(transp_queue_t *q)
{
	free(q->pair);
	memset(q, 0, sizeof(*q));
}

/* Per-render state (crosses lines, unlike the model). */
typedef struct {
	int skip_empty;
	int not_special;
	transp_queue_t q;
} render_state_t;

/* Transpose a chord root index (0-11) and render it, honoring the resolved
 * spell decision (SPELL_SHARP/SPELL_FLAT) and the Latin/English table.
 * Shared spelling encoding: "C#\0Db", … */
static const char *chord_str(char **i18n_table, size_t chord, int spell)
{
	const char *str = i18n_table[chord];
	if (spell == SPELL_FLAT && strchr(str, '#'))
		str += strlen(str) + 1;
	return str;
}

static size_t
html_escape_into(const char *src, char *dst, size_t dstsize, size_t srclen)
{
	size_t w = 0;
	const char *ent;
	size_t elen;

	while (srclen > 0 && *src && w < dstsize - 1) {
		srclen--;
		switch (*src) {
		case '&':
			ent = "&amp;";
			elen = 5;
			break;
		case '<':
			ent = "&lt;";
			elen = 4;
			break;
		case '>':
			ent = "&gt;";
			elen = 4;
			break;
		case '"':
			ent = "&quot;";
			elen = 6;
			break;
		default:
			dst[w++] = *src++;
			continue;
		}
		if (w + elen >= dstsize)
			break;
		memcpy(dst + w, ent, elen);
		w += elen;
		src++;
	}
	dst[w] = '\0';
	return w;
}

/* Append s[0..n) to out, advancing *o; guards against overflow. */
static void
out_append(char *out, size_t outsz, size_t *o, const char *s, size_t n)
{
	size_t space = outsz > *o ? outsz - *o : 0;
	if (space == 0)
		return;
	if (n > space - 1)
		n = space - 1;
	memcpy(out + *o, s, n);
	*o += n;
	out[*o] = '\0';
}

/* Emit a lyric line's fillers for queue entries that start at/behind j.
 * Returns the number of filler chars written. */
static void lyric_fill(
        render_state_t *st, int html, char *out, size_t outsz, size_t *o,
        size_t *j)
{
	while (st->q.n > 0 && *j >= st->q.pair[0]) {
		size_t start = st->q.pair[0];
		size_t len = st->q.pair[1];
		if (st->not_special) {
			while (*j < start + len) {
				char c = (*o > 0 && out[*o - 1] == ' ') ? ' '
				                                        : '-';
				out_append(out, outsz, o, &c, 1);
				(*j)++;
			}
		}
		/* drain regardless of not_special */
		if (st->q.n > 1) {
			memmove(&st->q.pair[0], &st->q.pair[2],
			        (st->q.n - 1) * 2 * sizeof(st->q.pair[0]));
		}
		st->q.n--;
	}
	(void)html;
}

/* Render one lyric line (a non-empty, non-comment line that is not a chord
 * line) into out. Walk bytes after the verse prefix, honoring the queue,
 * '<' passthrough, TRANSP_BREAK_SLASH and HTML escaping. */
static void render_lyric_line(
        render_state_t *st, const transp_pline_t *L, int flags, int html,
        char *out, size_t outsz, size_t *o)
{
	size_t j = 0;
	const char *s = L->text + (html ? L->verse_len : 0);
	const char *end = L->text + L->len;

	if (flags & TRANSP_HIDE_LYRICS) {
		/* skip the line entirely; queue left untouched (old proc_line)
		 */
		if (html)
			out_append(out, outsz, o, "<div></div>", 11);
		return;
	}

	if (html) {
		out_append(out, outsz, o, "<div>", 5);
		if (L->has_verse) {
			char esc[64];
			html_escape_into(
			        L->text, esc, sizeof(esc), L->verse_len);
			out_append(out, outsz, o, "<b>", 3);
			out_append(out, outsz, o, esc, strlen(esc));
			out_append(out, outsz, o, "</b>", 4);
		}
	}

	while (s < end) {
		lyric_fill(st, html, out, outsz, o, &j);

		if (*s == '<') {
			/* raw passthrough to end of line (pre-existing markup)
			 */
			out_append(out, outsz, o, s, (size_t)(end - s));
			j = 0;
			goto done;
		}
		if ((flags & TRANSP_BREAK_SLASH) && *s == '/' && s + 1 < end &&
		    s[1] == ' ')
		{
			s += 2;
			out_append(out, outsz, o, "\n", 1);
			j = 0;
			continue;
		}
		if (html) {
			const char *ent = NULL;
			size_t elen = 0;
			switch (*s) {
			case '&':
				ent = "&amp;";
				elen = 5;
				break;
			case '<':
				ent = "&lt;";
				elen = 4;
				break;
			case '>':
				ent = "&gt;";
				elen = 4;
				break;
			case '"':
				ent = "&quot;";
				elen = 6;
				break;
			default:
				break;
			}
			if (ent) {
				out_append(out, outsz, o, ent, elen);
				s++;
				j++;
				continue;
			}
		}
		out_append(out, outsz, o, s, 1);
		s++;
		j++;
	}

done:
	/* fill any remaining queue entries at end of line (if they start exactly here) */
	lyric_fill(st, html, out, outsz, o, &j);
	
	/* discard any remaining queue entries (chord line was longer than lyric line) */
	st->q.n = 0;

	if (html)
		out_append(out, outsz, o, "</div>", 6);
	else
		out_append(out, outsz, o, "\n", 1);
}

/* Render one chord line into out. Returns 1 on success, 0 on OOM (queue
 * push failed). */
static int render_chord_line(
        render_state_t *st, const transp_song_t *song, const transp_pline_t *L,
        int semitones, int flags, int html, char **root_table, char **en_table,
        char **latin_table, int spell, char *out, size_t outsz, size_t *o)
{
	size_t j = 0;
	int has_chords = 0;
	int no_space = 1;

	/* not_special resets per chord line (fixes the historical quirk) */
	st->not_special = 1;

	if (html)
		out_append(out, outsz, o, "<div>", 5);

	if (flags & TRANSP_HIDE_CHORDS) {
		/* emit nothing but the div; j still advances over leads */
		for (size_t i = 0; i < L->ntok; i++)
			j += L->toks[i].lead + L->toks[i].len;
		goto end;
	}

	/* lazy single <b> opened before the first token */
	for (size_t i = 0; i < L->ntok; i++) {
		const transp_ptoken_t *t = &L->toks[i];
		size_t lead = t->lead;

		if (flags & TRANSP_HIDE_LYRICS) {
			/* collapse a lead run to a single space after the first
			 * token, matching the historical no_space/has_chords */
			if (lead > 0 && has_chords) {
				out_append(out, outsz, o, " ", 1);
				no_space = 0;
			}
			j += lead;
		} else {
			out_append(out, outsz, o, t->text - lead, lead);
			j += lead;
		}

		if (t->info.kind == TRANSP_TOK_SPECIAL ||
		    t->info.kind == TRANSP_TOK_SEP)
		{
			if (html && !has_chords && !no_space) {
				/* not first token */
			}
			if (html && !has_chords) {
				out_append(out, outsz, o, "<b>", 3);
				no_space = 0;
			}
			if (html && has_chords && !no_space) {
				/* nothing */
			}
			has_chords = 1;
			if (t->info.kind == TRANSP_TOK_SPECIAL)
				st->not_special = 0;
			out_append(out, outsz, o, t->text, t->len);
			j += t->len;
			continue;
		}

		/* CHORD token */
		if (html && !has_chords) {
			out_append(out, outsz, o, "<b>", 3);
			no_space = 0;
		}
		has_chords = 1;
		no_space = 1;

		int root = t->info.root;
		const char *new_cstr =
		        chord_str(root_table, (size_t)((root + semitones) % 12), spell);
		size_t new_len = strlen(new_cstr);
		size_t root_len = t->info.root_len;
		size_t diff = new_len > root_len ? new_len - root_len : 0;
		size_t mod_len = t->info.mod_len;
		const char *mod = t->text + t->info.mod_off;

		/* Mod emission: the suffix is copied verbatim (never transposed),
		 * except that a slash bass is re-emitted family-spelled (CHORDS.md
		 * §10.3 step 3). prefix_len covers the suffix up to (not including)
		 * the '/' separator; a leading 'm' becomes '-' under Latin output.
		 * The split emits avoid a fixed stack buffer (mod_len is unbounded
		 * for long 'm'-runs). */
		size_t prefix_len = mod_len;
		const char *bass_name = NULL;
		if (t->info.bass >= 0) {
			prefix_len = t->info.bass_off - t->info.root_len;
			
			int bass_latin = 0;
			if (t->info.bass_len >= 2) {
				char b1 = t->text[t->info.bass_off + 1];
				if (b1 != '#' && b1 != 'b')
					bass_latin = 1;
			}
			char **bass_table = bass_latin ? latin_table : en_table;
			bass_name = chord_str(bass_table, (size_t)t->info.bass, spell);
		}

		int latin_m =
		        (prefix_len > 0 && mod[0] == 'm' &&
		         (flags & TRANSP_LATIN));

		/* Diff/absorb: grow the root, eating up to diff spaces from the
		 * following token's lead (port of the historical logic). */
		const transp_ptoken_t *next =
		        i + 1 < L->ntok ? &L->toks[i + 1] : NULL;
		size_t i_absorb = 0;
		if (!next) {
			/* end of line: absorb into the line's tail */
			j += diff;
			i_absorb = diff;
		} else {
			i_absorb = diff < next->lead ? diff : next->lead;
			j += i_absorb;
		}

		out_append(out, outsz, o, new_cstr, new_len);
		if (latin_m) {
			out_append(out, outsz, o, "-", 1);
			if (prefix_len > 1)
				out_append(out, outsz, o, mod + 1, prefix_len - 1);
		} else if (prefix_len > 0) {
			out_append(out, outsz, o, mod, prefix_len);
		}
		if (bass_name) {
			/* the '/' separator is the last byte of the prefix (the
			 * bass starts at mod[prefix_len]) */
			out_append(out, outsz, o, bass_name, strlen(bass_name));
		}
		j += root_len + mod_len;

		/* If the following char is not space/slash/NUL, add one space
		 * and grow the diff (this does NOT advance j, like the old
		 * proc_line). */
		if (next && next->lead - i_absorb == 0 &&
		    next->text[0] != '/' && next->text[0] != '\0')
		{
			out_append(out, outsz, o, " ", 1);
			diff++;
		}

		if (i_absorb < diff) {
			if (!q_push(&st->q, j, diff - i_absorb))
				return 0;
			j += diff - i_absorb;
		}
	}

end:
	if (html) {
		if (has_chords)
			out_append(out, outsz, o, "</b>", 4);
		/* space guard for empty divs */
		if (*o < 6)
			out_append(out, outsz, o, " ", 1);
		out_append(out, outsz, o, "</div>", 6);
	} else {
		out_append(out, outsz, o, "\n", 1);
	}
	return 1;
}

/* Render one comment line into out. */
static void render_comment_line(
        render_state_t *st, const transp_pline_t *L, int flags, int html,
        char *out, size_t outsz, size_t *o)
{
	size_t lead = 0;
	const char *p = L->text;
	while (p < L->text + L->len && *p == ' ')
		p++, lead++;

	if (flags & TRANSP_REMOVE_COMMENTS)
		st->skip_empty = 1;

	if (html) {
		out_append(out, outsz, o, "<div>", 5);
		if (lead > 0)
			out_append(out, outsz, o, L->text, lead);
		if (!(flags & TRANSP_REMOVE_COMMENTS)) {
			char esc[4096];
			const char *body = p;
			if (body < L->text + L->len && *body == '%')
				body++;
			html_escape_into(
			        body, esc, sizeof(esc),
			        (size_t)(L->text + L->len - body));
			out_append(out, outsz, o, "<b class='comment'>", 19);
			out_append(out, outsz, o, esc, strlen(esc));
			out_append(out, outsz, o, "</b>", 4);
		}
		if (*o < 6)
			out_append(out, outsz, o, " ", 1);
		out_append(out, outsz, o, "</div>", 6);
	} else {
		out_append(out, outsz, o, L->text, L->len);
		out_append(out, outsz, o, "\n", 1);
	}
}

/* Render one empty line into out. */
static void render_empty_line(
        render_state_t *st, int html, char *out, size_t outsz, size_t *o)
{
	if (st->skip_empty) {
		st->skip_empty = 0;
		return;
	}
	if (html) {
		out_append(out, outsz, o, "<div> </div>", 12);
	} else {
		out_append(out, outsz, o, "\n", 1);
	}
}

char *transp_render(
        const transp_song_t *song, int semitones, int flags, char **en_table,
        char **latin_table, int *key)
{
	int html = (flags & TRANSP_HTML) != 0;
	render_state_t st;
	size_t total = 64;
	char *out;
	char **root_table = (flags & TRANSP_LATIN) ? latin_table : en_table;

	/* Key-aware spelling: the family derives from the TARGET tonic
	 * ((detected key + semitones) % 12); TRANSP_BEMOL overrides to flats.
	 * semitones arrives already normalized to 0..11 by transp_buffer. */
	int family = (*key >= 0) ? spelling_family((*key + semitones) % 12)
	                         : SPELL_FAMILY_SHARP;
	int spell =
	        ((flags & TRANSP_BEMOL) || family == SPELL_FAMILY_FLAT)
	                ? SPELL_FLAT
	                : SPELL_SHARP;

	/* Size the result: sum of line lengths * 8 + 64 (HTML wrapping and
	 * longer chord names can expand each line significantly). */
	for (size_t i = 0; i < song->nlines; i++)
		total += song->lines[i].len * 8;

	out = malloc(total);
	if (!out)
		return NULL;
	out[0] = '\0';

	memset(&st, 0, sizeof(st));
	st.not_special = 1;

	for (size_t i = 0; i < song->nlines; i++) {
		const transp_pline_t *L = &song->lines[i];
		size_t o = strlen(out);
		size_t outsz = total;

		if (L->is_empty) {
			render_empty_line(&st, html, out, outsz, &o);
		} else if (L->is_comment) {
			render_comment_line(
			        &st, L, flags, html, out, outsz, &o);
		} else if (L->is_chord_line) {
			if (!render_chord_line(
			            &st, song, L, semitones, flags, html,
			            root_table, en_table, latin_table, spell, out, outsz, &o))
				goto oom;
		} else {
			render_lyric_line(&st, L, flags, html, out, outsz, &o);
		}
	}

	q_free(&st.q);
	return out;
oom:
	q_free(&st.q);
	free(out);
	return NULL;
}
