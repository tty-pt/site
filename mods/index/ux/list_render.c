#ifndef INDEX_UX_LIST_RENDER_C
#define INDEX_UX_LIST_RENDER_C

bud_node *list_render(const list_state_t *state)
{
	bud_node *inner;

	if (!state)
		return NULL;
	inner = state->nids == 0 ? idx_list_empty_layout(state)
	                         : idx_list_layout(state);
	if (!inner)
		return NULL;
	return bud_tpl("<div id='bud-root'>%node</div>", inner);
}

#endif
