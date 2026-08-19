#include <ttypt/axil.h>
#include <ttypt/axil-xy.h>
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
		xy_load(mod_line);
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

	axil_env_get(fd, uri, sizeof(uri), "DOCUMENT_URI");
	axil_env_get(fd, query, sizeof(query), "QUERY_STRING");

	{
		size_t flen = strlen(from_prefix);
		if (strncmp(uri, from_prefix, flen) != 0 ||
		    (uri[flen] != '\0' && uri[flen] != '/')) {
			axil_respond(fd, 404, "Not found");
			return 1;
		}
	}

	written = snprintf(
	        location, sizeof(location), "%s%s", to_prefix,
	        uri + strlen(from_prefix));
	if (written < 0 || (size_t)written >= sizeof(location)) {
		axil_respond(fd, 500, "Redirect path too long");
		return 1;
	}

	if (query[0]) {
		written += snprintf(
		        location + written, sizeof(location) - (size_t)written,
		        "?%s", query);
		if (written < 0 || (size_t)written >= sizeof(location)) {
			axil_respond(fd, 500, "Redirect path too long");
			return 1;
		}
	}

	axil_header_set(fd, "Location", location);
	axil_respond(fd, status, "");
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

void xy_install(void)
{
	xy_load("./mods/common/common");
	xy_load("./mods/source/source");
	load_modules_from_file("./mods.load");

	axil_register_handler("GET:/sb", songbook_redirect_handler);
	axil_register_handler("GET:/sb/*", songbook_redirect_handler);

	axil_register_handler("GET:/chords", song_redirect_handler);
	axil_register_handler("GET:/chords/*", song_redirect_handler);
}
