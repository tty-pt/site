static const char *find_ci(const char *hay, const char *needle)
{
	size_t nlen = strlen(needle);

	for (; *hay; hay++)
		if (strncasecmp(hay, needle, nlen) == 0)
			return hay;
	return NULL;
}

static void strip_style_blocks(char *buf)
{
	char *s = buf;

	while ((s = (char *)find_ci(s, "<style")) != NULL) {
		const char *close = find_ci(s + 6, "</style>");

		if (!close) {
			*s = '\0';
			break;
		}
		memmove(s, close + 8, strlen(close + 8) + 1);
	}
}

static char *extract_body(const char *raw)
{
	const char *b = find_ci(raw, "<body");
	const char *gt;
	const char *end;
	size_t len;
	char *out;

	if (!b)
		return NULL;
	gt = strchr(b, '>');
	if (!gt)
		return NULL;
	b = gt + 1;
	end = find_ci(b, "</body");
	len = end ? (size_t)(end - b) : strlen(b);
	out = malloc(len + 1);
	if (!out)
		return NULL;
	memcpy(out, b, len);
	out[len] = '\0';
	return out;
}

static char *sanitize_groff_html(const char *raw)
{
	char *out = extract_body(raw);

	if (!out)
		out = strdup(raw);
	if (!out)
		return NULL;
	strip_style_blocks(out);
	return out;
}

static int attr_needs_rewrite(const char *val, size_t len)
{
	if (len == 0 || val[0] == '#' || val[0] == '/')
		return 0;
	if (memchr(val, ':', len))
		return 0;
	if (memchr(val, '/', len))
		return len > 2 && val[0] == '.' && val[1] == '/';
	return 1;
}

static char *rewrite_image_paths(const char *html, const char *id)
{
	static const char *const attrs[] = { "src=\"", "href=\"" };
	size_t id_len = strlen(id);
	const char *p;
	char *out;
	size_t extra = 0;
	size_t o = 0;

	for (p = html; *p;) {
		int matched = 0;

		for (size_t a = 0; a < 2 && !matched; a++) {
			size_t alen = strlen(attrs[a]);
			const char *v;
			const char *e;
			size_t vlen;

			if (strncmp(p, attrs[a], alen) != 0)
				continue;
			v = p + alen;
			e = strchr(v, '"');
			vlen = e ? (size_t)(e - v) : strlen(v);
			if (!attr_needs_rewrite(v, vlen))
				continue;
			extra += id_len + 7;
			matched = 1;
			p = v + vlen;
		}
		if (!matched)
			p++;
	}

	out = malloc(strlen(html) + extra + 1);
	if (!out)
		return NULL;

	for (p = html; *p;) {
		int matched = 0;

		for (size_t a = 0; a < 2 && !matched; a++) {
			size_t alen = strlen(attrs[a]);
			const char *v;
			const char *e;
			size_t vlen;
			const char *name;
			size_t nlen;

			if (strncmp(p, attrs[a], alen) != 0)
				continue;
			v = p + alen;
			e = strchr(v, '"');
			vlen = e ? (size_t)(e - v) : strlen(v);
			if (!attr_needs_rewrite(v, vlen))
				continue;
			name = (vlen > 2 && v[0] == '.' && v[1] == '/') ? v + 2
			                                                : v;
			nlen = vlen - (size_t)(name - v);
			memcpy(out + o, attrs[a], alen);
			o += alen;
			memcpy(out + o, "/poem/", 6);
			o += 6;
			memcpy(out + o, id, id_len);
			o += id_len;
			out[o++] = '/';
			memcpy(out + o, name, nlen);
			o += nlen;
			p = v + vlen;
			matched = 1;
		}
		if (!matched)
			out[o++] = *p++;
	}
	out[o] = '\0';
	return out;
}

static bud_node *
poem_render_detail_body(const char *content, const char *owner, const char *id)
{
	char owner_text[128] = { 0 };
	bud_node *inner = NULL;

	if (owner && owner[0])
		snprintf(owner_text, sizeof(owner_text), "By %s", owner);

	if (content && content[0]) {
		char *sanitized = sanitize_groff_html(content);
		char *fixed =
		        sanitized ? rewrite_image_paths(sanitized, id) : NULL;

		free(sanitized);
		if (fixed) {
			inner = lx_el("div",
			              lx_attr("class", "poem-body font-serif "
			                               "leading-relaxed"),
			              lx_node(bud_raw(fixed)))
			                .data.node;
			free(fixed);
		}
	}
	if (!inner)
		inner = lx_el("p", lx_attr("class", "text-muted"),
		              lx_text("No content uploaded yet."))
		                .data.node;

	return lx_el("div", lx_attr("class", "flex flex-col gap-4"),
	             lx_node(inner),
	             (owner && owner[0])
	                     ? lx_el("div",
	                             lx_attr("class", "text-sm text-muted "
	                                              "text-right"),
	                             lx_text(owner_text))
	                     : lx_none())
	        .data.node;
}
