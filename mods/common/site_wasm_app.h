#ifndef SITE_WASM_APP_H
#define SITE_WASM_APP_H

#include <bud/bud.h>
#include <ttypt/stele_wasm.h>

#if defined(__wasm__)
int bud_slot_mount(int slot, bud_node *(*render_fn)(void));
int bud_slot_update(int slot, bud_node *(*render_fn)(void));
int bud_slot_dispatch_event(int slot, unsigned int node_id, const char *event_name, int bubbles, void *event_data);
int bud_slot_dispatch_any(unsigned int node_id, const char *event_name, int bubbles, void *event_data);
#else
static inline int bud_slot_mount(int slot, bud_node *(*render_fn)(void)) { (void)slot; (void)render_fn; return 0; }
static inline int bud_slot_update(int slot, bud_node *(*render_fn)(void)) { (void)slot; (void)render_fn; return 0; }
static inline int bud_slot_dispatch_event(int slot, unsigned int node_id, const char *event_name, int bubbles, void *event_data) { (void)slot; (void)node_id; (void)event_name; (void)bubbles; (void)event_data; return 0; }
static inline int bud_slot_dispatch_any(unsigned int node_id, const char *event_name, int bubbles, void *event_data) { (void)node_id; (void)event_name; (void)bubbles; (void)event_data; return 0; }
#endif

#define BUD_WASM_MODULE(mod_name, init_fn, render_fn, action_fn) \
    static int _bud_mod_##mod_name##_mount(int slot) { \
        return bud_slot_mount(slot, (bud_node *(*)(void))render_fn); \
    } \
    static int _bud_mod_##mod_name##_update(int slot) { \
        return bud_slot_update(slot, (bud_node *(*)(void))render_fn); \
    } \
    static int _bud_mod_##mod_name##_dispatch(unsigned int node_id, const char *event_name, int bubbles, void *event_data) { \
        return bud_slot_dispatch_any(node_id, event_name, bubbles, event_data); \
    } \
    const stl_module_ops_t stl_mod_##mod_name = { \
        .name = #mod_name, \
        .init = init_fn, \
        .mount = _bud_mod_##mod_name##_mount, \
        .update = _bud_mod_##mod_name##_update, \
        .action = action_fn, \
        .dispatch_event = _bud_mod_##mod_name##_dispatch, \
        .tick = NULL, \
        .cleanup = NULL, \
        .fetch_callback = NULL, \
        .render = (void *(*)(void))(render_fn) \
    }; \
    const stl_module_ops_t stele_mod_##mod_name = { \
        .name = #mod_name, \
        .init = init_fn, \
        .mount = _bud_mod_##mod_name##_mount, \
        .update = _bud_mod_##mod_name##_update, \
        .action = action_fn, \
        .dispatch_event = _bud_mod_##mod_name##_dispatch, \
        .tick = NULL, \
        .cleanup = NULL, \
        .fetch_callback = NULL, \
        .render = (void *(*)(void))(render_fn) \
    }

#define BUD_WASM_MODULE_EXT(mod_name, init_fn, render_fn, action_fn, tick_fn, cleanup_fn, fetch_fn) \
    static int _bud_mod_##mod_name##_mount(int slot) { \
        return bud_slot_mount(slot, (bud_node *(*)(void))render_fn); \
    } \
    static int _bud_mod_##mod_name##_update(int slot) { \
        return bud_slot_update(slot, (bud_node *(*)(void))render_fn); \
    } \
    static int _bud_mod_##mod_name##_dispatch(unsigned int node_id, const char *event_name, int bubbles, void *event_data) { \
        return bud_slot_dispatch_any(node_id, event_name, bubbles, event_data); \
    } \
    const stl_module_ops_t stl_mod_##mod_name = { \
        .name = #mod_name, \
        .init = init_fn, \
        .mount = _bud_mod_##mod_name##_mount, \
        .update = _bud_mod_##mod_name##_update, \
        .action = action_fn, \
        .dispatch_event = _bud_mod_##mod_name##_dispatch, \
        .tick = tick_fn, \
        .cleanup = cleanup_fn, \
        .fetch_callback = fetch_fn, \
        .render = (void *(*)(void))(render_fn) \
    }; \
    const stl_module_ops_t stele_mod_##mod_name = { \
        .name = #mod_name, \
        .init = init_fn, \
        .mount = _bud_mod_##mod_name##_mount, \
        .update = _bud_mod_##mod_name##_update, \
        .action = action_fn, \
        .dispatch_event = _bud_mod_##mod_name##_dispatch, \
        .tick = tick_fn, \
        .cleanup = cleanup_fn, \
        .fetch_callback = fetch_fn, \
        .render = (void *(*)(void))(render_fn) \
    }

#endif /* SITE_WASM_APP_H */
