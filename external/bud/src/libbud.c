#include "../include/bud/bud_jsx.h"
#include "../include/bud/bud.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>

/* Platform host function pointers (set by WASM adapter, NULL on native) */
void (*bud_host_fetch_fn)(const char *, size_t, int) = NULL;
void (*bud_host_log_fn)(const char *, size_t) = NULL;
void (*bud_host_set_location_fn)(const char *, size_t) = NULL;

typedef struct bud_attr bud_attr;
typedef struct bud_listener bud_listener;

struct bud_attr {
	char *name;
	char *value;
	int is_boolean;
	bud_attr *next;
};

struct bud_listener {
	char *event;
	int bubbles;
	bud_event_handler_fn handler;
	bud_listener *next;
};

struct bud_node {
	bud_node_kind kind;
	unsigned int id;
	char *tag;
	char *text;
	bud_attr *attrs;
	bud_listener *listeners;
	bud_node *parent;
	bud_node *first_child;
	bud_node *last_child;
	bud_node *next_sibling;
	bud_lifecycle_fn on_mount;
	bud_lifecycle_fn on_update;
	bud_lifecycle_fn on_unmount;
	void *lifecycle_user;
	int mounted;
	BUD_SRC_FIELDS
};

typedef struct bud_runtime {
	bud_node *root;
	bud_runtime_invalidate_fn invalidate;
	void *invalidate_user;
	int dirty;
	int mounted;
} bud_runtime;

typedef struct bud_buf {
	char *data;
	size_t len;
	size_t cap;
} bud_buf;

static char *bud_strdup(const char *value)
{
	size_t len;
	char *copy;

	if (!value) {
		return NULL;
	}

	len = strlen(value);
	copy = (char *)malloc(len + 1);
	if (!copy) {
		return NULL;
	}

	memcpy(copy, value, len + 1);
	return copy;
}

static void bud_buf_init(bud_buf *buf)
{
	buf->data = NULL;
	buf->len = 0;
	buf->cap = 0;
}

static int bud_buf_reserve(bud_buf *buf, size_t add)
{
	size_t need;
	size_t cap;
	char *data;

	need = buf->len + add + 1;
	if (need <= buf->cap) {
		return 0;
	}

	cap = buf->cap ? buf->cap : 128;
	while (cap < need) {
		cap *= 2;
	}

	data = (char *)realloc(buf->data, cap);
	if (!data) {
		return -1;
	}

	buf->data = data;
	buf->cap = cap;
	return 0;
}

static int bud_buf_append_n(bud_buf *buf, const char *text, size_t len)
{
	if (bud_buf_reserve(buf, len) != 0) {
		return -1;
	}

	memcpy(buf->data + buf->len, text, len);
	buf->len += len;
	buf->data[buf->len] = '\0';
	return 0;
}

static int bud_buf_append(bud_buf *buf, const char *text)
{
	if (!text) {
		return 0;
	}

	return bud_buf_append_n(buf, text, strlen(text));
}

static int bud_buf_append_char(bud_buf *buf, char ch)
{
	if (bud_buf_reserve(buf, 1) != 0) {
		return -1;
	}

	buf->data[buf->len++] = ch;
	buf->data[buf->len] = '\0';
	return 0;
}

static int bud_buf_append_escaped(bud_buf *buf, const char *text, int attr)
{
	const char *p;
	const char *start;

	if (!text) {
		return 0;
	}

	start = text;
	for (p = text; *p; p++) {
		const char *rep = NULL;
		size_t replen = 0;

		switch (*p) {
		case '&':
			rep = "&amp;";
			replen = 5;
			break;
		case '<':
			rep = "&lt;";
			replen = 4;
			break;
		case '>':
			rep = "&gt;";
			replen = 4;
			break;
		case '"':
			if (attr) {
				rep = "&quot;";
				replen = 6;
			}
			break;
		case '\'':
			if (attr) {
				rep = "&#39;";
				replen = 5;
			}
			break;
		default:
			break;
		}

		if (rep) {
			if (p > start) {
				if (bud_buf_append_n(buf, start, (size_t)(p - start)) != 0) {
					return -1;
				}
			}
			if (bud_buf_append_n(buf, rep, replen) != 0) {
				return -1;
			}
			start = p + 1;
		}
	}

	if (p > start) {
		if (bud_buf_append_n(buf, start, (size_t)(p - start)) != 0) {
			return -1;
		}
	}

	return 0;
}

static bud_node *bud_node_new(bud_node_kind kind)
{
	bud_node *node;

	node = (bud_node *)calloc(1, sizeof(*node));
	if (!node) {
		return NULL;
	}

	node->kind = kind;
	return node;
}

static int bud_is_void_tag(const char *tag)
{
	static const char *const void_tags[] = { "area",  "base",  "br",
		                                 "col",   "embed", "hr",
		                                 "img",   "input", "link",
		                                 "meta",  "param", "source",
		                                 "track", "wbr",   NULL };
	int i;

	if (!tag) {
		return 0;
	}

	for (i = 0; void_tags[i]; i++) {
		if (strcmp(tag, void_tags[i]) == 0) {
			return 1;
		}
	}

	return 0;
}

static void bud_attr_free(bud_attr *attr)
{
	bud_attr *next;

	while (attr) {
		next = attr->next;
		free(attr->name);
		free(attr->value);
		free(attr);
		attr = next;
	}
}

static void bud_listener_free(bud_listener *listener)
{
	bud_listener *next;

	while (listener) {
		next = listener->next;
		free(listener->event);
		free(listener);
		listener = next;
	}
}

static void bud_node_call_mount(bud_node *node, bud_runtime *runtime)
{
	bud_node *child;

	if (!node) {
		return;
	}

	if (node->mounted) {
		return;
	}

	node->mounted = 1;
	if (node->on_mount) {
		node->on_mount(node->lifecycle_user, runtime, node);
	}

	for (child = node->first_child; child; child = child->next_sibling) {
		bud_node_call_mount(child, runtime);
	}
}

static void bud_node_call_update(bud_node *node, bud_runtime *runtime)
{
	bud_node *child;

	if (!node || !node->mounted) {
		return;
	}

	if (node->on_update) {
		node->on_update(node->lifecycle_user, runtime, node);
	}

	for (child = node->first_child; child; child = child->next_sibling) {
		bud_node_call_update(child, runtime);
	}
}

static void bud_node_call_unmount(bud_node *node, bud_runtime *runtime)
{
	bud_node *child;
	bud_node *next;

	if (!node || !node->mounted) {
		return;
	}

	child = node->first_child;
	while (child) {
		next = child->next_sibling;
		bud_node_call_unmount(child, runtime);
		child = next;
	}

	if (node->on_unmount) {
		node->on_unmount(node->lifecycle_user, runtime, node);
	}
	node->mounted = 0;
}

static void bud_node_free_chain(bud_node *node)
{
	bud_node *child;
	bud_node *next;

	if (!node) {
		return;
	}

	bud_node_call_unmount(node, NULL);
	child = node->first_child;
	while (child) {
		next = child->next_sibling;
		bud_free(child);
		child = next;
	}

	bud_attr_free(node->attrs);
	bud_listener_free(node->listeners);
	free(node->tag);
	free(node->text);
	free(node);
}

static unsigned int bud_assign_ids(bud_node *node, unsigned int next_id)
{
	bud_node *child;

	if (!node) {
		return next_id;
	}

	node->id = next_id++;
	for (child = node->first_child; child; child = child->next_sibling) {
		next_id = bud_assign_ids(child, next_id);
	}

	return next_id;
}

static void bud_prepare_render(bud_node *root)
{
	bud_assign_ids(root, 0);
}

static int bud_append_attr(bud_buf *buf, const char *name, const char *value)
{
	if (bud_buf_append_char(buf, ' ') != 0) {
		return -1;
	}
	if (bud_buf_append(buf, name) != 0) {
		return -1;
	}
	if (bud_buf_append(buf, "=\"") != 0) {
		return -1;
	}
	if (bud_buf_append_escaped(buf, value, 1) != 0) {
		return -1;
	}
	if (bud_buf_append_char(buf, '"') != 0) {
		return -1;
	}

	return 0;
}

static bud_attr *bud_attr_find(bud_node *node, const char *name)
{
	bud_attr *attr;

	for (attr = node->attrs; attr; attr = attr->next) {
		if (strcmp(attr->name, name) == 0) {
			return attr;
		}
	}

	return NULL;
}

static bud_listener *bud_listener_find(bud_node *node, const char *event)
{
	bud_listener *listener;

	for (listener = node->listeners; listener; listener = listener->next) {
		if (strcmp(listener->event, event) == 0) {
			return listener;
		}
	}

	return NULL;
}

static int bud_listeners_to_attr(const bud_node *node, bud_buf *buf)
{
	const bud_listener *listener;
	char mode_buf[8];
	int len;
	int first;

	first = 1;
	for (listener = node->listeners; listener; listener = listener->next) {
		if (!first) {
			if (bud_buf_append_char(buf, ',') != 0) {
				return -1;
			}
		}
		if (bud_buf_append(buf, listener->event) != 0) {
			return -1;
		}
		if (bud_buf_append_char(buf, ':') != 0) {
			return -1;
		}
		len = snprintf(
		        mode_buf, sizeof(mode_buf), "%d",
		        listener->bubbles ? 1 : 0);
		if (len < 0) {
			return -1;
		}
		if (bud_buf_append(buf, mode_buf) != 0) {
			return -1;
		}
		first = 0;
	}

	return 0;
}

static int bud_render_html_node(const bud_node *node, bud_buf *buf)
{
	const bud_attr *attr;
	const bud_node *child;

	if (!node) {
		return 0;
	}

	switch (node->kind) {
	case BUD_NODE_FRAGMENT:
		for (child = node->first_child; child;
		     child = child->next_sibling)
		{
			if (bud_render_html_node(child, buf) != 0) {
				return -1;
			}
		}
		return 0;
	case BUD_NODE_TEXT:
		return bud_buf_append_escaped(buf, node->text, 0);
	case BUD_NODE_RAW_HTML:
		return bud_buf_append(buf, node->text);
	case BUD_NODE_ELEMENT:
		break;
	}

	if (bud_buf_append_char(buf, '<') != 0) {
		return -1;
	}
	if (bud_buf_append(buf, node->tag) != 0) {
		return -1;
	}
#ifdef BUD_DEBUG
	if (node->src_file && node->src_file[0]) {
		char src_buf[384];
		int src_len = snprintf(
		        src_buf, sizeof(src_buf), "%s:%d", node->src_file,
		        node->src_line);
		if (src_len > 0 && src_len < (int)sizeof(src_buf)) {
			if (bud_append_attr(buf, "data-bud-src", src_buf) != 0)
			{
				return -1;
			}
		}
	}
#endif
	for (attr = node->attrs; attr; attr = attr->next) {
		if (attr->is_boolean) {
			if (bud_buf_append_char(buf, ' ') != 0) {
				return -1;
			}
			if (bud_buf_append(buf, attr->name) != 0) {
				return -1;
			}
		} else {
			if (bud_append_attr(buf, attr->name, attr->value) != 0)
			{
				return -1;
			}
		}
	}
	if (bud_is_void_tag(node->tag)) {
		if (bud_buf_append(buf, " />") != 0) {
			return -1;
		}
		return 0;
	}
	if (bud_buf_append_char(buf, '>') != 0) {
		return -1;
	}
	for (child = node->first_child; child; child = child->next_sibling) {
		if (bud_render_html_node(child, buf) != 0) {
			return -1;
		}
	}
	if (bud_buf_append(buf, "</") != 0) {
		return -1;
	}
	if (bud_buf_append(buf, node->tag) != 0) {
		return -1;
	}
	if (bud_buf_append_char(buf, '>') != 0) {
		return -1;
	}
	return 0;
}

