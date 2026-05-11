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

	if (!text) {
		return 0;
	}

	for (p = text; *p; p++) {
		switch (*p) {
		case '&':
			if (bud_buf_append(buf, "&amp;") != 0) {
				return -1;
			}
			break;
		case '<':
			if (bud_buf_append(buf, "&lt;") != 0) {
				return -1;
			}
			break;
		case '>':
			if (bud_buf_append(buf, "&gt;") != 0) {
				return -1;
			}
			break;
		case '"':
			if (attr) {
				if (bud_buf_append(buf, "&quot;") != 0) {
					return -1;
				}
			} else {
				if (bud_buf_append_char(buf, *p) != 0) {
					return -1;
				}
			}
			break;
		case '\'':
			if (attr) {
				if (bud_buf_append(buf, "&#39;") != 0) {
					return -1;
				}
			} else {
				if (bud_buf_append_char(buf, *p) != 0) {
					return -1;
				}
			}
			break;
		default:
			if (bud_buf_append_char(buf, *p) != 0) {
				return -1;
			}
			break;
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
		        mode_buf,
		        sizeof(mode_buf),
		        "%d",
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
			if (emit(user,
			         "attr",
			         attr->name,
			         attr->value ? attr->value : "",
			         id_buf) != 0)
			{
				return -1;
			}
		}
		for (listener = node->listeners; listener;
		     listener = listener->next)
		{
			snprintf(
			        mode_buf,
			        sizeof(mode_buf),
			        "%d",
			        listener->bubbles ? 1 : 0);
			if (emit(user,
			         "listener",
			         listener->event,
			         mode_buf,
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

bud_node *
bud_component_render(const bud_component *component, const void *props)
{
	if (!component || !component->render) {
		return NULL;
	}

	return component->render(component->ctx, props);
}

int bud_set_attr(bud_node *node, const char *name, const char *value)
{
	bud_attr *attr;
	bud_attr *tail;
	char *name_copy;
	char *value_copy;

	if (!node || node->kind != BUD_NODE_ELEMENT || !name) {
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
        bud_node *node,
        const char *event,
        int bubbles,
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
        bud_node *node,
        bud_lifecycle_fn on_mount,
        bud_lifecycle_fn on_update,
        bud_lifecycle_fn on_unmount,
        void *user)
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
			if (emit(user,
			         "patch-attr",
			         id_buf,
			         attr->name,
			         attr->value ? attr->value : "") != 0)
			{
				return -1;
			}
		}
		for (listener = node->listeners; listener;
		     listener = listener->next)
		{
			snprintf(
			        bubbles_buf,
			        sizeof(bubbles_buf),
			        "%d",
			        listener->bubbles ? 1 : 0);
			if (emit(user,
			         "patch-listener",
			         id_buf,
			         listener->event,
			         bubbles_buf) != 0)
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

	if (emit(user,
	         "walk-enter",
	         depth_buf,
	         bud_kind_name(node->kind),
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
			if (emit(user,
			         "walk-attr",
			         id_buf,
			         attr->name,
			         attr->value) != 0)
			{
				return -1;
			}
		}
		for (listener = node->listeners; listener;
		     listener = listener->next)
		{
			snprintf(
			        bubbles_buf,
			        sizeof(bubbles_buf),
			        "%d",
			        listener->bubbles ? 1 : 0);
			if (emit(user,
			         "walk-listener",
			         id_buf,
			         listener->event,
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

	return emit(user,
	            "walk-leave",
	            depth_buf,
	            bud_kind_name(node->kind),
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
			            ops->user,
			            node,
			            depth,
			            index,
			            attr->name,
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
			            ops->user,
			            node,
			            depth,
			            index,
			            listener->event,
			            listener->bubbles) != 0)
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

#ifdef __wasm__
__attribute__((import_module("env"), import_name("bud_host_log"))) void
bud_dbg_log(const char *msg, size_t len);
#define DBG_LOG(msg)                                                           \
	do {                                                                   \
		const char *_m = (msg);                                        \
		bud_dbg_log(_m, strlen(_m));                                   \
	} while (0)
static void dbg_printf(const char *fmt, ...)
{
	char buf[256];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	bud_dbg_log(buf, strlen(buf));
}
#else
#define DBG_LOG(msg)                                                           \
	do {                                                                   \
	} while (0)
static void dbg_printf(const char *fmt, ...)
{
	(void)fmt;
}
#endif

static int bud_dispatch_event_node(bud_node *node, bud_event *event)
{
	bud_listener *listener;
	int allow_bubble;

	if (!node) {
		return 0;
	}

	event->current_target = node;
	allow_bubble = node != event->target;
	dbg_printf(
	        "DEN: node=%d listeners=%p\n",
	        node->id,
	        (void *)node->listeners);
	for (listener = node->listeners; listener; listener = listener->next) {
		dbg_printf(
		        "DEN: event=%s type=%s handler=%p\n",
		        listener->event,
		        event->type,
		        (void *)(intptr_t)listener->handler);
		if (strcmp(listener->event, event->type) != 0) {
			continue;
		}
		if (allow_bubble && !listener->bubbles) {
			dbg_printf("DEN: skip bubble for node=%d\n", node->id);
			continue;
		}
		if (listener->handler) {
			dbg_printf("DEN: calling handler node=%d\n", node->id);
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
        bud_runtime *runtime,
        bud_node *target,
        const char *event,
        void *event_user)
{
	bud_event ev;
	bud_node *current;

	if (!runtime || !target || !event) {
		return -1;
	}

	memset(&ev, 0, sizeof(ev));
	ev.type = event;
	ev.target = target;
	ev.current_target = target;
	ev.user = event_user;
	ev.bubbles = 1;

	current = target;
	while (current) {
		if (bud_dispatch_event_node(current, &ev) != 0) {
			return -1;
		}
		if (!ev.bubbles || ev.stopped) {
			break;
		}
		current = current->parent;
	}

	return 0;
}

void bud_free(bud_node *node)
{
	bud_node_free_chain(node);
}

void bud_free_string(char *value)
{
	free(value);
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
			        node,
			        args[i].data.attr.name,
			        args[i].data.attr.value);
			break;
		case BUD_ARG_EVENT:
			bud_on(node,
			       args[i].data.ev.event,
			       args[i].data.ev.bubbles);
			break;
		case BUD_ARG_BIND:
			bud_bind(
			        node,
			        args[i].data.bind.event,
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

/* ── JSON field extraction helpers (no json-c dependency) ── */

static void bud_json_unescape(const char **psrc, char **pdst, size_t *plen)
{
	const char *s = *psrc;
	char *d = *pdst;
	size_t remaining = *plen;

	while (*s && remaining > 1) {
		if (*s == '\\' && *(s + 1) == '"') {
			*d++ = '"';
			s += 2;
		} else if (*s == '\\' && *(s + 1) == '\\') {
			*d++ = '\\';
			s += 2;
		} else if (*s == '\\' && *(s + 1) == '/') {
			*d++ = '/';
			s += 2;
		} else if (*s == '\\' && *(s + 1) == 'n') {
			*d++ = '\n';
			s += 2;
		} else if (*s == '\\' && *(s + 1) == 'r') {
			*d++ = '\r';
			s += 2;
		} else if (*s == '\\' && *(s + 1) == 't') {
			*d++ = '\t';
			s += 2;
		} else if (*s == '"') {
			break;
		} else {
			*d++ = *s++;
		}
		remaining--;
	}
	*d = '\0';
	*psrc = s;
	*pdst = d;
	*plen = remaining;
}

void bud_json_str(const char *json, const char *key, char *out, size_t out_size)
{
	const char *p;
	char pattern[64];
	const char *val;

	out[0] = '\0';
	if (!json || !key || out_size == 0)
		return;

	snprintf(pattern, sizeof(pattern), "\"%s\":", key);
	p = strstr(json, pattern);
	if (!p)
		return;
	p += strlen(pattern);
	while (*p == ' ' || *p == '\t')
		p++;
	if (*p == '"') {
		p++;
		val = p;
		{
			char *dst = out;
			size_t rem = out_size - 1;
			bud_json_unescape(&val, &dst, &rem);
		}
		return;
	}
	/* Non-string value — copy until comma/brace/bracket */
	{
		size_t i = 0;
		while (*p && i < out_size - 1 && *p != ',' && *p != '}' &&
		       *p != ']')
		{
			if (*p == ' ')
				p++;
			else
				out[i++] = *p++;
		}
		out[i] = '\0';
	}
}

int bud_json_int(const char *json, const char *key, int default_val)
{
	const char *p;
	char pattern[64];

	if (!json || !key)
		return default_val;

	snprintf(pattern, sizeof(pattern), "\"%s\":", key);
	p = strstr(json, pattern);
	if (!p)
		return default_val;
	p += strlen(pattern);
	while (*p == ' ' || *p == '\t')
		p++;
	if (*p == '"')
		p++;
	while (*p && (*p < '0' || *p > '9') && *p != '-')
		p++;
	if (!*p)
		return default_val;
	return atoi(p);
}

void bud_json_data(const char *json, char *out, size_t out_size)
{
	const char *key;
	const char *val;

	out[0] = '\0';
	if (!json || out_size == 0)
		return;

	key = strstr(json, "\"data\"");
	if (!key)
		return;
	key += 6;
	while (*key == ':' || *key == ' ' || *key == '\t')
		key++;
	if (*key == '"')
		key++;
	val = key;
	{
		char *dst = out;
		size_t rem = out_size - 1;
		bud_json_unescape(&val, &dst, &rem);
	}
}

/* ── Table-driven state ── */

void bud_state_apply(
        void *state, const bud_field_desc_t *fields, const char *json)
{
	const bud_field_desc_t *f;

	if (!state || !fields || !json)
		return;

	for (f = fields; f->key; f++) {
		if (f->kind == 1)
			continue;
		if (f->is_int) {
			int *dest = (int *)((char *)state + f->offset);
			*dest = bud_json_int(json, f->key, 0);
		} else if (f->size > 0) {
			char *dest = (char *)state + f->offset;
			bud_json_str(json, f->key, dest, f->size);
		}
	}
}
