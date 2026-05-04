use dioxus::prelude::*;
use dioxus_ssr::render_element;
use indexmap::IndexMap;

use hyle::{ModelResult, Row, Source, Value};
use hyle_dioxus::{
    use_dioxus_mutation, use_filters, use_list_with_filters, use_context_provider,
    DioxusMutationOptions, HyleAdapter, HyleConfig, HyleFiltersState, HyleSourceState,
    UseFiltersOptions,
};
use hyle_dioxus_native::{HyleTableFilters, HyleTablePanel};

use crate::{
    IndexItem, RequestContext, ResponsePayload, blueprint::get_blueprint,
    layout, parse_pairs,
};

/// Convert parsed index items into a hyle `Source`.
///
/// For the `"song"` model, also extracts distinct `"type"` values and adds
/// a `"song_type"` lookup model so the reference field renders as a `<select>`.
pub fn items_to_source(model: &str, items: &[IndexItem]) -> Source {
    let rows: Vec<Row> = items
        .iter()
        .map(|item| {
            let mut row = IndexMap::new();
            row.insert("id".to_owned(), Value::String(item.id.clone()));
            row.insert("title".to_owned(), Value::String(item.title.clone()));
            for (k, v) in &item.extra {
                row.insert(k.clone(), Value::String(v.clone()));
            }
            row
        })
        .collect();

    let total = rows.len();
    let mut source: Source = IndexMap::new();
    source.insert(model.to_owned(), ModelResult::many(rows));

    // For song, build synthetic song_type lookup from distinct type values.
    if model == "song" {
        let mut seen = std::collections::BTreeSet::new();
        for item in items {
            for (k, v) in &item.extra {
                if k == "type" && !v.is_empty() {
                    seen.insert(v.clone());
                }
            }
        }
        let type_rows: Vec<Row> = seen
            .into_iter()
            .map(|t| {
                let mut row = IndexMap::new();
                row.insert("id".to_owned(), Value::String(t.clone()));
                row.insert("name".to_owned(), Value::String(t));
                row
            })
            .collect();
        source.insert("song_type".to_owned(), ModelResult::many(type_rows));
    }

    // For songbook, build synthetic choir_ref lookup from distinct choir values.
    if model == "songbook" {
        let mut seen = std::collections::BTreeSet::new();
        for item in items {
            for (k, v) in &item.extra {
                if k == "choir" && !v.is_empty() {
                    seen.insert(v.clone());
                }
            }
        }
        let choir_rows: Vec<Row> = seen
            .into_iter()
            .map(|c| {
                let mut row = IndexMap::new();
                row.insert("id".to_owned(), Value::String(c.clone()));
                row.insert("name".to_owned(), Value::String(c));
                row
            })
            .collect();
        source.insert("choir_ref".to_owned(), ModelResult::many(choir_rows));
    }

    let _ = total; // suppress unused warning; total is embedded in ModelResult
    source
}

/// Build a `HyleAdapter` whose source is already `Ready` (no async fetch).
/// Mutations are no-ops — the C layer handles all writes.
///
/// Must be called inside a Dioxus component (uses hooks internally).
pub fn use_static_adapter(source: Source) -> HyleAdapter {
    let source_signal: ReadSignal<HyleSourceState> =
        use_memo(move || HyleSourceState::Ready(source.clone())).into();
    let noop = || {
        use_dioxus_mutation(
            |_| async { Ok::<(), String>(()) },
            DioxusMutationOptions::default(),
        )
    };
    HyleAdapter {
        source: source_signal,
        create: noop(),
        update: noop(),
        delete: noop(),
    }
}

/// Render a hyle-driven filterable paginated table for a module list page.
///
/// Replaces `render_index_table` for all four modules.
pub fn render_hyle_list(
    ctx: &RequestContext<'_>,
    module: &'static str,
    icon: Option<&str>,
    items: Vec<IndexItem>,
    select_fields: &'static [&'static str],
    default_per_page: usize,
) -> ResponsePayload {
    let query_pairs = parse_pairs(ctx.query);

    // Parse page / per_page from URL query params.
    let page: usize = crate::get_pair(&query_pairs, "page")
        .and_then(|v| v.parse().ok())
        .unwrap_or(1);
    let per_page: usize = crate::get_pair(&query_pairs, "per_page")
        .and_then(|v| v.parse().ok())
        .unwrap_or(default_per_page);

    // Seed filter values from URL query params (no-JS progressive enhancement).
    let initial_committed: IndexMap<String, String> = select_fields
        .iter()
        .filter_map(|&key| {
            crate::get_pair(&query_pairs, key)
                .filter(|v| !v.is_empty())
                .map(|v| (key.to_owned(), v.to_owned()))
        })
        .collect();

    let source = items_to_source(module, &items);
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
        Some(icon.unwrap_or("🏠")),
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
            }
        },
    ));

    ResponsePayload {
        status: 200,
        content_type: "text/html; charset=utf-8".to_string(),
        location: None,
        body: format!(
            "<!DOCTYPE html><html lang=\"pt\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"><title>{module}</title><link rel=\"stylesheet\" href=\"/styles.css\"></head><body style=\"margin: 0\">{body}<script type=\"module\" src=\"/wasm.js\"></script></body></html>"
        ),
    }
}

// ── Inner component ───────────────────────────────────────────────────────────

#[derive(Props, Clone, PartialEq)]
struct HyleListInnerProps {
    blueprint: std::sync::Arc<hyle::Blueprint>,
    source: Source,
    module: &'static str,
    select_fields: &'static [&'static str],
    page: usize,
    per_page: usize,
    initial_committed: IndexMap<String, String>,
}

#[component]
fn HyleListInner(props: HyleListInnerProps) -> Element {
    let HyleListInnerProps {
        blueprint,
        source,
        module,
        select_fields,
        page,
        per_page,
        initial_committed,
    } = props;

    use_context_provider(|| HyleConfig { blueprint });
    let adapter = use_static_adapter(source);
    use_context_provider(|| adapter);

    let base_query = hyle::Query {
        model: module.to_owned(),
        select: select_fields.iter().map(|s| s.to_string()).collect(),
        page: Some(page),
        per_page: Some(per_page),
        ..Default::default()
    };

    let filters: HyleFiltersState = use_filters(
        base_query,
        UseFiltersOptions {
            initial_committed,
            change: None,
        },
    );
    let list = use_list_with_filters(filters);

    let row_href: Callback<hyle::Row, String> = Callback::new(move |row: hyle::Row| {
        let id = row
            .get("id")
            .and_then(|v| v.as_str())
            .unwrap_or("")
            .to_owned();
        format!("/{module}/{id}/")
    });

    rsx! {
        div { class: "center",
            HyleTablePanel {
                list,
                filters,
                row_href,
                HyleTableFilters {}
            }
        }
    }
}