static int bud_render_hydrated_html_node(const bud_node *node, bud_buf *buf)
{
	const bud_attr *attr;
	const bud_node *child;
	char id_buf[32];
	int len;

	if (!node) {
		return 0;
	}

	len = snprintf(id_buf, sizeof(id_buf), "%u", node->id);
	if (len < 0) {
		return -1;
	}

	switch (node->kind) {
	case BUD_NODE_FRAGMENT:
		if (bud_buf_append(buf, "<!--bud-fragment:") != 0) {
			return -1;
		}
		if (bud_buf_append(buf, id_buf) != 0) {
			return -1;
		}
		if (bud_buf_append(buf, "-->") != 0) {
			return -1;
		}
		for (child = node->first_child; child;
		     child = child->next_sibling)
		{
			if (bud_render_hydrated_html_node(child, buf) != 0) {
				return -1;
			}
		}
		if (bud_buf_append(buf, "<!--/bud-fragment:") != 0) {
			return -1;
		}
		if (bud_buf_append(buf, id_buf) != 0) {
			return -1;
		}
		if (bud_buf_append(buf, "-->") != 0) {
			return -1;
		}
		return 0;
	case BUD_NODE_TEXT:
		if (bud_buf_append(buf, "<!--bud-text:") != 0) {
			return -1;
		}
		if (bud_buf_append(buf, id_buf) != 0) {
			return -1;
		}
		if (bud_buf_append(buf, "-->") != 0) {
			return -1;
		}
		if (bud_buf_append_escaped(buf, node->text, 0) != 0) {
			return -1;
		}
		if (bud_buf_append(buf, "<!--/bud-text:") != 0) {
			return -1;
		}
		if (bud_buf_append(buf, id_buf) != 0) {
			return -1;
		}
		if (bud_buf_append(buf, "-->") != 0) {
			return -1;
		}
		return 0;
	case BUD_NODE_RAW_HTML:
		return bud_buf_append(buf, node->text);
	case BUD_NODE_ELEMENT:
		break;
	}

	if (bud_buf_append_char(buf, '<') != 0) {
		return -1;
	}
	if (bud_buf_append(buf, node->tag) != 0) {
		return -1;
	}
	if (bud_append_attr(buf, "data-bud-id", id_buf) != 0) {
		return -1;
	}
#ifdef BUD_DEBUG
	if (node->src_file && node->src_file[0]) {
		char src_buf[384];
		int src_len = snprintf(
		        src_buf, sizeof(src_buf), "%s:%d", node->src_file,
		        node->src_line);
		if (src_len > 0 && src_len < (int)sizeof(src_buf)) {
			if (bud_append_attr(buf, "data-bud-src", src_buf) != 0)
			{
				return -1;
			}
		}
	}
#endif
	if (node->listeners) {
		if (bud_buf_append(buf, " data-bud-on=\"") != 0) {
			return -1;
		}
		if (bud_listeners_to_attr(node, buf) != 0) {
			return -1;
		}
		if (bud_buf_append_char(buf, '"') != 0) {
			return -1;
		}
	}
	for (attr = node->attrs; attr; attr = attr->next) {
		if (attr->is_boolean) {
			if (bud_buf_append_char(buf, ' ') != 0) {
				return -1;
			}
			if (bud_buf_append(buf, attr->name) != 0) {
				return -1;
			}
		} else {
			if (bud_append_attr(buf, attr->name, attr->value) != 0)
			{
				return -1;
			}
		}
	}
	if (bud_is_void_tag(node->tag)) {
		if (bud_buf_append(buf, " />") != 0) {
			return -1;
		}
		return 0;
	}
	if (bud_buf_append_char(buf, '>') != 0) {
		return -1;
	}
	for (child = node->first_child; child; child = child->next_sibling) {
		if (bud_render_hydrated_html_node(child, buf) != 0) {
			return -1;
		}
	}
	if (bud_buf_append(buf, "</") != 0) {
		return -1;
	}
	if (bud_buf_append(buf, node->tag) != 0) {
		return -1;
	}
	if (bud_buf_append_char(buf, '>') != 0) {
		return -1;
	}
	return 0;
}

static int
bud_render_ops_node(const bud_node *node, bud_emit_fn emit, void *user)
{
	const bud_attr *attr;
	const bud_listener *listener;
	const bud_node *child;
	char id_buf[32];
	char mode_buf[8];
	int len;

	if (!node) {
		return 0;
	}

	len = snprintf(id_buf, sizeof(id_buf), "%u", node->id);
	if (len < 0) {
		return -1;
	}

	switch (node->kind) {
	case BUD_NODE_FRAGMENT:
		if (emit(user, "fragment-open", id_buf, NULL, NULL) != 0) {
			return -1;
		}
		for (child = node->first_child; child;
		     child = child->next_sibling)
		{
			if (bud_render_ops_node(child, emit, user) != 0) {
				return -1;
			}
		}
		return emit(user, "fragment-close", id_buf, NULL, NULL) != 0
		               ? -1
		               : 0;
	case BUD_NODE_ELEMENT:
		if (emit(user, "element-open", node->tag, id_buf, NULL) != 0) {
			return -1;
		}
		for (attr = node->attrs; attr; attr = attr->next) {
			if (emit(user, "attr", attr->name,
			         attr->value ? attr->value : "", id_buf) != 0)
			{
				return -1;
			}
		}
		for (listener = node->listeners; listener;
		     listener = listener->next)
		{
			snprintf(
			        mode_buf, sizeof(mode_buf), "%d",
			        listener->bubbles ? 1 : 0);
			if (emit(user, "listener", listener->event, mode_buf,
			         id_buf) != 0)
			{
				return -1;
			}
		}
		for (child = node->first_child; child;
		     child = child->next_sibling)
		{
			if (bud_render_ops_node(child, emit, user) != 0) {
				return -1;
			}
		}
		return emit(user, "element-close", node->tag, id_buf, NULL) != 0
		               ? -1
		               : 0;
	case BUD_NODE_TEXT:
		return emit(user, "text", node->text, id_buf, NULL) != 0 ? -1
		                                                         : 0;
	case BUD_NODE_RAW_HTML:
		return emit(user, "raw", node->text, id_buf, NULL) != 0 ? -1
		                                                        : 0;
	default:
		return -1;
	}
}

static int
bud_hydrate_node(const bud_node *node, bud_hydrate_lookup_fn lookup, void *user)
{
	const bud_node *child;
	const char *kind;
	const char *tag;
	const char *text;
	unsigned int id;

	if (!node) {
		return 0;
	}

	id = node->id;
	kind = NULL;
	tag = NULL;
	text = NULL;
	if (lookup(user, id, &kind, &tag, &text) != 0) {
		return -1;
	}

	switch (node->kind) {
	case BUD_NODE_FRAGMENT:
		if (!kind || strcmp(kind, "fragment") != 0) {
			return -1;
		}
		for (child = node->first_child; child;
		     child = child->next_sibling)
		{
			if (bud_hydrate_node(child, lookup, user) != 0) {
				return -1;
			}
		}
		return 0;
	case BUD_NODE_ELEMENT:
		if (!kind || strcmp(kind, "element") != 0) {
			return -1;
		}
		if (!tag || strcmp(tag, node->tag) != 0) {
			return -1;
		}
		for (child = node->first_child; child;
		     child = child->next_sibling)
		{
			if (bud_hydrate_node(child, lookup, user) != 0) {
				return -1;
			}
		}
		return 0;
	case BUD_NODE_TEXT:
		if (!kind || strcmp(kind, "text") != 0) {
			return -1;
		}
		if (!text || strcmp(text, node->text) != 0) {
			return -1;
		}
		return 0;
	case BUD_NODE_RAW_HTML:
		if (!kind || strcmp(kind, "raw") != 0) {
			return -1;
		}
		if (!text || strcmp(text, node->text) != 0) {
			return -1;
		}
		return 0;
	default:
		return -1;
	}
}

bud_node *bud_fragment(void)
{
	return bud_node_new(BUD_NODE_FRAGMENT);
}

bud_node *bud_element(const char *tag)
{
	bud_node *node;

	if (!tag) {
		return NULL;
	}

	node = bud_node_new(BUD_NODE_ELEMENT);
	if (!node) {
		return NULL;
	}

	node->tag = bud_strdup(tag);
	if (!node->tag) {
		free(node);
		return NULL;
	}

	return node;
}

bud_node *bud_text(const char *text)
{
	bud_node *node;

	node = bud_node_new(BUD_NODE_TEXT);
	if (!node) {
		return NULL;
	}

	node->text = bud_strdup(text ? text : "");
	if (!node->text) {
		free(node);
		return NULL;
	}

	return node;
}

bud_node *bud_raw(const char *html)
{
	bud_node *node;

	node = bud_node_new(BUD_NODE_RAW_HTML);
	if (!node) {
		return NULL;
	}

	node->text = bud_strdup(html ? html : "");
	if (!node->text) {
		free(node);
		return NULL;
	}

	return node;
}

void bud_raw_set_text(bud_node *node, const char *text)
{
	if (!node || node->kind != BUD_NODE_RAW_HTML)
		return;
	free(node->text);
	node->text = bud_strdup(text ? text : "");
}

static int is_html_void_tag(const char *tag)
{
	static const char *void_tags[] = { "area",  "base", "br",    "col",
		                           "embed", "hr",   "img",   "input",
		                           "link",  "meta", "param", "source",
		                           "track", "wbr",  NULL };
	if (!tag)
		return 0;
	for (int i = 0; void_tags[i]; i++) {
		if (strcmp(tag, void_tags[i]) == 0)
			return 1;
	}
	return 0;
}

static void bud_tpl_flush_text(bud_node *parent, char *buf, size_t *len)
{
	if (*len == 0 || !parent)
		return;
	buf[*len] = '\0';
	int all_ws = 1;
	for (size_t i = 0; i < *len; i++) {
		if (buf[i] != ' ' && buf[i] != '\t' && buf[i] != '\n' &&
		    buf[i] != '\r')
		{
			all_ws = 0;
			break;
		}
	}
	if (!all_ws) {
		bud_node *txt = bud_text(buf);
		if (txt)
			bud_append(parent, txt);
	}
	*len = 0;
}

