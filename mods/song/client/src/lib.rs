use wasm_bindgen::JsCast;
use wasm_bindgen::prelude::*;
use wasm_bindgen_futures::{JsFuture, spawn_local};
use web_sys::{
    Document, Element, Event, HtmlButtonElement, HtmlElement, HtmlFormElement, HtmlInputElement,
    HtmlSelectElement, RequestInit, Response, Url, UrlSearchParams, Window,
};

fn window() -> Result<Window, JsValue> {
    web_sys::window().ok_or_else(|| JsValue::from_str("missing window"))
}

fn document() -> Result<Document, JsValue> {
    window()?
        .document()
        .ok_or_else(|| JsValue::from_str("missing document"))
}

fn form() -> Result<Option<HtmlFormElement>, JsValue> {
    let Some(element) = document()?.get_element_by_id("transpose-form") else {
        return Ok(None);
    };
    Ok(element.dyn_into::<HtmlFormElement>().ok())
}

fn query_typed<T: JsCast>(root: &Element, selector: &str) -> Result<Option<T>, JsValue> {
    Ok(root.query_selector(selector)?.and_then(|node| node.dyn_into::<T>().ok()))
}

fn checkbox_value(root: &Element, name: &str) -> Result<bool, JsValue> {
    let selector = format!("input[name=\"{name}\"]");
    Ok(query_typed::<HtmlInputElement>(root, &selector)?
        .map(|input| input.checked())
        .unwrap_or(false))
}

fn form_song_id(form: &HtmlFormElement) -> Result<String, JsValue> {
    if let Some(id) = form.get_attribute("data-song-id") {
        if !id.is_empty() {
            return Ok(id);
        }
    }
    let action = Url::new(&form.action())?;
    let path = action.pathname();
    let parts: Vec<&str> = path.split('/').filter(|part| !part.is_empty()).collect();
    Ok(parts.get(1).copied().unwrap_or_default().to_string())
}

fn form_params(form: &HtmlFormElement) -> Result<UrlSearchParams, JsValue> {
    let params = UrlSearchParams::new()?;
    if let Some(select) = query_typed::<HtmlSelectElement>(form.as_ref(), "select[name=\"t\"]")? {
        params.append("t", &select.value());
    }

    for name in ["b", "l", "m"] {
        if checkbox_value(form.as_ref(), name)? {
            params.append(name, "1");
        }
    }
    params.append("h", "1");
    Ok(params)
}

fn page_url(form: &HtmlFormElement, params: &UrlSearchParams) -> Result<String, JsValue> {
    let action = Url::new(&form.action())?;
    let query = params.to_string().as_string().unwrap_or_default();
    if query.is_empty() {
        Ok(action.pathname())
    } else {
        Ok(format!("{}?{}", action.pathname(), query))
    }
}

fn api_url(song_id: &str, params: &UrlSearchParams) -> String {
    let query = params.to_string().as_string().unwrap_or_default();
    if query.is_empty() {
        format!("/api/song/{song_id}/transpose")
    } else {
        format!("/api/song/{song_id}/transpose?{query}")
    }
}

fn viewer_save_url(container: &Element) -> Option<String> {
    container
        .get_attribute("data-detail-viewer-save-url")
        .filter(|value| !value.is_empty())
}

