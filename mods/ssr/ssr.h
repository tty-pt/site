#ifndef SSR_H
#define SSR_H

#include <ttypt/ndx-mod.h>

NDX_DECL(int, ssr_register_module, const char *, id, const char *, title);
NDX_DECL(int, ssr_proxy_get, int, fd, const char *, path);
NDX_DECL(int, ssr_proxy_post, int, fd, const char *, path, const char *, body_data, size_t, body_len);

#endif
