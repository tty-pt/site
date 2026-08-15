#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <iconv.h>
#include <locale.h>
#include "stoma/stoma.h"

static iconv_t fold_cd = (iconv_t)-1;
static int fold_ready = 0;

static void fold_init(void)
{
	const char *lc;

	if (fold_ready)
		return;
	fold_ready = 1;

	/* TRANSLIT output depends on the process LC_CTYPE: under a C/POSIX
	 * locale accented chars fold to '?' instead of base letters. Force a
	 * UTF-8 CTYPE locale lazily so folding is stable regardless of the
	 * host environment. */
	lc = setlocale(LC_CTYPE, "");
	if (!lc || !strstr(lc, "UTF-8")) {
		if (!setlocale(LC_CTYPE, "C.UTF-8") &&
		    !setlocale(LC_CTYPE, "en_US.UTF-8"))
			setlocale(LC_CTYPE, "pt_PT.UTF-8");
	}
	fold_cd = iconv_open("ASCII//TRANSLIT", "UTF-8");
}

int stoma_fold(char *out, size_t outsz, const char *in)
{
	char *in_ptr = (char *)in;
	char *out_ptr = out;
	size_t in_len;
	size_t out_len;
	size_t i;

	if (outsz == 0)
		return -1;

	fold_init();
	if (fold_cd == (iconv_t)-1)
		return -1;

	in_len = strlen(in);
	out_len = outsz - 1;
	while (in_len > 0 && out_len > 0) {
		size_t res =
		        iconv(fold_cd, (void *)&in_ptr, &in_len,
		              (void *)&out_ptr, &out_len);
		if (res != (size_t)-1)
			continue;
		if (errno != EILSEQ && errno != EINVAL)
			break;
		in_ptr++;
		in_len--;
		iconv(fold_cd, NULL, NULL, (void *)&out_ptr, &out_len);
	}

	if (in_len > 0) { /* output too small */
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
void stoma_tokenize(
        const char *folded, void (*cb)(const char *tok, size_t len, void *user),
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
