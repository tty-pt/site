#include "markdown.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct {
	char *data;
	size_t len;
	size_t cap;
} md_buf_t;

static int buf_init(md_buf_t *b, size_t initial)
{
	b->data = malloc(initial);
	if (!b->data)
		return -1;
	b->len = 0;
	b->cap = initial;
	b->data[0] = '\0';
	return 0;
}

static int buf_grow(md_buf_t *b, size_t needed)
{
	if (b->len + needed + 1 <= b->cap)
		return 0;
	size_t ncap = b->cap * 2;
	if (ncap < b->len + needed + 1)
		ncap = b->len + needed + 1024;
	char *nd = realloc(b->data, ncap);
	if (!nd)
		return -1;
	b->data = nd;
	b->cap = ncap;
	return 0;
}

static int buf_append(md_buf_t *b, const char *s, size_t n)
{
	if (!s || n == 0)
		return 0;
	if (buf_grow(b, n) != 0)
		return -1;
	memcpy(b->data + b->len, s, n);
	b->len += n;
	b->data[b->len] = '\0';
	return 0;
}

static int buf_puts(md_buf_t *b, const char *s)
{
	return s ? buf_append(b, s, strlen(s)) : 0;
}

static int buf_putc(md_buf_t *b, char c)
{
	if (buf_grow(b, 1) != 0)
		return -1;
	b->data[b->len++] = c;
	b->data[b->len] = '\0';
	return 0;
}

static int buf_append_escaped(md_buf_t *b, const char *s, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		char c = s[i];
		switch (c) {
		case '&':
			if (buf_puts(b, "&amp;") != 0) return -1;
			break;
		case '<':
			if (buf_puts(b, "&lt;") != 0) return -1;
			break;
		case '>':
			if (buf_puts(b, "&gt;") != 0) return -1;
			break;
		case '"':
			if (buf_puts(b, "&quot;") != 0) return -1;
			break;
		case '\'':
			if (buf_puts(b, "&#39;") != 0) return -1;
			break;
		default:
			if (buf_putc(b, c) != 0) return -1;
			break;
		}
	}
	return 0;
}

static int is_safe_url(const char *url, size_t len)
{
	if (len >= 11 && strncasecmp(url, "javascript:", 11) == 0)
		return 0;
	if (len >= 5 && strncasecmp(url, "data:", 5) == 0)
		return 0;
	if (len >= 5 && strncasecmp(url, "vbscript:", 9) == 0)
		return 0;
	return 1;
}

static int parse_inline(md_buf_t *out, const char *text, size_t len)
{
	size_t i = 0;
	while (i < len) {
		/* Inline code: `...` */
		if (text[i] == '`') {
			size_t start = i + 1;
			size_t end = start;
			while (end < len && text[end] != '`')
				end++;
			if (end < len) {
				buf_puts(out, "<code>");
				buf_append_escaped(out, text + start, end - start);
				buf_puts(out, "</code>");
				i = end + 1;
				continue;
			}
		}

		/* Markdown link: [text](url) */
		if (text[i] == '[') {
			size_t lstart = i + 1;
			size_t lend = lstart;
			while (lend < len && text[lend] != ']') {
				if (text[lend] == '\\' && lend + 1 < len)
					lend += 2;
				else
					lend++;
			}
			if (lend < len && lend + 1 < len && text[lend + 1] == '(') {
				size_t ustart = lend + 2;
				size_t uend = ustart;
				while (uend < len && text[uend] != ')')
					uend++;
				if (uend < len) {
					size_t ulen = uend - ustart;
					if (is_safe_url(text + ustart, ulen)) {
						int is_external = (ulen > 8 && (strncmp(text + ustart, "http://", 7) == 0 ||
						                                strncmp(text + ustart, "https://", 8) == 0));
						buf_puts(out, "<a href=\"");
						buf_append_escaped(out, text + ustart, ulen);
						if (is_external)
							buf_puts(out, "\" target=\"_blank\" rel=\"noopener noreferrer\">");
						else
							buf_puts(out, "\">");
						parse_inline(out, text + lstart, lend - lstart);
						buf_puts(out, "</a>");
						i = uend + 1;
						continue;
					}
				}
			}
		}

		/* Auto-link raw http:// and https:// URLs */
		if ((i == 0 || isspace((unsigned char)text[i - 1])) &&
		    (strncmp(text + i, "http://", 7) == 0 || strncmp(text + i, "https://", 8) == 0)) {
			size_t ustart = i;
			size_t uend = ustart;
			while (uend < len && !isspace((unsigned char)text[uend]) &&
			       text[uend] != ')' && text[uend] != '>' && text[uend] != ']' && text[uend] != '"')
				uend++;
			/* Trim trailing punctuation */
			while (uend > ustart && (text[uend - 1] == '.' || text[uend - 1] == ',' ||
			                        text[uend - 1] == ';' || text[uend - 1] == ':'))
				uend--;
			size_t ulen = uend - ustart;
			if (ulen > 0) {
				buf_puts(out, "<a href=\"");
				buf_append_escaped(out, text + ustart, ulen);
				buf_puts(out, "\" target=\"_blank\" rel=\"noopener noreferrer\">");
				buf_append_escaped(out, text + ustart, ulen);
				buf_puts(out, "</a>");
				i = uend;
				continue;
			}
		}

		/* Bold & Italic: ***text*** */
		if (i + 2 < len && text[i] == '*' && text[i + 1] == '*' && text[i + 2] == '*') {
			size_t start = i + 3;
			size_t end = start;
			while (end + 2 < len && !(text[end] == '*' && text[end + 1] == '*' && text[end + 2] == '*'))
				end++;
			if (end + 2 < len) {
				buf_puts(out, "<strong><em>");
				parse_inline(out, text + start, end - start);
				buf_puts(out, "</em></strong>");
				i = end + 3;
				continue;
			}
		}

		/* Bold: **text** or __text__ */
		if (i + 1 < len && ((text[i] == '*' && text[i + 1] == '*') || (text[i] == '_' && text[i + 1] == '_'))) {
			char delim = text[i];
			size_t start = i + 2;
			size_t end = start;
			while (end + 1 < len && !(text[end] == delim && text[end + 1] == delim))
				end++;
			if (end + 1 < len) {
				buf_puts(out, "<strong>");
				parse_inline(out, text + start, end - start);
				buf_puts(out, "</strong>");
				i = end + 2;
				continue;
			}
		}

		/* Italic: *text* or _text_ */
		if (text[i] == '*' || text[i] == '_') {
			char delim = text[i];
			size_t start = i + 1;
			size_t end = start;
			while (end < len && text[end] != delim)
				end++;
			if (end < len && end > start) {
				buf_puts(out, "<em>");
				parse_inline(out, text + start, end - start);
				buf_puts(out, "</em>");
				i = end + 1;
				continue;
			}
		}

		/* Default: escape character */
		buf_append_escaped(out, text + i, 1);
		i++;
	}
	return 0;
}

