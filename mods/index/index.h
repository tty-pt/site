#ifndef INDEX_MOD_H
#define INDEX_MOD_H

#include <ttypt/ndx-mod.h>

typedef void (*index_cleanup_fn)(const char *id);

NDX_DECL(unsigned, index_open, const char *, name, unsigned, mask, unsigned, flags, index_cleanup_fn, cleanup);

NDX_DECL(unsigned, index_put, unsigned, hd, char *, key, char *, value);

NDX_DECL(unsigned, index_get, unsigned, hd, char *, value, size_t, len, char *, key);

NDX_DECL(int, index_page, unsigned, fd, unsigned, hd, char *, path, char *, title);

NDX_DECL(int, index_id, char *, result, size_t, result_len, const char *, title, size_t, title_len);
NDX_DECL(int, index_add_item, int, fd, char *, body, char *, id_out, size_t, id_len);

NDX_DECL(int, index_del, unsigned, hd, char *, key);

NDX_DECL(int, core_get, int, fd, char *, body);
NDX_DECL(int, core_post, int, fd, char *, body, size_t, len);

#endif
