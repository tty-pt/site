#ifndef BUD_BUD_JSX_H
#define BUD_BUD_JSX_H

#include "bud.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum bud_arg_type {
	BUD_ARG_NONE = 0,
	BUD_ARG_NODE = 1,
	BUD_ARG_ATTR = 2,
	BUD_ARG_EVENT = 3,
	BUD_ARG_BIND = 4
} bud_arg_type;

typedef struct bud_arg {
	bud_arg_type type;
	union {
		bud_node *node;
		struct {
			const char *name;
			const char *value;
		} attr;
		struct {
			const char *event;
			int bubbles;
		} ev;
		struct {
			const char *event;
			int bubbles;
			bud_event_handler_fn handler;
		} bind;
	} data;
} bud_arg;

#define lx_none() ((bud_arg){ .type = BUD_ARG_NONE })
#define lx_attr(k, v)                                                          \
	((bud_arg){ .type = BUD_ARG_ATTR, .data.attr = { (k), (v) } })
#define lx_text(str)                                                           \
	((bud_arg){ .type = BUD_ARG_NODE, .data.node = bud_text(str) })
#define lx_node(n) ((bud_arg){ .type = BUD_ARG_NODE, .data.node = (n) })
#define lx_on(e, b)                                                            \
	((bud_arg){ .type = BUD_ARG_EVENT, .data.ev = { (e), (b) } })
#define lx_bind(e, b, h)                                                       \
	((bud_arg){ .type = BUD_ARG_BIND, .data.bind = { (e), (b), (h) } })

bud_node *bud_el_impl(const char *tag, size_t count, const bud_arg *args);
bud_node *bud_frag_impl(size_t count, const bud_arg *args);

/* Note: In C99, __VA_ARGS__ cannot be empty. For empty nodes, use lx_el("tag",
 * lx_none()) */
#define lx_el(tag, ...)                                                        \
	((bud_arg){                                                            \
	        .type = BUD_ARG_NODE,                                          \
	        .data.node = bud_el_impl(                                      \
	                (tag),                                                 \
	                sizeof((bud_arg[]){ __VA_ARGS__ }) / sizeof(bud_arg),  \
	                (bud_arg[]){ __VA_ARGS__ }) })

#define lx_frag(...)                                                           \
	((bud_arg){                                                            \
	        .type = BUD_ARG_NODE,                                          \
	        .data.node = bud_frag_impl(                                    \
	                sizeof((bud_arg[]){ __VA_ARGS__ }) / sizeof(bud_arg),  \
	                (bud_arg[]){ __VA_ARGS__ }) })

#ifdef __cplusplus
}
#endif

#endif
