#include <string.h>
#include <ctype.h>
#include <iconv.h>
#include <errno.h>
#include <locale.h>
#include "stoma/stoma.h"

int stoma_fold(char *out, size_t outsz, const char *in)
{
	static iconv_t cd = (iconv_t)-1;
	static int    localized = 0;
	char   *in_ptr = (char *)in;
	char   *out_ptr = out;
	size_t  in_len = strlen(in);
	size_t  out_len;
	size_t  i;

	if (outsz == 0)
		return -1;

	/* glibc's TRANSLIT needs the process locale; init once lazily so the
	 * library works standalone without the host setting a locale. */
	if (!localized) {
		setlocale(LC_CTYPE, "");
		localized = 1;
	}

	if (cd == (iconv_t)-1)
		cd = iconv_open("ASCII//TRANSLIT", "UTF-8");
	if (cd == (iconv_t)-1)
		return -1;

	out_len = outsz - 1;
	while (in_len > 0 && out_len > 0) {
		size_t res = iconv(cd, (void *)&in_ptr, &in_len,
			(void *)&out_ptr, &out_len);
		if (res != (size_t)-1)
			continue;
		if (errno != EILSEQ && errno != EINVAL)
			break;
		in_ptr++;
		in_len--;
		iconv(cd, NULL, NULL, (void *)&out_ptr, &out_len);
	}

	if (in_len > 0) {	/* output too small */
		*out_ptr = '\0';
		return -1;
	}

	for (i = 0; i < (size_t)(out_ptr - out); i++)
		if (out[i] >= 'A' && out[i] <= 'Z')
			out[i] = (char)(out[i] + 32);
	*out_ptr = '\0';
	return (int)(out_ptr - out);
}

/* Internal tokenizer: calls cb(token, len, user) for each maximal
 * run of [a-z0-9] in the folded string. Tokens may be a single char. */
void stoma_tokenize(const char *folded,
	void (*cb)(const char *tok, size_t len, void *user),
	void *user)
{
	const char *p = folded;

	if (!folded || !cb)
		return;
	while (*p) {
		if (!isalnum((unsigned char)*p)) {
			p++;
			continue;
		}
		const char *start = p;
		while (*p && isalnum((unsigned char)*p))
			p++;
		cb(start, (size_t)(p - start), user);
	}
}
