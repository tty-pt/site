#include "../include/bud/bud_app.h"
#include "../include/bud/bud.h"
#include <stddef.h>

#ifdef __wasm__

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

__attribute__((import_module("env"), import_name("bud_host_log"))) void
bud_host_log(const char *msg, size_t len);
#define WASM_LOG(msg)                                                          \
	do {                                                                   \
		const char *_m = (msg);                                        \
		bud_host_log(_m, strlen(_m));                                  \
	} while (0)
static void wasm_printf(const char *fmt, ...)
{
	char buf[256];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	bud_host_log(buf, strlen(buf));
}

__attribute__((import_module("env"), import_name("bud_host_mark_dirty"))) void
bud_host_mark_dirty(void);

__attribute__((import_module("env"), import_name("bud_host_flush"))) void
bud_host_flush(void);

__attribute__((import_module("env"), import_name("bud_host_fetch"))) void
bud_host_fetch(const char *url, size_t url_len, int request_id);

__attribute__((import_module("env"), import_name("bud_host_set_location"))) void
bud_host_set_location(const char *url, size_t url_len);

/* Platform host function pointers (declared in bud.h) */
extern void (*bud_host_fetch_fn)(const char *, size_t, int);
extern void (*bud_host_log_fn)(const char *, size_t);
extern void (*bud_host_set_location_fn)(const char *, size_t);

__attribute__((constructor)) static void wasm_platform_init(void)
{
	bud_host_fetch_fn = bud_host_fetch;
	bud_host_log_fn = bud_host_log;
	bud_host_set_location_fn = bud_host_set_location;
}

static bud_runtime *runtime = NULL;
static char wasm_dbg_buf[65536];

static int noop_emit(
        void *user, const char *op, const char *a, const char *b, const char *c)
{
	(void)user;
	(void)op;
	(void)a;
	(void)b;
	(void)c;
	return 0;
}

static int emit_patch_wrapper(
        void *user, const char *op, const char *a, const char *b, const char *c)
{
	bud_host_emit_patch(
	        op,
	        op ? strlen(op) : 0,
	        a,
	        a ? strlen(a) : 0,
	        b,
	        b ? strlen(b) : 0,
	        c,
	        c ? strlen(c) : 0);
	return 0;
}

__attribute__((export_name("bud_app_mount"))) void wasm_mount(void)
{
	bud_host_emit_patch("MOUNT_START", 11, NULL, 0, NULL, 0, NULL, 0);
	if (!runtime) {
		bud_node *app = bud_app_render();
		bud_render_ops(app, noop_emit, NULL);
		runtime = bud_runtime_new(app);
		bud_runtime_mount(runtime);
		bud_host_emit_patch(
		        "MOUNT_DONE", 10, NULL, 0, NULL, 0, NULL, 0);
	}
}

__attribute__((export_name("bud_app_update"))) void wasm_update(void)
{
	if (!runtime)
		return;
	bud_runtime_free(runtime);
	bud_node *app = bud_app_render();
	runtime = bud_runtime_new(app);
	bud_runtime_mount(runtime);
	bud_runtime_mark_dirty(runtime);
}

__attribute__((export_name("bud_app_unmount"))) void wasm_unmount(void)
{
	if (runtime) {
		bud_runtime_free(runtime);
		runtime = NULL;
	}
}

__attribute__((export_name("bud_app_mark_dirty"))) void wasm_mark_dirty(void)
{
	if (runtime) {
		bud_runtime_mark_dirty(runtime);
	}
}

__attribute__((export_name("bud_app_flush"))) void wasm_flush(void)
{
	bud_host_emit_patch("DEBUG_FLUSH", 11, NULL, 0, NULL, 0, NULL, 0);
	if (runtime) {
		bud_render_patch_ops(
		        bud_runtime_root(runtime), emit_patch_wrapper, NULL);
		bud_runtime_flush(runtime);
	}
}

static bud_node *find_node_by_id(const bud_node *node, unsigned int id)
{
	if (!node)
		return NULL;
	if (bud_node_id(node) == id)
		return (bud_node *)node;
	for (size_t i = 0; i < bud_node_child_count(node); i++) {
		bud_node *found = find_node_by_id(bud_node_child(node, i), id);
		if (found)
			return found;
	}
	return NULL;
}

bud_runtime *wasm_get_runtime(void)
{
	return runtime;
}

void bud_patch_attr(bud_node *node, const char *name, const char *value)
{
	if (!node || !name || !value)
		return;
	char id_str[16];
	snprintf(id_str, sizeof(id_str), "%u", bud_node_id(node));
	bud_host_emit_patch(
	        "patch-attr",
	        10,
	        id_str,
	        strlen(id_str),
	        name,
	        strlen(name),
	        value,
	        strlen(value));
}

