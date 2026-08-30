#define I18N_IMPL
#include "i18n.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <ttypt/xy-mod.h>
#include <ttypt/axil.h>
#include <ttypt/auth.h>

#include "../common/common.h"

static const char *i18n_canonical_lang(const char *lang)
{
	if (!lang || !lang[0])
		return NULL;
	if (i18n_is_pt_locale(lang))
		return I18N_LOCALE_PT;
	if ((lang[0] == 'e' || lang[0] == 'E') &&
	    (lang[1] == 'n' || lang[1] == 'N')) {
		char next = lang[2];
		if (next == '\0' || next == '-' || next == '_' || next == ';')
			return I18N_LOCALE_EN;
	}
	return NULL;
}

const char *i18n_parse_query_lang(const char *qs)
{
	const char *p;
	char val[32];
	size_t len;

	if (!qs || !qs[0])
		return NULL;

	p = qs;
	while (*p) {
		if ((p == qs || *(p - 1) == '&') &&
		    (strncmp(p, "lang=", 5) == 0 || strncmp(p, "l=", 2) == 0)) {
			const char *start = (p[1] == '=') ? (p + 2) : (p + 5);
			const char *end = strchr(start, '&');
			len = end ? (size_t)(end - start) : strlen(start);
			if (len > 0 && len < sizeof(val)) {
				memcpy(val, start, len);
				val[len] = '\0';
				return i18n_canonical_lang(val);
			}
		}
		p++;
	}

	return NULL;
}

const char *i18n_parse_cookie_lang(const char *cookie)
{
	const char *p;
	char val[32];
	size_t len;

	if (!cookie || !cookie[0])
		return NULL;

	p = cookie;
	while (*p) {
		while (*p == ' ' || *p == '\t' || *p == ';')
			p++;
		if (strncmp(p, "lang=", 5) == 0 || strncmp(p, "locale=", 7) == 0) {
			const char *start = (p[0] == 'l' && p[1] == 'a') ? (p + 5) : (p + 7);
			const char *end = strpbrk(start, "; \t\r\n");
			len = end ? (size_t)(end - start) : strlen(start);
			if (len > 0 && len < sizeof(val)) {
				memcpy(val, start, len);
				val[len] = '\0';
				return i18n_canonical_lang(val);
			}
		}
		p = strpbrk(p, ";");
		if (!p)
			break;
		p++;
	}

	return NULL;
}

/*
 * Parse Accept-Language header format:
 *   pt-PT,pt;q=0.9,en-US;q=0.8,en;q=0.7
 * Returns "pt", "en", or NULL if neither is acceptable.
 */
const char *i18n_parse_accept_language(const char *hdr)
{
	const char *p;
	int pt_q = 0;
	int en_q = 0;
	int position = 0;

	if (!hdr || !hdr[0])
		return NULL;

	p = hdr;
	while (*p) {
		const char *tag_start;
		const char *tag_end;
		const char *next_item;
		int q_val = 1000; /* Default weight 1.0 */
		int is_pt = 0;
		int is_en = 0;
		size_t tag_len;

		while (*p == ' ' || *p == '\t')
			p++;
		if (!*p)
			break;

		tag_start = p;
		tag_end = strpbrk(p, ";,");
		if (!tag_end)
			tag_end = p + strlen(p);

		tag_len = (size_t)(tag_end - tag_start);
		while (tag_len > 0 && (tag_start[tag_len - 1] == ' ' || tag_start[tag_len - 1] == '\t'))
			tag_len--;

		if (tag_len >= 2) {
			if ((tag_start[0] == 'p' || tag_start[0] == 'P') &&
			    (tag_start[1] == 't' || tag_start[1] == 'T')) {
				if (tag_len == 2 || tag_start[2] == '-' || tag_start[2] == '_')
					is_pt = 1;
			} else if ((tag_start[0] == 'e' || tag_start[0] == 'E') &&
			           (tag_start[1] == 'n' || tag_start[1] == 'N')) {
				if (tag_len == 2 || tag_start[2] == '-' || tag_start[2] == '_')
					is_en = 1;
			}
		}

		/* Check for ;q= factor */
		if (*tag_end == ';') {
			const char *q_ptr = strstr(tag_end, "q=");
			if (q_ptr) {
				q_ptr += 2;
				while (*q_ptr == ' ')
					q_ptr++;
				if (*q_ptr >= '0' && *q_ptr <= '1') {
					float f = 0.0f;
					if (sscanf(q_ptr, "%f", &f) == 1)
						q_val = (int)(f * 1000.0f);
				}
			}
		}

		/* Apply position bias for tie-breaking when q values are equal */
		if (q_val > 0) {
			int score = (q_val * 100) - position;
			if (score < 1)
				score = 1;
			if (is_pt && score > pt_q)
				pt_q = score;
			if (is_en && score > en_q)
				en_q = score;
		}

		position++;
		next_item = strchr(p, ',');
		if (!next_item)
			break;
		p = next_item + 1;
	}

	if (pt_q > 0 && pt_q >= en_q)
		return I18N_LOCALE_PT;
	if (en_q > 0 && en_q > pt_q)
		return I18N_LOCALE_EN;

	return NULL;
}

