pub fn body_str(b: &[u8]) -> &str {
    std::str::from_utf8(b).unwrap_or("")
}

pub fn display_or_id<'a>(title: &'a str, id: &'a str) -> &'a str {
    if title.is_empty() { id } else { title }
}

pub fn key_names(use_bemol: bool, use_latin: bool) -> &'static [&'static str] {
    match (use_bemol, use_latin) {
        (false, false) => &[
            "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
        ],
        (true, false) => &[
            "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B",
        ],
        (false, true) => &[
            "Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si",
        ],
        (true, true) => &[
            "Do", "Reb", "Re", "Mib", "Mi", "Fa", "Solb", "Sol", "Lab", "La", "Sib", "Si",
        ],
    }
}

pub fn key_transpose_options(
    original_key: i32,
    use_bemol: bool,
    use_latin: bool,
) -> Vec<(i32, String)> {
    key_names(use_bemol, use_latin)
        .iter()
        .enumerate()
        .map(|(i, key)| {
            let semitones = ((i as i32 - original_key) % 12 + 12) % 12;
            let suffix = if semitones == 0 { " (Original)" } else { "" };
            (semitones, format!("{key}{suffix}"))
        })
        .collect()
}
