#include "bud/bud.h"
#include "bud/bud_app.h"
#include <ttypt/iso.h>
#include <string.h>

#if defined(__wasm__)
ISO_IMPORT(void, stele, stl_host_emit_patch,
           int slot,
           const char *op, size_t op_len,
           const char *a, size_t a_len,
           const char *b, size_t b_len,
           const char *c, size_t c_len);

ISO_IMPORT(void, stele, stl_host_fetch, const char *url, size_t url_len, int req_id);
ISO_IMPORT(void, stele, stl_host_log, const char *msg, size_t len);
ISO_IMPORT(void, stele, stl_host_set_location, const char *url, size_t url_len);

extern void (*bud_host_fetch_fn)(const char *url, size_t len, int id);
extern void (*bud_host_log_fn)(const char *msg, size_t len);
extern void (*bud_host_set_location_fn)(const char *url, size_t len);

__attribute__((constructor)) static void site_platform_init(void)
{
	bud_host_fetch_fn = (void (*)(const char *, size_t, int))stl_host_fetch;
	bud_host_log_fn = (void (*)(const char *, size_t))stl_host_log;
	bud_host_set_location_fn = (void (*)(const char *, size_t))stl_host_set_location;
}

#define BUD_MAX_SLOTS 4
static bud_runtime *s_runtimes[BUD_MAX_SLOTS];
static bud_node *(*s_render_fns[BUD_MAX_SLOTS])(void);
static char s_wasm_dbg_buf[65536];

