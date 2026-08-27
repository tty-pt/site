#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stoma/stoma.h>

#include "common_internal.h"

XY_IMPL(int, str_list_contains, const char *, list, const char *, token)
{
	return stoma_list_contains(list, token);
}

XY_IMPL(int, str_list_append, char *, out, size_t, out_sz,
        const char *, token)
{
	return stoma_list_append(out, out_sz, token);
}

XY_IMPL(int, str_list_normalize, const char *, input, char *, out,
        size_t, out_sz)
{
	return stoma_list_normalize(input, out, out_sz);
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
