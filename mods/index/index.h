#ifndef INDEX_MOD_H
#define INDEX_MOD_H

#include <ttypt/ndx-mod.h>

typedef void (*index_cleanup_fn)(const char *id);

/* Optional serializer for index_render_list.
 * Writes one line for (id, val) into out (up to out_sz bytes).
 * Returns the number of bytes written (like snprintf, without NUL).
 * NULL → default "id val\r\n" format. */
typedef size_t (*index_format_fn)(
        const char *id, const char *val, char *out, size_t out_sz);

typedef int (*index_handler_fn)(int fd, char *body);
typedef int (*index_detail_handler_fn)(int fd, char *body);

NDX_HOOK_DECL(unsigned, index_open,
	const char *, name,
	const char *, dataset_name,
	index_cleanup_fn, cleanup,
	index_detail_handler_fn, detail_handler,
	index_handler_fn, add_handler,
	index_handler_fn, edit_get_handler,
	index_handler_fn, edit_post_handler);

NDX_HOOK_DECL(int, index_add_item,
	int, fd,
	char *, body,
	char *, id_out,
	size_t, id_len);

NDX_HOOK_DECL(int, core_get, int, fd, char *, body);
NDX_HOOK_DECL(int, index_render_list,
	int, fd,
	unsigned, hd,
	index_format_fn, fmt);
/* Ownership helpers — moved from auth; they're item metadata ops, not auth */

NDX_HOOK_DECL(int, item_record_ownership,
	const char *, item_path,
	const char *, username);

NDX_HOOK_DECL(int, check_item_access,
	int, fd,
	const char *, module,
	char *, id, size_t, id_sz,
	const char **, user,
	char *, item_path, size_t, path_sz);

#endif