static int noop_emit(
        void *user, const char *op, const char *a, const char *b, const char *c)
{
	(void)user; (void)op; (void)a; (void)b; (void)c;
	return 0;
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

static int emit_patch_wrapper(
        void *user, const char *op, const char *a, const char *b, const char *c)
{
	int slot = (int)(intptr_t)user;
	stl_host_emit_patch(
	        slot, op, op ? strlen(op) : 0, a, a ? strlen(a) : 0, b,
	        b ? strlen(b) : 0, c, c ? strlen(c) : 0);
	return 0;
}

void bud_patch_attr(bud_node *node, const char *name, const char *value)
{
	if (!node || !name || !value)
		return;
	int slot = 0;
	if (s_runtimes[1] && find_node_by_id(bud_runtime_root(s_runtimes[1]), bud_node_id(node)))
		slot = 1;
	char id_str[16];
	snprintf(id_str, sizeof(id_str), "%u", bud_node_id(node));
	stl_host_emit_patch(
	        slot, "patch-attr", 10, id_str, strlen(id_str), name, strlen(name),
	        value, strlen(value));
}

void bud_patch_text(bud_node *node, const char *value)
{
	if (!node || !value)
		return;
	int slot = 0;
	if (s_runtimes[1] && find_node_by_id(bud_runtime_root(s_runtimes[1]), bud_node_id(node)))
		slot = 1;
	char id_str[16];
	snprintf(id_str, sizeof(id_str), "%u", bud_node_id(node));
	stl_host_emit_patch(
	        slot, "patch-text", 10, value, strlen(value),
	        id_str, strlen(id_str), NULL, 0);
}

int bud_slot_mount(int slot, bud_node *(*render_fn)(void))
{
	if (slot < 0 || slot >= BUD_MAX_SLOTS || !render_fn)
		return -1;
	s_render_fns[slot] = render_fn;
	if (s_runtimes[slot]) {
		bud_runtime_free(s_runtimes[slot]);
		s_runtimes[slot] = NULL;
	}
	bud_node *root = render_fn();
	if (root) {
		bud_render_ops(root, noop_emit, NULL);
		s_runtimes[slot] = bud_runtime_new(root);
		bud_runtime_mount(s_runtimes[slot]);
	}
	return 0;
}

int bud_slot_update(int slot, bud_node *(*render_fn)(void))
{
	if (slot < 0 || slot >= BUD_MAX_SLOTS)
		return -1;
	if (!render_fn)
		render_fn = s_render_fns[slot];
	if (!render_fn || !s_runtimes[slot])
		return -1;
	bud_node *old_root = bud_runtime_root(s_runtimes[slot]);
	bud_node *new_root = render_fn();
	if (!new_root)
		return -1;
	bud_vdom_diff(old_root, new_root, emit_patch_wrapper, (void *)(intptr_t)slot);
	bud_runtime_free(s_runtimes[slot]);
	s_runtimes[slot] = bud_runtime_new(new_root);
	bud_runtime_mount(s_runtimes[slot]);
	return 0;
}

void bud_app_set_state(void)
{
	if (s_runtimes[1] && s_render_fns[1]) {
		bud_slot_update(1, s_render_fns[1]);
		return;
	}
	if (s_runtimes[0] && s_render_fns[0]) {
		bud_slot_update(0, s_render_fns[0]);
		return;
	}
	if (s_runtimes[0] && !s_render_fns[0] && &bud_app_render) {
		bud_slot_update(0, bud_app_render);
		return;
	}
	if (!s_runtimes[0] && &bud_app_render) {
		bud_slot_mount(0, bud_app_render);
	}
}

ISO_EXPORT_AS(void, wasm_mount, bud_app_mount)
{
	if (&bud_app_render) {
		bud_slot_mount(0, bud_app_render);
	}
}

ISO_EXPORT_AS(void, wasm_update, bud_app_update)
{
	bud_app_set_state();
}

ISO_EXPORT_AS(void, wasm_unmount, bud_app_unmount)
{
	for (int i = 0; i < BUD_MAX_SLOTS; i++) {
		if (s_runtimes[i]) {
			bud_runtime_free(s_runtimes[i]);
			s_runtimes[i] = NULL;
			s_render_fns[i] = NULL;
		}
	}
}

int bud_slot_dispatch_event(int slot, unsigned int node_id, const char *event_name, int bubbles, void *event_data)
{
	(void)bubbles;
	if (slot < 0 || slot >= BUD_MAX_SLOTS || !s_runtimes[slot])
		return -1;
	bud_node *root = bud_runtime_root(s_runtimes[slot]);
	bud_node *target = (node_id == 0) ? root : find_node_by_id(root, node_id);
	if (!target)
		target = root;
	return bud_runtime_dispatch(s_runtimes[slot], target, event_name, event_data);
}

int bud_slot_dispatch_any(unsigned int node_id, const char *event_name, int bubbles, void *event_data)
{
	int order[] = { 1, 0, 2, 3 };
	for (size_t i = 0; i < sizeof(order)/sizeof(order[0]); i++) {
		int s = order[i];
		if (s_runtimes[s]) {
			bud_node *root = bud_runtime_root(s_runtimes[s]);
			bud_node *target = (node_id == 0) ? root : find_node_by_id(root, node_id);
			if (target) {
				return bud_runtime_dispatch(s_runtimes[s], target, event_name, event_data);
			}
		}
	}
	return -1;
}

ISO_EXPORT_AS(int, wasm_dispatch_event, bud_app_dispatch,
        unsigned int node_id, const char *event_name, int bubbles,
        void *event_data)
{
	return bud_slot_dispatch_any(node_id, event_name, bubbles, event_data);
}

ISO_EXPORT(const char *, wasm_get_tree_text, void)
{
	bud_node *root = s_runtimes[1] ? bud_runtime_root(s_runtimes[1]) :
	                 s_runtimes[0] ? bud_runtime_root(s_runtimes[0]) : NULL;
	if (!root) {
		s_wasm_dbg_buf[0] = '\0';
		return s_wasm_dbg_buf;
	}
	bud_sprint_tree(root, s_wasm_dbg_buf, sizeof(s_wasm_dbg_buf));
	return s_wasm_dbg_buf;
}
#endif