char *markdown_to_html(const char *md)
{
	if (!md)
		return strdup("");

	md_buf_t out;
	if (buf_init(&out, 4096) != 0)
		return NULL;

	const char *p = md;
	int in_code_block = 0;
	int in_ul = 0;
	int in_ol = 0;
	int in_quote = 0;
	int in_para = 0;

	while (*p) {
		/* Find end of line */
		const char *line_start = p;
		while (*p && *p != '\n')
			p++;
		size_t line_len = (size_t)(p - line_start);
		if (line_len > 0 && line_start[line_len - 1] == '\r')
			line_len--;
		if (*p == '\n')
			p++;

		/* Check for fenced code block ``` */
		if (line_len >= 3 && strncmp(line_start, "```", 3) == 0) {
			if (in_code_block) {
				buf_puts(&out, "</code></pre>\n");
				in_code_block = 0;
			} else {
				if (in_para) { buf_puts(&out, "</p>\n"); in_para = 0; }
				if (in_ul) { buf_puts(&out, "</ul>\n"); in_ul = 0; }
				if (in_ol) { buf_puts(&out, "</ol>\n"); in_ol = 0; }
				if (in_quote) { buf_puts(&out, "</blockquote>\n"); in_quote = 0; }

				buf_puts(&out, "<pre><code");
				const char *lang = line_start + 3;
				while (lang < line_start + line_len && isspace((unsigned char)*lang))
					lang++;
				size_t lang_len = (size_t)((line_start + line_len) - lang);
				while (lang_len > 0 && isspace((unsigned char)lang[lang_len - 1]))
					lang_len--;
				if (lang_len > 0) {
					buf_puts(&out, " class=\"language-");
					buf_append_escaped(&out, lang, lang_len);
					buf_puts(&out, "\"");
				}
				buf_puts(&out, ">");
				in_code_block = 1;
			}
			continue;
		}

		if (in_code_block) {
			buf_append_escaped(&out, line_start, line_len);
			buf_putc(&out, '\n');
			continue;
		}

		/* Trim leading whitespace for block inspection */
		size_t trim_start = 0;
		while (trim_start < line_len && isspace((unsigned char)line_start[trim_start]))
			trim_start++;

		/* Blank line */
		if (trim_start == line_len) {
			if (in_para) { buf_puts(&out, "</p>\n"); in_para = 0; }
			if (in_ul) { buf_puts(&out, "</ul>\n"); in_ul = 0; }
			if (in_ol) { buf_puts(&out, "</ol>\n"); in_ol = 0; }
			if (in_quote) { buf_puts(&out, "</blockquote>\n"); in_quote = 0; }
			continue;
		}

		const char *l = line_start + trim_start;
		size_t llen = line_len - trim_start;

		/* Horizontal rule: --- or *** */
		if (llen >= 3 && ((l[0] == '-' && l[1] == '-' && l[2] == '-') ||
		                  (l[0] == '*' && l[1] == '*' && l[2] == '*'))) {
			size_t hr_idx = 0;
			char hr_char = l[0];
			while (hr_idx < llen && (l[hr_idx] == hr_char || isspace((unsigned char)l[hr_idx])))
				hr_idx++;
			if (hr_idx == llen) {
				if (in_para) { buf_puts(&out, "</p>\n"); in_para = 0; }
				if (in_ul) { buf_puts(&out, "</ul>\n"); in_ul = 0; }
				if (in_ol) { buf_puts(&out, "</ol>\n"); in_ol = 0; }
				if (in_quote) { buf_puts(&out, "</blockquote>\n"); in_quote = 0; }
				buf_puts(&out, "<hr>\n");
				continue;
			}
		}

		/* Headings: #, ##, ###, #### */
		if (l[0] == '#') {
			int level = 0;
			while (level < 6 && (size_t)level < llen && l[level] == '#')
				level++;
			if ((size_t)level < llen && l[level] == ' ') {
				if (in_para) { buf_puts(&out, "</p>\n"); in_para = 0; }
				if (in_ul) { buf_puts(&out, "</ul>\n"); in_ul = 0; }
				if (in_ol) { buf_puts(&out, "</ol>\n"); in_ol = 0; }
				if (in_quote) { buf_puts(&out, "</blockquote>\n"); in_quote = 0; }

				size_t hstart = level + 1;
				while (hstart < llen && isspace((unsigned char)l[hstart]))
					hstart++;

				switch (level) {
				case 1:
					buf_puts(&out, "<h1>");
					break;
				case 2:
					buf_puts(&out, "<h2>");
					break;
				case 3:
					buf_puts(&out, "<h3>");
					break;
				default:
					buf_puts(&out, "<h4>");
					break;
				}
				parse_inline(&out, l + hstart, llen - hstart);
				if (level == 1) buf_puts(&out, "</h1>\n");
				else if (level == 2) buf_puts(&out, "</h2>\n");
				else if (level == 3) buf_puts(&out, "</h3>\n");
				else buf_puts(&out, "</h4>\n");
				continue;
			}
		}

		/* Blockquote: > quote */
		if (l[0] == '>') {
			if (in_para) { buf_puts(&out, "</p>\n"); in_para = 0; }
			if (in_ul) { buf_puts(&out, "</ul>\n"); in_ul = 0; }
			if (in_ol) { buf_puts(&out, "</ol>\n"); in_ol = 0; }
			if (!in_quote) {
				buf_puts(&out, "<blockquote>\n");
				in_quote = 1;
			}
			size_t qstart = 1;
			while (qstart < llen && isspace((unsigned char)l[qstart]))
				qstart++;
			parse_inline(&out, l + qstart, llen - qstart);
			buf_puts(&out, "<br>\n");
			continue;
		} else if (in_quote) {
			buf_puts(&out, "</blockquote>\n");
			in_quote = 0;
		}

		/* Unordered list item: - or * */
		if (llen >= 2 && (l[0] == '-' || l[0] == '*') && isspace((unsigned char)l[1])) {
			if (in_para) { buf_puts(&out, "</p>\n"); in_para = 0; }
			if (in_ol) { buf_puts(&out, "</ol>\n"); in_ol = 0; }
			if (!in_ul) {
				buf_puts(&out, "<ul>\n");
				in_ul = 1;
			}
			size_t istart = 2;
			while (istart < llen && isspace((unsigned char)l[istart]))
				istart++;
			buf_puts(&out, "  <li>");
			parse_inline(&out, l + istart, llen - istart);
			buf_puts(&out, "</li>\n");
			continue;
		}

		/* Ordered list item: 1. , 2. etc. */
		int is_ol_item = 0;
		size_t num_digits = 0;
		while (num_digits < llen && isdigit((unsigned char)l[num_digits]))
			num_digits++;
		if (num_digits > 0 && num_digits + 1 < llen && l[num_digits] == '.' && isspace((unsigned char)l[num_digits + 1]))
			is_ol_item = 1;

		if (is_ol_item) {
			if (in_para) { buf_puts(&out, "</p>\n"); in_para = 0; }
			if (in_ul) { buf_puts(&out, "</ul>\n"); in_ul = 0; }
			if (!in_ol) {
				buf_puts(&out, "<ol>\n");
				in_ol = 1;
			}
			size_t istart = num_digits + 2;
			while (istart < llen && isspace((unsigned char)l[istart]))
				istart++;
			buf_puts(&out, "  <li>");
			parse_inline(&out, l + istart, llen - istart);
			buf_puts(&out, "</li>\n");
			continue;
		}

		/* If we were in a list and hit normal text, close list */
		if (in_ul) { buf_puts(&out, "</ul>\n"); in_ul = 0; }
		if (in_ol) { buf_puts(&out, "</ol>\n"); in_ol = 0; }

		/* Paragraph line */
		if (!in_para) {
			buf_puts(&out, "<p>");
			in_para = 1;
		} else {
			buf_putc(&out, ' ');
		}
		parse_inline(&out, l, llen);
	}

	if (in_code_block) buf_puts(&out, "</code></pre>\n");
	if (in_para) buf_puts(&out, "</p>\n");
	if (in_ul) buf_puts(&out, "</ul>\n");
	if (in_ol) buf_puts(&out, "</ol>\n");
	if (in_quote) buf_puts(&out, "</blockquote>\n");

	return out.data;
}
