#ifndef SITE_WASM_APP_H
#define SITE_WASM_APP_H

#include <bud/bud.h>
#include <bud/bud_app.h>
#include <bud/bud_state.h>
#include <hyle/schema.h>
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

#if defined(__wasm__)
#define _BUD_WASM_EXT_EXPORTS(mod_name, init_fn, render_fn, action_fn, fetch_fn) \
    __attribute__((weak)) bud_node *bud_app_render(void) { \
        bud_node *(*_rnd_fn)(void) = (bud_node *(*)(void))(render_fn); \
        return _rnd_fn ? _rnd_fn() : NULL; \
    } \
    __attribute__((weak)) ISO_EXPORT(int, wasm_init, const char *json, int len) { \
        int (*_in_fn)(const char *, int) = (int (*)(const char *, int))(init_fn); \
        if (_in_fn) _in_fn(json, len); \
        return _bud_mod_##mod_name##_mount(1); \
    } \
    __attribute__((weak)) ISO_EXPORT(int, wasm_init_bin, const void *bin, int len) { \
        int (*_in_fn)(const char *, int) = (int (*)(const char *, int))(init_fn); \
        if (_in_fn) _in_fn((const char *)bin, len); \
        return _bud_mod_##mod_name##_mount(1); \
    } \
    __attribute__((weak)) ISO_EXPORT(void, wasm_action, int action_id, const char *data) { \
        void (*_act_fn)(int, const char *) = (void (*)(int, const char *))(action_fn); \
        if (_act_fn) _act_fn(action_id, data); \
    } \
    __attribute__((weak)) ISO_EXPORT(void, wasm_fetch_callback, int req_id, const char *data, int len) { \
        void (*_f_fn)(int, const char *, int) = (void (*)(int, const char *, int))(fetch_fn); \
        if (_f_fn) _f_fn(req_id, data, len); \
    }
#else
#define _BUD_WASM_EXT_EXPORTS(mod_name, init_fn, render_fn, action_fn, fetch_fn)
#endif

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
    }; \
    _BUD_WASM_EXT_EXPORTS(mod_name, init_fn, render_fn, action_fn, fetch_fn)

typedef struct {
    const hyle_schema_desc_t *schema;
    const bud_field_desc_t *bud_fields;
    void *state;
    bud_node *(*render)(void);
    void (*on_action)(int id, const char *data);
    void (*on_cleanup)(void);
} site_wasm_app_desc_t;

#define SITE_WASM_MODULE(mod_name, ...) \
    static const site_wasm_app_desc_t _site_wasm_app_##mod_name = __VA_ARGS__; \
    static int _site_wasm_##mod_name##_init(const char *json, int len) { \
        size_t jlen = len >= 0 ? (size_t)len : 0; \
        if (_site_wasm_app_##mod_name.bud_fields && _site_wasm_app_##mod_name.state) { \
            wasm_state_init(json, len, _site_wasm_app_##mod_name.bud_fields, _site_wasm_app_##mod_name.state); \
        } \
        if (_site_wasm_app_##mod_name.schema && _site_wasm_app_##mod_name.state) { \
            hyle_bud_state_apply_len(_site_wasm_app_##mod_name.state, _site_wasm_app_##mod_name.schema, json, jlen); \
        } \
        return 0; \
    } \
    static void _site_wasm_##mod_name##_fetch(int req_id, const char *data, int len) { \
        (void)req_id; \
        if (_site_wasm_app_##mod_name.schema && _site_wasm_app_##mod_name.state) { \
            hyle_bud_state_apply_len(_site_wasm_app_##mod_name.state, _site_wasm_app_##mod_name.schema, data, (size_t)len); \
        } \
        if (_site_wasm_app_##mod_name.bud_fields && _site_wasm_app_##mod_name.state) { \
            bud_state_apply_len(_site_wasm_app_##mod_name.state, _site_wasm_app_##mod_name.bud_fields, data, (size_t)len); \
        } \
        bud_app_set_state(); \
    } \
    BUD_WASM_MODULE_EXT(mod_name, _site_wasm_##mod_name##_init, _site_wasm_app_##mod_name.render, \
                        _site_wasm_app_##mod_name.on_action, NULL, _site_wasm_app_##mod_name.on_cleanup, _site_wasm_##mod_name##_fetch)

#endif /* SITE_WASM_APP_H */
