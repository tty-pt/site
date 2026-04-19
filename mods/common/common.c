#include <ttypt/ndx-mod.h>

#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <openssl/evp.h>

#include <ttypt/ndc.h>
NDX_DEF(int, json_escape, const char *, in, char *, out, size_t, outlen) {
	size_t j = 0;
	for (size_t i = 0; in[i] && j + 2 < outlen; i++) {
		unsigned char c = (unsigned char)in[i];
		if (c == '"' || c == '\\') {
			if (j + 2 >= outlen) break;
			out[j++] = '\\';
			out[j++] = c;
		} else if (c == '\n') {
			if (j + 2 >= outlen) break;
			out[j++] = '\\';
			out[j++] = 'n';
		} else if (c == '\r') {
			if (j + 2 >= outlen) break;
			out[j++] = '\\';
			out[j++] = 'r';
		} else if (c == '\t') {
			if (j + 2 >= outlen) break;
			out[j++] = '\\';
			out[j++] = 't';
		} else if (c < 0x20) {
			if (j + 6 >= outlen) break;
			j += snprintf(out + j, outlen - j, "\\u%04x", c);
		} else {
			out[j++] = c;
		}
	}
	out[j] = '\0';
	return 0;
}

NDX_DEF(int, url_encode, const char *, in, char *, out, size_t, outlen) {
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

NDX_DEF(int, b64_encode, const char *, in, char *, out, size_t, outlen) {
	EVP_EncodeBlock((unsigned char *)out, (const unsigned char *)in, (int)strlen(in));
	return 0;
}

NDX_DEF(int, respond_plain, int, fd, int, status, const char *, msg)
{
	ndc_header_set(fd, "Content-Type", "text/plain");
	ndc_respond(fd, status, msg);
	return 1;
}

NDX_DEF(int, respond_json, int, fd, int, status, const char *, msg)
{
	ndc_header_set(fd, "Content-Type", "application/json");
	ndc_respond(fd, status, msg);
	return 1;
}

NDX_DEF(int, redirect, int, fd, const char *, location)
{
	ndc_header_set(fd, "Location", (char *)location);
	ndc_respond(fd, 303, "");
	return 0;
}

NDX_DEF(int, read_meta_file,
	const char *, item_path, const char *, name,
	char *, buf, size_t, sz)
{
	char p[PATH_MAX];
	snprintf(p, sizeof(p), "%s/%s", item_path, name);
	FILE *mfp = fopen(p, "r");
	if (!mfp)
		return -1;
	if (fgets(buf, (int)sz - 1, mfp)) {
		size_t l = strlen(buf);
		if (l > 0 && buf[l - 1] == '\n') buf[l - 1] = '\0';
	}
	fclose(mfp);
	return 0;
}

NDX_DEF(int, write_meta_file,
	const char *, item_path, const char *, name,
	const char *, buf, size_t, sz)
{
	char p[PATH_MAX];
	snprintf(p, sizeof(p), "%s/%s", item_path, name);
	FILE *fp = fopen(p, "w");
	if (!fp)
		return -1;
	fwrite(buf, 1, sz, fp);
	fclose(fp);
	return 0;
}

NDX_DEF(char *, slurp_file, const char *, path)
{
	FILE *fp = fopen(path, "r");
	if (!fp)
		return NULL;
	fseek(fp, 0, SEEK_END);
	long fsize = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	if (fsize <= 0) {
		fclose(fp);
		return strdup("");
	}
	char *buf = malloc((size_t)fsize + 1);
	if (!buf) {
		fclose(fp);
		return NULL;
	}
	size_t got = fread(buf, 1, (size_t)fsize, fp);
	fclose(fp);
	buf[got] = '\0';
	return buf;
}

NDX_DEF(int, get_doc_root, int, fd, char *, buf, size_t, len)
{
	buf[0] = '\0';
	ndc_env_get(fd, buf, "DOCUMENT_ROOT");
	if (!buf[0])
		snprintf(buf, len, ".");
	return 0;
}

/* core_post is defined in index.c; declare here to call it from respond_error */
NDX_DECL(int, core_post, int, fd, char *, body, size_t, len);

/*
 * respond_error — send a pretty HTML error page when the client accepts
 * text/html (browser navigation), or a plain-text response otherwise
 * (API / curl / test clients).
 */
NDX_DEF(int, respond_error, int, fd, int, status, const char *, msg)
{
	char accept[256] = {0};
	ndc_header_get(fd, "Accept", accept, sizeof(accept));
	if (strstr(accept, "text/html")) {
		char enc[512] = {0};
		char body[640];
		int len;
		call_url_encode(msg, enc, sizeof(enc));
		len = snprintf(body, sizeof(body), "status=%d&error=%s", status, enc);
		return call_core_post(fd, body, (size_t)len);
	}
	return call_respond_plain(fd, status, msg);
}

MODULE_API void
ndx_install(void)
{
}
