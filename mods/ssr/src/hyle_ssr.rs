use dioxus::prelude::*;
use dioxus_ssr::render_element;
use indexmap::IndexMap;

use hyle::{Column, Row, Source};
use hyle_dioxus::HyleDataState;
use hyle_dioxus::{
	HyleConfig, HyleFiltersState, HyleListState, HyleSourceState, UseFiltersOptions,
	use_context_provider, use_filters, use_list_with_filters,
};
use hyle_dioxus_native::{
	item_to_source, use_static_adapter,
	HyleFormFields, HyleTableBody, HyleTableFilterBar, HyleTableFilters, HyleTablePanel,
};

use ndc_dioxus_shared::{
	RequestContext, ResponsePayload, parse_pairs,
};
use crate::site_ui::layout;
use crate::blueprint::get_blueprint;

pub fn render_hyle_list_with_source(
	ctx: &RequestContext<'_>,
	module: &'static str,
	icon: &str,
	source: Source,
	select_fields: &'static [&'static str],
	default_per_page: usize,
	pre_filtered: bool,
) -> ResponsePayload {
	let query_pairs = parse_pairs(ctx.query);

	let page: usize = ndc_dioxus_shared::get_pair(&query_pairs, "page")
		.and_then(|v| v.parse().ok())
		.unwrap_or(1);
	let per_page: usize = ndc_dioxus_shared::get_pair(&query_pairs, "per_page")
		.and_then(|v| v.parse().ok())
		.unwrap_or(default_per_page);
	let sort_field: Option<String> = ndc_dioxus_shared::get_pair(&query_pairs, "sort")
		.filter(|s| !s.is_empty())
		.map(|s| s.to_owned());
	let sort_ascending: bool = ndc_dioxus_shared::get_pair(&query_pairs, "asc")
		.and_then(|v| v.parse().ok())
		.unwrap_or(true);

	let initial_committed: IndexMap<String, String> = select_fields
		.iter()
		.filter_map(|&key| {
			ndc_dioxus_shared::get_pair(&query_pairs, key)
				.filter(|v| !v.is_empty())
				.map(|v| (key.to_owned(), v.to_owned()))
		})
		.collect();

	let blueprint = get_blueprint();
	let collection_href = format!("/{module}/");
	let add_href = format!("/{module}/add");
	let user = ctx.remote_user;

	let menu_items = user.map(|_| {
		rsx! {
			a { href: "{add_href}", class: "btn",
				span { "➕" }
				label { "add" }
			}
		}
	});

	let body = render_element(layout(
		user,
		module,
		&collection_href,
		icon,
		menu_items,
		rsx! {
			HyleListInner {
				blueprint,
				source,
				module,
				select_fields,
				page,
				per_page,
				initial_committed,
				pre_filtered,
				sort_field,
				sort_ascending,
			}
		},
	));

	ResponsePayload {
		status: 200,
		content_type: "text/html; charset=utf-8".to_string(),
		location: None,
		body: ndc_dioxus_shared::render_document_html_with_head(module, &body, "", ""),
	}
}

/// Thin wrapper: same as `render_hyle_list_with_source` but signals the
/// component to treat the source as pre-filtered (C-side already applied
/// filter/sort/pagination). The component builds `HyleListState` from
/// `blueprint.resolve` (no re-apply_view) and renders a native `<form>`
/// without JS-intercepted pagination buttons.
pub fn render_hyle_list_queried(
	ctx: &RequestContext<'_>,
	module: &'static str,
	icon: &str,
	source: Source,
	select_fields: &'static [&'static str],
	default_per_page: usize,
) -> ResponsePayload {
	render_hyle_list_with_source(
		ctx, module, icon, source, select_fields, default_per_page, true,
	)
}

