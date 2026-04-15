#include <ttypt/ndx-mod.h>
#include <ttypt/ndc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/*
 * mods/static/static.c
 *
 * Domain-based static file server.
 *
 * For a request to example.com/foo/bar.css the file served is:
 *   <DOCUMENT_ROOT>/example.com/foo/bar.css
 *
 * Directory requests resolve to index.html inside that directory.
 *
 * Missing files are served with <DOCUMENT_ROOT>/example.com/status/404.html
 * (HTTP 404).  If that file is also absent, a plain-text 404 is returned.
 *
 * No Fresh/proxy dependency — fully standalone.
 */

/* Strip the port suffix from a Host header value, in-place.
 * "example.com:8080" → "example.com"
 * IPv6 addresses ("[::1]:8080") are left untouched to avoid ambiguity. */
static void strip_port(char *host)
{
	if (!host || host[0] == '[')
		return;
	char *colon = strrchr(host, ':');
	if (colon)
		*colon = '\0';
}

/* Serve a file from `path` with the given HTTP `status`.
 * Sends Content-Type: text/html and closes the connection.
 * Returns 0 on success, 1 on failure (caller should send a fallback). */
static int serve_html_file(int fd, const char *path, int status)
{
	FILE *fp = fopen(path, "rb");
	if (!fp)
		return 1;

	fseek(fp, 0, SEEK_END);
	long len = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	if (len < 0) {
		fclose(fp);
		return 1;
	}

	char *buf = malloc((size_t)len);
	if (!buf) {
		fclose(fp);
		return 1;
	}

	size_t got = fread(buf, 1, (size_t)len, fp);
	fclose(fp);

	ndc_header(fd, "Content-Type", "text/html; charset=utf-8");
	ndc_head(fd, status);
	ndc_write(fd, buf, got);
	ndc_close(fd);
	free(buf);
	return 0;
}

static int static_handler(int fd, char *body)
{
	char host[256]     = {0};
	char uri[1024]     = {0};
	char doc_root[512] = {0};
	char path[2048]    = {0};

	ndc_env_get(fd, host, "HTTP_HOST");
	ndc_env_get(fd, uri, "DOCUMENT_URI");
	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");

	/* Fallback if DOCUMENT_ROOT is not set */
	const char *root = (doc_root[0] != '\0') ? doc_root : ".";

	/* Normalise domain (strip port, e.g. localhost:8080 → localhost) */
	strip_port(host);

	if (!host[0]) {
		ndc_head(fd, 400);
		ndc_body(fd, "Bad Request: missing Host header");
		return 1;
	}

	/* Prevent directory traversal in either host or uri */
	if (strstr(host, "..") || strstr(uri, "..")) {
		ndc_head(fd, 400);
		ndc_body(fd, "Bad Request");
		return 1;
	}

	/* Build filesystem path: <root>/<domain><uri> */
	snprintf(path, sizeof(path), "%s/%s%s", root, host, uri);

	/* Remove trailing slash for stat, then decide */
	size_t plen = strlen(path);
	if (plen > 1 && path[plen - 1] == '/')
		path[--plen] = '\0';

	struct stat st;
	if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
		/* Append /index.html for directories */
		strncat(path, "/index.html", sizeof(path) - plen - 1);
	}

	/* Attempt to serve the file */
	if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
		ndc_sendfile(fd, path);
		return 0;
	}

	/* File not found — try <root>/<domain>/status/404.html */
	char path404[2048] = {0};
	snprintf(path404, sizeof(path404), "%s/%s/status/404.html", root, host);

	if (serve_html_file(fd, path404, 404) == 0)
		return 0;

	/* Last resort: plain-text 404 */
	ndc_head(fd, 404);
	ndc_body(fd, "Not found");
	return 1;
}

void ndx_install(void)
{
	ndc_config.default_handler = static_handler;
}

void ndx_open(void) {}
