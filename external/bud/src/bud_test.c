#include "../include/bud/bud_jsx.h"
#include "../include/bud/bud.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

typedef struct test_ops {
	char buffer[1024];
	size_t len;
} test_ops;

typedef struct hydrate_expect {
	unsigned int id;
	const char *kind;
	const char *tag;
	const char *text;
} hydrate_expect;

typedef struct walk_ops {
	char buffer[2048];
	size_t len;
} walk_ops;

typedef struct lifecycle_log {
	char buffer[512];
	size_t len;
} lifecycle_log;

static int test_emit(
        void *user, const char *op, const char *a, const char *b, const char *c)
{
	test_ops *ops;
	size_t need;
	int wrote;

	(void)c;
	ops = (test_ops *)user;
	need = strlen(op) + 1;
	if (a) {
		need += strlen(a) + 1;
	}
	if (b) {
		need += strlen(b) + 1;
	}
	if (c) {
		need += strlen(c) + 1;
	}
	if (ops->len + need + 1 >= sizeof(ops->buffer)) {
		return -1;
	}

	wrote = snprintf(
	        ops->buffer + ops->len,
	        sizeof(ops->buffer) - ops->len,
	        "%s%s%s%s%s%s%s\n",
	        op,
	        a ? ":" : "",
	        a ? a : "",
	        b ? ":" : "",
	        b ? b : "",
	        c ? ":" : "",
	        c ? c : "");
	if (wrote < 0) {
		return -1;
	}

	ops->len += (size_t)wrote;
	return 0;
}

static int check(int cond, const char *msg)
{
	if (cond) {
		return 0;
	}

	fprintf(stderr, "bud test failed: %s\n", msg);
	return 1;
}

static int hydrate_lookup(
        void *user,
        unsigned int id,
        const char **kind,
        const char **tag,
        const char **text)
{
	hydrate_expect *expect;

	expect = (hydrate_expect *)user;
	while (expect->kind) {
		if (expect->id == id) {
			*kind = expect->kind;
			*tag = expect->tag;
			*text = expect->text;
			return 0;
		}
		expect++;
	}

	return -1;
}

static const char *walk_kind_name(bud_node_kind kind)
{
	switch (kind) {
	case BUD_NODE_FRAGMENT:
		return "fragment";
	case BUD_NODE_ELEMENT:
		return "element";
	case BUD_NODE_TEXT:
		return "text";
	default:
		return "unknown";
	}
}

static int walk_push(walk_ops *ops, const char *fmt, ...)
{
	va_list ap;
	int wrote;

	if (ops->len >= sizeof(ops->buffer)) {
		return -1;
	}

	va_start(ap, fmt);
	wrote = vsnprintf(
	        ops->buffer + ops->len,
	        sizeof(ops->buffer) - ops->len,
	        fmt,
	        ap);
	va_end(ap);
	if (wrote < 0) {
		return -1;
	}
	if ((size_t)wrote >= sizeof(ops->buffer) - ops->len) {
		return -1;
	}

	ops->len += (size_t)wrote;
	return 0;
}

static int walk_enter(void *user, const bud_node *node, size_t depth)
{
	walk_ops *ops;
	const char *label;
	bud_node_kind kind;

	ops = (walk_ops *)user;
	kind = bud_node_kind_of(node);
	label = NULL;
	if (kind == BUD_NODE_TEXT) {
		label = bud_node_text(node);
	} else {
		label = bud_node_tag(node);
	}
	if (!label) {
		label = "";
	}

	return walk_push(
	        ops, "enter:%zu:%s:%s\n", depth, walk_kind_name(kind), label);
}

static int walk_attr(
        void *user,
        const bud_node *node,
        size_t depth,
        size_t index,
        const char *name,
        const char *value)
{
	(void)node;
	return walk_push(
	        (walk_ops *)user,
	        "attr:%zu:%zu:%s=%s\n",
	        depth,
	        index,
	        name,
	        value);
}

static int walk_listener(
        void *user,
        const bud_node *node,
        size_t depth,
        size_t index,
        const char *event,
        int bubbles)
{
	walk_ops *ops;

	(void)node;
	ops = (walk_ops *)user;
	return walk_push(
	        ops,
	        "listener:%zu:%zu:%s:%d\n",
	        depth,
	        index,
	        event,
	        bubbles ? 1 : 0);
}

