static bud_node *
software_render_detail_body(
	const software_cache_t *meta,
	const char *rendered_html,
	const char *owner,
	const char *id)
{
	(void)id;

	/* Badges */
	bud_node *version_badge = NULL;
	if (meta && meta->version[0]) {
		char v_buf[80];
		snprintf(v_buf, sizeof(v_buf), "v%s", meta->version);
		version_badge = bud_tpl(
			"<span class='software-badge software-badge-version'>%s</span>",
			v_buf);
	}

	bud_node *category_badge = NULL;
	if (meta && meta->category[0]) {
		category_badge = bud_tpl(
			"<span class='software-badge software-badge-category'>%s</span>",
			meta->category);
	}

	bud_node *license_badge = NULL;
	if (meta && meta->license[0]) {
		license_badge = bud_tpl(
			"<span class='software-badge software-badge-license'>%s</span>",
			meta->license);
	}

	bud_node *badges_row = bud_tpl(
		"<div class='software-badges'>%node %node %node</div>",
		version_badge, category_badge, license_badge);

	/* Summary */
	bud_node *summary_node = NULL;
	if (meta && meta->summary[0]) {
		summary_node = bud_tpl(
			"<p class='software-summary'>%s</p>",
			meta->summary);
	}

	/* GitHub Action Button & Clone Box */
	bud_node *github_btn = NULL;
	bud_node *clone_box = NULL;
	if (meta && meta->repo_url[0]) {
		github_btn = bud_tpl(
			"<a href='%s' target='_blank' rel='noopener noreferrer' class='btn' "
			"style='display:inline-flex;align-items:center;gap:0.4rem;padding:0.35rem 0.75rem;font-size:0.875rem;text-decoration:none;'>"
			"<svg width='16' height='16' viewBox='0 0 16 16' style='width:16px;height:16px;min-width:16px;max-width:16px;fill:currentColor;flex-shrink:0;' aria-hidden='true'>"
			"<path d='M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27.68 0 1.36.09 2 .27 1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.013 8.013 0 0016 8c0-4.42-3.58-8-8-8z'/>"
			"</svg>"
			"<span>GitHub</span></a>",
			meta->repo_url);

		char clone_buf[600];
		snprintf(clone_buf, sizeof(clone_buf), "git clone %s", meta->repo_url);
		clone_box = bud_tpl(
			"<div class='software-clone-box'>"
			"<span class='software-clone-prompt' aria-hidden='true'>$</span>"
			"<code class='software-clone-cmd'>%s</code>"
			"</div>",
			clone_buf);
	}

	bud_node *actions_row = bud_tpl(
		"<div class='software-actions'>%node %node</div>",
		github_btn, clone_box);

	/* Markdown Content Body */
	bud_node *body_node = NULL;
	if (rendered_html && rendered_html[0]) {
		body_node = bud_tpl(
			"<div class='software-readme'>%raw</div>",
			rendered_html);
	} else {
		body_node = bud_tpl(
			"<div class='software-readme text-muted' style='text-align:center;'>"
			"<p>No README documentation uploaded yet.</p></div>");
	}

	/* Owner / Maintainer */
	bud_node *owner_node = NULL;
	if (owner && owner[0]) {
		owner_node = bud_tpl(
			"<div class='software-maintainer'>Maintained by: <strong>%s</strong></div>",
			owner);
	}

	return bud_tpl(
		"<div class='software-detail-container'>"
		"%node %node %node %node %node"
		"</div>",
		badges_row, summary_node, actions_row, body_node, owner_node);
}

