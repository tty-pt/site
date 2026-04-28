use wasm_bindgen::JsCast;
use wasm_bindgen::prelude::*;
use wasm_bindgen_futures::{JsFuture, spawn_local};
use web_sys::{
    Document, Element, Event, FormData, HtmlButtonElement, HtmlDataListElement, HtmlElement,
    HtmlFormElement, HtmlInputElement, HtmlOptionElement, HtmlSelectElement, RequestInit, Response,
    UrlSearchParams,
};

#[derive(Clone)]
struct DatalistOption {
    value: String,
    chord_type: String,
}

fn document() -> Result<Document, JsValue> {
    web_sys::window()
        .and_then(|window| window.document())
        .ok_or_else(|| JsValue::from_str("missing document"))
}

fn query_typed<T: JsCast>(root: &Element, selector: &str) -> Result<Option<T>, JsValue> {
    Ok(root.query_selector(selector)?.and_then(|node| node.dyn_into::<T>().ok()))
}

fn viewer_save_url(container: &Element) -> Option<String> {
    container
        .get_attribute("data-detail-viewer-save-url")
        .filter(|value| !value.is_empty())
}

async fn post_viewer_prefs(url: String, body: String) -> Result<(), JsValue> {
    let window = web_sys::window().ok_or_else(|| JsValue::from_str("missing window"))?;
    let init = RequestInit::new();
    init.set_method("POST");
    init.set_body(&JsValue::from_str(&body));

    let response = JsFuture::from(window.fetch_with_str_and_init(&url, &init)).await?;
    let response: Response = response.dyn_into()?;
    if response.ok() || response.status() == 204 {
        return Ok(());
    }

    Err(JsValue::from_str(&format!("HTTP {}", response.status())))
}

fn datalist_options(datalist: &HtmlDataListElement) -> Vec<DatalistOption> {
    let options = datalist.query_selector_all("option").ok();
    let Some(options) = options else {
        return Vec::new();
    };

    (0..options.length())
        .filter_map(|index| options.item(index))
        .filter_map(|node| node.dyn_into::<HtmlOptionElement>().ok())
        .map(|option| DatalistOption {
            value: option.value(),
            chord_type: option.get_attribute("data-chord-type").unwrap_or_default(),
        })
        .collect()
}

fn apply_datalist_filter(input: &HtmlInputElement, datalist: &HtmlDataListElement, source: &[DatalistOption]) {
    let selected = input.value().trim().to_string();
    let has_exact_match = !selected.is_empty()
        && source.iter().any(|option| option.chord_type == selected);

    datalist.set_text_content(None);

    for option in source.iter().filter(|option| {
        !has_exact_match || option.chord_type == selected
    }) {
        let Some(doc) = datalist.owner_document() else {
            return;
        };
        let Ok(element) = doc.create_element("option") else {
            return;
        };
        let Ok(option_element) = element.dyn_into::<HtmlOptionElement>() else {
            return;
        };
        option_element.set_value(&option.value);
        if !option.chord_type.is_empty() {
            let _ = option_element.set_attribute("data-chord-type", &option.chord_type);
        }
        let _ = datalist.append_child(&option_element);
    }
}

fn init_edit_rows() -> Result<(), JsValue> {
    let inputs = document()?.query_selector_all("input[data-songbook-format-input]")?;
    for index in 0..inputs.length() {
        let Some(node) = inputs.item(index) else {
            continue;
        };
        let Ok(input) = node.dyn_into::<HtmlInputElement>() else {
            continue;
        };
        let Some(list_id) = input.get_attribute("data-songbook-song-list") else {
            continue;
        };
        let Some(datalist_node) = document()?.get_element_by_id(&list_id) else {
            continue;
        };
        let Ok(datalist) = datalist_node.dyn_into::<HtmlDataListElement>() else {
            continue;
        };
        let source = datalist_options(&datalist);
        apply_datalist_filter(&input, &datalist, &source);

        let input_clone = input.clone();
        let datalist_clone = datalist.clone();
        let source_clone = source.clone();
        let on_input = Closure::<dyn FnMut(Event)>::wrap(Box::new(move |_event: Event| {
            apply_datalist_filter(&input_clone, &datalist_clone, &source_clone);
        }));
        input.add_event_listener_with_callback("input", on_input.as_ref().unchecked_ref())?;
        on_input.forget();
    }
    Ok(())
}

fn hide_submit(form: &HtmlFormElement) -> Option<HtmlButtonElement> {
    let Ok(button) = query_typed::<HtmlButtonElement>(form.as_ref(), "button[type=\"submit\"]") else {
        return None;
    };
    let Some(button) = button else {
        return None;
    };
    button.set_hidden(true);
    let _ = button.set_attribute("aria-hidden", "true");
    button.set_tab_index(-1);
    Some(button)
}

fn songbook_transpose_params(select: &HtmlSelectElement) -> UrlSearchParams {
    let params = UrlSearchParams::new().unwrap();
    params.append("t", &select.value());
    params.append("h", "1");
    params
}

