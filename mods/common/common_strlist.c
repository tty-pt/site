#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stoma/stoma.h>

#include "common_internal.h"

static void strlist_trim(char *s)
{
	if (!s || !s[0])
		return;
	char *p = s;
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
		p++;
	if (p > s)
		memmove(s, p, strlen(p) + 1);
	size_t len = strlen(s);
	while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' ||
	                   s[len - 1] == '\r' || s[len - 1] == '\n'))
	{
		s[--len] = '\0';
	}
}

XY_IMPL(int, str_list_contains, const char *, list, const char *, token)
{
	if (!list || !list[0] || !token || !token[0])
		return 0;

	char copy[8192];
	char *tok;
	char *saveptr;

	snprintf(copy, sizeof(copy), "%s", list);
	tok = strtok_r(copy, "\n", &saveptr);
	while (tok) {
		strlist_trim(tok);
		if (strcmp(tok, token) == 0)
			return 1;
		tok = strtok_r(NULL, "\n", &saveptr);
	}
	return 0;
}

XY_IMPL(int, str_list_append, char *, out, size_t, out_sz,
        const char *, token)
{
	if (!out || out_sz == 0 || !token || !token[0])
		return -1;

	if (str_list_contains(out, token))
		return 0;

	size_t cur_len = strlen(out);
	if (cur_len == 0) {
		snprintf(out, out_sz, "%s", token);
	} else {
		if (cur_len + 1 + strlen(token) >= out_sz)
			return -1;
		snprintf(out + cur_len, out_sz - cur_len, "\n%s", token);
	}
	return 0;
}

XY_IMPL(int, str_list_normalize, const char *, input, char *, out,
        size_t, out_sz)
{
	if (!out || out_sz == 0)
		return -1;

	out[0] = '\0';
	if (!input || !input[0])
		return 0;

	char copy[8192];
	char *tok;
	char *saveptr;

	snprintf(copy, sizeof(copy), "%s", input);
	tok = strtok_r(copy, "\n", &saveptr);
	while (tok) {
		strlist_trim(tok);
		if (tok[0])
			str_list_append(out, out_sz, tok);
		tok = strtok_r(NULL, "\n", &saveptr);
	}
	return 0;
}

XY_IMPL(int, str_list_for_each, const char *, list,
        str_list_cb, cb, void *, user)
{
	char copy[8192];
	char *tok;
	char *saveptr;

	if (!list || !list[0] || !cb)
		return 0;

	snprintf(copy, sizeof(copy), "%s", list);
	tok = strtok_r(copy, "\n", &saveptr);
	while (tok) {
		char t[256];
		int rc;

		snprintf(t, sizeof(t), "%s", tok);
		str_trim(t);
		if (t[0]) {
			rc = cb(t, user);
			if (rc != 0)
				return rc;
		}
		tok = strtok_r(NULL, "\n", &saveptr);
	}
	return 0;
}