void bud_patch_text(bud_node *node, const char *value)
{
	if (!node || !value)
		return;
	char id_str[16];
	snprintf(id_str, sizeof(id_str), "%u", bud_node_id(node));
	bud_host_emit_patch(
	        "patch-text",
	        11,
	        value,
	        strlen(value),
	        id_str,
	        strlen(id_str),
	        NULL,
	        0);
}

__attribute__((export_name("bud_patch_innerhtml"))) void
bud_patch_innerhtml(unsigned int node_id, const char *html)
{
	if (!html)
		return;
	char id_str[16];
	snprintf(id_str, sizeof(id_str), "%u", node_id);
	bud_host_emit_patch(
	        "patch-innerhtml",
	        15,
	        id_str,
	        strlen(id_str),
	        html,
	        strlen(html),
	        NULL,
	        0);
}

__attribute__((export_name("bud_app_dispatch"))) int wasm_dispatch_event(
        unsigned int node_id,
        const char *event_name,
        int bubbles,
        void *event_data)
{
	if (runtime) {
		bud_node *root = bud_runtime_root(runtime);
		bud_node *target = find_node_by_id(root, node_id);
		if (target) {
			bud_host_emit_patch(
			        "DISPATCH", 8, NULL, 0, NULL, 0, NULL, 0);
			int ret = bud_runtime_dispatch(
			        runtime, target, event_name, event_data);
			return ret;
		}
		wasm_printf("dispatch: node=%d NOT FOUND\n", node_id);
		return -2;
	}
	return -1;
}

/* ── Debug exports ── */

__attribute__((export_name("wasm_dump_tree"))) void wasm_dump_tree(void)
{
	bud_node *root;

	if (!runtime) {
		WASM_LOG("(no runtime)");
		return;
	}
	root = bud_runtime_root(runtime);
	if (!root) {
		WASM_LOG("(no root)");
		return;
	}
	bud_sprint_tree(root, wasm_dbg_buf, sizeof(wasm_dbg_buf));
	WASM_LOG("--- WASM tree ---");
	WASM_LOG(wasm_dbg_buf);
	WASM_LOG("--- end tree ---");
}

__attribute__((export_name("wasm_get_tree_text"))) const char *
wasm_get_tree_text(void)
{
	bud_node *root;

	if (!runtime) {
		wasm_dbg_buf[0] = '\0';
		return wasm_dbg_buf;
	}
	root = bud_runtime_root(runtime);
	if (!root) {
		wasm_dbg_buf[0] = '\0';
		return wasm_dbg_buf;
	}
	bud_sprint_tree(root, wasm_dbg_buf, sizeof(wasm_dbg_buf));
	return wasm_dbg_buf;
}

__attribute__((export_name("wasm_get_src"))) const char *
wasm_get_src(unsigned int node_id)
{
	bud_node *root;
	bud_node *target;

	if (!runtime) {
		wasm_dbg_buf[0] = '\0';
		return wasm_dbg_buf;
	}
	root = bud_runtime_root(runtime);
	if (!root) {
		wasm_dbg_buf[0] = '\0';
		return wasm_dbg_buf;
	}
	target = find_node_by_id(root, node_id);
	if (!target) {
		snprintf(
		        wasm_dbg_buf,
		        sizeof(wasm_dbg_buf),
		        "node=%d NOT IN WASM TREE",
		        node_id);
		return wasm_dbg_buf;
	}
	snprintf(
	        wasm_dbg_buf,
	        sizeof(wasm_dbg_buf),
	        "%s",
	        bud_node_get_src(target));
	return wasm_dbg_buf;
}

#else

/* Native (non-WASM) stubs — these are needed so that libbud.so exports
 * the wasm_* symbols for use by native modules like mods/song/song.so.
 * In the native server context there is no browser DOM, so each stub
 * is a no-op or returns a sensible default. */

void wasm_mount(void)
{
	(void)0;
}

void wasm_update(void)
{
	(void)0;
}

void wasm_unmount(void)
{
	(void)0;
}

void wasm_mark_dirty(void)
{
	(void)0;
}

void wasm_flush(void)
{
	(void)0;
}

bud_runtime *wasm_get_runtime(void)
{
	return NULL;
}

void bud_patch_attr(bud_node *node, const char *name, const char *value)
{
	(void)node;
	(void)name;
	(void)value;
}

void bud_patch_text(bud_node *node, const char *value)
{
	(void)node;
	(void)value;
}

int wasm_dispatch_event(
        unsigned int node_id,
        const char *event_name,
        int bubbles,
        void *event_data)
{
	(void)node_id;
	(void)event_name;
	(void)bubbles;
	(void)event_data;
	return -1;
}

void bud_patch_innerhtml(unsigned int node_id, const char *html)
{
	(void)node_id;
	(void)html;
}

void bud_host_set_location(const char *url, size_t len)
{
	(void)url;
	(void)len;
}

void wasm_dump_tree(void)
{
	(void)0;
}

const char *wasm_get_tree_text(void)
{
	return "";
}

const char *wasm_get_src(unsigned int node_id)
{
	(void)node_id;
	return "";
}

#endif