bud_node *bud_vtpl(const char *fmt, va_list ap)
{
	if (!fmt)
		return NULL;

	bud_node *stack[64];
	int depth = 0;

	bud_node *root = bud_fragment();
	if (!root)
		return NULL;
	stack[depth++] = root;

	char text_buf[4096];
	size_t text_len = 0;

	const char *p = fmt;

	while (*p) {
		if (*p == '<') {
			/* If comment: <!-- ... --> */
			if (p[1] == '!' && p[2] == '-' && p[3] == '-') {
				bud_tpl_flush_text(
				        stack[depth - 1], text_buf, &text_len);
				p += 4;
				while (*p && !(*p == '-' && p[1] == '-' &&
				               p[2] == '>'))
				{
					p++;
				}
				if (*p)
					p += 3;
				continue;
			}

			/* If closing tag: </tag> */
			if (p[1] == '/') {
				bud_tpl_flush_text(
				        stack[depth - 1], text_buf, &text_len);
				p += 2;
				const char *tag_start = p;
				while (*p && *p != '>' && *p != ' ' &&
				       *p != '\t' && *p != '\n' && *p != '\r')
				{
					p++;
				}
				char tag_name[64];
				size_t tlen = (size_t)(p - tag_start);
				if (tlen >= sizeof(tag_name))
					tlen = sizeof(tag_name) - 1;
				memcpy(tag_name, tag_start, tlen);
				tag_name[tlen] = '\0';

				while (*p && *p != '>')
					p++;
				if (*p == '>')
					p++;

				if (depth > 1) {
					for (int d = depth - 1; d >= 1; d--) {
						const char *st_tag =
						        bud_node_tag(stack[d]);
						if (st_tag &&
						    strcmp(st_tag, tag_name) ==
						            0)
						{
							depth = d;
							break;
						}
					}
				}
				continue;
			}

			/* Opening tag: <tag ... > */
			if ((p[1] >= 'a' && p[1] <= 'z') ||
			    (p[1] >= 'A' && p[1] <= 'Z'))
			{
				bud_tpl_flush_text(
				        stack[depth - 1], text_buf, &text_len);
				p++; /* skip '<' */
				const char *tag_start = p;
				while (*p && *p != '>' && *p != '/' &&
				       *p != ' ' && *p != '\t' && *p != '\n' &&
				       *p != '\r')
				{
					p++;
				}
				char tag_name[64];
				size_t tlen = (size_t)(p - tag_start);
				if (tlen >= sizeof(tag_name))
					tlen = sizeof(tag_name) - 1;
				memcpy(tag_name, tag_start, tlen);
				tag_name[tlen] = '\0';

				bud_node *elem = bud_element(tag_name);
				if (!elem)
					break;

				bud_append(stack[depth - 1], elem);

				int self_closing = is_html_void_tag(tag_name);

				/* Parse attributes */
				while (*p && *p != '>' && *p != '/') {
					while (*p == ' ' || *p == '\t' ||
					       *p == '\n' || *p == '\r')
						p++;
					if (!*p || *p == '>' || *p == '/')
						break;

					/* Check for %bind */
					if (strncmp(p, "%bind", 5) == 0) {
						p += 5;
						const char *ev_name = va_arg(
						        ap, const char *);
						bud_event_handler_fn handler = va_arg(
						        ap,
						        bud_event_handler_fn);
						if (ev_name && handler) {
							bud_bind(
							        elem, ev_name,
							        0, handler);
						}
						continue;
					}

					/* Check for %b (boolean attribute e.g.
					 * "selected", "checked", "disabled", or
					 * NULL) */
					if (strncmp(p, "%b", 2) == 0 &&
					    (p[2] == ' ' || p[2] == '\t' ||
					     p[2] == '\n' || p[2] == '\r' ||
					     p[2] == '>' || p[2] == '/'))
					{
						p += 2;
						const char *b_name = va_arg(
						        ap, const char *);
						if (b_name && b_name[0]) {
							bud_set_bool_attr(
							        elem, b_name);
						}
						continue;
					}

					/* Parse attribute name */
					const char *attr_start = p;
					while (*p && *p != '=' && *p != '>' &&
					       *p != '/' && *p != ' ' &&
					       *p != '\t' && *p != '\n' &&
					       *p != '\r')
					{
						p++;
					}
					char attr_name[64];
					size_t alen = (size_t)(p - attr_start);
					if (alen >= sizeof(attr_name))
						alen = sizeof(attr_name) - 1;
					memcpy(attr_name, attr_start, alen);
					attr_name[alen] = '\0';

					while (*p == ' ' || *p == '\t' ||
					       *p == '\n' || *p == '\r')
						p++;

					if (*p == '=') {
						p++;
						while (*p == ' ' ||
						       *p == '\t' ||
						       *p == '\n' || *p == '\r')
							p++;
						char attr_val[2048];
						size_t vlen = 0;

						if (*p == '"' || *p == '\'') {
							char quote = *p++;
							while (*p &&
							       *p != quote)
							{
								if (*p == '%') {
									if (p[1] ==
									    's')
									{
										const char *s = va_arg(
										        ap,
										        const char
										                *);
										if (s)
										{
											size_t sl = strlen(
											        s);
											if (vlen + sl <
											    sizeof(attr_val))
											{
												memcpy(attr_val +
												               vlen,
												       s,
												       sl);
												vlen += sl;
											}
										}
										p += 2;
										continue;
									} else if (
									        p[1] == 'd' ||
									        p[1] == 'i')
									{
										int d = va_arg(
										        ap,
										        int);
										char num[32];
										int nl = snprintf(
										        num,
										        sizeof(num),
										        "%d",
										        d);
										if (nl > 0 &&
										    vlen + (size_t)nl <
										            sizeof(attr_val))
										{
											memcpy(attr_val +
											               vlen,
											       num,
											       (size_t)nl);
											vlen += (size_t)
											        nl;
										}
										p += 2;
										continue;
									} else if (
									        strncmp(p,
									                "%zu",
									                3) ==
									        0)
									{
										size_t z = va_arg(
										        ap,
										        size_t);
										char num[32];
										int nl = snprintf(
										        num,
										        sizeof(num),
										        "%zu",
										        z);
										if (nl > 0 &&
										    vlen + (size_t)nl <
										            sizeof(attr_val))
										{
											memcpy(attr_val +
											               vlen,
											       num,
											       (size_t)nl);
											vlen += (size_t)
											        nl;
										}
										p += 3;
										continue;
									} else if (
									        strncmp(p,
									                "%lu",
									                3) ==
									        0)
									{
										unsigned long lu = va_arg(
										        ap,
										        unsigned long);
										char num[32];
										int nl = snprintf(
										        num,
										        sizeof(num),
										        "%lu",
										        lu);
										if (nl > 0 &&
										    vlen + (size_t)nl <
										            sizeof(attr_val))
										{
											memcpy(attr_val +
											               vlen,
											       num,
											       (size_t)nl);
											vlen += (size_t)
											        nl;
										}
										p += 3;
										continue;
									} else if (
									        strncmp(p,
									                "%ld",
									                3) ==
									        0)
									{
										long ld = va_arg(
										        ap,
										        long);
										char num[32];
										int nl = snprintf(
										        num,
										        sizeof(num),
										        "%ld",
										        ld);
										if (nl > 0 &&
										    vlen + (size_t)nl <
										            sizeof(attr_val))
										{
											memcpy(attr_val +
											               vlen,
											       num,
											       (size_t)nl);
											vlen += (size_t)
											        nl;
										}
										p += 3;
										continue;
									} else if (
									        p[1] ==
									        'u')
									{
										unsigned int u = va_arg(
										        ap,
										        unsigned int);
										char num[32];
										int nl = snprintf(
										        num,
										        sizeof(num),
										        "%u",
										        u);
										if (nl > 0 &&
										    vlen + (size_t)nl <
										            sizeof(attr_val))
										{
											memcpy(attr_val +
											               vlen,
											       num,
											       (size_t)nl);
											vlen += (size_t)
											        nl;
										}
										p += 2;
										continue;
									} else if (
									        p[1] ==
									        '%')
									{
										if (vlen + 1 <
										    sizeof(attr_val))
											attr_val[vlen++] =
											        '%';
										p += 2;
										continue;
									}
								}
								if (vlen + 1 <
								    sizeof(attr_val))
								{
									attr_val[vlen++] =
									        *p;
								}
								p++;
							}
							if (*p == quote)
								p++;
						} else {
							while (*p &&
							       *p != ' ' &&
							       *p != '\t' &&
							       *p != '\n' &&
							       *p != '\r' &&
							       *p != '>' &&
							       *p != '/')
							{
								if (*p == '%' &&
								    p[1] == 's')
								{
									const char *s = va_arg(
									        ap,
									        const char
									                *);
									if (s) {
										size_t sl = strlen(
										        s);
										if (vlen + sl <
										    sizeof(attr_val))
										{
											memcpy(attr_val +
											               vlen,
											       s,
											       sl);
											vlen += sl;
										}
									}
									p += 2;
									continue;
								}
								if (vlen + 1 <
								    sizeof(attr_val))
								{
									attr_val[vlen++] =
									        *p;
								}
								p++;
							}
						}
						attr_val[vlen] = '\0';
						bud_set_attr(
						        elem, attr_name,
						        attr_val);
					} else {
						bud_set_bool_attr(
						        elem, attr_name);
					}
				}

				while (*p == ' ' || *p == '\t' || *p == '\n' ||
				       *p == '\r')
					p++;
				if (*p == '/' && p[1] == '>') {
					self_closing = 1;
					p += 2;
				} else if (*p == '>') {
					p++;
				}

				if (!self_closing && depth < 63) {
					stack[depth++] = elem;
				}
				continue;
			}
		}

		/* Check for body specifiers: %s, %d, %i, %u, %node, %raw, %% */
		if (*p == '%') {
			if (strncmp(p, "%node", 5) == 0) {
				bud_tpl_flush_text(
				        stack[depth - 1], text_buf, &text_len);
				p += 5;
				bud_node *child = va_arg(ap, bud_node *);
				if (child) {
					bud_append(stack[depth - 1], child);
				}
				continue;
			} else if (strncmp(p, "%raw", 4) == 0) {
				bud_tpl_flush_text(
				        stack[depth - 1], text_buf, &text_len);
				p += 4;
				const char *raw_html = va_arg(ap, const char *);
				if (raw_html) {
					bud_node *rnode = bud_raw(raw_html);
					if (rnode)
						bud_append(
						        stack[depth - 1],
						        rnode);
				}
				continue;
			} else if (p[1] == 's') {
				bud_tpl_flush_text(
				        stack[depth - 1], text_buf, &text_len);
				p += 2;
				const char *str = va_arg(ap, const char *);
				if (str && str[0]) {
					bud_node *tn = bud_text(str);
					if (tn)
						bud_append(
						        stack[depth - 1], tn);
				}
				continue;
			} else if (p[1] == 'd' || p[1] == 'i') {
				bud_tpl_flush_text(
				        stack[depth - 1], text_buf, &text_len);
				p += 2;
				int val = va_arg(ap, int);
				char num[32];
				snprintf(num, sizeof(num), "%d", val);
				bud_node *tn = bud_text(num);
				if (tn)
					bud_append(stack[depth - 1], tn);
				continue;
			} else if (strncmp(p, "%zu", 3) == 0) {
				bud_tpl_flush_text(
				        stack[depth - 1], text_buf, &text_len);
				p += 3;
				size_t val = va_arg(ap, size_t);
				char num[32];
				snprintf(num, sizeof(num), "%zu", val);
				bud_node *tn = bud_text(num);
				if (tn)
					bud_append(stack[depth - 1], tn);
				continue;
			} else if (strncmp(p, "%lu", 3) == 0) {
				bud_tpl_flush_text(
				        stack[depth - 1], text_buf, &text_len);
				p += 3;
				unsigned long val = va_arg(ap, unsigned long);
				char num[32];
				snprintf(num, sizeof(num), "%lu", val);
				bud_node *tn = bud_text(num);
				if (tn)
					bud_append(stack[depth - 1], tn);
				continue;
			} else if (strncmp(p, "%ld", 3) == 0) {
				bud_tpl_flush_text(
				        stack[depth - 1], text_buf, &text_len);
				p += 3;
				long val = va_arg(ap, long);
				char num[32];
				snprintf(num, sizeof(num), "%ld", val);
				bud_node *tn = bud_text(num);
				if (tn)
					bud_append(stack[depth - 1], tn);
				continue;
			} else if (p[1] == 'u') {
				bud_tpl_flush_text(
				        stack[depth - 1], text_buf, &text_len);
				p += 2;
				unsigned int val = va_arg(ap, unsigned int);
				char num[32];
				snprintf(num, sizeof(num), "%u", val);
				bud_node *tn = bud_text(num);
				if (tn)
					bud_append(stack[depth - 1], tn);
				continue;
			} else if (p[1] == '%') {
				if (text_len + 1 < sizeof(text_buf)) {
					text_buf[text_len++] = '%';
				}
				p += 2;
				continue;
			}
		}

		if (text_len + 1 < sizeof(text_buf)) {
			text_buf[text_len++] = *p;
		}
		p++;
	}

	bud_tpl_flush_text(stack[depth - 1], text_buf, &text_len);

	if (bud_node_child_count(root) == 1) {
		bud_node *first = (bud_node *)bud_node_child(root, 0);
		if (first && bud_node_kind_of(first) == BUD_NODE_ELEMENT) {
			bud_detach(first);
			bud_free(root);
			return first;
		}
	}

	return root;
}

bud_node *bud_tpl(const char *fmt, ...)
{
	va_list ap;
	bud_node *result;

	va_start(ap, fmt);
	result = bud_vtpl(fmt, ap);
	va_end(ap);

	return result;
}

bud_node *
bud_component_render(const bud_component *component, const void *props)
{
	if (!component || !component->render) {
		return NULL;
	}

	return component->render(component->ctx, props);
}

static int bud_is_valid_name(const char *name)
{
	const char *p;
	if (!name || !name[0])
		return 0;
	if (!((name[0] >= 'a' && name[0] <= 'z') ||
	      (name[0] >= 'A' && name[0] <= 'Z') || name[0] == '_'))
		return 0;
	for (p = name + 1; *p; p++) {
		char c = *p;
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		    (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ':')
			continue;
		return 0;
	}
	return 1;
}

int bud_set_attr(bud_node *node, const char *name, const char *value)
{
	bud_attr *attr;
	bud_attr *tail;
	char *name_copy;
	char *value_copy;

	if (!node || node->kind != BUD_NODE_ELEMENT || !name ||
	    !bud_is_valid_name(name))
	{
		return -1;
	}

	name_copy = bud_strdup(name);
	value_copy = bud_strdup(value ? value : "");
	if (!name_copy || !value_copy) {
		free(name_copy);
		free(value_copy);
		return -1;
	}

	attr = bud_attr_find(node, name);
	if (attr) {
		free(attr->value);
		attr->value = value_copy;
		free(name_copy);
		return 0;
	}

	attr = (bud_attr *)calloc(1, sizeof(*attr));
	if (!attr) {
		free(name_copy);
		free(value_copy);
		return -1;
	}

	attr->name = name_copy;
	attr->value = value_copy;
	if (!node->attrs) {
		node->attrs = attr;
		return 0;
	}

	tail = node->attrs;
	while (tail->next) {
		tail = tail->next;
	}

	tail->next = attr;
	return 0;
}

bud_arg bud_attr_fmt(const char *name, const char *fmt, ...)
{
	char tmp[512];
	va_list ap;
	char *heap;
	va_start(ap, fmt);
	vsnprintf(tmp, sizeof(tmp), fmt, ap);
	va_end(ap);
	heap = bud_strdup(tmp);
	if (!heap)
		heap = bud_strdup("");
	return (bud_arg){ .type = BUD_ARG_ATTR_FMT,
		          .data.attr = { name, heap ? heap : "" } };
}