static int walk_leave(void *user, const bud_node *node, size_t depth)
{
	walk_ops *ops;
	bud_node_kind kind;
	const char *label;

	ops = (walk_ops *)user;
	kind = bud_node_kind_of(node);
	label = NULL;
	if (kind == BUD_NODE_TEXT) {
		label = bud_node_text(node);
	} else {
		label = bud_node_tag(node);
	}
	if (!label) {
		label = "";
	}

	return walk_push(
	        ops, "leave:%zu:%s:%s\n", depth, walk_kind_name(kind), label);
}

static int lifecycle_push(lifecycle_log *log, const char *fmt, ...)
{
	va_list ap;
	int wrote;

	if (log->len >= sizeof(log->buffer)) {
		return -1;
	}

	va_start(ap, fmt);
	wrote = vsnprintf(
	        log->buffer + log->len,
	        sizeof(log->buffer) - log->len,
	        fmt,
	        ap);
	va_end(ap);
	if (wrote < 0) {
		return -1;
	}
	if ((size_t)wrote >= sizeof(log->buffer) - log->len) {
		return -1;
	}

	log->len += (size_t)wrote;
	return 0;
}

static void on_mount(void *user, bud_runtime *runtime, const bud_node *node)
{
	lifecycle_log *log;

	(void)runtime;
	log = (lifecycle_log *)user;
	lifecycle_push(log, "mount:%s\n", bud_node_tag(node));
}

static void on_update(void *user, bud_runtime *runtime, const bud_node *node)
{
	lifecycle_log *log;

	(void)runtime;
	log = (lifecycle_log *)user;
	lifecycle_push(log, "update:%s\n", bud_node_tag(node));
}

static void on_unmount(void *user, bud_runtime *runtime, const bud_node *node)
{
	lifecycle_log *log;

	(void)runtime;
	log = (lifecycle_log *)user;
	lifecycle_push(log, "unmount:%s\n", bud_node_tag(node));
}

static void on_invalidate(void *user, bud_runtime *runtime)
{
	lifecycle_log *log;

	(void)runtime;
	log = (lifecycle_log *)user;
	lifecycle_push(log, "invalidate\n");
}

static int on_click_stop(bud_event *event)
{
	lifecycle_log *log;
	const char *tag;

	log = (lifecycle_log *)event->user;
	tag = event->current_target ? bud_node_tag(event->current_target) : "";
	lifecycle_push(log, "click:%s:%s\n", event->type, tag);
	bud_event_stop_propagation(event);
	return 0;
}

static int on_submit(bud_event *event)
{
	lifecycle_log *log;
	const char *tag;

	log = (lifecycle_log *)event->user;
	tag = event->current_target ? bud_node_tag(event->current_target) : "";
	lifecycle_push(log, "submit:%s:%s\n", event->type, tag);
	return 0;
}

static int test_jsx(void)
{
	bud_arg tree = lx_frag(
	        lx_el("main",
	              lx_attr("data-role", "app"),
	              lx_el("h1", lx_text("Hello")),
	              lx_el("button",
	                    lx_on("click", 1),
	                    lx_on("submit", 0),
	                    lx_text("Click me")),
	              lx_el("p", lx_text("C DOM runtime"))));
	char *html = bud_render_html(tree.data.node);
	int rc = 0;
	if (!html ||
	    !strstr(html,
	            "<main data-role=\"app\"><h1>Hello</h1><button>Click "
	            "me</button><p>C DOM runtime</p></main>"))
	{
		fprintf(stderr, "JSX test failed: %s\n", html ? html : "NULL");
		rc = 1;
	}
	bud_free_string(html);
	bud_free(tree.data.node);
	return rc;
}

static int test_bool_attr(void)
{
	bud_node *el;
	char *html;
	int rc;

	rc = 0;
	el = bud_element("input");
	if (!el) {
		return 1;
	}

	rc |= bud_set_bool_attr(el, "checked");
	rc |= bud_set_attr(el, "name", "agree");
	if (rc != 0) {
		fprintf(stderr, "bool attr setup failed\n");
		bud_free(el);
		return 1;
	}

	html = bud_render_html(el);
	if (!html || !strstr(html, "checked") || strstr(html, "checked=\"\"")) {
		fprintf(stderr,
		        "bool attr test failed: %s\n",
		        html ? html : "NULL");
		bud_free_string(html);
		bud_free(el);
		return 1;
	}

	bud_free_string(html);
	bud_free(el);
	return 0;
}