XY_IMPL(const char *, i18n_resolve_locale, int, fd)
{
	char qs[1024] = { 0 };
	char cookie[2048] = { 0 };
	char accept_lang[1024] = { 0 };
	const char *lang;

	if (fd <= 0)
		return I18N_LOCALE_EN;

	/* Priority 1: Query string ?lang=... */
	if (axil_env_get(fd, qs, sizeof(qs), "QUERY_STRING") == 0 && qs[0]) {
		lang = i18n_parse_query_lang(qs);
		if (lang)
			return lang;
	}

	/* Priority 2: Cookie lang=... */
	if (axil_env_get(fd, cookie, sizeof(cookie), "HTTP_COOKIE") == 0 && cookie[0]) {
		lang = i18n_parse_cookie_lang(cookie);
		if (lang)
			return lang;
	}

	/* Priority 3: Session user preference if logged in */
	{
		const char *username = get_request_user(fd);
		if (username && username[0]) {
			char pref[32] = { 0 };
			if (user_pref_read(username, "lang", pref, sizeof(pref)) == 0 && pref[0]) {
				lang = i18n_canonical_lang(pref);
				if (lang)
					return lang;
			}
		}
	}

	/* Priority 4: Accept-Language HTTP header */
	if (axil_env_get(fd, accept_lang, sizeof(accept_lang), "HTTP_ACCEPT_LANGUAGE") == 0 && accept_lang[0]) {
		lang = i18n_parse_accept_language(accept_lang);
		if (lang)
			return lang;
	}

	/* Default fallback: English */
	return I18N_LOCALE_EN;
}

XY_IMPL(int, i18n_set_user_locale, const char *, username, const char *, lang)
{
	const char *canon;

	if (!username || !username[0])
		return -1;

	canon = i18n_canonical_lang(lang);
	if (!canon)
		canon = I18N_LOCALE_EN;

	return user_pref_write(username, "lang", canon);
}

#define MAX_REGISTERED_DICTS 32
static struct {
	const i18n_entry_t *entries;
	size_t count;
} g_registered_dicts[MAX_REGISTERED_DICTS];
static size_t g_registered_dict_count = 0;

XY_IMPL(int, i18n_register_dict, const i18n_entry_t *, entries, size_t, count)
{
	if (!entries || count == 0)
		return 0;
	if (g_registered_dict_count >= MAX_REGISTERED_DICTS)
		return -1;
	g_registered_dicts[g_registered_dict_count].entries = entries;
	g_registered_dicts[g_registered_dict_count].count = count;
	g_registered_dict_count++;
	return 0;
}

XY_IMPL(const char *, i18n_translate, const char *, lang, const char *, msgid)
{
	size_t d, i;

	if (!msgid || !msgid[0])
		return "";
	if (!i18n_is_pt_locale(lang))
		return msgid;

	for (d = 0; d < g_registered_dict_count; d++) {
		const i18n_entry_t *table = g_registered_dicts[d].entries;
		size_t cnt = g_registered_dicts[d].count;
		for (i = 0; i < cnt; i++) {
			if (strcmp(table[i].en, msgid) == 0)
				return table[i].pt;
		}
	}

	return i18n_t(lang, msgid);
}

static int i18n_set_handler(int fd, char *body)
{
	char qs[1024] = { 0 };
	char ret[512] = "/";
	char cookie_hdr[256];
	const char *lang_canon = I18N_LOCALE_EN;
	const char *p;

	(void)body;
	if (axil_env_get(fd, qs, sizeof(qs), "QUERY_STRING") == 0 && qs[0]) {
		const char *qlang = i18n_parse_query_lang(qs);
		if (qlang)
			lang_canon = qlang;

		p = strstr(qs, "return=");
		if (p) {
			size_t rlen;
			const char *rstart = p + 7;
			const char *rend = strchr(rstart, '&');
			rlen = rend ? (size_t)(rend - rstart) : strlen(rstart);
			if (rlen > 0 && rlen < sizeof(ret) - 1 && rstart[0] == '/') {
				memcpy(ret, rstart, rlen);
				ret[rlen] = '\0';
			}
		}
	}

	/* Save to user preference if authenticated */
	const char *username = get_request_user(fd);
	if (username && username[0]) {
		i18n_set_user_locale(username, lang_canon);
	}

	/* Set language cookie (1 year expiration) */
	snprintf(cookie_hdr, sizeof(cookie_hdr),
	         "lang=%s; Path=/; Max-Age=31536000; SameSite=Lax", lang_canon);
	axil_header_set(fd, "Set-Cookie", cookie_hdr);
	axil_header_set(fd, "Location", ret);
	axil_respond(fd, 303, "");
	return 0;
}

void xy_install(void)
{
	xy_load("./mods/common/common");

	axil_register_handler("GET:/i18n/set", i18n_set_handler);
	axil_register_handler("GET:/api/i18n/set", i18n_set_handler);
}
