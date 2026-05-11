#include <ctype.h>
#include <errno.h>
#include <iconv.h>
#include <stdio.h>
#include <string.h>

#include <ttypt/axil.h>
#include <ttypt/ndx-mod.h>

#include "common_internal.h"

NDX_LISTENER(int, str_trim, char *, s)
{
	char *start;
	char *end;

	if (!s || !s[0])
		return 0;

	start = s;
	while (*start == ' ' || *start == '\t' || *start == '\r' ||
	       *start == '\n')
		start++;
	if (start != s)
		memmove(s, start, strlen(start) + 1);

	end = s + strlen(s);
	while (end > s) {
		if (end[-1] != ' ' && end[-1] != '\t' && end[-1] != '\r' &&
		    end[-1] != '\n')
			break;
		end--;
	}
	*end = '\0';
	return 0;
}
