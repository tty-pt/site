#include <ttypt/ndc.h>
#include <ttypt/ndc-ndx.h>
#include <string.h>
#include <stdio.h>
#include <dlfcn.h>

static int load_modules_from_file(const char *path)
{
	char mod_line[1030];
	char line[512];
	FILE *fp = fopen(path, "r");

	if (!fp)
		return 0;

	while (fgets(line, sizeof(line), fp)) {
		size_t len = strlen(line);
		while (len > 0 &&
		       (line[len - 1] == '\n' || line[len - 1] == '\r'))
		{
			line[len - 1] = '\0';
			len--;
		}
		if (len == 0 || line[0] == '#')
			continue;
		snprintf(mod_line, sizeof(mod_line), "mods/%s/%s", line, line);
		ndx_load(mod_line);
	}

	fclose(fp);
	return 1;
}

static int alias_redirect(
        int fd, const char *from_prefix, const char *to_prefix, int status)
{
	char uri[1024] = { 0 };
	char query[512] = { 0 };
	char location[1600];
	int written;

	ndc_env_get(fd, uri, "DOCUMENT_URI");
	ndc_env_get(fd, query, "QUERY_STRING");

	if (strncmp(uri, from_prefix, strlen(from_prefix)) != 0) {
		ndc_respond(fd, 404, "Not found");
		return 1;
	}

	written = snprintf(
	        location,
	        sizeof(location),
	        "%s%s",
	        to_prefix,
	        uri + strlen(from_prefix));
	if (written < 0 || (size_t)written >= sizeof(location)) {
		ndc_respond(fd, 500, "Redirect path too long");
		return 1;
	}

	if (query[0]) {
		written += snprintf(
		        location + written,
		        sizeof(location) - (size_t)written,
		        "?%s",
		        query);
		if (written < 0 || (size_t)written >= sizeof(location)) {
			ndc_respond(fd, 500, "Redirect path too long");
			return 1;
		}
	}

	ndc_header_set(fd, "Location", location);
	ndc_respond(fd, status, "");
	return 0;
}

static int songbook_redirect_handler(int fd, char *body)
{
	(void)body;
	return alias_redirect(fd, "/sb", "/songbook", 303);
}

static int song_redirect_handler(int fd, char *body)
{
	(void)body;
	return alias_redirect(fd, "/chords", "/song", 301);
}

void ndx_install(void)
{
	ndx_load("./mods/common/common");
	ndx_load("./mods/ssr/ssr");
	ndx_load("./mods/source/source");
	load_modules_from_file("./mods.load");

	ndc_register_handler("GET:/sb", songbook_redirect_handler);
	ndc_register_handler("GET:/sb/*", songbook_redirect_handler);

	ndc_register_handler("GET:/chords", song_redirect_handler);
	ndc_register_handler("GET:/chords/*", song_redirect_handler);
}
