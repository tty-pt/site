#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ttypt/ndx-mod.h>

#include "common_internal.h"

static int fb_reserve(form_body_t *fb, size_t extra)
{
	size_t need = fb->len + extra + 1;
	size_t nc;
	char *nb;

	if (need <= fb->cap)
		return 0;
	nc = fb->cap ? fb->cap : 2048;
	while (nc < need)
		nc *= 2;
	nb = realloc(fb->buf, nc);
	if (!nb)
		return -1;
	fb->buf = nb;
	fb->cap = nc;
	return 0;
}

NDX_LISTENER(form_body_t *, form_body_new, int, dummy)
{
	form_body_t *fb;

	(void)dummy;
	fb = calloc(1, sizeof(*fb));
	if (!fb)
		return NULL;
	fb->cap = 2048;
	fb->buf = malloc(fb->cap);
	if (!fb->buf) {
		free(fb);
		return NULL;
	}
	fb->buf[0] = '\0';
	fb->first = 1;
	return fb;
}

NDX_LISTENER(int, form_body_add,
	form_body_t *, fb, const char *, name, const char *, value)
{
	const char *v = value ? value : "";
	size_t n_cap;
	size_t v_cap;
	char *en;
	char *ev;

	if (!fb || !name)
		return -1;

	n_cap = strlen(name) * 3 + 2;
	v_cap = strlen(v) * 3 + 2;
	en = malloc(n_cap);
	ev = malloc(v_cap);
	if (!en || !ev) {
		free(en);
		free(ev);
		return -1;
	}

	url_encode(name, en, n_cap);
	url_encode(v, ev, v_cap);

	if (fb_reserve(fb, strlen(en) + strlen(ev) + 3) != 0) {
		free(en);
		free(ev);
		return -1;
	}
	if (!fb->first)
		fb->buf[fb->len++] = '&';
	fb->first = 0;
	fb->len += snprintf(fb->buf + fb->len, fb->cap - fb->len,
		"%s=%s", en, ev);

	free(en);
	free(ev);
	return 0;
}

NDX_LISTENER(int, form_body_free, form_body_t *, fb)
{
	if (!fb)
		return 0;
	free(fb->buf);
	free(fb);
	return 0;
}

char *
form_body_finish(form_body_t *fb, size_t *out_len)
{
	char *out;

	if (!fb) {
		if (out_len)
			*out_len = 0;
		return NULL;
	}
	if (out_len)
		*out_len = fb->len;
	out = fb->buf;
	free(fb);
	return out;
}
