#ifndef INDEX_UX_LIST_QUERY_C
#define INDEX_UX_LIST_QUERY_C

static void idx_url_decode(char *s)
{
	char *d = s;

	if (!s)
		return;
	while (*s) {
		if (*s == '%' && s[1] && s[2]) {
			unsigned int v;
			sscanf(s + 1, "%2x", &v);
			*d++ = (char)v;
			s += 3;
		} else if (*s == '+') {
			*d++ = ' ';
			s++;
		} else {
			*d++ = *s++;
		}
	}
	*d = '\0';
}

static const char *
idx_query_param(const char *qs, const char *name, char *buf, size_t len)
{
	const char *p;
	size_t nlen;

	if (!qs || !name || !buf || len == 0)
		return NULL;
	nlen = strlen(name);
	p = qs;
	while (p) {
		p = strstr(p, name);
		if (!p)
			return NULL;
		if ((p == qs || p[-1] == '&') && p[nlen] == '=') {
			const char *end;
			size_t n;

			p += nlen + 1;
			end = strchr(p, '&');
			n = end ? (size_t)(end - p) : strlen(p);
			if (n >= len)
				n = len - 1;
			memcpy(buf, p, n);
			buf[n] = '\0';
			idx_url_decode(buf);
			return buf;
		}
		p++;
	}
	return NULL;
}

/* Collect every occurrence of a repeated query param and join the
 * url-decoded values with commas (display-only for multi-select). */
static const char *
idx_query_params_join(const char *qs, const char *name, char *buf, size_t len)
{
	const char *p;
	size_t nlen;
	size_t pos = 0;

	if (!qs || !name || !buf || len == 0)
		return NULL;
	buf[0] = '\0';
	nlen = strlen(name);
	p = qs;
	while (p) {
		p = strstr(p, name);
		if (!p)
			break;
		if ((p == qs || p[-1] == '&') && p[nlen] == '=') {
			const char *end;
			size_t n;
			char tmp[512];

			p += nlen + 1;
			end = strchr(p, '&');
			n = end ? (size_t)(end - p) : strlen(p);
			if (n >= sizeof(tmp))
				n = sizeof(tmp) - 1;
			memcpy(tmp, p, n);
			tmp[n] = '\0';
			idx_url_decode(tmp);
			if (pos > 0 && pos + 1 < len) {
				buf[pos++] = ',';
				buf[pos] = '\0';
			}
			n = strlen(tmp);
			if (pos + n + 1 > len)
				n = len - pos - 1;
			memcpy(buf + pos, tmp, n);
			pos += n;
			buf[pos] = '\0';
		}
		p++;
	}
	return pos > 0 ? buf : NULL;
}

static void idx_parse_sort(const char *qs, char *field, size_t flen, int *asc)
{
	char sort_val[128];
	const char *sv;
	const char *colon;

	*asc = 1;
	field[0] = '\0';
	sv = idx_query_param(qs, "sort", sort_val, sizeof(sort_val));
	if (!sv)
		return;
	colon = strchr(sv, ':');
	if (colon) {
		size_t n = (size_t)(colon - sv);
		if (n >= flen)
			n = flen - 1;
		memcpy(field, sv, n);
		field[n] = '\0';
		*asc = (strcmp(colon + 1, "desc") != 0);
	} else {
		strncpy(field, sv, flen - 1);
		field[flen - 1] = '\0';
		*asc = 1;
	}
}

static void col_tok_label(char *out, size_t len, const char *key)
{
	size_t i;

	for (i = 0; key[i] && i < len - 1; i++)
		out[i] = key[i] == '_' ? ' ' : key[i];
	out[i] = '\0';
}

/* True when the state carries an active search (omni q, any custom
 * field selection, or a Content lookup term). Pure/wasm-safe; gates
 * the picker result table on BOTH sides (id alignment). */
int list_has_query(const list_state_t *s)
{
	int i;

	if (!s)
		return 0;
	if (s->q[0])
		return 1;
	if (!s->custom)
		return 0;
	for (i = 0; i < s->ncols; i++)
		if (s->cols[i].current[0])
			return 1;
	if (s->content_field[0]) {
		char value[512];

		if (idx_query_param(
		            s->query, s->content_field, value, sizeof(value)))
			return 1;
	}
	return 0;
}

#endif
