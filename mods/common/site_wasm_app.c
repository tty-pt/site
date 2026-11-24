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

__attribute__((constructor)) static void site_platform_init(void)
{
	bud_host_fetch_fn = (void (*)(const char *, size_t, int))stl_host_fetch;
	bud_host_log_fn = (void (*)(const char *, size_t))stl_host_log;
	bud_host_set_location_fn = (void (*)(const char *, size_t))stl_host_set_location;
}

#define BUD_MAX_SLOTS 4
#define BUD_MAX_RETIRED_ROOTS 64
#define BUD_ARENA_POOL_CAP 8
typedef struct {
	bud_node *root;
	bud_arena *arena;
} retired_vdom_t;
static bud_runtime *s_runtimes[BUD_MAX_SLOTS];
static bud_arena *s_slot_arenas[BUD_MAX_SLOTS];
static bud_node *(*s_render_fns[BUD_MAX_SLOTS])(void);
static bud_arena *s_arena_pool[BUD_ARENA_POOL_CAP];
static size_t s_arena_pool_len = 0;
static retired_vdom_t s_retired[BUD_MAX_RETIRED_ROOTS];
static size_t s_retired_count = 0;
static int s_dispatch_depth = 0;
static int s_active_slot = -1;
static char s_wasm_dbg_buf[65536];

static bud_arena *acquire_render_arena(void)
{
	if (s_arena_pool_len > 0)
		return s_arena_pool[--s_arena_pool_len];
	return bud_arena_new(8192);
}

static void release_render_arena(bud_arena *a)
{
	if (!a)
		return;
	bud_arena_reset(a);
	if (s_arena_pool_len < BUD_ARENA_POOL_CAP)
		s_arena_pool[s_arena_pool_len++] = a;
	else
		bud_arena_free(a);
}

static void free_vdom_root(bud_node *root, bud_arena *arena)
{
	if (root)
		bud_free(root);
	release_render_arena(arena);
}

static void retire_vdom_root(bud_node *old_root, bud_arena *old_arena)
{
	if (!old_root && !old_arena)
		return;
	if (s_dispatch_depth <= 0) {
		free_vdom_root(old_root, old_arena);
		return;
	}
	if (s_retired_count < BUD_MAX_RETIRED_ROOTS) {
		s_retired[s_retired_count].root = old_root;
		s_retired[s_retired_count].arena = old_arena;
		s_retired_count++;
	} else {
		free_vdom_root(s_retired[0].root, s_retired[0].arena);
		memmove(&s_retired[0], &s_retired[1],
		        (BUD_MAX_RETIRED_ROOTS - 1) * sizeof(retired_vdom_t));
		s_retired[BUD_MAX_RETIRED_ROOTS - 1].root = old_root;
		s_retired[BUD_MAX_RETIRED_ROOTS - 1].arena = old_arena;
	}
}

static void flush_retired_vdom_roots(void)
{
	if (s_dispatch_depth > 0)
		return;
	while (s_retired_count > 0) {
		retired_vdom_t r = s_retired[--s_retired_count];
		s_retired[s_retired_count].root = NULL;
		s_retired[s_retired_count].arena = NULL;
		free_vdom_root(r.root, r.arena);
	}
}

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

static int resolve_node_slot(const bud_node *node)
{
	unsigned int id = bud_node_id(node);
	if (s_active_slot >= 0 && s_active_slot < BUD_MAX_SLOTS &&
	    s_runtimes[s_active_slot] &&
	    find_node_by_id(bud_runtime_root(s_runtimes[s_active_slot]), id))
		return s_active_slot;
	if (s_runtimes[1] && find_node_by_id(bud_runtime_root(s_runtimes[1]), id))
		return 1;
	for (int i = 0; i < BUD_MAX_SLOTS; i++) {
		if (s_runtimes[i] && find_node_by_id(bud_runtime_root(s_runtimes[i]), id))
			return i;
	}
	return s_active_slot >= 0 ? s_active_slot : 0;
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
	int slot = resolve_node_slot(node);
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
	int slot = resolve_node_slot(node);
	char id_str[16];
	snprintf(id_str, sizeof(id_str), "%u", bud_node_id(node));
	stl_host_emit_patch(
	        slot, "patch-text", 10, value, strlen(value),
	        id_str, strlen(id_str), NULL, 0);
}

