#ifndef INDEX_MOD_H
#define INDEX_MOD_H

#include <ttypt/ndx-mod.h>

typedef void (*index_cleanup_fn)(const char *id);
typedef void (*index_tsv_cb)(const char *id, const char *val, void *user);
typedef int (*index_item_read_fn)(const char *path, char *out, size_t sz);

NDX_HOOK_DECL(unsigned, index_open, const char *, name, unsigned, mask, unsigned, flags, index_cleanup_fn, cleanup);

NDX_HOOK_DECL(unsigned, index_put, unsigned, hd, char *, key, char *, value);

NDX_HOOK_DECL(int, index_id, char *, result, size_t, result_len, const char *, title, size_t, title_len);
NDX_HOOK_DECL(int, index_add_item, int, fd, char *, body, char *, id_out, size_t, id_len);

NDX_HOOK_DECL(int, index_tsv_load, unsigned, hd, const char *, path, index_tsv_cb, cb, void *, user);
NDX_HOOK_DECL(int, index_tsv_save, unsigned, hd, const char *, path);
NDX_HOOK_DECL(int, index_tsv_rebuild, const char *, doc_root, const char *, module, unsigned, hd, index_item_read_fn, item_read_fn);

NDX_HOOK_DECL(int, core_get, int, fd, char *, body);
NDX_HOOK_DECL(size_t, index_get_module_count, int, dummy);
NDX_HOOK_DECL(const char *, index_get_module_id, size_t, i);
NDX_HOOK_DECL(const char *, index_get_module_title, size_t, i);
NDX_HOOK_DECL(unsigned, index_get_module_flags, size_t, i);

#endif
