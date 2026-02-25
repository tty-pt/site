#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>

void query_param(char *query, const char *key, char *out, size_t out_len);
void json_escape(const char *in, char *out, size_t outlen);
void url_encode(const char *in, char *out, size_t outlen);
void get_cookie(const char *cookie, char *token, size_t len);

#endif
