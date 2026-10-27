#ifndef CHAT_MARKDOWN_H
#define CHAT_MARKDOWN_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Simple, fast Markdown-to-HTML parser for chat messages */
typedef struct {
	char *data;
	size_t len;
	size_t cap;
} chat_md_buf_t;

static int chat_buf_grow(chat_md_buf_t *b, size_t needed)
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

static int chat_buf_append(chat_md_buf_t *b, const char *s, size_t n)
{
	if (chat_buf_grow(b, n) != 0)
		return -1;
	memcpy(b->data + b->len, s, n);
	b->len += n;
	b->data[b->len] = '\0';
	return 0;
}

static int chat_buf_appends(chat_md_buf_t *b, const char *s)
{
	return chat_buf_append(b, s, strlen(s));
}

static int chat_buf_append_escaped(chat_md_buf_t *b, const char *s, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		char c = s[i];
		switch (c) {
		case '&':
			if (chat_buf_appends(b, "&amp;") != 0) return -1;
			break;
		case '<':
			if (chat_buf_appends(b, "&lt;") != 0) return -1;
			break;
		case '>':
			if (chat_buf_appends(b, "&gt;") != 0) return -1;
			break;
		case '"':
			if (chat_buf_appends(b, "&quot;") != 0) return -1;
			break;
		default:
			if (chat_buf_append(b, &c, 1) != 0) return -1;
			break;
		}
	}
	return 0;
}

static char *chat_markdown_to_html(const char *md)
{
	if (!md) return strdup("");

	/* If content already looks like HTML (e.g. <table>, <div>, <p>), pass through safely */
	if (strstr(md, "<table>") != NULL || strstr(md, "<div") != NULL) {
		return strdup(md);
	}

	chat_md_buf_t out;
	out.cap = strlen(md) * 2 + 128;
	out.len = 0;
	out.data = malloc(out.cap);
	if (!out.data) return NULL;
	out.data[0] = '\0';

	const char *p = md;
	/* Skip leading blank lines */
	while (*p == '\n' || *p == '\r') p++;

	int in_code_block = 0;
	int in_list = 0;

	while (*p) {
		/* Line slice */
		const char *line_start = p;
		while (*p && *p != '\n' && *p != '\r') p++;
		size_t line_len = (size_t)(p - line_start);
		if (*p == '\r' && *(p + 1) == '\n') p += 2;
		else if (*p == '\n' || *p == '\r') p++;

		/* Check for fenced code block */
		if (line_len >= 3 && line_start[0] == '`' && line_start[1] == '`' && line_start[2] == '`') {
			if (in_code_block) {
				chat_buf_appends(&out, "</code></pre>\n");
				in_code_block = 0;
			} else {
				if (in_list) {
					chat_buf_appends(&out, "</ul>\n");
					in_list = 0;
				}
				chat_buf_appends(&out, "<pre class='chat-code-block'><code>");
				in_code_block = 1;
			}
			continue;
		}

		if (in_code_block) {
			chat_buf_append_escaped(&out, line_start, line_len);
			chat_buf_appends(&out, "\n");
			continue;
		}

		/* Empty line */
		if (line_len == 0) {
			if (in_list) {
				chat_buf_appends(&out, "</ul>\n");
				in_list = 0;
			}
			continue;
		}

		/* List items: -, *, or number. */
		if ((line_len >= 2 && (line_start[0] == '-' || line_start[0] == '*') && line_start[1] == ' ') ||
		    (line_len >= 3 && line_start[0] >= '0' && line_start[0] <= '9' && line_start[1] == '.' && line_start[2] == ' '))
		{
			if (!in_list) {
				chat_buf_appends(&out, "<ul class='chat-list'>\n");
				in_list = 1;
			}
			size_t prefix_len = (line_start[1] == ' ') ? 2 : 3;
			chat_buf_appends(&out, "<li>");
			chat_buf_append_escaped(&out, line_start + prefix_len, line_len - prefix_len);
			chat_buf_appends(&out, "</li>\n");
			continue;
		}

		if (in_list) {
			chat_buf_appends(&out, "</ul>\n");
			in_list = 0;
		}

		/* Headers: #, ##, ### */
		if (line_len >= 2 && line_start[0] == '#') {
			int level = 0;
			while (level < 4 && level < (int)line_len && line_start[level] == '#')
				level++;
			if (level < (int)line_len && line_start[level] == ' ') {
				char hopen[16], hclose[16];
				snprintf(hopen, sizeof(hopen), "<h%d>", level + 1);
				snprintf(hclose, sizeof(hclose), "</h%d>\n", level + 1);
				chat_buf_appends(&out, hopen);
				chat_buf_append_escaped(&out, line_start + level + 1, line_len - level - 1);
				chat_buf_appends(&out, hclose);
				continue;
			}
		}

		/* Regular paragraph line */
		chat_buf_appends(&out, "<p>");
		/* Inline formatting: bold **text** or inline `code` */
		size_t i = 0;
		while (i < line_len) {
			if (i + 1 < line_len && line_start[i] == '*' && line_start[i + 1] == '*') {
				const char *close_b = strstr(line_start + i + 2, "**");
				if (close_b && (size_t)(close_b - line_start) < line_len) {
					chat_buf_appends(&out, "<strong>");
					size_t inner_len = (size_t)(close_b - (line_start + i + 2));
					chat_buf_append_escaped(&out, line_start + i + 2, inner_len);
					chat_buf_appends(&out, "</strong>");
					i = (size_t)(close_b - line_start) + 2;
					continue;
				}
			} else if (line_start[i] == '`') {
				const char *close_c = strchr(line_start + i + 1, '`');
				if (close_c && (size_t)(close_c - line_start) < line_len) {
					chat_buf_appends(&out, "<code>");
					size_t inner_len = (size_t)(close_c - (line_start + i + 1));
					chat_buf_append_escaped(&out, line_start + i + 1, inner_len);
					chat_buf_appends(&out, "</code>");
					i = (size_t)(close_c - line_start) + 1;
					continue;
				}
			}
			chat_buf_append_escaped(&out, line_start + i, 1);
			i++;
		}
		chat_buf_appends(&out, "</p>\n");
	}

	if (in_code_block) {
		chat_buf_appends(&out, "</code></pre>\n");
	}
	if (in_list) {
		chat_buf_appends(&out, "</ul>\n");
	}

	return out.data;
}

#endif
