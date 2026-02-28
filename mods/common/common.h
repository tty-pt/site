#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>

#include <ttypt/ndx.h>

NDX_DECL(int, json_escape, const char *, in, char *, out, size_t, outlen);
NDX_DECL(int, url_encode, const char *, in, char *, out, size_t, outlen);
NDX_DECL(int, get_cookie, const char *, cookie, char *, token, size_t, len);
NDX_DECL(int, query_param, char *, query, const char *, key, char *, out, size_t, out_len);

#endif