int bud_set_bool_attr(bud_node *node, const char *name)
{
	bud_attr *attr;
	bud_attr *tail;
	char *name_copy;

	if (!node || node->kind != BUD_NODE_ELEMENT || !name) {
		return -1;
	}

	attr = bud_attr_find(node, name);
	if (attr) {
		attr->is_boolean = 1;
		return 0;
	}

	name_copy = bud_strdup(name);
	if (!name_copy) {
		return -1;
	}

	attr = (bud_attr *)calloc(1, sizeof(*attr));
	if (!attr) {
		free(name_copy);
		return -1;
	}

	attr->name = name_copy;
	attr->is_boolean = 1;
	if (!node->attrs) {
		node->attrs = attr;
		return 0;
	}

	tail = node->attrs;
	while (tail->next) {
		tail = tail->next;
	}

	tail->next = attr;
	return 0;
}

const char *bud_get_attr(const bud_node *node, const char *name)
{
	bud_attr *attr;

	if (!node || node->kind != BUD_NODE_ELEMENT || !name) {
		return NULL;
	}

	attr = bud_attr_find((bud_node *)node, name);
	if (!attr) {
		return NULL;
	}

	return attr->value;
}

static int bud_class_has(const char *classes, const char *cls)
{
	const char *p;
	size_t len;

	if (!classes || !cls || !*cls) {
		return 0;
	}

	len = strlen(cls);
	p = classes;
	while (*p) {
		while (*p == ' ') {
			p++;
		}
		if (!*p) {
			break;
		}
		if (strncmp(p, cls, len) == 0 &&
		    (p[len] == ' ' || p[len] == '\0'))
		{
			return 1;
		}
		while (*p && *p != ' ') {
			p++;
		}
	}

	return 0;
}

int bud_add_class(bud_node *node, const char *cls)
{
	const char *cur;
	char *new_classes;
	size_t cur_len;
	size_t cls_len;

	if (!node || node->kind != BUD_NODE_ELEMENT || !cls || !*cls) {
		return -1;
	}

	cur = bud_get_attr(node, "class");
	if (bud_class_has(cur, cls)) {
		return 0;
	}

	cls_len = strlen(cls);
	if (cur) {
		cur_len = strlen(cur);
		new_classes = (char *)malloc(cur_len + 1 + cls_len + 1);
		if (!new_classes) {
			return -1;
		}
		memcpy(new_classes, cur, cur_len);
		new_classes[cur_len] = ' ';
		memcpy(new_classes + cur_len + 1, cls, cls_len + 1);
		bud_set_attr(node, "class", new_classes);
		free(new_classes);
	} else {
		bud_set_attr(node, "class", cls);
	}

	return 0;
}

int bud_remove_class(bud_node *node, const char *cls)
{
	const char *cur;
	char *p;
	char *new_classes;
	size_t cls_len;

	if (!node || node->kind != BUD_NODE_ELEMENT || !cls || !*cls) {
		return -1;
	}

	cur = bud_get_attr(node, "class");
	if (!cur || !bud_class_has(cur, cls)) {
		return 0;
	}

	cls_len = strlen(cls);
	new_classes = (char *)malloc(strlen(cur) + 1);
	if (!new_classes) {
		return -1;
	}

	p = new_classes;
	while (*cur) {
		while (*cur == ' ') {
			cur++;
		}
		if (!*cur) {
			break;
		}
		if (strncmp(cur, cls, cls_len) == 0 &&
		    (cur[cls_len] == ' ' || cur[cls_len] == '\0'))
		{
			cur += cls_len;
			continue;
		}
		while (*cur && *cur != ' ') {
			*p++ = *cur++;
		}
		if (*cur == ' ') {
			*p++ = ' ';
		}
	}
	if (p > new_classes && *(p - 1) == ' ') {
		*(p - 1) = '\0';
	} else {
		*p = '\0';
	}

	if (*new_classes) {
		bud_set_attr(node, "class", new_classes);
	} else {
		bud_set_attr(node, "class", "");
	}

	free(new_classes);
	return 0;
}

int bud_toggle_class(bud_node *node, const char *cls)
{
	const char *cur;

	if (!node || node->kind != BUD_NODE_ELEMENT || !cls || !*cls) {
		return -1;
	}

	cur = bud_get_attr(node, "class");
	if (bud_class_has(cur, cls)) {
		return bud_remove_class(node, cls);
	}

	return bud_add_class(node, cls);
}

int bud_detach(bud_node *node)
{
	bud_node *child;
	bud_node *prev;

	if (!node || !node->parent) {
		return -1;
	}

	prev = NULL;
	child = node->parent->first_child;
	while (child) {
		if (child == node) {
			if (prev) {
				prev->next_sibling = node->next_sibling;
			} else {
				node->parent->first_child = node->next_sibling;
			}
			if (node->parent->last_child == node) {
				node->parent->last_child = prev;
			}
			node->parent = NULL;
			node->next_sibling = NULL;
			return 0;
		}
		prev = child;
		child = child->next_sibling;
	}

	return -1;
}

int bud_on(bud_node *node, const char *event, int bubbles)
{
	return bud_bind(node, event, bubbles, NULL);
}

int bud_bind(
        bud_node *node, const char *event, int bubbles,
        bud_event_handler_fn handler)
{
	bud_listener *listener;
	bud_listener *tail;
	char *event_copy;

	if (!node || node->kind != BUD_NODE_ELEMENT || !event) {
		return -1;
	}

	event_copy = bud_strdup(event);
	if (!event_copy) {
		return -1;
	}

	listener = bud_listener_find(node, event);
	if (listener) {
		listener->bubbles = bubbles ? 1 : 0;
		listener->handler = handler;
		free(event_copy);
		return 0;
	}

	listener = (bud_listener *)calloc(1, sizeof(*listener));
	if (!listener) {
		free(event_copy);
		return -1;
	}

	listener->event = event_copy;
	listener->bubbles = bubbles ? 1 : 0;
	listener->handler = handler;
	if (!node->listeners) {
		node->listeners = listener;
		return 0;
	}

	tail = node->listeners;
	while (tail->next) {
		tail = tail->next;
	}

	tail->next = listener;
	return 0;
}

int bud_set_lifecycle(
        bud_node *node, bud_lifecycle_fn on_mount, bud_lifecycle_fn on_update,
        bud_lifecycle_fn on_unmount, void *user)
{
	if (!node) {
		return -1;
	}

	node->on_mount = on_mount;
	node->on_update = on_update;
	node->on_unmount = on_unmount;
	node->lifecycle_user = user;
	return 0;
}

int bud_append(bud_node *parent, bud_node *child)
{
	if (!parent || !child) {
		return -1;
	}

	if (parent->kind == BUD_NODE_TEXT || parent->kind == BUD_NODE_RAW_HTML)
	{
		return -1;
	}

	child->parent = parent;
	child->next_sibling = NULL;
	if (!parent->first_child) {
		parent->first_child = child;
		parent->last_child = child;
		return 0;
	}

	parent->last_child->next_sibling = child;
	parent->last_child = child;
	return 0;
}

bud_node_kind bud_node_kind_of(const bud_node *node)
{
	if (!node) {
		return BUD_NODE_FRAGMENT;
	}

	return node->kind;
}

const char *bud_node_tag(const bud_node *node)
{
	if (!node) {
		return NULL;
	}

	return node->tag;
}

const char *bud_node_text(const bud_node *node)
{
	if (!node) {
		return NULL;
	}

	return node->text;
}

unsigned int bud_node_id(const bud_node *node)
{
	if (!node) {
		return 0;
	}

	return node->id;
}

static size_t bud_count_children(const bud_node *node)
{
	const bud_node *child;
	size_t count;

	count = 0;
	if (!node) {
		return 0;
	}

	for (child = node->first_child; child; child = child->next_sibling) {
		count++;
	}

	return count;
}

size_t bud_node_child_count(const bud_node *node)
{
	return bud_count_children(node);
}

const bud_node *bud_node_child(const bud_node *node, size_t index)
{
	const bud_node *child;
	size_t i;

	if (!node) {
		return NULL;
	}

	child = node->first_child;
	for (i = 0; child; i++) {
		if (i == index) {
			return child;
		}
		child = child->next_sibling;
	}

	return NULL;
}

static const bud_attr *bud_attr_at(const bud_node *node, size_t index)
{
	const bud_attr *attr;
	size_t i;

	if (!node) {
		return NULL;
	}

	attr = node->attrs;
	for (i = 0; attr; i++) {
		if (i == index) {
			return attr;
		}
		attr = attr->next;
	}

	return NULL;
}

size_t bud_node_attr_count(const bud_node *node)
{
	size_t count;
	const bud_attr *attr;

	count = 0;
	if (!node) {
		return 0;
	}

	for (attr = node->attrs; attr; attr = attr->next) {
		count++;
	}

	return count;
}

const char *bud_node_attr_name(const bud_node *node, size_t index)
{
	const bud_attr *attr;

	attr = bud_attr_at(node, index);
	if (!attr) {
		return NULL;
	}

	return attr->name;
}

const char *bud_node_attr_value(const bud_node *node, size_t index)
{
	const bud_attr *attr;

	attr = bud_attr_at(node, index);
	if (!attr) {
		return NULL;
	}

	return attr->value;
}

static const bud_listener *bud_listener_at(const bud_node *node, size_t index)
{
	const bud_listener *listener;
	size_t i;

	if (!node) {
		return NULL;
	}

	listener = node->listeners;
	for (i = 0; listener; i++) {
		if (i == index) {
			return listener;
		}
		listener = listener->next;
	}

	return NULL;
}

size_t bud_node_listener_count(const bud_node *node)
{
	size_t count;
	const bud_listener *listener;

	count = 0;
	if (!node) {
		return 0;
	}

	for (listener = node->listeners; listener; listener = listener->next) {
		count++;
	}

	return count;
}

const char *bud_node_listener_event(const bud_node *node, size_t index)
{
	const bud_listener *listener;

	listener = bud_listener_at(node, index);
	if (!listener) {
		return NULL;
	}

	return listener->event;
}

int bud_node_listener_bubbles(const bud_node *node, size_t index)
{
	const bud_listener *listener;

	listener = bud_listener_at(node, index);
	if (!listener) {
		return -1;
	}

	return listener->bubbles;
}

char *bud_render_html(const bud_node *root)
{
	bud_buf buf;

	bud_buf_init(&buf);
	bud_prepare_render((bud_node *)root);
	if (bud_render_html_node(root, &buf) != 0) {
		free(buf.data);
		return NULL;
	}

	if (!buf.data) {
		buf.data = (char *)malloc(1);
		if (!buf.data) {
			return NULL;
		}
		buf.data[0] = '\0';
	}

	return buf.data;
}

char *bud_render_hydrated_html(const bud_node *root)
{
	bud_buf buf;

	bud_buf_init(&buf);
	bud_prepare_render((bud_node *)root);
	if (bud_render_hydrated_html_node(root, &buf) != 0) {
		free(buf.data);
		return NULL;
	}

	if (!buf.data) {
		buf.data = (char *)malloc(1);
		if (!buf.data) {
			return NULL;
		}
		buf.data[0] = '\0';
	}

	return buf.data;
}

int bud_render_ops(const bud_node *root, bud_emit_fn emit, void *user)
{
	if (!emit) {
		return -1;
	}

	bud_prepare_render((bud_node *)root);
	return bud_render_ops_node(root, emit, user);
}

int bud_render_hydration_ops(const bud_node *root, bud_emit_fn emit, void *user)
{
	if (!emit) {
		return -1;
	}

	bud_prepare_render((bud_node *)root);
	return bud_render_ops_node(root, emit, user);
}