static int test_raw_html(void)
{
	bud_node *el;
	bud_node *raw;
	char *html;
	int rc;

	rc = 0;
	el = bud_element("div");
	raw = bud_raw("<b>bold</b> & <i>italic</i>");
	if (!el || !raw) {
		bud_free(el);
		bud_free(raw);
		return 1;
	}

	rc = bud_append(el, raw);
	if (rc != 0) {
		fprintf(stderr, "raw html append failed\n");
		bud_free(el);
		return 1;
	}

	html = bud_render_html(el);
	if (!html || strstr(html, "&lt;") || !strstr(html, "<b>bold</b>")) {
		fprintf(stderr,
		        "raw html test failed: %s\n",
		        html ? html : "NULL");
		bud_free_string(html);
		bud_free(el);
		return 1;
	}

	bud_free_string(html);
	bud_free(el);
	return 0;
}

static int test_get_attr(void)
{
	bud_node *el;
	const char *val;
	int rc;

	rc = 0;
	el = bud_element("div");
	if (!el) {
		return 1;
	}

	rc = bud_set_attr(el, "data-x", "hello");
	if (rc != 0) {
		fprintf(stderr, "get attr setup failed\n");
		bud_free(el);
		return 1;
	}

	val = bud_get_attr(el, "data-x");
	if (!val || strcmp(val, "hello") != 0) {
		fprintf(stderr,
		        "get attr value failed: %s\n",
		        val ? val : "NULL");
		bud_free(el);
		return 1;
	}

	val = bud_get_attr(el, "nonexistent");
	if (val) {
		fprintf(stderr, "get attr nonexistent should be NULL\n");
		bud_free(el);
		return 1;
	}

	bud_free(el);
	return 0;
}

static int test_class_helpers(void)
{
	bud_node *el;
	const char *val;
	int rc;

	rc = 0;
	el = bud_element("div");
	if (!el) {
		return 1;
	}

	rc = bud_add_class(el, "foo");
	if (rc != 0) {
		fprintf(stderr, "add class failed\n");
		bud_free(el);
		return 1;
	}

	val = bud_get_attr(el, "class");
	if (!val || strcmp(val, "foo") != 0) {
		fprintf(stderr, "add class result: %s\n", val ? val : "NULL");
		bud_free(el);
		return 1;
	}

	rc = bud_add_class(el, "foo");
	if (rc != 0) {
		fprintf(stderr, "add class duplicate failed\n");
		bud_free(el);
		return 1;
	}

	val = bud_get_attr(el, "class");
	if (!val || strcmp(val, "foo") != 0) {
		fprintf(stderr,
		        "add class duplicate result: %s\n",
		        val ? val : "NULL");
		bud_free(el);
		return 1;
	}

	rc = bud_add_class(el, "bar");
	if (rc != 0) {
		fprintf(stderr, "add class second failed\n");
		bud_free(el);
		return 1;
	}

	val = bud_get_attr(el, "class");
	if (!val || strcmp(val, "foo bar") != 0) {
		fprintf(stderr,
		        "add class second result: %s\n",
		        val ? val : "NULL");
		bud_free(el);
		return 1;
	}

	rc = bud_remove_class(el, "foo");
	if (rc != 0) {
		fprintf(stderr, "remove class failed\n");
		bud_free(el);
		return 1;
	}

	val = bud_get_attr(el, "class");
	if (!val || strcmp(val, "bar") != 0) {
		fprintf(stderr,
		        "remove class result: %s\n",
		        val ? val : "NULL");
		bud_free(el);
		return 1;
	}

	rc = bud_remove_class(el, "foo");
	if (rc != 0) {
		fprintf(stderr, "remove class absent failed\n");
		bud_free(el);
		return 1;
	}

	rc = bud_toggle_class(el, "foo");
	if (rc != 0) {
		fprintf(stderr, "toggle class add failed\n");
		bud_free(el);
		return 1;
	}

	val = bud_get_attr(el, "class");
	if (!val || strcmp(val, "bar foo") != 0) {
		fprintf(stderr,
		        "toggle class add result: %s\n",
		        val ? val : "NULL");
		bud_free(el);
		return 1;
	}

	rc = bud_toggle_class(el, "bar");
	if (rc != 0) {
		fprintf(stderr, "toggle class remove failed\n");
		bud_free(el);
		return 1;
	}

	val = bud_get_attr(el, "class");
	if (!val || strcmp(val, "foo") != 0) {
		fprintf(stderr,
		        "toggle class remove result: %s\n",
		        val ? val : "NULL");
		bud_free(el);
		return 1;
	}

	bud_free(el);
	return 0;
}

