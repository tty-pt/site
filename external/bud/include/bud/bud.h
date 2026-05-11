#ifndef BUD_BUD_H
#define BUD_BUD_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum bud_node_kind {
	BUD_NODE_FRAGMENT = 0,
	BUD_NODE_ELEMENT = 1,
	BUD_NODE_TEXT = 2,
	BUD_NODE_RAW_HTML = 3
} bud_node_kind;

typedef struct bud_node bud_node;
typedef struct bud_runtime bud_runtime;

typedef int (*bud_emit_fn)(
        void *user,
        const char *op,
        const char *a,
        const char *b,
        const char *c);

typedef int (*bud_hydrate_lookup_fn)(
        void *user,
        unsigned int id,
        const char **kind,
        const char **tag,
        const char **text);

typedef void (*bud_runtime_invalidate_fn)(void *user, bud_runtime *runtime);

typedef struct bud_event {
	const char *type;
	bud_node *target;
	bud_node *current_target;
	void *user;
	int bubbles;
	int stopped;
	int default_prevented;
} bud_event;

typedef int (*bud_event_handler_fn)(bud_event *event);

typedef void (*bud_lifecycle_fn)(
        void *user, bud_runtime *runtime, const bud_node *node);

typedef int (*bud_walk_enter_fn)(
        void *user, const bud_node *node, size_t depth);
typedef int (*bud_walk_attr_fn)(
        void *user,
        const bud_node *node,
        size_t depth,
        size_t index,
        const char *name,
        const char *value);
typedef int (*bud_walk_listener_fn)(
        void *user,
        const bud_node *node,
        size_t depth,
        size_t index,
        const char *event,
        int bubbles);
typedef int (*bud_walk_leave_fn)(
        void *user, const bud_node *node, size_t depth);

typedef struct bud_walk_ops {
	bud_walk_enter_fn enter_node;
	bud_walk_attr_fn attr;
	bud_walk_listener_fn listener;
	bud_walk_leave_fn leave_node;
	void *user;
} bud_walk_ops;

typedef bud_node *(*bud_component_fn)(void *ctx, const void *props);

typedef struct bud_component {
	bud_component_fn render;
	void *ctx;
} bud_component;

bud_node *bud_fragment(void);
bud_node *bud_element(const char *tag);
bud_node *bud_text(const char *text);
bud_node *bud_raw(const char *html);
void bud_raw_set_text(bud_node *node, const char *text);
bud_node *
bud_component_render(const bud_component *component, const void *props);

int bud_set_attr(bud_node *node, const char *name, const char *value);
int bud_set_bool_attr(bud_node *node, const char *name);
const char *bud_get_attr(const bud_node *node, const char *name);
int bud_add_class(bud_node *node, const char *cls);
int bud_remove_class(bud_node *node, const char *cls);
int bud_toggle_class(bud_node *node, const char *cls);
int bud_detach(bud_node *node);
int bud_on(bud_node *node, const char *event, int bubbles);
int bud_bind(
        bud_node *node,
        const char *event,
        int bubbles,
        bud_event_handler_fn handler);
int bud_set_lifecycle(
        bud_node *node,
        bud_lifecycle_fn on_mount,
        bud_lifecycle_fn on_update,
        bud_lifecycle_fn on_unmount,
        void *user);
int bud_append(bud_node *parent, bud_node *child);

bud_node_kind bud_node_kind_of(const bud_node *node);
const char *bud_node_tag(const bud_node *node);
const char *bud_node_text(const bud_node *node);
unsigned int bud_node_id(const bud_node *node);
size_t bud_node_child_count(const bud_node *node);
const bud_node *bud_node_child(const bud_node *node, size_t index);
size_t bud_node_attr_count(const bud_node *node);
const char *bud_node_attr_name(const bud_node *node, size_t index);
const char *bud_node_attr_value(const bud_node *node, size_t index);
size_t bud_node_listener_count(const bud_node *node);
const char *bud_node_listener_event(const bud_node *node, size_t index);
int bud_node_listener_bubbles(const bud_node *node, size_t index);

char *bud_render_html(const bud_node *root);
char *bud_render_hydrated_html(const bud_node *root);
int bud_render_ops(const bud_node *root, bud_emit_fn emit, void *user);
int bud_render_hydration_ops(
        const bud_node *root, bud_emit_fn emit, void *user);
int bud_render_patch_ops(const bud_node *root, bud_emit_fn emit, void *user);
int bud_render_walk_ops(const bud_node *root, bud_emit_fn emit, void *user);
int bud_hydrate(const bud_node *root, bud_hydrate_lookup_fn lookup, void *user);
int bud_walk(const bud_node *root, const bud_walk_ops *ops);

bud_runtime *bud_runtime_new(bud_node *root);
void bud_runtime_free(bud_runtime *runtime);
bud_node *bud_runtime_root(const bud_runtime *runtime);
int bud_runtime_set_invalidate(
        bud_runtime *runtime, bud_runtime_invalidate_fn fn, void *user);
int bud_runtime_mark_dirty(bud_runtime *runtime);
int bud_runtime_is_dirty(const bud_runtime *runtime);
int bud_runtime_flush(bud_runtime *runtime);
int bud_runtime_mount(bud_runtime *runtime);
int bud_runtime_update(bud_runtime *runtime);
int bud_runtime_unmount(bud_runtime *runtime);
int bud_runtime_dispatch(
        bud_runtime *runtime,
        bud_node *target,
        const char *event,
        void *event_user);
void bud_event_stop_propagation(bud_event *event);
void bud_event_prevent_default(bud_event *event);

void bud_free(bud_node *node);
void bud_free_string(char *value);

/* JSON field extraction helpers (no json-c dependency) */
void bud_json_str(
        const char *json, const char *key, char *out, size_t out_size);
int bud_json_int(const char *json, const char *key, int default_val);
void bud_json_data(const char *json, char *out, size_t out_size);

/* Table-driven state: one field definition drives wasm_init, wasm_set_* */
typedef struct bud_field_desc {
	const char *key;
	size_t offset;
	size_t size;
	int is_int;
	int kind; /* 0=include, 1=exclude */
	/* Server-side extensions (unused by WASM, used by source generators) */
	int qm_type;
	int source_type;
	int writable;
	int required;
	size_t min_length;
	const char *ref_source;
	const char *ref_inverse;
	int in_meta;
	const char *file;
} bud_field_desc_t;

/* Server-side qmap type constants — must match qmap.h QM_* values */
#define BUD_QM_STR 2
#define BUD_QM_VSTR 8
#define BUD_QM_MULTI_REF 7

void bud_state_apply(
        void *state, const bud_field_desc_t *fields, const char *json);

/* Platform host function pointers (set by WASM adapter, NULL on native) */
extern void (*bud_host_fetch_fn)(const char *url, size_t len, int id);
extern void (*bud_host_log_fn)(const char *msg, size_t len);
extern void (*bud_host_set_location_fn)(const char *url, size_t len);

#ifdef __cplusplus
}
#endif

#endif
