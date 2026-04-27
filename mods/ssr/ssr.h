#ifndef SSR_MOD_H
#define SSR_MOD_H

#include <stddef.h>
#include <ttypt/ndx-mod.h>

#ifndef SSR_IMPL
NDX_HOOK_DECL(int, ssr_render,
	int, fd,
	const char *, method,
	const char *, path,
	const char *, query,
	const char *, body,
	size_t, body_len,
	const char *, remote_user,
	const char *, forwarded_host,
	const char *, modules_header);
#endif

#endif
