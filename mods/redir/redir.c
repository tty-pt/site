#include <stdio.h>
#include <string.h>

#include <ttypt/ndc.h>
#include <ttypt/ndx-mod.h>

#include "../common/common.h"

static int alias_redirect(int fd, const char *from_prefix,
                          const char *to_prefix, int status) {
	char uri[1024] = { 0 };
	char query[512] = { 0 };
	char location[1600];
	int written;

	ndc_env_get(fd, uri, "DOCUMENT_URI");
	ndc_env_get(fd, query, "QUERY_STRING");

	if (strncmp(uri, from_prefix, strlen(from_prefix)) != 0)
		return not_found(fd, "Not found");

	written = snprintf(location, sizeof(location), "%s%s", to_prefix,
	                   uri + strlen(from_prefix));
	if (written < 0 || (size_t)written >= sizeof(location))
		return server_error(fd, "Redirect path too long");

	if (query[0]) {
		written +=
		    snprintf(location + written,
		             sizeof(location) - (size_t)written, "?%s", query);
		if (written < 0 || (size_t)written >= sizeof(location))
			return server_error(fd, "Redirect path too long");
	}

	ndc_header_set(fd, "Location", location);
	ndc_respond(fd, status, "");
	return 0;
}

static int songbook_redirect_handler(int fd, char *body) {
	(void)body;
	return alias_redirect(fd, "/sb", "/songbook", 303);
}

static int song_redirect_handler(int fd, char *body) {
	(void)body;
	return alias_redirect(fd, "/chords", "/song", 301);
}

void ndx_install(void) {
	ndx_load("./mods/common/common");

	ndc_register_handler("GET:/sb", songbook_redirect_handler);
	ndc_register_handler("GET:/sb/*", songbook_redirect_handler);

	ndc_register_handler("GET:/chords", song_redirect_handler);
	ndc_register_handler("GET:/chords/*", song_redirect_handler);
}