static int
bud_render_patch_ops_node(const bud_node *node, bud_emit_fn emit, void *user)
{
	const bud_attr *attr;
	const bud_listener *listener;
	const bud_node *child;
	char id_buf[32];
	char bubbles_buf[8];
	int len;

	if (!node) {
		return 0;
	}

	len = snprintf(id_buf, sizeof(id_buf), "%u", node->id);
	if (len < 0) {
		return -1;
	}

	switch (node->kind) {
	case BUD_NODE_FRAGMENT:
		if (emit(user, "patch-open", "fragment", id_buf, NULL) != 0) {
			return -1;
		}
		for (child = node->first_child; child;
		     child = child->next_sibling)
		{
			if (bud_render_patch_ops_node(child, emit, user) != 0) {
				return -1;
			}
		}
		return emit(user, "patch-close", "fragment", id_buf, NULL) != 0
		               ? -1
		               : 0;
	case BUD_NODE_ELEMENT:
		if (emit(user, "patch-open", node->tag, id_buf, NULL) != 0) {
			return -1;
		}
		for (attr = node->attrs; attr; attr = attr->next) {
			if (emit(user, "patch-attr", id_buf, attr->name,
			         attr->value ? attr->value : "") != 0)
			{
				return -1;
			}
		}
		for (listener = node->listeners; listener;
		     listener = listener->next)
		{
			snprintf(
			        bubbles_buf, sizeof(bubbles_buf), "%d",
			        listener->bubbles ? 1 : 0);
			if (emit(user, "patch-listener", id_buf,
			         listener->event, bubbles_buf) != 0)
			{
				return -1;
			}
		}
		for (child = node->first_child; child;
		     child = child->next_sibling)
		{
			if (bud_render_patch_ops_node(child, emit, user) != 0) {
				return -1;
			}
		}
		return emit(user, "patch-close", node->tag, id_buf, NULL) != 0
		               ? -1
		               : 0;
	case BUD_NODE_TEXT:
		return emit(user, "patch-text", node->text, id_buf, NULL) != 0
		               ? -1
		               : 0;
	case BUD_NODE_RAW_HTML:
		return emit(user, "patch-raw", node->text, id_buf, NULL) != 0
		               ? -1
		               : 0;
	default:
		return -1;
	}
}

int bud_render_patch_ops(const bud_node *root, bud_emit_fn emit, void *user)
{
	char id_buf[32];
	int len;

	if (!emit) {
		return -1;
	}

	if (!root) {
		return 0;
	}

	bud_prepare_render((bud_node *)root);
	len = snprintf(id_buf, sizeof(id_buf), "%u", root->id);
	if (len < 0) {
		return -1;
	}
	if (emit(user, "patch-clear", id_buf, NULL, NULL) != 0) {
		return -1;
	}
	return bud_render_patch_ops_node(root, emit, user);
}

static const char *bud_get_node_key(const bud_node *node)
{
	if (!node || node->kind != BUD_NODE_ELEMENT)
		return NULL;
	const bud_attr *attr = bud_attr_find((bud_node *)node, "data-key");
	if (attr && attr->value && attr->value[0])
		return attr->value;
	attr = bud_attr_find((bud_node *)node, "key");
	if (attr && attr->value && attr->value[0])
		return attr->value;
	return NULL;
}

static int bud_vdom_diff_node(
        bud_node *old_node, bud_node *new_node, bud_emit_fn emit, void *user)
{
	char id_buf[32];
	const bud_attr *new_attr, *old_attr;
	const bud_node *old_child, *new_child;
	int len;

	if (!old_node && !new_node)
		return 0;

	if (!old_node && new_node) {
		return bud_render_patch_ops_node(new_node, emit, user);
	}

	if (old_node && !new_node) {
		len = snprintf(id_buf, sizeof(id_buf), "%u", old_node->id);
		if (len < 0)
			return -1;
		return emit(user, "patch-remove", id_buf, NULL, NULL);
	}

	new_node->id = old_node->id;
	len = snprintf(id_buf, sizeof(id_buf), "%u", old_node->id);
	if (len < 0)
		return -1;

	if (old_node->kind != new_node->kind ||
	    (old_node->kind == BUD_NODE_ELEMENT &&
	     strcmp(old_node->tag, new_node->tag) != 0))
	{
		char *html = bud_render_html(new_node);
		if (!html)
			return -1;
		int rc = emit(user, "patch-replace", id_buf, html, NULL);
		bud_free_string(html);
		return rc;
	}

	if (old_node->kind == BUD_NODE_TEXT) {
		const char *ot = old_node->text ? old_node->text : "";
		const char *nt = new_node->text ? new_node->text : "";
		if (strcmp(ot, nt) != 0) {
			return emit(user, "patch-text", nt, id_buf, NULL);
		}
		return 0;
	}

	if (old_node->kind == BUD_NODE_RAW_HTML) {
		const char *ot = old_node->text ? old_node->text : "";
		const char *nt = new_node->text ? new_node->text : "";
		if (strcmp(ot, nt) != 0) {
			return emit(user, "patch-innerhtml", id_buf, nt, NULL);
		}
		return 0;
	}

	if (old_node->kind == BUD_NODE_ELEMENT) {
		for (new_attr = new_node->attrs; new_attr;
		     new_attr = new_attr->next)
		{
			old_attr = bud_attr_find(old_node, new_attr->name);
			const char *nv = new_attr->value ? new_attr->value : "";
			if (!old_attr ||
			    strcmp(old_attr->value ? old_attr->value : "",
			           nv) != 0)
			{
				if (emit(user, "patch-attr", id_buf,
				         new_attr->name, nv) != 0)
					return -1;
			}
		}
		for (old_attr = old_node->attrs; old_attr;
		     old_attr = old_attr->next)
		{
			if (!bud_attr_find(new_node, old_attr->name)) {
				if (emit(user, "patch-attr", id_buf,
				         old_attr->name, "") != 0)
					return -1;
			}
		}

		/* If this element contains raw HTML, emit patch-innerhtml on
		 * this element directly */
		if (old_node->first_child &&
		    old_node->first_child->kind == BUD_NODE_RAW_HTML &&
		    new_node->first_child &&
		    new_node->first_child->kind == BUD_NODE_RAW_HTML &&
		    !old_node->first_child->next_sibling &&
		    !new_node->first_child->next_sibling)
		{
			const char *ot = old_node->first_child->text
			                         ? old_node->first_child->text
			                         : "";
			const char *nt = new_node->first_child->text
			                         ? new_node->first_child->text
			                         : "";
			if (strcmp(ot, nt) != 0) {
				return emit(
				        user, "patch-innerhtml", id_buf, nt,
				        NULL);
			}
			return 0;
		}
	}

	int has_keys = 0;
	for (const bud_node *c = old_node->first_child; c; c = c->next_sibling)
	{
		if (bud_get_node_key(c)) {
			has_keys = 1;
			break;
		}
	}
	if (!has_keys) {
		for (const bud_node *c = new_node->first_child; c;
		     c = c->next_sibling)
		{
			if (bud_get_node_key(c)) {
				has_keys = 1;
				break;
			}
		}
	}

	if (has_keys) {
#define BUD_MAX_DIFF_CHILDREN 256
		const bud_node *old_children[BUD_MAX_DIFF_CHILDREN];
		int matched[BUD_MAX_DIFF_CHILDREN] = { 0 };
		int n_old = 0;

		for (const bud_node *c = old_node->first_child;
		     c && n_old < BUD_MAX_DIFF_CHILDREN; c = c->next_sibling)
		{
			old_children[n_old++] = c;
		}

		for (const bud_node *nc = new_node->first_child; nc;
		     nc = nc->next_sibling)
		{
			const char *nkey = bud_get_node_key(nc);
			int found_idx = -1;
			if (nkey) {
				for (int i = 0; i < n_old; i++) {
					if (!matched[i]) {
						const char *okey =
						        bud_get_node_key(
						                old_children
						                        [i]);
						if (okey &&
						    strcmp(okey, nkey) == 0)
						{
							found_idx = i;
							break;
						}
					}
				}
			}

			if (found_idx >= 0) {
				matched[found_idx] = 1;
				if (bud_vdom_diff_node(
				            (bud_node *)old_children[found_idx],
				            (bud_node *)nc, emit, user) != 0)
					return -1;
			} else {
				if (bud_vdom_diff_node(
				            NULL, (bud_node *)nc, emit, user) !=
				    0)
					return -1;
			}
		}

		for (int i = 0; i < n_old; i++) {
			if (!matched[i]) {
				if (bud_vdom_diff_node(
				            (bud_node *)old_children[i], NULL,
				            emit, user) != 0)
					return -1;
			}
		}
		return 0;
	}

	old_child = old_node->first_child;
	new_child = new_node->first_child;
	while (old_child || new_child) {
		const bud_node *next_old =
		        old_child ? old_child->next_sibling : NULL;
		const bud_node *next_new =
		        new_child ? new_child->next_sibling : NULL;
		if (bud_vdom_diff_node(
		            (bud_node *)old_child, (bud_node *)new_child, emit,
		            user) != 0)
			return -1;
		old_child = next_old;
		new_child = next_new;
	}

	return 0;
}

int bud_vdom_diff(
        bud_node *old_root, bud_node *new_root, bud_emit_fn emit, void *user)
{
	if (!emit)
		return -1;
	if (!old_root && !new_root)
		return 0;
	if (old_root)
		bud_prepare_render(old_root);
	if (new_root)
		bud_prepare_render(new_root);
	return bud_vdom_diff_node(old_root, new_root, emit, user);
}

static const char *bud_kind_name(bud_node_kind kind)
{
	switch (kind) {
	case BUD_NODE_FRAGMENT:
		return "fragment";
	case BUD_NODE_ELEMENT:
		return "element";
	case BUD_NODE_TEXT:
		return "text";
	case BUD_NODE_RAW_HTML:
		return "raw";
	default:
		return "unknown";
	}
}

static int bud_render_walk_ops_node(
        const bud_node *node, bud_emit_fn emit, void *user, size_t depth)
{
	const bud_attr *attr;
	const bud_listener *listener;
	const bud_node *child;
	char depth_buf[32];
	char id_buf[32];
	char bubbles_buf[8];
	int len;

	if (!node) {
		return 0;
	}

	len = snprintf(depth_buf, sizeof(depth_buf), "%zu", depth);
	if (len < 0) {
		return -1;
	}
	len = snprintf(id_buf, sizeof(id_buf), "%u", node->id);
	if (len < 0) {
		return -1;
	}

	if (emit(user, "walk-enter", depth_buf, bud_kind_name(node->kind),
	         id_buf) != 0)
	{
		return -1;
	}

	switch (node->kind) {
	case BUD_NODE_FRAGMENT:
		for (child = node->first_child; child;
		     child = child->next_sibling)
		{
			if (bud_render_walk_ops_node(
			            child, emit, user, depth + 1) != 0)
			{
				return -1;
			}
		}
		break;
	case BUD_NODE_ELEMENT:
		for (attr = node->attrs; attr; attr = attr->next) {
			if (emit(user, "walk-attr", id_buf, attr->name,
			         attr->value) != 0)
			{
				return -1;
			}
		}
		for (listener = node->listeners; listener;
		     listener = listener->next)
		{
			snprintf(
			        bubbles_buf, sizeof(bubbles_buf), "%d",
			        listener->bubbles ? 1 : 0);
			if (emit(user, "walk-listener", id_buf, listener->event,
			         bubbles_buf) != 0)
			{
				return -1;
			}
		}
		for (child = node->first_child; child;
		     child = child->next_sibling)
		{
			if (bud_render_walk_ops_node(
			            child, emit, user, depth + 1) != 0)
			{
				return -1;
			}
		}
		break;
	case BUD_NODE_TEXT:
		if (emit(user, "walk-text", id_buf, depth_buf, node->text) != 0)
		{
			return -1;
		}
		break;
	case BUD_NODE_RAW_HTML:
		if (emit(user, "walk-raw", id_buf, depth_buf, node->text) != 0)
		{
			return -1;
		}
		break;
	default:
		return -1;
	}

	return emit(user, "walk-leave", depth_buf, bud_kind_name(node->kind),
	            id_buf) != 0
	               ? -1
	               : 0;
}

int bud_render_walk_ops(const bud_node *root, bud_emit_fn emit, void *user)
{
	if (!emit) {
		return -1;
	}

	bud_prepare_render((bud_node *)root);
	return bud_render_walk_ops_node(root, emit, user, 0);
}

int bud_hydrate(const bud_node *root, bud_hydrate_lookup_fn lookup, void *user)
{
	if (!lookup) {
		return -1;
	}

	bud_prepare_render((bud_node *)root);
	return bud_hydrate_node(root, lookup, user);
}

static int
bud_walk_node(const bud_node *node, const bud_walk_ops *ops, size_t depth)
{
	const bud_attr *attr;
	const bud_listener *listener;
	const bud_node *child;
	size_t index;

	if (!node) {
		return 0;
	}

	if (ops->enter_node) {
		if (ops->enter_node(ops->user, node, depth) != 0) {
			return -1;
		}
	}

	index = 0;
	for (attr = node->attrs; attr; attr = attr->next) {
		if (ops->attr) {
			if (ops->attr(
			            ops->user, node, depth, index, attr->name,
			            attr->value) != 0)
			{
				return -1;
			}
		}
		index++;
	}

	index = 0;
	for (listener = node->listeners; listener; listener = listener->next) {
		if (ops->listener) {
			if (ops->listener(
			            ops->user, node, depth, index,
			            listener->event, listener->bubbles) != 0)
			{
				return -1;
			}
		}
		index++;
	}

	for (child = node->first_child; child; child = child->next_sibling) {
		if (bud_walk_node(child, ops, depth + 1) != 0) {
			return -1;
		}
	}

	if (ops->leave_node) {
		if (ops->leave_node(ops->user, node, depth) != 0) {
			return -1;
		}
	}

	return 0;
}

