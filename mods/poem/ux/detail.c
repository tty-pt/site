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

static int is_dangerous_tag(const char *tag)
{
	static const char *const bad[] = {
		"script", "iframe", "svg",      "object",  "embed",
		"form",   "input",  "button",   "select",  "textarea",
		"link",   "meta",   "style",    "base",    "head",
		"applet", "frame",  "frameset", "noframes"
	};

	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
		if (strcasecmp(tag, bad[i]) == 0)
			return 1;
	return 0;
}

static int is_safe_attr(const char *name, const char *val)
{
	if (strncasecmp(name, "on", 2) == 0)
		return 0;
	if (strcasecmp(name, "href") == 0 || strcasecmp(name, "src") == 0) {
		if (val && strncasecmp(val, "javascript:", 11) == 0)
			return 0;
		if (val && strncasecmp(val, "data:", 5) == 0)
			return 0;
		if (val && strncasecmp(val, "vbscript:", 9) == 0)
			return 0;
	}
	return 1;
}

static void
append_str(char **buf, size_t *cap, size_t *len, const char *s, size_t slen)
{
	if (*len + slen + 1 >= *cap) {
		*cap = *cap ? *cap * 2 : 1024;
		while (*len + slen + 1 >= *cap)
			*cap *= 2;
		char *nb = realloc(*buf, *cap);
		if (!nb)
			return;
		*buf = nb;
	}
	memcpy(*buf + *len, s, slen);
	*len += slen;
	(*buf)[*len] = '\0';
}

static char *sanitize_html(const char *html)
{
	char *out = NULL;
	size_t cap = 0, len = 0;
	const char *p = html;
	const char *tag_end;
	char tagname[64];

	while (*p) {
		if (*p == '<') {
			tag_end = strchr(p, '>');
			if (!tag_end) {
				append_str(&out, &cap, &len, p, strlen(p));
				break;
			}
			int closing = (p[1] == '/');
			const char *name_start = p + (closing ? 2 : 1);
			const char *name_end = name_start;
			while (name_end < tag_end && *name_end != ' ' &&
			       *name_end != '/' && *name_end != '>')
				name_end++;
			size_t name_len = name_end - name_start;
			if (name_len < sizeof(tagname)) {
				memcpy(tagname, name_start, name_len);
				tagname[name_len] = '\0';
			} else {
				tagname[0] = '\0';
			}

			if (is_dangerous_tag(tagname)) {
				if (!closing) {
					const char *close_tag =
					        find_ci(tag_end + 1, "</");
					if (close_tag &&
					    strncasecmp(
					            close_tag + 2, tagname,
					            name_len) == 0)
					{
						const char *gt2 =
						        strchr(close_tag, '>');
						if (gt2)
							p = gt2 + 1;
						else
							p = tag_end + 1;
					} else {
						p = tag_end + 1;
					}
				} else {
					p = tag_end + 1;
				}
				continue;
			}

			char *attr_out = NULL;
			size_t attr_cap = 0, attr_len = 0;
			const char *attr_p = name_end;

			while (attr_p < tag_end) {
				while (attr_p < tag_end &&
				       (*attr_p == ' ' || *attr_p == '\t' ||
				        *attr_p == '\n' || *attr_p == '\r'))
					attr_p++;
				if (attr_p >= tag_end || *attr_p == '/' ||
				    *attr_p == '>')
					break;
				const char *aname = attr_p;
				while (attr_p < tag_end && *attr_p != '=' &&
				       *attr_p != ' ' && *attr_p != '\t' &&
				       *attr_p != '\n' && *attr_p != '\r' &&
				       *attr_p != '/' && *attr_p != '>')
					attr_p++;
				size_t alen = attr_p - aname;
				while (attr_p < tag_end &&
				       (*attr_p == ' ' || *attr_p == '\t' ||
				        *attr_p == '\n' || *attr_p == '\r'))
					attr_p++;
				char *aval = NULL;
				size_t vlen = 0;
				if (attr_p < tag_end && *attr_p == '=') {
					attr_p++;
					while (attr_p < tag_end &&
					       (*attr_p == ' ' ||
					        *attr_p == '\t' ||
					        *attr_p == '\n' ||
					        *attr_p == '\r'))
						attr_p++;
					if (attr_p < tag_end &&
					    (*attr_p == '"' || *attr_p == '\''))
					{
						char quote = *attr_p++;
						const char *vstart = attr_p;
						while (attr_p < tag_end &&
						       *attr_p != quote)
							attr_p++;
						vlen = attr_p - vstart;
						aval = malloc(vlen + 1);
						if (aval) {
							memcpy(aval, vstart,
							       vlen);
							aval[vlen] = '\0';
						}
						if (attr_p < tag_end)
							attr_p++;
					} else {
						const char *vstart = attr_p;
						while (attr_p < tag_end &&
						       *attr_p != ' ' &&
						       *attr_p != '\t' &&
						       *attr_p != '\n' &&
						       *attr_p != '\r' &&
						       *attr_p != '/' &&
						       *attr_p != '>')
							attr_p++;
						vlen = attr_p - vstart;
						aval = malloc(vlen + 1);
						if (aval) {
							memcpy(aval, vstart,
							       vlen);
							aval[vlen] = '\0';
						}
					}
				}

				if (is_safe_attr(aname, aval ? aval : "")) {
					append_str(
					        &attr_out, &attr_cap, &attr_len,
					        " ", 1);
					append_str(
					        &attr_out, &attr_cap, &attr_len,
					        aname, alen);
					if (aval) {
						append_str(
						        &attr_out, &attr_cap,
						        &attr_len, "=\"", 2);
						append_str(
						        &attr_out, &attr_cap,
						        &attr_len, aval, vlen);
						append_str(
						        &attr_out, &attr_cap,
						        &attr_len, "\"", 1);
					}
				}
				free(aval);
			}

			if (closing) {
				append_str(&out, &cap, &len, "</", 2);
				append_str(&out, &cap, &len, tagname, name_len);
				append_str(&out, &cap, &len, ">", 1);
			} else {
				append_str(&out, &cap, &len, "<", 1);
				append_str(&out, &cap, &len, tagname, name_len);
				if (attr_out && attr_len > 0) {
					append_str(
					        &out, &cap, &len, attr_out,
					        attr_len);
				}
				if (tag_end[-1] == '/')
					append_str(&out, &cap, &len, " /", 2);
				append_str(&out, &cap, &len, ">", 1);
			}
			free(attr_out);
			p = tag_end + 1;
		} else {
			const char *next_lt = strchr(p, '<');
			size_t txt_len =
			        next_lt ? (size_t)(next_lt - p) : strlen(p);
			append_str(&out, &cap, &len, p, txt_len);
			p += txt_len;
		}
	}
	return out;
}
static char *sanitize_groff_html(const char *raw)
{
	char *body = extract_body(raw);
	char *sanitized = body ? sanitize_html(body) : sanitize_html(raw);

	if (body)
		free(body);
	if (!sanitized)
		return strdup("");
	strip_style_blocks(sanitized);
	return sanitized;
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
