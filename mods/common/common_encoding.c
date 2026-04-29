#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <openssl/evp.h>
#include <ttypt/ndx-mod.h>

#include "common_internal.h"

NDX_LISTENER(int, json_escape, const char *, in, char *, out, size_t, outlen)
{
	size_t j = 0;

	for (size_t i = 0; in[i] && j + 2 < outlen; i++) {
		unsigned char c = (unsigned char)in[i];

		if (c == '"' || c == '\\') {
			if (j + 2 >= outlen)
				break;
			out[j++] = '\\';
			out[j++] = c;
		} else if (c == '\n') {
			if (j + 2 >= outlen)
				break;
			out[j++] = '\\';
			out[j++] = 'n';
		} else if (c == '\r') {
			if (j + 2 >= outlen)
				break;
			out[j++] = '\\';
			out[j++] = 'r';
		} else if (c == '\t') {
			if (j + 2 >= outlen)
				break;
			out[j++] = '\\';
			out[j++] = 't';
		} else if (c < 0x20) {
			if (j + 6 >= outlen)
				break;
			j += snprintf(out + j, outlen - j, "\\u%04x", c);
		} else {
			out[j++] = c;
		}
	}

	out[j] = '\0';
	return 0;
}

NDX_LISTENER(int, url_encode, const char *, in, char *, out, size_t, outlen)
{
	size_t j = 0;

	for (size_t i = 0; in[i] && j + 4 < outlen; i++) {
		unsigned char c = (unsigned char)in[i];

		if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
			out[j++] = c;
		} else {
			j += snprintf(out + j, outlen - j, "%%%02X", c);
		}
	}

	out[j] = '\0';
	return (int)j;
}

NDX_LISTENER(int, b64_encode, const char *, in, char *, out, size_t, outlen)
{
	(void)outlen;
	EVP_EncodeBlock((unsigned char *)out, (const unsigned char *)in,
		(int)strlen(in));
	return 0;
}
