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
		return -1;

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
		if (xy_load(mod_line) != XY_OK)
			fprintf(stderr, "warning: module %s failed to load\n",
			        mod_line);
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
		    (uri[flen] != '\0' && uri[flen] != '/'))
		{
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

static int gig_redirect_handler(int fd, char *body)
{
	(void)body;
	return alias_redirect(fd, "/sb", "/gig", 303);
}

static int song_redirect_handler(int fd, char *body)
{
	(void)body;
	return alias_redirect(fd, "/chords", "/song", 301);
}

/* Pre-rename URL shims (301 permanent). */
static int old_gig_redirect_handler(int fd, char *body)
{
	(void)body;
	return alias_redirect(fd, "/songbook", "/gig", 301);
}

static int old_grp_redirect_handler(int fd, char *body)
{
	(void)body;
	return alias_redirect(fd, "/choir", "/grp", 301);
}

static int grp_url_redirect_handler(int fd, char *body)
{
	(void)body;
	return alias_redirect(fd, "/group", "/grp", 301);
}

void xy_install(void)
{
	int i18n_ok = (xy_load("./mods/i18n/i18n") == XY_OK);
	int common_ok = (xy_load("./mods/common/common") == XY_OK);
	int source_ok = (xy_load("./mods/source/source") == XY_OK);

	if (!i18n_ok)
		fprintf(stderr, "warning: i18n module failed to load\n");
	if (!common_ok)
		fprintf(stderr, "warning: common module failed to load\n");
	if (!source_ok)
		fprintf(stderr, "warning: source module failed to load\n");

	if (common_ok && source_ok)
		load_modules_from_file("./mods.load");

	axil_register_handler("GET:/sb", gig_redirect_handler);
	axil_register_handler("GET:/sb/*", gig_redirect_handler);

	axil_register_handler("GET:/chords", song_redirect_handler);
	axil_register_handler("GET:/chords/*", song_redirect_handler);

	axil_register_handler("GET:/songbook", old_gig_redirect_handler);
	axil_register_handler("GET:/songbook/*", old_gig_redirect_handler);

	axil_register_handler("GET:/choir", old_grp_redirect_handler);
	axil_register_handler("GET:/choir/*", old_grp_redirect_handler);

	axil_register_handler("GET:/group", grp_url_redirect_handler);
	axil_register_handler("GET:/group/*", grp_url_redirect_handler);
}