async fn enhance_transpose(
    form: HtmlFormElement,
    row: Element,
    song_id: String,
    chord_data: Option<HtmlElement>,
    target_key: Option<Element>,
    select: HtmlSelectElement,
    submit_button: Option<HtmlButtonElement>,
) -> Result<(), JsValue> {
    if let Some(chord_data) = chord_data.as_ref() {
        chord_data.style().set_property("opacity", "0.5")?;
    }
    if let Some(button) = submit_button.as_ref() {
        button.set_disabled(true);
    }

    let window = web_sys::window().ok_or_else(|| JsValue::from_str("missing window"))?;

    let save_init = RequestInit::new();
    save_init.set_method("POST");
    let form_data = FormData::new_with_form(&form)?;
    save_init.set_body(&form_data);

    let save_response = JsFuture::from(window.fetch_with_str_and_init(&form.action(), &save_init)).await?;
    let save_response: Response = save_response.dyn_into()?;
    if !save_response.ok() {
        return Err(JsValue::from_str(&format!("HTTP {}", save_response.status())));
    }

    let params = songbook_transpose_params(&select);
    let transpose_url = format!(
        "/api/song/{}/transpose?{}",
        song_id,
        params.to_string().as_string().unwrap_or_default()
    );
    let transpose_response = JsFuture::from(window.fetch_with_str(&transpose_url)).await?;
    let transpose_response: Response = transpose_response.dyn_into()?;
    if !transpose_response.ok() {
        return Err(JsValue::from_str(&format!("HTTP {}", transpose_response.status())));
    }

    let json = JsFuture::from(transpose_response.json()?).await?;
    let payload: js_sys::Object = json.dyn_into()?;
    let chord_html = js_sys::Reflect::get(&payload, &JsValue::from_str("data"))?
        .as_string()
        .ok_or_else(|| JsValue::from_str("missing chord data"))?;

    if let Some(chord_data) = chord_data {
        chord_data.set_inner_html(&chord_html);
        let _ = chord_data.style().remove_property("opacity");
    }

    if let Some(target_key) = target_key {
        if let Some(option) = query_typed::<HtmlOptionElement>(select.as_ref(), "option:checked")? {
            target_key.set_text_content(Some(&option.text().replace(" (Original)", "")));
        }
    }

    let _ = row.set_attribute("data-songbook-enhanced", "1");
    Ok(())
}

fn init_detail_forms() -> Result<(), JsValue> {
    let forms = document()?.query_selector_all("form[data-songbook-transpose-form=\"1\"]")?;
    for index in 0..forms.length() {
        let Some(node) = forms.item(index) else {
            continue;
        };
        let Ok(form) = node.dyn_into::<HtmlFormElement>() else {
            continue;
        };
        let Some(song_id) = form.get_attribute("data-song-id") else {
            continue;
        };
        let Some(row) = form.closest("[data-songbook-item=\"1\"]")? else {
            continue;
        };
        let Some(select) = query_typed::<HtmlSelectElement>(form.as_ref(), "select[name=\"t\"]")? else {
            continue;
        };
        let submit_button = hide_submit(&form);
        let chord_data = query_typed::<HtmlElement>(&row, "[data-songbook-chord-data=\"1\"]")?;
        let target_key = query_typed::<Element>(&row, "[data-songbook-target-key=\"1\"]")?;

        let form_clone = form.clone();
        let row_clone = row.clone();
        let song_id_clone = song_id.clone();
        let chord_clone = chord_data.clone();
        let target_clone = target_key.clone();
        let select_clone = select.clone();
        let button_clone = submit_button.clone();

        let on_change = Closure::<dyn FnMut(Event)>::wrap(Box::new(move |_event: Event| {
            if form_clone.has_attribute("data-songbook-transpose-busy") {
                return;
            }
            let _ = form_clone.set_attribute("data-songbook-transpose-busy", "1");

            let fallback_form = form_clone.clone();
            let inner_form = form_clone.clone();
            let inner_row = row_clone.clone();
            let inner_song = song_id_clone.clone();
            let inner_chord = chord_clone.clone();
            let inner_target = target_clone.clone();
            let inner_select = select_clone.clone();
            let inner_button = button_clone.clone();

            spawn_local(async move {
                let result = enhance_transpose(
                    inner_form.clone(),
                    inner_row,
                    inner_song,
                    inner_chord.clone(),
                    inner_target,
                    inner_select,
                    inner_button.clone(),
                )
                .await;

                if let Some(chord_data) = inner_chord.as_ref() {
                    let _ = chord_data.style().remove_property("opacity");
                }
                if let Some(button) = inner_button.as_ref() {
                    button.set_disabled(false);
                }
                let _ = inner_form.remove_attribute("data-songbook-transpose-busy");

                if result.is_err() {
                    let _ = fallback_form.submit();
                }
            });
        }));
        select.add_event_listener_with_callback("change", on_change.as_ref().unchecked_ref())?;
        on_change.forget();
    }
    Ok(())
}