async fn post_viewer_prefs(url: String, body: String) -> Result<(), JsValue> {
    let window = window()?;
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

async fn save_song_viewer_prefs(form: &HtmlFormElement) -> Result<(), JsValue> {
    let Some(container) = document()?.query_selector("[data-detail-viewer-controls=\"song\"]")? else {
        return Ok(());
    };
    let Some(url) = viewer_save_url(&container) else {
        return Ok(());
    };
    let Some(zoom) = query_typed::<HtmlInputElement>(&container, "[data-detail-viewer-zoom=\"1\"]")? else {
        return Ok(());
    };
    let body = format!(
        "v={}&b={}&l={}",
        zoom.value().parse::<i32>().unwrap_or(100).clamp(70, 170),
        if checkbox_value(form.as_ref(), "b")? { 1 } else { 0 },
        if checkbox_value(form.as_ref(), "l")? { 1 } else { 0 }
    );
    post_viewer_prefs(url, body).await
}

fn update_song_media(media_slot: Option<&Element>, payload: &js_sys::Object) {
    let Some(media_slot) = media_slot else {
        return;
    };

    let show_media = js_sys::Reflect::get(payload, &JsValue::from_str("showMedia"))
        .ok()
        .and_then(|value| value.as_bool())
        .unwrap_or(false);
    let yt = js_sys::Reflect::get(payload, &JsValue::from_str("yt"))
        .ok()
        .and_then(|value| value.as_string())
        .unwrap_or_default();
    let audio = js_sys::Reflect::get(payload, &JsValue::from_str("audio"))
        .ok()
        .and_then(|value| value.as_string())
        .unwrap_or_default();
    let pdf = js_sys::Reflect::get(payload, &JsValue::from_str("pdf"))
        .ok()
        .and_then(|value| value.as_string())
        .unwrap_or_default();

    if !show_media || (yt.is_empty() && audio.is_empty() && pdf.is_empty()) {
        media_slot.set_inner_html("");
        return;
    }

    let mut html = String::from(
        "<div id=\"media-container\" class=\"flex flex-col gap-4 w-full max-w-xl\">",
    );
    if !yt.is_empty() {
        html.push_str(&format!(
            "<iframe src=\"https://www.youtube.com/embed/{yt}\" class=\"w-full aspect-video border-none\" allowfullscreen></iframe>"
        ));
    }
    if !audio.is_empty() {
        html.push_str(&format!(
            "<audio controls class=\"w-full\"><source src=\"{audio}\" type=\"audio/mpeg\"></audio>"
        ));
    }
    if !pdf.is_empty() {
        html.push_str(&format!(
            "<a href=\"{pdf}\" target=\"_blank\" rel=\"noopener\" class=\"text-blue-600\">📄 View PDF</a>"
        ));
    }
    html.push_str("</div>");
    media_slot.set_inner_html(&html);
}

async fn update_song_detail(
    form: HtmlFormElement,
    chord_data: HtmlElement,
    media_slot: Option<Element>,
    submit_button: Option<HtmlButtonElement>,
) -> Result<(), JsValue> {
    let song_id = form_song_id(&form)?;
    if song_id.is_empty() {
        return Err(JsValue::from_str("missing song id"));
    }
    let params = form_params(&form)?;
    let fetch_path = api_url(&song_id, &params);
    let page_path = page_url(&form, &params)?;
    let window = window()?;
    let history = window.history()?;

    chord_data.style().set_property("opacity", "0.5")?;
    if let Some(button) = submit_button.as_ref() {
        button.set_disabled(true);
    }

    let init = RequestInit::new();
    init.set_method("GET");

    let response = JsFuture::from(window.fetch_with_str_and_init(&fetch_path, &init)).await?;
    let response: Response = response.dyn_into()?;
    if !response.ok() {
        return Err(JsValue::from_str(&format!("HTTP {}", response.status())));
    }

    let json = JsFuture::from(response.json()?).await?;
    let payload: js_sys::Object = json.dyn_into()?;
    let chord_html = js_sys::Reflect::get(&payload, &JsValue::from_str("data"))?
        .as_string()
        .ok_or_else(|| JsValue::from_str("missing chord data in response"))?;

    chord_data.set_inner_html(&chord_html);
    update_song_media(media_slot.as_ref(), &payload);
    history.push_state_with_url(&JsValue::NULL, "", Some(&page_path))?;
    let _ = save_song_viewer_prefs(&form).await;
    Ok(())
}

fn spawn_update(
    form: HtmlFormElement,
    chord_data: HtmlElement,
    media_slot: Option<Element>,
    submit_button: Option<HtmlButtonElement>,
) {
    if form.has_attribute("data-song-transpose-busy") {
        return;
    }

    let _ = form.set_attribute("data-song-transpose-busy", "1");
    let submit_fallback = form.clone();

    spawn_local(async move {
        let result = update_song_detail(
            form.clone(),
            chord_data.clone(),
            media_slot.clone(),
            submit_button.clone(),
        )
        .await;

        if let Some(button) = submit_button.as_ref() {
            button.set_disabled(false);
        }
        let _ = chord_data.style().remove_property("opacity");
        let _ = form.remove_attribute("data-song-transpose-busy");

        if result.is_err() {
            let _ = submit_fallback.submit();
        }
    });
}

fn bind_song_transpose(form: HtmlFormElement) -> Result<(), JsValue> {
    if form.get_attribute("data-song-transpose-enhanced").as_deref() == Some("1") {
        return Ok(());
    }

    let chord_data = document()?
        .get_element_by_id("chord-data")
        .ok_or_else(|| JsValue::from_str("missing chord-data element"))?
        .dyn_into::<HtmlElement>()?;
    let media_slot = document()?.get_element_by_id("media-slot");
    let submit_button =
        query_typed::<HtmlButtonElement>(form.as_ref(), "button[type=\"submit\"]")?;

    form.set_attribute("data-song-transpose-enhanced", "1")?;

    if let Some(button) = submit_button.as_ref() {
        button.set_hidden(true);
        let _ = button.set_attribute("aria-hidden", "true");
        button.set_tab_index(-1);
    }

    let submit_form = form.clone();
    let submit_chord = chord_data.clone();
    let submit_media = media_slot.clone();
    let submit_button_clone = submit_button.clone();
    let on_submit = Closure::<dyn FnMut(Event)>::wrap(Box::new(move |event: Event| {
        event.prevent_default();
        spawn_update(
            submit_form.clone(),
            submit_chord.clone(),
            submit_media.clone(),
            submit_button_clone.clone(),
        );
    }));
    form.add_event_listener_with_callback("submit", on_submit.as_ref().unchecked_ref())?;
    on_submit.forget();

    let change_form = form.clone();
    let change_chord = chord_data.clone();
    let change_media = media_slot.clone();
    let change_button = submit_button.clone();
    let on_change = Closure::<dyn FnMut(Event)>::wrap(Box::new(move |_event: Event| {
        spawn_update(
            change_form.clone(),
            change_chord.clone(),
            change_media.clone(),
            change_button.clone(),
        );
    }));
    form.add_event_listener_with_callback("change", on_change.as_ref().unchecked_ref())?;
    on_change.forget();

    let reload_window = window()?;
    let on_popstate = Closure::<dyn FnMut(Event)>::wrap(Box::new(move |_event: Event| {
        let _ = reload_window.location().reload();
    }));
    window()?.add_event_listener_with_callback("popstate", on_popstate.as_ref().unchecked_ref())?;
    on_popstate.forget();

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
    let Some(container) = document()?.query_selector("[data-detail-viewer-controls=\"song\"]")? else {
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
    let Some(scroll) = document()?
        .query_selector("[data-detail-viewer-scroll=\"1\"]")?
        .and_then(|node| node.dyn_into::<HtmlElement>().ok())
    else {
        return Ok(());
    };
    let Some(target) = document()?
        .get_element_by_id("chord-data")
        .and_then(|node| node.dyn_into::<HtmlElement>().ok())
    else {
        return Ok(());
    };
    let scopes = vec![scope];
    let targets = vec![target];
    let scrolls = vec![scroll];
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
    if let Some(form) = form()? {
        bind_song_transpose(form)?;
    }
    Ok(())
}