int bud_walk(const bud_node *root, const bud_walk_ops *ops)
{
	if (!ops) {
		return -1;
	}

	return bud_walk_node(root, ops, 0);
}

bud_runtime *bud_runtime_new(bud_node *root)
{
	bud_runtime *runtime;

	runtime = (bud_runtime *)calloc(1, sizeof(*runtime));
	if (!runtime) {
		return NULL;
	}

	runtime->root = root;
	return runtime;
}

void bud_runtime_free(bud_runtime *runtime)
{
	if (!runtime) {
		return;
	}

	bud_runtime_unmount(runtime);
	free(runtime);
}

bud_node *bud_runtime_root(const bud_runtime *runtime)
{
	if (!runtime) {
		return NULL;
	}

	return runtime->root;
}

int bud_runtime_set_invalidate(
        bud_runtime *runtime, bud_runtime_invalidate_fn fn, void *user)
{
	if (!runtime) {
		return -1;
	}

	runtime->invalidate = fn;
	runtime->invalidate_user = user;
	return 0;
}

int bud_runtime_mark_dirty(bud_runtime *runtime)
{
	if (!runtime) {
		return -1;
	}

	runtime->dirty = 1;
	return 0;
}

int bud_runtime_is_dirty(const bud_runtime *runtime)
{
	if (!runtime) {
		return 0;
	}

	return runtime->dirty;
}

int bud_runtime_flush(bud_runtime *runtime)
{
	if (!runtime) {
		return -1;
	}

	if (!runtime->dirty) {
		return 0;
	}

	runtime->dirty = 0;
	if (runtime->invalidate) {
		runtime->invalidate(runtime->invalidate_user, runtime);
	}

	return 0;
}

int bud_runtime_mount(bud_runtime *runtime)
{
	if (!runtime) {
		return -1;
	}

	if (runtime->mounted) {
		return 0;
	}

	runtime->mounted = 1;
	bud_node_call_mount(runtime->root, runtime);
	return 0;
}

int bud_runtime_update(bud_runtime *runtime)
{
	if (!runtime) {
		return -1;
	}

	if (!runtime->mounted) {
		return bud_runtime_mount(runtime);
	}

	bud_node_call_update(runtime->root, runtime);
	return 0;
}

int bud_runtime_unmount(bud_runtime *runtime)
{
	if (!runtime) {
		return -1;
	}

	if (!runtime->mounted) {
		return 0;
	}

	bud_node_call_unmount(runtime->root, runtime);
	runtime->mounted = 0;
	return 0;
}

static void bud_event_stop_default(bud_event *event)
{
	if (!event) {
		return;
	}

	event->stopped = 1;
}

void bud_event_stop_propagation(bud_event *event)
{
	bud_event_stop_default(event);
}

void bud_event_prevent_default(bud_event *event)
{
	if (!event) {
		return;
	}

	event->default_prevented = 1;
}

static int bud_dispatch_event_node(
        bud_node *node, bud_event *event, const char *listener_event)
{
	bud_listener *listener;
	int allow_bubble;

	if (!node) {
		return 0;
	}

	event->current_target = node;
	allow_bubble = node != event->target;
	for (listener = node->listeners; listener; listener = listener->next) {
		if (strcmp(listener->event, listener_event) != 0) {
			continue;
		}
		if (allow_bubble && !listener->bubbles) {
			continue;
		}
		if (listener->handler) {
			if (listener->handler(event) != 0) {
				return -1;
			}
		}
		if (event->stopped) {
			return 0;
		}
	}

	return 0;
}

int bud_runtime_dispatch(
        bud_runtime *runtime, bud_node *target, const char *event,
        void *event_user)
{
	bud_event ev;
	bud_node *current;
	char *logical_event;
	char *target_suffix;
	int rc;

	if (!runtime || !target || !event) {
		return -1;
	}
	logical_event = malloc(strlen(event) + 1);
	if (!logical_event)
		return -1;
	strcpy(logical_event, event);
	target_suffix = strchr(logical_event, '@');
	if (target_suffix)
		*target_suffix = '\0';

	memset(&ev, 0, sizeof(ev));
	ev.type = logical_event;
	ev.target = target;
	ev.current_target = target;
	ev.user = event_user;
	ev.bubbles = 1;

	current = target;
	rc = 0;
	while (current) {
		if (bud_dispatch_event_node(current, &ev, event) != 0) {
			rc = -1;
			break;
		}
		if (!ev.bubbles || ev.stopped) {
			break;
		}
		current = current->parent;
	}

	free(logical_event);
	return rc;
}

void bud_free(bud_node *node)
{
	bud_node_free_chain(node);
}

void bud_free_string(char *value)
{
	free(value);
}

void bud_node_set_src(bud_node *node, const char *file, int line)
{
	if (!node)
		return;
#ifdef BUD_DEBUG
	node->src_file = file;
	node->src_line = line;
#else
	(void)file;
	(void)line;
#endif
}

static char bud_src_buf[256];

const char *bud_node_get_src(const bud_node *node)
{
	if (!node)
		return "(null)";
#ifdef BUD_DEBUG
	if (node->src_file) {
		snprintf(
		        bud_src_buf, sizeof(bud_src_buf), "%s:%d",
		        node->src_file, node->src_line);
		return bud_src_buf;
	}
#endif
	snprintf(
	        bud_src_buf, sizeof(bud_src_buf), "node=%d kind=%u tag=%s",
	        bud_node_id(node), (unsigned)bud_node_kind_of(node),
	        bud_node_tag(node) ? bud_node_tag(node) : "(null)");
	return bud_src_buf;
}

/* ── Tree dump for debugging ── */

static int bud_sprint_tree_node(
        const bud_node *node, char *buf, size_t bufsz, size_t *pos, int depth)
{
	const bud_attr *attr;
	size_t i;
	int n;

	if (!node)
		return 0;

	/* indent */
	for (i = 0; i < (size_t)depth && *pos < bufsz; i++) {
		buf[*pos] = ' ';
		(*pos)++;
	}
	if (*pos >= bufsz)
		return -1;

	/* id= */
	n = snprintf(buf + *pos, bufsz - *pos, "id=%u ", bud_node_id(node));
	if (n < 0 || (size_t)n >= bufsz - *pos)
		return -1;
	*pos += n;

	/* kind */
	switch (bud_node_kind_of(node)) {
	case BUD_NODE_FRAGMENT:
		n = snprintf(buf + *pos, bufsz - *pos, "kind=FRAGMENT");
		break;
	case BUD_NODE_ELEMENT:
		n = snprintf(buf + *pos, bufsz - *pos, "kind=ELEMENT");
		break;
	case BUD_NODE_TEXT:
		n = snprintf(buf + *pos, bufsz - *pos, "kind=TEXT");
		break;
	case BUD_NODE_RAW_HTML:
		n = snprintf(buf + *pos, bufsz - *pos, "kind=RAW_HTML");
		break;
	default:
		n = snprintf(buf + *pos, bufsz - *pos, "kind=?");
		break;
	}
	if (n < 0 || (size_t)n >= bufsz - *pos)
		return -1;
	*pos += n;

	/* tag (for elements) */
	if (bud_node_kind_of(node) == BUD_NODE_ELEMENT && bud_node_tag(node)) {
		n = snprintf(
		        buf + *pos, bufsz - *pos, " tag=%s",
		        bud_node_tag(node));
		if (n < 0 || (size_t)n >= bufsz - *pos)
			return -1;
		*pos += n;

		/* first two key attrs: id, class */
		for (attr = node->attrs; attr; attr = attr->next) {
			if (strcmp(attr->name, "id") == 0 ||
			    strcmp(attr->name, "class") == 0)
			{
				n = snprintf(
				        buf + *pos, bufsz - *pos, " %s=\"%s\"",
				        attr->name, attr->value);
				if (n < 0 || (size_t)n >= bufsz - *pos)
					return -1;
				*pos += n;
			}
		}
	}

	/* text snippet (for TEXT) */
	if (bud_node_kind_of(node) == BUD_NODE_TEXT && bud_node_text(node)) {
		const char *t = bud_node_text(node);
		size_t tlen = strlen(t);
		n = snprintf(
		        buf + *pos, bufsz - *pos, " \"%.*s%s\"",
		        (int)(tlen < 40 ? tlen : 40), t,
		        tlen > 40 ? "..." : "");
		if (n < 0 || (size_t)n >= bufsz - *pos)
			return -1;
		*pos += n;
	}

	/* source location */
#ifdef BUD_DEBUG
	if (node->src_file) {
		n = snprintf(
		        buf + *pos, bufsz - *pos, " [%s:%d]", node->src_file,
		        node->src_line);
		if (n < 0 || (size_t)n >= bufsz - *pos)
			return -1;
		*pos += n;
	}
#endif

	/* newline */
	if (*pos < bufsz) {
		buf[*pos] = '\n';
		(*pos)++;
	}

	/* children */
	for (i = 0; i < bud_node_child_count(node); i++) {
		if (bud_sprint_tree_node(
		            bud_node_child(node, i), buf, bufsz, pos,
		            depth + 1) != 0)
			return -1;
	}

	return 0;
}

int bud_sprint_tree(const bud_node *root, char *buf, size_t bufsz)
{
	size_t pos = 0;

	if (!root || !buf || bufsz == 0)
		return -1;

	buf[0] = '\0';
	if (bud_sprint_tree_node(root, buf, bufsz, &pos, 0) != 0)
		return -1;

	if (pos < bufsz)
		buf[pos] = '\0';
	else
		buf[bufsz - 1] = '\0';

	return 0;
}

bud_node *bud_el_impl(const char *tag, size_t count, const bud_arg *args)
{
	bud_node *node;
	size_t i;

	node = bud_element(tag);
	if (!node) {
		return NULL;
	}

	for (i = 0; i < count; i++) {
		switch (args[i].type) {
		case BUD_ARG_NODE:
			if (args[i].data.node) {
				bud_append(node, args[i].data.node);
			}
			break;
		case BUD_ARG_ATTR:
			bud_set_attr(
			        node, args[i].data.attr.name,
			        args[i].data.attr.value);
			break;
		case BUD_ARG_ATTR_FMT:
			bud_set_attr(
			        node, args[i].data.attr.name,
			        args[i].data.attr.value);
			free((void *)args[i].data.attr.value);
			break;
		case BUD_ARG_EVENT:
			bud_on(node, args[i].data.ev.event,
			       args[i].data.ev.bubbles);
			break;
		case BUD_ARG_BIND:
			bud_bind(
			        node, args[i].data.bind.event,
			        args[i].data.bind.bubbles,
			        args[i].data.bind.handler);
			break;
		case BUD_ARG_NONE:
		default:
			break;
		}
	}

	return node;
}

bud_node *bud_frag_impl(size_t count, const bud_arg *args)
{
	bud_node *node;
	size_t i;

	node = bud_fragment();
	if (!node) {
		return NULL;
	}

	for (i = 0; i < count; i++) {
		switch (args[i].type) {
		case BUD_ARG_NODE:
			if (args[i].data.node) {
				bud_append(node, args[i].data.node);
			}
			break;
		case BUD_ARG_NONE:
		case BUD_ARG_ATTR:
		case BUD_ARG_EVENT:
		case BUD_ARG_BIND:
		default:
			break;
		}
	}

	return node;
}

/* ── JSON field extraction helpers (jsmn-backed, length-aware) ── */

#define JSMN_PARENT_LINKS
#define JSMN_STATIC
#include "jsmn.h"

#define BUD_JSON_MAX_TOKS 512

static int bud__jsoneq(const char *js, const jsmntok_t *tok, const char *s)
{
	size_t slen = strlen(s);
	return tok->type == JSMN_STRING &&
	       (size_t)(tok->end - tok->start) == slen &&
	       strncmp(js + tok->start, s, slen) == 0;
}

