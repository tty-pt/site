#include <string.h>
#include <ctype.h>
#include "stoma/stoma.h"

/* Fold a UTF-8 string to lowercase, preserving accents (accent-sensitive
 * search). ASCII A-Z and the Latin-1 Supplement uppercase letters
 * (U+00C0-U+00DE, encoded as 0xC3 0x80-0x9E, minus U+00D7 '×') are
 * lowercased; everything else is copied verbatim. Lowercasing never grows
 * the output, so the result fits whenever outsz > strlen(in). The fold is
 * locale-independent (no iconv, no setlocale). */
int stoma_fold(char *out, size_t outsz, const char *in)
{
	const char *in_ptr = in;
	char *out_ptr = out;
	size_t in_len = strlen(in);
	size_t out_len = outsz;

	if (outsz == 0)
		return -1;
	if (in_len >= out_len)
		return -1;

	while (in_len > 0) {
		unsigned char c = (unsigned char)*in_ptr;

		if (c >= 'A' && c <= 'Z') {
			*out_ptr++ = (char)(c + 32);
		} else if (c == 0xC3 && in_len >= 2) {
			unsigned char b = (unsigned char)in_ptr[1];

			if (b >= 0x80 && b <= 0x9E && b != 0x97) {
				*out_ptr++ = (char)0xC3;
				*out_ptr++ = (char)(b + 0x20);
				in_ptr += 2;
				in_len -= 2;
				continue;
			}
			*out_ptr++ = (char)c;
		} else {
			*out_ptr++ = (char)c;
		}
		in_ptr++;
		in_len--;
	}
	*out_ptr = '\0';
	return (int)(out_ptr - out);
}

/* Internal tokenizer: calls cb(token, len, user) for each maximal run of
 * word characters in the folded string. A word character is an ASCII
 * alphanumeric or any byte >= 0x80 (UTF-8 accented letters and other
 * non-ASCII letters stay inside their token). Everything else (space,
 * punctuation, '?') separates tokens. */
void stoma_tokenize(
        const char *folded, void (*cb)(const char *tok, size_t len, void *user),
        void *user)
{
	const char *p = folded;

	if (!folded || !cb)
		return;
	while (*p) {
		const char *start = p;
		unsigned char c = (unsigned char)*p;

		if (!isalnum(c) && c < 0x80) {
			p++;
			continue;
		}
		while (*p) {
			unsigned char w = (unsigned char)*p;

			if (!isalnum(w) && w < 0x80)
				break;
			p++;
		}
		cb(start, (size_t)(p - start), user);
	}
}