/// Render a hyle-driven edit form for a module item.
pub fn render_hyle_edit_with_source(
	ctx: &RequestContext<'_>,
	module: &'static str,
	icon: &str,
	id: &str,
	source: Source,
	fields: IndexMap<String, String>,
	select_fields: &'static [&'static str],
	enctype: &'static str,
) -> ResponsePayload {
	let title = fields.get("title").map(|s| s.as_str()).unwrap_or("");
	let heading = format!("Edit {}", ndc_dioxus_shared::display_or_id(title, id));
	let action = ndc_dioxus_shared::edit_path(module, id);
	let cancel_href = ndc_dioxus_shared::item_path(module, id);
	let csrf_token = ctx.csrf_token.to_owned();
	let blueprint = get_blueprint();
	let user = ctx.remote_user;

	crate::site_ui::edit_form_page(
		user,
		&heading,
		&action,
		icon,
		rsx! {
			HyleEditInner {
				blueprint,
				source,
				module,
				select_fields,
				action: action.clone(),
				enctype,
				csrf_token,
				{ crate::site_ui::form_actions(&cancel_href, "Save Changes", None) }
			}
		},
	)
}

pub fn render_hyle_edit(
	ctx: &RequestContext<'_>,
	module: &'static str,
	icon: &str,
	id: &str,
	initial: IndexMap<String, String>,
	select_fields: &'static [&'static str],
	enctype: &'static str,
) -> ResponsePayload {
	let blueprint = get_blueprint();
	let source = item_to_source(&blueprint, module, id, &initial);
	render_hyle_edit_with_source(
		ctx,
		module,
		icon,
		id,
		source,
		initial,
		select_fields,
		enctype,
	)
}

// ── Site-specific list component ────────────────────────────────────

#[derive(Props, Clone, PartialEq)]
pub(crate) struct HyleListInnerProps {
	pub blueprint: std::sync::Arc<hyle::Blueprint>,
	pub source: Source,
	pub module: &'static str,
	pub select_fields: &'static [&'static str],
	pub page: usize,
	pub per_page: usize,
	pub initial_committed: IndexMap<String, String>,
	pub pre_filtered: bool,
	pub sort_field: Option<String>,
	pub sort_ascending: bool,
}