int bud_slot_mount(int slot, bud_node *(*render_fn)(void))
{
	bud_host_fetch_fn = (void (*)(const char *, size_t, int))stl_host_fetch;
	bud_host_log_fn = (void (*)(const char *, size_t))stl_host_log;
	bud_host_set_location_fn = (void (*)(const char *, size_t))stl_host_set_location;
	if (slot < 0 || slot >= BUD_MAX_SLOTS || !render_fn)
		return -1;
	s_render_fns[slot] = render_fn;
	if (s_runtimes[slot]) {
		bud_node *old_root = bud_runtime_root(s_runtimes[slot]);
		bud_arena *old_arena = s_slot_arenas[slot];
		bud_runtime_free(s_runtimes[slot]);
		s_runtimes[slot] = NULL;
		s_slot_arenas[slot] = NULL;
		retire_vdom_root(old_root, old_arena);
	}
	bud_arena *arena = acquire_render_arena();
	bud_arena *prev = bud_arena_push(arena);
	bud_node *root = bud_expand_tree(render_fn());
	bud_arena_pop(prev);
	if (root) {
		bud_render_ops(root, noop_emit, NULL);
		s_runtimes[slot] = bud_runtime_new(root);
		s_slot_arenas[slot] = arena;
		bud_runtime_mount(s_runtimes[slot]);
	} else {
		release_render_arena(arena);
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
	bud_arena *old_arena = s_slot_arenas[slot];
	bud_arena *new_arena = acquire_render_arena();
	bud_arena *prev = bud_arena_push(new_arena);
	bud_node *new_root = bud_expand_tree(render_fn());
	bud_arena_pop(prev);
	if (!new_root) {
		release_render_arena(new_arena);
		return -1;
	}
	bud_vdom_diff(old_root, new_root, emit_patch_wrapper, (void *)(intptr_t)slot);
	bud_runtime_free(s_runtimes[slot]);
	s_slot_arenas[slot] = new_arena;
	retire_vdom_root(old_root, old_arena);
	s_runtimes[slot] = bud_runtime_new(new_root);
	bud_runtime_mount(s_runtimes[slot]);
	return 0;
}

void bud_app_set_state(void)
{
	if (s_active_slot >= 0 && s_active_slot < BUD_MAX_SLOTS &&
	    s_runtimes[s_active_slot] && s_render_fns[s_active_slot]) {
		bud_slot_update(s_active_slot, s_render_fns[s_active_slot]);
		return;
	}
	int updated = 0;
	for (int i = 0; i < BUD_MAX_SLOTS; i++) {
		if (s_runtimes[i] && s_render_fns[i]) {
			bud_slot_update(i, s_render_fns[i]);
			updated = 1;
		}
	}
	if (updated)
		return;
#ifndef QSYS_UNIFIED_SITE
	if (s_runtimes[1] && !s_render_fns[1] && &bud_app_render) {
		bud_slot_update(1, bud_app_render);
		return;
	}
	if (s_runtimes[0] && !s_render_fns[0] && &bud_app_render) {
		bud_slot_update(0, bud_app_render);
		return;
	}
	if (!s_runtimes[1] && !s_runtimes[0] && &bud_app_render) {
		bud_slot_mount(1, bud_app_render);
	}
#endif
}

ISO_EXPORT_AS(void, wasm_mount, bud_app_mount)
{
	bud_host_fetch_fn = (void (*)(const char *, size_t, int))stl_host_fetch;
	bud_host_log_fn = (void (*)(const char *, size_t))stl_host_log;
	bud_host_set_location_fn = (void (*)(const char *, size_t))stl_host_set_location;
#ifndef QSYS_UNIFIED_SITE
	if (!s_runtimes[1] && !s_runtimes[0] && &bud_app_render) {
		bud_slot_mount(1, bud_app_render);
	}
#endif
}

ISO_EXPORT_AS(void, wasm_update, bud_app_update)
{
	bud_app_set_state();
}

ISO_EXPORT_AS(void, wasm_unmount, bud_app_unmount)
{
	for (int i = 0; i < BUD_MAX_SLOTS; i++) {
		if (s_runtimes[i]) {
			bud_node *old_root = bud_runtime_root(s_runtimes[i]);
			bud_arena *old_arena = s_slot_arenas[i];
			bud_runtime_free(s_runtimes[i]);
			s_runtimes[i] = NULL;
			s_slot_arenas[i] = NULL;
			s_render_fns[i] = NULL;
			retire_vdom_root(old_root, old_arena);
		}
	}
	flush_retired_vdom_roots();
	if (s_dispatch_depth <= 0) {
		while (s_arena_pool_len > 0) {
			bud_arena_free(s_arena_pool[--s_arena_pool_len]);
			s_arena_pool[s_arena_pool_len] = NULL;
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
	int prev_slot = s_active_slot;
	s_active_slot = slot;
	s_dispatch_depth++;
	int rc = bud_runtime_dispatch(s_runtimes[slot], target, event_name, event_data);
	s_dispatch_depth--;
	s_active_slot = prev_slot;
	flush_retired_vdom_roots();
	return rc;
}

int bud_slot_dispatch_any(unsigned int node_id, const char *event_name, int bubbles, void *event_data)
{
	(void)bubbles;
	int order[] = { 1, 0, 2, 3 };
	for (size_t i = 0; i < sizeof(order)/sizeof(order[0]); i++) {
		int s = order[i];
		if (s_runtimes[s]) {
			bud_node *root = bud_runtime_root(s_runtimes[s]);
			bud_node *target = (node_id == 0) ? root : find_node_by_id(root, node_id);
			if (target) {
				int prev_slot = s_active_slot;
				s_active_slot = s;
				s_dispatch_depth++;
				int rc = bud_runtime_dispatch(s_runtimes[s], target, event_name, event_data);
				s_dispatch_depth--;
				s_active_slot = prev_slot;
				flush_retired_vdom_roots();
				return rc;
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

ISO_EXPORT(int, stl_optimistic_update, const char *dataset, const char *id, const char *patch_json)
{
	(void)dataset; (void)id; (void)patch_json;
	bud_app_set_state();
	return 0;
}

ISO_EXPORT(int, stl_optimistic_rollback, const char *dataset, const char *id)
{
	(void)dataset; (void)id;
	bud_app_set_state();
	return 0;
}

ISO_EXPORT_AS(int, stele_optimistic_update, stl_optimistic_update, const char *dataset, const char *id, const char *patch_json);
ISO_EXPORT_AS(int, stele_optimistic_rollback, stl_optimistic_rollback, const char *dataset, const char *id);
#endif
