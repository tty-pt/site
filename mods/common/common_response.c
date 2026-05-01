#include <stdio.h>
#include <string.h>

#include <ttypt/ndc.h>
#include <ttypt/ndx-mod.h>

#include "common_internal.h"
#include "../ssr/ssr.h"

NDX_LISTENER(int, respond_plain, int, fd, int, status, const char *, msg)
{
	ndc_header_set(fd, "Content-Type", "text/plain");
	ndc_respond(fd, status, msg);
	return 1;
}

NDX_LISTENER(int, respond_json, int, fd, int, status, const char *, msg)
{
	ndc_header_set(fd, "Content-Type", "application/json");
	ndc_respond(fd, status, msg);
	return 1;
}

NDX_LISTENER(int, respond_error, int, fd, int, status, const char *, msg)
{
	char accept[256] = {0};

	ndc_header_get(fd, "Accept", accept, sizeof(accept));
	if (strstr(accept, "text/html")) {
		char enc[512] = {0};
		char body[640], uri[512] = {0}, query[512] = {0};
		int len;

		url_encode(msg, enc, sizeof(enc));
		len = snprintf(body, sizeof(body), "status=%d&error=%s",
			status, enc);
		ndc_env_get(fd, uri, "DOCUMENT_URI");
		ndc_env_get(fd, query, "QUERY_STRING");

		return ssr_render(fd, "POST", uri, query, body, (size_t)len,
			get_request_user(fd));
	}

	return respond_plain(fd, status, msg);
}

NDX_LISTENER(int, bad_request, int, fd, const char *, msg)
{
	return respond_error(fd, 400, msg ? msg : "Bad request");
}

NDX_LISTENER(int, server_error, int, fd, const char *, msg)
{
	return respond_error(fd, 500, msg ? msg : "Internal server error");
}

NDX_LISTENER(int, not_found, int, fd, const char *, msg)
{
	return respond_error(fd, 404, msg ? msg : "Not found");
}

NDX_LISTENER(int, redirect, int, fd, const char *, location)
{
	ndc_header_set(fd, "Location", (char *)location);
	ndc_respond(fd, 303, "");
	return 0;
}

NDX_LISTENER(int, redirect_to_item, int, fd, const char *, module, const char *, id)
{
	char loc[256];
	snprintf(loc, sizeof(loc), "/%s/%s", module, id);
	return redirect(fd, loc);
}