#[component]
pub(crate) fn HyleListInner(props: HyleListInnerProps) -> Element {
	let HyleListInnerProps {
		blueprint,
		source,
		module,
		select_fields,
		page,
		per_page,
		initial_committed,
		pre_filtered,
		sort_field,
		sort_ascending,
	} = props;

	let bp = blueprint.clone();
	use_context_provider(|| HyleConfig { blueprint });
	let adapter = use_static_adapter(source);
	let source_signal = adapter.source;
	use_context_provider(|| adapter);

	let base_query = hyle::Query {
		model: module.to_owned(),
		select: select_fields.iter().map(|s| s.to_string()).collect(),
		page: Some(page),
		per_page: Some(per_page),
		..Default::default()
	};
	let query_clone = base_query.clone();

	let filters: HyleFiltersState = use_filters(
		base_query,
		UseFiltersOptions {
			initial_committed,
			change: None,
		},
	);

	let row_href: Callback<Row, String> = Callback::new(move |row: Row| {
		let id = row
			.get("id")
			.and_then(|v| if let hyle::Value::String(s) = v { Some(s.as_str()) } else { None })
			.unwrap_or("")
			.to_owned();
		format!("/{module}/{id}/")
	});

	let filter_keys = Some(select_fields.iter().map(|s| s.to_string()).collect());

	if pre_filtered {
		let bq = query_clone.clone();
		let data: Memo<HyleDataState> = use_memo(move || {
			let manifest = match bp.manifest(bq.clone()) {
				Ok(m) => m,
				Err(e) => return HyleDataState::Error {
					error: e.to_string(), manifest: None,
				},
			};
			let src = match source_signal.read().clone() {
				HyleSourceState::Ready(s) => s,
				_ => return HyleDataState::Loading { manifest: Some(manifest) },
			};
			match bp.resolve(&manifest, &src) {
				Ok(outcome) => {
					let model = bp.models.get(&manifest.base);
					let columns: Vec<Column> = manifest.fields.iter()
						.filter_map(|key| {
							model.and_then(|m| m.fields.get(key))
								.map(|field| Column {
									key: key.clone(),
									label: field.label.clone(),
									field: field.clone(),
								})
						})
						.collect();
					let rows = outcome.rows();
					HyleDataState::Ready {
						manifest,
						outcome,
						rows,
						columns,
						row: None,
						fields: vec![],
					}
				}
				Err(e) => HyleDataState::Error {
					error: e.to_string(), manifest: Some(manifest),
				},
			}
		});

		let q = query_clone.clone();
		let list = HyleListState {
			data,
			query: use_memo(move || q.clone()),
			page: use_signal(|| page),
			per_page: use_signal(|| per_page),
			sort_field: use_signal(|| sort_field),
			sort_ascending: use_signal(|| sort_ascending),
		};

		use_context_provider(|| filters);

		let prev_page = page.saturating_sub(1).max(1);
		let next_page = page + 1;
		let (total, row_count) = match &*data.read() {
			HyleDataState::Ready { outcome, rows, .. } => (outcome.total, rows.len()),
			_ => (0, 0),
		};

		rsx! {
			div { class: "center",
				form { method: "get",
					input { r#type: "hidden", name: "page", value: "1" }
					HyleTableFilterBar { filters, only: filter_keys }
					HyleTableFilters {}
					HyleTableBody { list, row_href: Some(row_href) }
					div { class: "hyle-table-footer",
						div { class: "hyle-pagination",
							button { r#type: "submit", name: "page", value: "{prev_page}",
								disabled: page <= 1, "← Prev" }
							span { "Page {page}" }
							button { r#type: "submit", name: "page", value: "{next_page}",
								disabled: row_count < per_page, "Next →" }
							select { name: "per_page",
								for n in [5usize, 10, 20, 50, 100] {
									option { value: "{n}", selected: n == per_page, "{n} / page" }
								}
							}
							button { r#type: "submit", "Apply" }
						}
						span { class: "hyle-row-count", "{row_count} of {total} rows" }
					}
				}
			}
		}
	} else {
		let list = use_list_with_filters(filters);

		rsx! {
			div { class: "center",
				HyleTablePanel {
					list,
					filters: Some(filters),
					row_href,
					HyleTableFilterBar { filters, only: filter_keys }
					HyleTableFilters {}
				}
			}
		}
	}
}

// ── Site-specific edit form component ───────────────────────────────

#[component]
pub(crate) fn HyleEditInner(
	blueprint: std::sync::Arc<hyle::Blueprint>,
	source: Source,
	module: &'static str,
	select_fields: &'static [&'static str],
	action: String,
	enctype: &'static str,
	csrf_token: String,
	children: Element,
) -> Element {
	use_context_provider(|| HyleConfig { blueprint });
	let adapter = use_static_adapter(source.clone());
	use_context_provider(|| adapter);

	let initial_committed: IndexMap<String, String> = if let Some(res) = source.get(module) {
		let rows = res.rows();
		if !rows.is_empty() {
			let row = &rows[0];
			select_fields
				.iter()
				.map(|&k| {
					let val = match row.get(k) {
						Some(hyle::Value::String(s)) => s.clone(),
						Some(hyle::Value::Array(arr)) => arr
							.iter()
							.filter_map(|v| match v {
								hyle::Value::String(s) => Some(s.clone()),
								_ => None,
							})
							.collect::<Vec<_>>()
							.join(","),
						_ => String::new(),
					};
					(k.to_owned(), val)
				})
				.collect()
		} else {
			IndexMap::new()
		}
	} else {
		IndexMap::new()
	};

	let base_query = hyle::Query {
		model: module.to_owned(),
		select: select_fields.iter().map(|s| s.to_string()).collect(),
		..Default::default()
	};

	let filters: HyleFiltersState = use_filters(
		base_query,
		UseFiltersOptions {
			initial_committed,
			change: None,
		},
	);

	let only: Vec<String> = select_fields.iter().map(|s| s.to_string()).collect();

	rsx! {
		form {
			method: "POST",
			action: "{action}",
			enctype: "{enctype}",
			class: "flex flex-col gap-4 w-full max-w-2xl",
			input { r#type: "hidden", name: "csrf_token", value: "{csrf_token}" }
			HyleFormFields { filters, only }
			{children}
		}
	}
}
