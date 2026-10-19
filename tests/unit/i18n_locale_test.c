#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../mods/i18n/i18n_dict.h"

/* Forward declare parsing functions */
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

static const char *i18n_parse_query_lang(const char *qs)
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

static const char *i18n_parse_cookie_lang(const char *cookie)
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

static const char *i18n_parse_accept_language(const char *hdr)
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
		int q_val = 1000;
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
		return "pt";
	if (en_q > 0 && en_q > pt_q)
		return "en";

	return NULL;
}

int main(void)
{
	printf("=== Testing i18n locale negotiation ===\n");

	/* Query string parsing tests */
	assert(strcmp(i18n_parse_query_lang("lang=pt"), "pt") == 0);
	assert(strcmp(i18n_parse_query_lang("lang=pt-PT"), "pt") == 0);
	assert(strcmp(i18n_parse_query_lang("lang=pt_PT"), "pt") == 0);
	assert(strcmp(i18n_parse_query_lang("lang=en"), "en") == 0);
	assert(strcmp(i18n_parse_query_lang("foo=bar&lang=pt&baz=1"), "pt") == 0);
	assert(strcmp(i18n_parse_query_lang("l=pt"), "pt") == 0);
	assert(i18n_parse_query_lang("foo=bar") == NULL);
	assert(i18n_parse_query_lang(NULL) == NULL);

	/* Cookie parsing tests */
	assert(strcmp(i18n_parse_cookie_lang("lang=pt"), "pt") == 0);
	assert(strcmp(i18n_parse_cookie_lang("lang=pt-PT; other=123"), "pt") == 0);
	assert(strcmp(i18n_parse_cookie_lang("QSESSION=abc; lang=en; theme=dark"), "en") == 0);
	assert(strcmp(i18n_parse_cookie_lang("locale=pt_PT"), "pt") == 0);
	assert(i18n_parse_cookie_lang("QSESSION=abc; other=xyz") == NULL);
	assert(i18n_parse_cookie_lang(NULL) == NULL);

	/* Accept-Language parsing tests */
	assert(strcmp(i18n_parse_accept_language("pt"), "pt") == 0);
	assert(strcmp(i18n_parse_accept_language("pt-PT"), "pt") == 0);
	assert(strcmp(i18n_parse_accept_language("pt_PT"), "pt") == 0);
	assert(strcmp(i18n_parse_accept_language("pt-BR"), "pt") == 0);
	assert(strcmp(i18n_parse_accept_language("en"), "en") == 0);
	assert(strcmp(i18n_parse_accept_language("en-US"), "en") == 0);
	assert(strcmp(i18n_parse_accept_language("en-GB,en;q=0.9"), "en") == 0);

	/* Accept-Language weighted negotiation tests */
	assert(strcmp(i18n_parse_accept_language("pt-PT,pt;q=0.9,en-US;q=0.8,en;q=0.7"), "pt") == 0);
	assert(strcmp(i18n_parse_accept_language("en-US,en;q=0.9,pt-PT;q=0.8"), "en") == 0);
	assert(strcmp(i18n_parse_accept_language("pt,en;q=0.8"), "pt") == 0);
	assert(strcmp(i18n_parse_accept_language("en,pt;q=0.5"), "en") == 0);

	/* Unmatched languages */
	assert(i18n_parse_accept_language("fr-FR,fr;q=0.9,de;q=0.8") == NULL);
	assert(i18n_parse_accept_language("es-ES,es;q=0.9") == NULL);
	assert(i18n_parse_accept_language("") == NULL);
	assert(i18n_parse_accept_language(NULL) == NULL);

	printf("ALL PASS: i18n locale negotiation assertions passed\n");
	return 0;
}
