#include "bud/bud.h"
#include "bud/bud_jsx.h"
#include <stdio.h>

typedef struct {
	int counter;
} app_state_t;

static app_state_t app_state = { 0 };

extern void wasm_update(void);
extern void wasm_flush(void);

static int on_increment(bud_event *event)
{
	(void)event;
	app_state.counter++;
	wasm_update();
	wasm_flush();
	return 0;
}

bud_node *bud_app_render(void)
{
	char count_str[32];
	snprintf(count_str, sizeof(count_str), "Count: %d", app_state.counter);

	return lx_el("div",
	             lx_attr("class", "card"),
	             lx_el("h1", lx_text("Interactive C WASM Demo")),
	             lx_el("p",
	                   lx_text("This page is hydrated by a C WASM "
	                           "module.")),
	             lx_el("button",
	                   lx_bind("click", 1, on_increment),
	                   lx_text(count_str)))
	        .data.node;
}