fn apply_viewer_state(
    scopes: &[HtmlElement],
    targets: &[HtmlElement],
    scrolls: &[HtmlElement],
    zoom: &HtmlInputElement,
    wrap: &HtmlInputElement,
    label: &Element,
) {
    let scale = zoom.value().parse::<i32>().unwrap_or(100).clamp(70, 170);
    let wrap_lines = wrap.checked();
    label.set_text_content(Some(&format!("{scale}%")));

    for scope in scopes {
        let style = scope.style();
        let _ = style.set_property("font-size", &format!("calc({:.2} * 0.8rem)", scale as f64 / 100.0));
    }

    for target in targets {
        let style = target.style();
        let _ = style.set_property("white-space", if wrap_lines { "break-spaces" } else { "pre" });
        let _ = style.set_property("display", if wrap_lines { "block" } else { "inline-block" });
        let _ = style.set_property("min-width", if wrap_lines { "0" } else { "100%" });
    }

    for scroll in scrolls {
        let style = scroll.style();
        let _ = style.set_property("overflow-x", if wrap_lines { "visible" } else { "auto" });
    }
}

fn spawn_save_zoom_pref(container: Element, zoom: HtmlInputElement) {
    let Some(url) = viewer_save_url(&container) else {
        return;
    };
    let body = format!(
        "v={}",
        zoom.value().parse::<i32>().unwrap_or(100).clamp(70, 170)
    );
    spawn_local(async move {
        let _ = post_viewer_prefs(url, body).await;
    });
}

fn init_detail_viewer() -> Result<(), JsValue> {
    let Some(container) = document()?.query_selector("[data-detail-viewer-controls=\"songbook\"]")? else {
        return Ok(());
    };
    let Some(zoom) = query_typed::<HtmlInputElement>(&container, "[data-detail-viewer-zoom=\"1\"]")? else {
        return Ok(());
    };
    let Some(wrap) = query_typed::<HtmlInputElement>(&container, "[data-detail-viewer-wrap=\"1\"]")? else {
        return Ok(());
    };
    let Some(label) = container.query_selector("[data-detail-viewer-zoom-label=\"1\"]")? else {
        return Ok(());
    };
    let Some(scope) = document()?
        .query_selector("[data-detail-viewer-scope=\"1\"]")?
        .and_then(|node| node.dyn_into::<HtmlElement>().ok())
    else {
        return Ok(());
    };
    let nodes = document()?.query_selector_all("[data-songbook-chord-data=\"1\"]")?;
    let scroll_nodes = document()?.query_selector_all("[data-detail-viewer-scroll=\"1\"]")?;
    let scopes = vec![scope];
    let mut targets = Vec::new();
    let mut scrolls = Vec::new();
    for index in 0..nodes.length() {
        if let Some(node) = nodes.item(index).and_then(|node| node.dyn_into::<HtmlElement>().ok()) {
            targets.push(node);
        }
    }
    for index in 0..scroll_nodes.length() {
        if let Some(node) = scroll_nodes.item(index).and_then(|node| node.dyn_into::<HtmlElement>().ok()) {
            scrolls.push(node);
        }
    }
    if targets.is_empty() {
        return Ok(());
    }
    apply_viewer_state(&scopes, &targets, &scrolls, &zoom, &wrap, &label);

    let zoom_scopes = scopes.clone();
    let zoom_targets = targets.clone();
    let zoom_scrolls = scrolls.clone();
    let zoom_input = zoom.clone();
    let zoom_wrap = wrap.clone();
    let zoom_label = label.clone();
    let on_zoom = Closure::<dyn FnMut(Event)>::wrap(Box::new(move |_event: Event| {
        apply_viewer_state(&zoom_scopes, &zoom_targets, &zoom_scrolls, &zoom_input, &zoom_wrap, &zoom_label);
    }));
    zoom.add_event_listener_with_callback("input", on_zoom.as_ref().unchecked_ref())?;
    on_zoom.forget();

    let save_container = container.clone();
    let save_zoom = zoom.clone();
    let on_zoom_change = Closure::<dyn FnMut(Event)>::wrap(Box::new(move |_event: Event| {
        spawn_save_zoom_pref(save_container.clone(), save_zoom.clone());
    }));
    zoom.add_event_listener_with_callback("change", on_zoom_change.as_ref().unchecked_ref())?;
    on_zoom_change.forget();

    let wrap_scopes = scopes.clone();
    let wrap_targets = targets.clone();
    let wrap_scrolls = scrolls.clone();
    let wrap_zoom = zoom.clone();
    let wrap_input = wrap.clone();
    let wrap_label = label.clone();
    let on_wrap = Closure::<dyn FnMut(Event)>::wrap(Box::new(move |_event: Event| {
        apply_viewer_state(&wrap_scopes, &wrap_targets, &wrap_scrolls, &wrap_zoom, &wrap_input, &wrap_label);
    }));
    wrap.add_event_listener_with_callback("change", on_wrap.as_ref().unchecked_ref())?;
    on_wrap.forget();

    Ok(())
}

#[wasm_bindgen(start)]
pub fn start() -> Result<(), JsValue> {
    init_detail_viewer()?;
    init_edit_rows()?;
    init_detail_forms()?;
    Ok(())
}