static int test_detach(void)
{
	bud_node *parent;
	bud_node *child;
	int rc;

	rc = 0;
	parent = bud_element("div");
	child = bud_element("span");
	if (!parent || !child) {
		bud_free(parent);
		bud_free(child);
		return 1;
	}

	rc = bud_append(parent, child);
	if (rc != 0) {
		fprintf(stderr, "detach setup append failed\n");
		bud_free(parent);
		return 1;
	}

	if (bud_node_child_count(parent) != 1) {
		fprintf(stderr, "detach setup child count\n");
		bud_free(parent);
		return 1;
	}

	rc = bud_detach(child);
	if (rc != 0) {
		fprintf(stderr, "detach failed\n");
		bud_free(parent);
		bud_free(child);
		return 1;
	}

	if (bud_node_child_count(parent) != 0) {
		fprintf(stderr, "detach parent child count\n");
		bud_free(parent);
		bud_free(child);
		return 1;
	}

	rc = bud_detach(child);
	if (rc == 0) {
		fprintf(stderr, "detach orphan should fail\n");
		bud_free(parent);
		bud_free(child);
		return 1;
	}

	bud_free(parent);
	bud_free(child);
	return 0;
}

int main(void)
{

	if (test_jsx() != 0)
		return 1;
	if (test_bool_attr() != 0)
		return 1;
	if (test_raw_html() != 0)
		return 1;
	if (test_get_attr() != 0)
		return 1;
	if (test_class_helpers() != 0)
		return 1;
	if (test_detach() != 0)
		return 1;
	bud_node *root;
	bud_node *main_el;
	bud_node *h1;
	bud_node *button;
	bud_node *p;
	bud_node *text;
	bud_runtime *runtime;
	char *html;
	char *hydrated_html;
	test_ops ops;
	test_ops walk_stream;
	test_ops patch_stream;
	walk_ops walk;
	lifecycle_log lifecycle;
	hydrate_expect expect[] = { { 0, "fragment", NULL, NULL },
		                    { 1, "element", "main", NULL },
		                    { 2, "element", "h1", NULL },
		                    { 3, "text", NULL, "Hello & <world>" },
		                    { 4, "element", "button", NULL },
		                    { 5, "text", NULL, "Click me" },
		                    { 6, "element", "p", NULL },
		                    { 7, "text", NULL, "C DOM runtime" },
		                    { 0, NULL, NULL, NULL } };
	int rc;

	root = NULL;
	main_el = NULL;
	h1 = NULL;
	button = NULL;
	p = NULL;
	text = NULL;
	runtime = NULL;
	html = NULL;
	hydrated_html = NULL;

	memset(&ops, 0, sizeof(ops));
	memset(&walk_stream, 0, sizeof(walk_stream));
	memset(&patch_stream, 0, sizeof(patch_stream));
	memset(&walk, 0, sizeof(walk));
	memset(&lifecycle, 0, sizeof(lifecycle));

	root = bud_fragment();
	main_el = bud_element("main");
	h1 = bud_element("h1");
	button = bud_element("button");
	p = bud_element("p");
	text = bud_text("C DOM runtime");

	rc = check(root != NULL, "fragment alloc");
	if (rc != 0) {
		goto cleanup;
	}
	rc = check(main_el != NULL, "element alloc");
	if (rc != 0) {
		goto cleanup;
	}
	rc = check(h1 != NULL, "h1 alloc");
	if (rc != 0) {
		goto cleanup;
	}
	rc = check(button != NULL, "button alloc");
	if (rc != 0) {
		goto cleanup;
	}
	rc = check(p != NULL, "p alloc");
	if (rc != 0) {
		goto cleanup;
	}
	rc = check(text != NULL, "text alloc");
	if (rc != 0) {
		goto cleanup;
	}

	rc = bud_set_attr(main_el, "data-role", "app");
	rc |= bud_on(button, "click", 1);
	rc |= bud_on(button, "keydown", 0);
	rc |= bud_on(button, "submit", 0);
	rc |= bud_bind(button, "click", 1, on_click_stop);
	rc |= bud_bind(button, "submit", 0, on_submit);
	rc |= bud_bind(main_el, "submit", 1, on_submit);
	rc |= bud_set_lifecycle(
	        main_el, on_mount, on_update, on_unmount, &lifecycle);
	if (rc != 0) {
		fprintf(stderr, "bud test failed: set attr/listener\n");
		goto cleanup;
	}

	rc = bud_append(root, main_el);
	rc |= bud_append(main_el, h1);
	rc |= bud_append(main_el, button);
	rc |= bud_append(main_el, p);
	rc |= bud_append(h1, bud_text("Hello & <world>"));
	rc |= bud_append(button, bud_text("Click me"));
	rc |= bud_append(p, text);
	if (rc != 0) {
		fprintf(stderr, "bud test failed: append\n");
		goto cleanup;
	}

	runtime = bud_runtime_new(root);
	rc = check(runtime != NULL, "runtime alloc");
	if (rc != 0) {
		goto cleanup;
	}
	rc = check(bud_runtime_root(runtime) == root, "runtime root");
	if (rc != 0) {
		goto cleanup;
	}
	rc = bud_runtime_set_invalidate(runtime, on_invalidate, &lifecycle);
	if (rc != 0) {
		fprintf(stderr, "bud test failed: runtime invalidate\n");
		goto cleanup;
	}
	rc = bud_runtime_mount(runtime);
	if (rc != 0) {
		fprintf(stderr, "bud test failed: runtime mount\n");
		goto cleanup;
	}
	rc = bud_runtime_update(runtime);
	if (rc != 0) {
		fprintf(stderr, "bud test failed: runtime update\n");
		goto cleanup;
	}
	rc = bud_runtime_mark_dirty(runtime);
	if (rc != 0) {
		fprintf(stderr, "bud test failed: runtime dirty\n");
		goto cleanup;
	}
	rc = check(bud_runtime_is_dirty(runtime) == 1, "runtime dirty flag");
	if (rc != 0) {
		goto cleanup;
	}
	rc = bud_runtime_flush(runtime);
	if (rc != 0) {
		fprintf(stderr, "bud test failed: runtime flush\n");
		goto cleanup;
	}
	rc = check(bud_runtime_is_dirty(runtime) == 0, "runtime clean");
	if (rc != 0) {
		goto cleanup;
	}
	rc = bud_runtime_dispatch(runtime, button, "click", &lifecycle);
	if (rc != 0) {
		fprintf(stderr, "bud test failed: runtime dispatch click\n");
		goto cleanup;
	}
	rc = bud_runtime_dispatch(runtime, button, "submit", &lifecycle);
	if (rc != 0) {
		fprintf(stderr, "bud test failed: runtime dispatch submit\n");
		goto cleanup;
	}
	rc = bud_runtime_unmount(runtime);
	if (rc != 0) {
		fprintf(stderr, "bud test failed: runtime unmount\n");
		goto cleanup;
	}

	rc = check(bud_node_child_count(root) == 1, "root child count");
	if (rc != 0) {
		goto cleanup;
	}
	rc = check(bud_node_child_count(main_el) == 3, "main child count");
	if (rc != 0) {
		goto cleanup;
	}
	rc = check(bud_node_child(main_el, 1) == button, "child accessor");
	if (rc != 0) {
		goto cleanup;
	}
	rc = check(bud_node_attr_count(main_el) == 1, "attr count");
	if (rc != 0) {
		goto cleanup;
	}
	rc =
	        check(strcmp(bud_node_attr_name(main_el, 0), "data-role") == 0,
	              "attr name");
	if (rc != 0) {
		goto cleanup;
	}
	rc =
	        check(strcmp(bud_node_attr_value(main_el, 0), "app") == 0,
	              "attr value");
	if (rc != 0) {
		goto cleanup;
	}
	rc = check(bud_node_listener_count(button) == 3, "listener count");
	if (rc != 0) {
		goto cleanup;
	}
	rc =
	        check(strcmp(bud_node_listener_event(button, 0), "click") == 0,
	              "listener event 0");
	if (rc != 0) {
		goto cleanup;
	}
	rc =
	        check(bud_node_listener_bubbles(button, 0) == 1,
	              "listener bubbles 0");
	if (rc != 0) {
		goto cleanup;
	}
	rc = check(
	        strcmp(bud_node_listener_event(button, 1), "keydown") == 0,
	        "listener event 1");
	if (rc != 0) {
		goto cleanup;
	}
	rc =
	        check(bud_node_listener_bubbles(button, 1) == 0,
	              "listener bubbles 1");
	if (rc != 0) {
		goto cleanup;
	}
	rc =
	        check(strcmp(bud_node_listener_event(button, 2), "submit") == 0,
	              "listener event 2");
	if (rc != 0) {
		goto cleanup;
	}
	rc =
	        check(bud_node_listener_bubbles(button, 2) == 0,
	              "listener bubbles 2");
	if (rc != 0) {
		goto cleanup;
	}

	{
		bud_walk_ops visitor;

		visitor.enter_node = walk_enter;
		visitor.attr = walk_attr;
		visitor.listener = walk_listener;
		visitor.leave_node = walk_leave;
		visitor.user = &walk;
		rc = bud_walk(root, &visitor);
	}
	if (rc != 0) {
		fprintf(stderr, "bud test failed: walk\n");
		goto cleanup;
	}
	rc =
	        check(strstr(walk.buffer, "enter:0:fragment:") != NULL,
	              "walk enter fragment");
	if (rc != 0) {
		fprintf(stderr, "walk: %s\n", walk.buffer);
		goto cleanup;
	}
	rc =
	        check(strstr(walk.buffer, "attr:1:0:data-role=app") != NULL,
	              "walk attr");
	if (rc != 0) {
		fprintf(stderr, "walk: %s\n", walk.buffer);
		goto cleanup;
	}
	rc =
	        check(strstr(walk.buffer, "listener:2:0:click:1") != NULL,
	              "walk listener");
	if (rc != 0) {
		fprintf(stderr, "walk: %s\n", walk.buffer);
		goto cleanup;
	}
	rc =
	        check(strstr(walk.buffer, "leave:0:fragment:") != NULL,
	              "walk leave fragment");
	if (rc != 0) {
		fprintf(stderr, "walk: %s\n", walk.buffer);
		goto cleanup;
	}

	rc = bud_render_walk_ops(root, test_emit, &walk_stream);
	if (rc != 0) {
		fprintf(stderr, "bud test failed: render walk ops\n");
		goto cleanup;
	}
	rc = check(
	        strstr(walk_stream.buffer, "walk-enter:0:fragment:0") != NULL,
	        "walk stream enter fragment");
	if (rc != 0) {
		fprintf(stderr, "walk stream: %s\n", walk_stream.buffer);
		goto cleanup;
	}
	rc = check(
	        strstr(walk_stream.buffer, "walk-listener:4:click:1") != NULL,
	        "walk stream listener");
	if (rc != 0) {
		fprintf(stderr, "walk stream: %s\n", walk_stream.buffer);
		goto cleanup;
	}
	rc = check(
	        strstr(walk_stream.buffer, "walk-text:5:3:Click me") != NULL,
	        "walk stream text");
	if (rc != 0) {
		fprintf(stderr, "walk stream: %s\n", walk_stream.buffer);
		goto cleanup;
	}

	html = bud_render_html(root);
	rc = check(html != NULL, "render html");
	if (rc != 0) {
		goto cleanup;
	}
	rc =
	        check(strcmp(html,
	                     "<main data-role=\"app\"><h1>Hello &amp; "
	                     "&lt;world&gt;</h1><button>Click me</button><p>C "
	                     "DOM runtime</p></main>") == 0,
	              "html output");
	if (rc != 0) {
		fprintf(stderr, "got: %s\n", html);
		goto cleanup;
	}

	hydrated_html = bud_render_hydrated_html(root);
	rc = check(hydrated_html != NULL, "render hydrated html");
	if (rc != 0) {
		goto cleanup;
	}
	rc =
	        check(strstr(hydrated_html, "data-bud-id=\"1\"") != NULL,
	              "hydrated id");
	if (rc != 0) {
		fprintf(stderr, "got hydrated: %s\n", hydrated_html);
		goto cleanup;
	}
	rc = check(
	        strstr(hydrated_html,
	               "data-bud-on=\"click:1,keydown:0,submit:0\"") != NULL,
	        "hydrated listener attr");
	if (rc != 0) {
		fprintf(stderr, "got hydrated: %s\n", hydrated_html);
		goto cleanup;
	}
	rc =
	        check(strstr(hydrated_html, "<!--bud-text:3-->") != NULL,
	              "hydrated text marker");
	if (rc != 0) {
		fprintf(stderr, "got hydrated: %s\n", hydrated_html);
		goto cleanup;
	}

	rc = bud_render_hydration_ops(root, test_emit, &ops);
	if (rc != 0) {
		fprintf(stderr, "bud test failed: render hydration ops\n");
		goto cleanup;
	}
	rc =
	        check(strstr(ops.buffer, "listener:click:1:4") != NULL,
	              "ops listener");
	if (rc != 0) {
		fprintf(stderr, "ops: %s\n", ops.buffer);
		goto cleanup;
	}
	rc =
	        check(strstr(ops.buffer, "element-open:button:4") != NULL,
	              "ops open with id");
	if (rc != 0) {
		fprintf(stderr, "ops: %s\n", ops.buffer);
		goto cleanup;
	}

	rc = bud_render_patch_ops(root, test_emit, &patch_stream);
	if (rc != 0) {
		fprintf(stderr, "bud test failed: render patch ops\n");
		goto cleanup;
	}
	rc =
	        check(strstr(patch_stream.buffer, "patch-clear:0") != NULL,
	              "patch clear");
	if (rc != 0) {
		fprintf(stderr, "patch: %s\n", patch_stream.buffer);
		goto cleanup;
	}
	rc =
	        check(strstr(patch_stream.buffer, "patch-open:main:1") != NULL,
	              "patch open element");
	if (rc != 0) {
		fprintf(stderr, "patch: %s\n", patch_stream.buffer);
		goto cleanup;
	}
	rc =
	        check(strstr(patch_stream.buffer,
	                     "patch-attr:1:data-role:app") != NULL,
	              "patch attr");
	if (rc != 0) {
		fprintf(stderr, "patch: %s\n", patch_stream.buffer);
		goto cleanup;
	}
	rc = check(
	        strstr(patch_stream.buffer, "patch-listener:4:click:1") != NULL,
	        "patch listener");
	if (rc != 0) {
		fprintf(stderr, "patch: %s\n", patch_stream.buffer);
		goto cleanup;
	}
	rc =
	        check(strstr(patch_stream.buffer,
	                     "patch-text:C DOM runtime:7") != NULL,
	              "patch text");
	if (rc != 0) {
		fprintf(stderr, "patch: %s\n", patch_stream.buffer);
		goto cleanup;
	}

	rc = bud_hydrate(root, hydrate_lookup, expect);
	if (rc != 0) {
		fprintf(stderr, "bud test failed: hydrate validation\n");
		goto cleanup;
	}

	rc =
	        check(strstr(lifecycle.buffer, "mount:main") != NULL,
	              "lifecycle mount");
	if (rc != 0) {
		fprintf(stderr, "lifecycle: %s\n", lifecycle.buffer);
		goto cleanup;
	}
	rc =
	        check(strstr(lifecycle.buffer, "update:main") != NULL,
	              "lifecycle update");
	if (rc != 0) {
		fprintf(stderr, "lifecycle: %s\n", lifecycle.buffer);
		goto cleanup;
	}
	rc =
	        check(strstr(lifecycle.buffer, "invalidate") != NULL,
	              "runtime invalidate");
	if (rc != 0) {
		fprintf(stderr, "lifecycle: %s\n", lifecycle.buffer);
		goto cleanup;
	}
	rc =
	        check(strstr(lifecycle.buffer, "click:click:button") != NULL,
	              "event click");
	if (rc != 0) {
		fprintf(stderr, "lifecycle: %s\n", lifecycle.buffer);
		goto cleanup;
	}
	rc =
	        check(strstr(lifecycle.buffer, "submit:submit:button") != NULL,
	              "event submit target");
	if (rc != 0) {
		fprintf(stderr, "lifecycle: %s\n", lifecycle.buffer);
		goto cleanup;
	}
	rc =
	        check(strstr(lifecycle.buffer, "submit:submit:main") != NULL,
	              "event submit bubble");
	if (rc != 0) {
		fprintf(stderr, "lifecycle: %s\n", lifecycle.buffer);
		goto cleanup;
	}
	rc =
	        check(strstr(lifecycle.buffer, "unmount:main") != NULL,
	              "lifecycle unmount");
	if (rc != 0) {
		fprintf(stderr, "lifecycle: %s\n", lifecycle.buffer);
		goto cleanup;
	}

	rc = 0;

cleanup:
	bud_free_string(html);
	bud_free_string(hydrated_html);
	bud_runtime_free(runtime);
	bud_free(root);
	return rc;
}
