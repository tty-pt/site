#ifndef SITE_PATHS_C
#define SITE_PATHS_C

#include "site_ui.h"

#include <stdio.h>
#include <string.h>

static void url_encode(const char *src, char *dst, size_t dst_len)
{
	static const char hex[] = "0123456789ABCDEF";
	if (!src || !dst || dst_len == 0)
		return;
	while (*src && dst_len > 1) {
		unsigned char c = (unsigned char)*src;
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		    (c >= '0' && c <= '9') || c == '-' || c == '_' ||
		    c == '.' || c == '~')
		{
			*dst++ = c;
			dst_len--;
		} else {
			if (dst_len < 4)
				break;
			*dst++ = '%';
			*dst++ = hex[c >> 4];
			*dst++ = hex[c & 15];
			dst_len -= 3;
		}
		src++;
	}
	*dst = '\0';
}

void parent_path(const char *path, char *buf, size_t len)
{
	const char *slash;
	if (!path || !buf || len == 0)
		return;
	slash = strrchr(path, '/');
	if (!slash || slash == path) {
		snprintf(buf, len, "/");
		return;
	}
	if (*(slash + 1) == '\0') {
		/* Path ends with /; skip it and find the previous / */
		const char *p = slash - 1;
		while (p > path && *p != '/')
			p--;
		if (*p == '/') {
			slash = p;
		} else {
			snprintf(buf, len, "/");
			return;
		}
	}
	size_t n = (size_t)(slash - path) + 1;
	if (n >= len)
		n = len - 1;
	memcpy(buf, path, n);
	buf[n] = '\0';
}

const char *site_ui_module_icon(const char *module)
{
	if (!module || !module[0])
		return "\xf0\x9f\x8f\xa0";
	if (strcmp(module, "song") == 0)
		return "\xf0\x9f\x8e\xb5";
	if (strcmp(module, "poem") == 0)
		return "\xf0\x9f\x93\x9d";
	if (strcmp(module, "gig") == 0)
		return "\xf0\x9f\x8e\xa4";
	if (strcmp(module, "grp") == 0)
		return "\xf0\x9f\x91\xa5";
	return "\xf0\x9f\x8f\xa0";
}

const char *site_ui_module_display(const char *module)
{
	if (module && strcmp(module, "grp") == 0)
		return "group";
	return module;
}

void site_ui_item_path(
        const char *module, const char *id, char *buf, size_t len)
{
	snprintf(buf, len, "/%s/%s", module, id);
}

void item_action_path(
        const char *module, const char *id, const char *action, char *buf,
        size_t len)
{
	snprintf(buf, len, "/%s/%s/%s", module, id, action);
}

void site_ui_collection_path(const char *module, char *buf, size_t len)
{
	snprintf(buf, len, "/%s/", module);
}

void auth_path(const char *action, char *buf, size_t len)
{
	snprintf(buf, len, "/auth/%s", action);
}

void login_href(const char *ret, char *buf, size_t len)
{
	char encoded[1024];
	if (!ret || !*ret) {
		snprintf(buf, len, "/auth/login");
		return;
	}
	url_encode(ret, encoded, sizeof(encoded));
	snprintf(buf, len, "/auth/login?ret=%s", encoded);
}

#endif
