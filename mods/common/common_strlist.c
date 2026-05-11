#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "common_internal.h"

NDX_LISTENER(int, str_list_contains, const char *, list, const char *, token)
{
	if (!list || !list[0] || !token || !token[0])
		return 0;

	char copy[8192];
	char *tok;
	char *saveptr;

	snprintf(copy, sizeof(copy), "%s", list);
	tok = strtok_r(copy, "\n", &saveptr);
	while (tok) {
		if (strcmp(tok, token) == 0)
			return 1;
		tok = strtok_r(NULL, "\n", &saveptr);
	}
	return 0;
}

NDX_LISTENER(int, str_list_append, char *, out, size_t, out_sz,
        const char *, token)
{
	size_t used;
	size_t len;

	if (!out || !token || !token[0])
		return 0;
	if (str_list_contains(out, token))
		return 0;

	used = strlen(out);
	len = strlen(token);
	if (used && used + 1 >= out_sz)
		return -1;
	if (used)
		out[used++] = '\n';
	if (used + len >= out_sz)
		return -1;
	memcpy(out + used, token, len);
	used += len;
	out[used] = '\0';
	return 0;
}

NDX_LISTENER(int, str_list_normalize, const char *, input, char *, out,
        size_t, out_sz)
{
	char copy[8192];
	char *tok;
	char *saveptr;

	if (!out || out_sz == 0)
		return 0;

	copy[0] = '\0';
	if (input && input[0])
		snprintf(copy, sizeof(copy), "%s", input);
	out[0] = '\0';
	if (!copy[0])
		return 0;

	tok = strtok_r(copy, "\r\n", &saveptr);
	while (tok) {
		char t[256];
		snprintf(t, sizeof(t), "%s", tok);
		str_trim(t);
		if (t[0])
			str_list_append(out, out_sz, t);
		tok = strtok_r(NULL, "\r\n", &saveptr);
	}
	return 0;
}

NDX_LISTENER(int, str_list_for_each, const char *, list,
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