static int bud__hex_val(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/* Unescape a jsmn string slice (without outer quotes) into out. */
static void
bud__unescape_slice(const char *src, size_t slen, char *out, size_t out_sz)
{
	size_t i = 0;
	size_t o = 0;

	if (!out || out_sz == 0)
		return;
	out[0] = '\0';
	for (i = 0; i < slen && o + 1 < out_sz; i++) {
		char c = src[i];
		if (c == '\\' && i + 1 < slen) {
			char e = src[i + 1];
			switch (e) {
			case '"':
				out[o++] = '"';
				i++;
				break;
			case '\\':
				out[o++] = '\\';
				i++;
				break;
			case '/':
				out[o++] = '/';
				i++;
				break;
			case 'b':
				out[o++] = '\b';
				i++;
				break;
			case 'f':
				out[o++] = '\f';
				i++;
				break;
			case 'n':
				out[o++] = '\n';
				i++;
				break;
			case 'r':
				out[o++] = '\r';
				i++;
				break;
			case 't':
				out[o++] = '\t';
				i++;
				break;
			case 'u': {
				if (i + 5 < slen) {
					int h0 = bud__hex_val(src[i + 2]);
					int h1 = bud__hex_val(src[i + 3]);
					int h2 = bud__hex_val(src[i + 4]);
					int h3 = bud__hex_val(src[i + 5]);
					if (h0 >= 0 && h1 >= 0 && h2 >= 0 &&
					    h3 >= 0)
					{
						unsigned cp =
						        (unsigned)((h0 << 12) |
						                   (h1 << 8) |
						                   (h2 << 4) |
						                   h3);
						if (cp < 0x80) {
							out[o++] = (char)cp;
						} else if (
						        cp < 0x800 &&
						        o + 2 < out_sz)
						{
							out[o++] = (char)(0xC0 |
							                  (cp >>
							                   6));
							out[o++] =
							        (char)(0x80 |
							               (cp &
							                0x3F));
						} else if (o + 3 < out_sz) {
							out[o++] = (char)(0xE0 |
							                  (cp >>
							                   12));
							out[o++] =
							        (char)(0x80 |
							               ((cp >>
							                 6) &
							                0x3F));
							out[o++] =
							        (char)(0x80 |
							               (cp &
							                0x3F));
						}
						i += 5;
						break;
					}
				}
				out[o++] = '\\';
				break;
			}
			default:
				out[o++] = e;
				i++;
				break;
			}
		} else {
			out[o++] = c;
		}
	}
	out[o] = '\0';
}

static int bud__parse_tokens(
        const char *js, size_t len, jsmntok_t *stack_buf, unsigned stack_n,
        jsmntok_t **out_toks, unsigned *out_ntoks, jsmntok_t **heap_toks)
{
	jsmn_parser p;
	int r;

	*heap_toks = NULL;
	jsmn_init(&p);
	r = jsmn_parse(&p, js, len, stack_buf, stack_n);
	if (r != JSMN_ERROR_NOMEM) {
		*out_toks = stack_buf;
		*out_ntoks = r >= 0 ? (unsigned)r : 0;
		return r;
	}
	{
		unsigned cap = stack_n * 2;
		while (cap < 8192) {
			jsmntok_t *heap =
			        (jsmntok_t *)malloc(cap * sizeof(jsmntok_t));
			if (!heap)
				return JSMN_ERROR_NOMEM;
			jsmn_init(&p);
			r = jsmn_parse(&p, js, len, heap, cap);
			if (r != JSMN_ERROR_NOMEM) {
				*out_toks = heap;
				*out_ntoks = r >= 0 ? (unsigned)r : 0;
				*heap_toks = heap;
				return r;
			}
			free(heap);
			cap *= 2;
		}
	}
	return JSMN_ERROR_NOMEM;
}

static int bud__find_key(
        const char *js, jsmntok_t *toks, unsigned ntoks, const char *key,
        int *val_idx)
{
	unsigned i;

	if (ntoks == 0 || toks[0].type != JSMN_OBJECT)
		return 0;
	i = 1;
	while (i + 1 < ntoks) {
		jsmntok_t *kt = &toks[i];
		jsmntok_t *vt;
		unsigned next;

		if (kt->type != JSMN_STRING) {
			i++;
			continue;
		}
		vt = &toks[i + 1];
		if (bud__jsoneq(js, kt, key)) {
			*val_idx = (int)(i + 1);
			return 1;
		}
		next = i + 2;
		while (next < ntoks && toks[next].start != -1 &&
		       toks[next].start < vt->end)
			next++;
		i = next;
	}
	return 0;
}

void bud_json_str_len(
        const char *json, size_t len, const char *key, char *out,
        size_t out_size)
{
	jsmntok_t stack[BUD_JSON_MAX_TOKS];
	jsmntok_t *toks = NULL;
	jsmntok_t *heap = NULL;
	unsigned ntoks = 0;
	int r;
	int vidx;

	if (!out || out_size == 0)
		return;
	out[0] = '\0';
	if (!json || !key)
		return;
	if (len == 0)
		len = strlen(json);
	if (len == 0)
		return;
	r = bud__parse_tokens(
	        json, len, stack, BUD_JSON_MAX_TOKS, &toks, &ntoks, &heap);
	if (r < 0)
		goto out;
	if (!bud__find_key(json, toks, ntoks, key, &vidx))
		goto out;
	{
		jsmntok_t *vt = &toks[vidx];
		if (vt->type == JSMN_STRING) {
			bud__unescape_slice(
			        json + vt->start, (size_t)(vt->end - vt->start),
			        out, out_size);
		} else if (vt->type == JSMN_PRIMITIVE) {
			size_t slen = (size_t)(vt->end - vt->start);
			size_t n = slen < out_size - 1 ? slen : out_size - 1;
			memcpy(out, json + vt->start, n);
			out[n] = '\0';
		} else if (vt->type == JSMN_OBJECT || vt->type == JSMN_ARRAY) {
			size_t slen = (size_t)(vt->end - vt->start);
			size_t n = slen < out_size - 1 ? slen : out_size - 1;
			memcpy(out, json + vt->start, n);
			out[n] = '\0';
		}
	}
out:
	if (heap)
		free(heap);
}

void bud_json_str(const char *json, const char *key, char *out, size_t out_size)
{
	size_t len = json ? strlen(json) : 0;
	bud_json_str_len(json, len, key, out, out_size);
}

int bud_json_int_len(
        const char *json, size_t len, const char *key, int default_val)
{
	jsmntok_t stack[BUD_JSON_MAX_TOKS];
	jsmntok_t *toks = NULL;
	jsmntok_t *heap = NULL;
	unsigned ntoks = 0;
	int r;
	int vidx;

	if (!json || !key)
		return default_val;
	if (len == 0)
		len = strlen(json);
	if (len == 0)
		return default_val;
	r = bud__parse_tokens(
	        json, len, stack, BUD_JSON_MAX_TOKS, &toks, &ntoks, &heap);
	if (r < 0) {
		if (heap)
			free(heap);
		return default_val;
	}
	if (!bud__find_key(json, toks, ntoks, key, &vidx)) {
		if (heap)
			free(heap);
		return default_val;
	}
	{
		jsmntok_t *vt = &toks[vidx];
		const char *s = json + vt->start;
		size_t slen = (size_t)(vt->end - vt->start);
		char buf[64];
		size_t n = slen < sizeof(buf) - 1 ? slen : sizeof(buf) - 1;
		char *end = NULL;
		long v;
		memcpy(buf, s, n);
		buf[n] = '\0';
		if (vt->type == JSMN_STRING) {
			char *p = buf;
			while (*p && (*p < '0' || *p > '9') && *p != '-')
				p++;
			if (!*p) {
				if (heap)
					free(heap);
				return default_val;
			}
			v = strtol(p, &end, 10);
		} else {
			v = strtol(buf, &end, 10);
			if (end == buf) {
				if (heap)
					free(heap);
				return default_val;
			}
		}
		if (heap)
			free(heap);
		return (int)v;
	}
}

int bud_json_int(const char *json, const char *key, int default_val)
{
	size_t len = json ? strlen(json) : 0;
	return bud_json_int_len(json, len, key, default_val);
}

void bud_json_data_len(const char *json, size_t len, char *out, size_t out_size)
{
	bud_json_str_len(json, len, "data", out, out_size);
}

void bud_json_data(const char *json, char *out, size_t out_size)
{
	size_t len = json ? strlen(json) : 0;
	bud_json_data_len(json, len, out, out_size);
}

int bud_json_array_for_each_len(
        const char *json, size_t len,
        void (*fn)(const char *elem, size_t elen, void *user), void *user)
{
	jsmntok_t stack[BUD_JSON_MAX_TOKS];
	jsmntok_t *toks = NULL;
	jsmntok_t *heap = NULL;
	unsigned ntoks = 0;
	int r;
	unsigned idx;
	int count = 0;

	if (!json || !fn)
		return -1;
	if (len == 0)
		len = strlen(json);
	r = bud__parse_tokens(
	        json, len, stack, BUD_JSON_MAX_TOKS, &toks, &ntoks, &heap);
	if (r < 0) {
		if (heap)
			free(heap);
		return -1;
	}
	if (ntoks == 0 || toks[0].type != JSMN_ARRAY) {
		if (heap)
			free(heap);
		return -1;
	}
	idx = 1;
	for (int i = 0; i < toks[0].size; i++) {
		unsigned next;
		const char *elem;
		size_t elen;

		if (idx >= ntoks)
			break;
		elem = json + toks[idx].start;
		elen = (size_t)(toks[idx].end - toks[idx].start);
		if (toks[idx].type == JSMN_STRING) {
			if (toks[idx].start > 0 &&
			    json[toks[idx].start - 1] == '"' &&
			    (size_t)toks[idx].end < len &&
			    json[toks[idx].end] == '"')
			{
				elem = json + toks[idx].start - 1;
				elen += 2;
			}
		} else if (
		        toks[idx].type == JSMN_OBJECT ||
		        toks[idx].type == JSMN_ARRAY)
		{
			elem = json + toks[idx].start;
			elen = (size_t)(toks[idx].end - toks[idx].start);
		}
		fn(elem, elen, user);
		count++;
		next = idx + 1;
		while (next < ntoks && toks[next].start != -1 &&
		       toks[next].start < toks[idx].end)
			next++;
		idx = next;
	}
	if (heap)
		free(heap);
	return count;
}

int bud_json_array_for_each(
        const char *json, void (*fn)(const char *elem, size_t len, void *user),
        void *user)
{
	size_t len = json ? strlen(json) : 0;
	return bud_json_array_for_each_len(json, len, fn, user);
}

int bud_json_array_for_each_key_len(
        const char *json, size_t len, const char *key,
        void (*fn)(const char *elem, size_t elen, void *user), void *user)
{
	jsmntok_t stack[BUD_JSON_MAX_TOKS];
	jsmntok_t *toks = NULL;
	jsmntok_t *heap = NULL;
	unsigned ntoks = 0;
	int r;
	int vidx;
	unsigned idx;
	int n;

	if (!json || !key || !fn)
		return -1;
	if (len == 0)
		len = strlen(json);
	r = bud__parse_tokens(
	        json, len, stack, BUD_JSON_MAX_TOKS, &toks, &ntoks, &heap);
	if (r < 0) {
		if (heap)
			free(heap);
		return -1;
	}
	if (!bud__find_key(json, toks, ntoks, key, &vidx)) {
		if (heap)
			free(heap);
		return -1;
	}
	if (toks[vidx].type != JSMN_ARRAY) {
		if (heap)
			free(heap);
		return -1;
	}
	n = toks[vidx].size;
	idx = (unsigned)(vidx + 1);
	for (int i = 0; i < n; i++) {
		unsigned next;
		const char *elem;
		size_t elen;

		if (idx >= ntoks)
			break;
		elem = json + toks[idx].start;
		elen = (size_t)(toks[idx].end - toks[idx].start);
		if (toks[idx].type == JSMN_STRING) {
			if (toks[idx].start > 0 &&
			    json[toks[idx].start - 1] == '"' &&
			    (size_t)toks[idx].end < len &&
			    json[toks[idx].end] == '"')
			{
				elem = json + toks[idx].start - 1;
				elen += 2;
			}
		} else if (
		        toks[idx].type == JSMN_OBJECT ||
		        toks[idx].type == JSMN_ARRAY)
		{
			elem = json + toks[idx].start;
			elen = (size_t)(toks[idx].end - toks[idx].start);
		}
		fn(elem, elen, user);
		next = idx + 1;
		while (next < ntoks && toks[next].start != -1 &&
		       toks[next].start < toks[idx].end)
			next++;
		idx = next;
	}
	if (heap)
		free(heap);
	return n;
}

/* ── Table-driven state ── */

void bud_state_apply_stride_len(
        void *state, const void *fields, size_t field_stride, const char *json,
        size_t len)
{
	jsmntok_t stack[BUD_JSON_MAX_TOKS];
	jsmntok_t *toks = NULL;
	jsmntok_t *heap = NULL;
	unsigned ntoks = 0;
	int r;

	if (!state || !fields || !json)
		return;
	if (field_stride < sizeof(bud_field_desc_t))
		field_stride = sizeof(bud_field_desc_t);
	if (len == 0)
		len = strlen(json);
	if (len == 0)
		return;
	r = bud__parse_tokens(
	        json, len, stack, BUD_JSON_MAX_TOKS, &toks, &ntoks, &heap);
	if (r < 0) {
		if (heap)
			free(heap);
		return;
	}
	if (ntoks == 0 || toks[0].type != JSMN_OBJECT) {
		if (heap)
			free(heap);
		return;
	}
	for (const char *p = (const char *)fields;; p += field_stride) {
		const bud_field_desc_t *f = (const bud_field_desc_t *)p;
		if (!f->key)
			break;
		int vidx;
		if (f->kind == 1 || f->kind == 5)
			continue;
		if (!bud__find_key(json, toks, ntoks, f->key, &vidx))
			continue;
		if (f->is_int) {
			int *dest = (int *)((char *)state + f->offset);
			jsmntok_t *vt = &toks[vidx];
			const char *s = json + vt->start;
			size_t slen = (size_t)(vt->end - vt->start);
			char buf[64];
			size_t n =
			        slen < sizeof(buf) - 1 ? slen : sizeof(buf) - 1;
			char *end = NULL;
			long v;
			memcpy(buf, s, n);
			buf[n] = '\0';
			if (vt->type == JSMN_STRING) {
				char *pstr = buf;
				while (*pstr && (*pstr < '0' || *pstr > '9') &&
				       *pstr != '-')
					pstr++;
				if (!*pstr)
					continue;
				v = strtol(pstr, &end, 10);
			} else {
				v = strtol(buf, &end, 10);
				if (end == buf)
					continue;
			}
			*dest = (int)v;
		} else if (f->size > 0) {
			char *dest = (char *)state + f->offset;
			jsmntok_t *vt = &toks[vidx];
			if (vt->type == JSMN_STRING) {
				bud__unescape_slice(
				        json + vt->start,
				        (size_t)(vt->end - vt->start), dest,
				        f->size);
			} else if (vt->type == JSMN_PRIMITIVE) {
				size_t slen = (size_t)(vt->end - vt->start);
				size_t n =
				        slen < f->size - 1 ? slen : f->size - 1;
				memcpy(dest, json + vt->start, n);
				dest[n] = '\0';
			} else if (
			        vt->type == JSMN_OBJECT ||
			        vt->type == JSMN_ARRAY)
			{
				size_t slen = (size_t)(vt->end - vt->start);
				size_t n =
				        slen < f->size - 1 ? slen : f->size - 1;
				memcpy(dest, json + vt->start, n);
				dest[n] = '\0';
			}
		}
	}
	if (heap)
		free(heap);
}

void bud_state_apply_stride(
        void *state, const void *fields, size_t field_stride, const char *json)
{
	size_t len = json ? strlen(json) : 0;
	bud_state_apply_stride_len(state, fields, field_stride, json, len);
}

void bud_state_apply_len(
        void *state, const bud_field_desc_t *fields, const char *json,
        size_t len)
{
	bud_state_apply_stride_len(
	        state, fields, sizeof(bud_field_desc_t), json, len);
}

void bud_state_apply(
        void *state, const bud_field_desc_t *fields, const char *json)
{
	size_t len = json ? strlen(json) : 0;
	bud_state_apply_len(state, fields, json, len);
}

struct apply_array_ctx {
	char *array_out;
	size_t elem_size;
	int count;
	int max_elems;
	const void *schema;
	size_t schema_stride;
};

static void apply_array_cb(const char *elem, size_t len, void *user)
{
	struct apply_array_ctx *ctx = (struct apply_array_ctx *)user;
	char *dest;

	if (ctx->count >= ctx->max_elems)
		return;
	dest = ctx->array_out + (ctx->count * ctx->elem_size);
	memset(dest, 0, ctx->elem_size);
	bud_state_apply_stride_len(
	        dest, ctx->schema, ctx->schema_stride, elem, len);
	ctx->count++;
}

void bud_state_apply_array_stride_len(
        const char *json, size_t len, const char *key, void *array_out,
        size_t elem_size, int *count_out, int max_elems, const void *schema,
        size_t schema_stride)
{
	jsmntok_t stack[BUD_JSON_MAX_TOKS];
	jsmntok_t *toks = NULL;
	jsmntok_t *heap = NULL;
	unsigned ntoks = 0;
	int r;
	int vidx;
	struct apply_array_ctx ctx;

	if (!json || !key || !array_out || !schema)
		return;
	if (schema_stride < sizeof(bud_field_desc_t))
		schema_stride = sizeof(bud_field_desc_t);
	if (len == 0)
		len = strlen(json);
	ctx.array_out = (char *)array_out;
	ctx.elem_size = elem_size;
	ctx.count = 0;
	ctx.max_elems = max_elems;
	ctx.schema = schema;
	ctx.schema_stride = schema_stride;
	r = bud__parse_tokens(
	        json, len, stack, BUD_JSON_MAX_TOKS, &toks, &ntoks, &heap);
	if (r < 0)
		goto done;
	if (!bud__find_key(json, toks, ntoks, key, &vidx))
		goto done;
	{
		jsmntok_t *vt = &toks[vidx];
		const char *arr_json;
		size_t arr_len;
		unsigned idx;
		int n;

		if (vt->type != JSMN_ARRAY)
			goto done;
		arr_json = json + vt->start;
		arr_len = (size_t)(vt->end - vt->start);
		n = vt->size;
		idx = (unsigned)(vidx + 1);
		for (int i = 0; i < n; i++) {
			unsigned next;
			const char *elem;
			size_t elen;

			if (idx >= ntoks)
				break;
			elem = json + toks[idx].start;
			elen = (size_t)(toks[idx].end - toks[idx].start);
			if (toks[idx].type == JSMN_OBJECT ||
			    toks[idx].type == JSMN_ARRAY)
			{
				elem = json + toks[idx].start;
				elen = (size_t)(toks[idx].end -
				                toks[idx].start);
			} else if (toks[idx].type == JSMN_STRING) {
				if (toks[idx].start > 0 &&
				    json[toks[idx].start - 1] == '"' &&
				    (size_t)toks[idx].end < len &&
				    json[toks[idx].end] == '"')
				{
					elem = json + toks[idx].start - 1;
					elen += 2;
				}
			}
			(void)arr_json;
			(void)arr_len;
			apply_array_cb(elem, elen, &ctx);
			next = idx + 1;
			while (next < ntoks && toks[next].start != -1 &&
			       toks[next].start < toks[idx].end)
				next++;
			idx = next;
		}
	}
done:
	if (heap)
		free(heap);
	if (count_out)
		*count_out = ctx.count;
}

void bud_state_apply_array_len(
        const char *json, size_t len, const char *key, void *array_out,
        size_t elem_size, int *count_out, int max_elems,
        const bud_field_desc_t *schema)
{
	bud_state_apply_array_stride_len(
	        json, len, key, array_out, elem_size, count_out, max_elems,
	        schema, sizeof(bud_field_desc_t));
}

void bud_state_apply_array(
        const char *json, const char *key, void *array_out, size_t elem_size,
        int *count_out, int max_elems, const bud_field_desc_t *schema)
{
	size_t len = json ? strlen(json) : 0;
	bud_state_apply_array_len(
	        json, len, key, array_out, elem_size, count_out, max_elems,
	        schema);
}

/* ── Form-aware API action handler ──────────────────────────────────── */

/* Get the current value of a single form control from the Bud DOM tree.
   Returns a pointer to the attribute value (do not free). */
static const char *bud_control_value(bud_node *node)
{
	const char *tag;
	bud_node *child;

	if (!node || node->kind != BUD_NODE_ELEMENT)
		return NULL;
	tag = node->tag;
	if (!tag)
		return NULL;

	if (strcmp(tag, "select") == 0) {
		for (child = node->first_child; child;
		     child = child->next_sibling)
		{
			if (child->kind == BUD_NODE_ELEMENT && child->tag &&
			    strcmp(child->tag, "option") == 0 &&
			    bud_attr_find(child, "selected"))
			{
				const char *v = bud_get_attr(child, "value");
				return v ? v : "";
			}
		}
		return NULL;
	}

	if (strcmp(tag, "input") == 0) {
		const char *type = bud_get_attr(node, "type");
		if (type && strcmp(type, "checkbox") == 0)
			return bud_attr_find(node, "checked") ? "1" : "0";
		return bud_get_attr(node, "value");
	}

	return NULL;
}

/* Recursively walk node children and collect name=value pairs from every
   named form control into the query buffer.  For the event-target control,
   use event->user (the live JS value) rather than the stale Bud DOM value,
   unless a pre-handler has updated the Bud DOM attribute. */
static void bud_collect_form_query(
        bud_node *node, bud_node *target, const char *target_val, char *buf,
        size_t buf_sz, int *pos)
{
	const char *tag;
	const char *type;
	const char *name;
	const char *value;
	bud_node *child;
	int n;

	if (!node || node->kind != BUD_NODE_ELEMENT)
		return;
	tag = node->tag;
	if (!tag)
		goto recurse;

	if (strcmp(tag, "select") == 0 || strcmp(tag, "input") == 0) {
		type = strcmp(tag, "input") == 0 ? bud_get_attr(node, "type")
		                                 : NULL;
		if (type && (strcmp(type, "hidden") == 0 ||
		             strcmp(type, "submit") == 0))
			goto recurse;

		name = bud_get_attr(node, "name");
		if (!name)
			goto recurse;

		if (node == target) {
			/* Prefer the live event value (event->user) — the Bud
			 * DOM reflects render-time state and is stale for the
			 * target control (the browser, not Bud, updates the
			 * real DOM on user interaction).  Fall back to Bud DOM
			 * only when the live value is absent. */
			value = target_val;
			if (!value || !value[0])
				value = bud_control_value(node);
		} else {
			value = bud_control_value(node);
		}

		if (value && value[0]) {
			n = snprintf(
			        buf + *pos, buf_sz - *pos, "%s%s=%s",
			        *pos > 0 ? "&" : "", name, value);
			if (n > 0 && (size_t)n < buf_sz - *pos)
				*pos += n;
		}
		return;
	}

recurse:
	for (child = node->first_child; child; child = child->next_sibling)
		bud_collect_form_query(
		        child, target, target_val, buf, buf_sz, pos);
}

int bud_api_action_handler(bud_event *event)
{
	bud_node *curr = event->target;
	const char *action = NULL;

	while (curr) {
		action = bud_get_attr(curr, "data-action");
		if (action)
			break;
		curr = curr->parent;
	}

	if (!action || !bud_host_fetch_fn)
		return 0;

	/* Find enclosing <form> element */
	curr = event->target;
	while (curr && curr->kind == BUD_NODE_ELEMENT) {
		if (curr->tag && strcmp(curr->tag, "form") == 0)
			break;
		curr = curr->parent;
	}

	char url[2048];

	if (curr && curr->kind == BUD_NODE_ELEMENT && curr->tag &&
	    strcmp(curr->tag, "form") == 0)
	{
		char query[1024];
		int pos = 0;

		bud_collect_form_query(
		        curr, event->target, (const char *)event->user, query,
		        sizeof(query), &pos);

		if (pos > 0) {
			snprintf(
			        url, sizeof(url), "%s%c%s", action,
			        strchr(action, '?') ? '&' : '?', query);
			bud_host_fetch_fn(url, strlen(url), 1);

			/* Sync browser location via form's action path */
			if (bud_host_set_location_fn) {
				const char *path = bud_get_attr(curr, "action");
				if (path) {
					snprintf(
					        url, sizeof(url), "%s?%s", path,
					        query);
					bud_host_set_location_fn(
					        url, strlen(url));
				}
			}
		} else {
			bud_host_fetch_fn(action, strlen(action), 1);
		}
	} else {
		/* No parent form — original single-value behaviour */
		const char *value = (const char *)event->user;
		if (value && value[0]) {
			snprintf(
			        url, sizeof(url), "%s%c%s", action,
			        strchr(action, '?') ? '&' : '?', value);
		} else {
			snprintf(url, sizeof(url), "%s", action);
		}
		bud_host_fetch_fn(url, strlen(url), 1);
	}

	return 0;
}
