#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>

#include <ttypt/ndx.h>

NDX_DECL(const char *, get_session_user, const char *, token);
NDX_DECL(int, json_escape, const char *, in, char *, out, size_t, outlen);
NDX_DECL(int, url_encode, const char *, in, char *, out, size_t, outlen);
NDX_DECL(int, get_cookie, const char *, cookie, char *, token, size_t, len);

NDX_DECL(int, query_param, char *, query, const char *, key, char *, out, size_t, out_len);

int json_escape(const char *in, char *out, size_t outlen);
int url_encode(const char *in, char *out, size_t outlen);
int get_cookie(const char *cookie, char *token, size_t len);

#endif
