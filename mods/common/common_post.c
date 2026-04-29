#include <stdlib.h>
#include <string.h>

#include <ttypt/ndx-mod.h>

#include "common_internal.h"

NDX_LISTENER(int, core_post_json, int, fd, const char *, json)
{
	return core_post(fd, (char *)json, strlen(json));
}

NDX_LISTENER(int, core_post_form, int, fd, form_body_t *, fb)
{
	size_t pb_len = 0;
	char *post_body = form_body_finish(fb, &pb_len);
	int rc;

	if (!post_body)
		return respond_error(fd, 500, "OOM");

	rc = core_post(fd, post_body, pb_len);
	free(post_body);
	return rc;
}

NDX_LISTENER(int, core_post_form_builder,
	int, fd, form_body_builder_cb, cb, void *, user)
{
	form_body_t *fb = form_body_new(0);
	int rc;

	if (!fb)
		return respond_error(fd, 500, "OOM");
	if (!cb) {
		form_body_free(fb);
		return respond_error(fd, 500, "Missing form builder");
	}

	rc = cb(fd, fb, user);
	if (rc != 0) {
		form_body_free(fb);
		return rc;
	}

	return core_post_form(fd, fb);
}
