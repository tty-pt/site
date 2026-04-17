#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>

#include <ttypt/ndx-mod.h>

NDX_DECL(int, json_escape, const char *, in, char *, out, size_t, outlen);
NDX_DECL(int, url_encode, const char *, in, char *, out, size_t, outlen);
NDX_DECL(int, b64_encode, const char *, in, char *, out, size_t, outlen);
NDX_DECL(int, get_cookie, const char *, cookie, char *, token, size_t, len);
NDX_DECL(int, query_parse, char *, query);
NDX_DECL(int, query_exists, const char *, name);
NDX_DECL(int, query_param, const char *, name, char *, buf, size_t, buf_len);


#endif
