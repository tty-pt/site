use url::form_urlencoded;

pub fn split_path(path: &str) -> Vec<&str> {
    path.trim_matches('/')
        .split('/')
        .filter(|part| !part.is_empty())
        .collect()
}

pub fn parse_pairs(text: &str) -> Vec<(String, String)> {
    form_urlencoded::parse(text.as_bytes())
        .into_owned()
        .collect()
}

pub fn get_pair<'a>(pairs: &'a [(String, String)], key: &str) -> Option<&'a str> {
    pairs
        .iter()
        .rev()
        .find(|(k, _)| k == key)
        .map(|(_, v)| v.as_str())
}

pub fn parent_path(path: &str) -> String {
    let mut parts: Vec<&str> = split_path(path);
    parts.pop();
    if parts.is_empty() {
        "/".to_string()
    } else {
        format!("/{}", parts.join("/"))
    }
}

pub fn collection_path(module: &str) -> String {
    format!("/{module}/")
}

pub fn item_path(module: &str, id: &str) -> String {
    format!("/{module}/{id}")
}

pub fn item_action_path(module: &str, id: &str, action: &str) -> String {
    format!("/{module}/{id}/{action}")
}

pub fn auth_path(action: &str) -> String {
    format!("/auth/{action}")
}

pub fn edit_path(module: &str, id: &str) -> String {
    if id.is_empty() {
        format!("/{module}/add")
    } else {
        format!("/{module}/{id}/edit")
    }
}
