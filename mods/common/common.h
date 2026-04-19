#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>
#include <ttypt/ndx-mod.h>

NDX_DECL(int, json_escape, const char *, in, char *, out, size_t, outlen);
NDX_DECL(int, url_encode, const char *, in, char *, out, size_t, outlen);
NDX_DECL(int, b64_encode, const char *, in, char *, out, size_t, outlen);
NDX_DECL(int, respond_plain, int, fd, int, status, const char *, msg);
NDX_DECL(int, respond_json, int, fd, int, status, const char *, msg);
NDX_DECL(int, respond_error, int, fd, int, status, const char *, msg);
NDX_DECL(int, redirect, int, fd, const char *, location);
NDX_DECL(int, read_meta_file, const char *, item_path, const char *, name, char *, buf, size_t, sz);
NDX_DECL(int, write_meta_file, const char *, item_path, const char *, name, const char *, buf, size_t, sz);
NDX_DECL(char *, slurp_file, const char *, path);
NDX_DECL(int, get_doc_root, int, fd, char *, buf, size_t, len);

#endif
