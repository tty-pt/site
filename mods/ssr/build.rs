use std::env;
use std::fs;
use std::path::PathBuf;

fn main() {
    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let mods_dir = manifest_dir.join("..");
    let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());
    let generated = out_dir.join("generated_routes.rs");
    let mut modules: Vec<(String, PathBuf)> = Vec::new();
    let mut render_hooks: Vec<(String, PathBuf)> = Vec::new();

    println!("cargo:rerun-if-changed={}", mods_dir.display());

    for entry in fs::read_dir(&mods_dir).unwrap() {
        let entry = entry.unwrap();
        let path = entry.path();
        if !path.is_dir() {
            continue;
        }

        let name = entry.file_name().to_string_lossy().into_owned();
        if name == "ssr" || name == "core" {
            continue;
        }

        let route_file = path.join("ssr.rs");
        let route_file = if route_file.is_file() {
            route_file.canonicalize().unwrap()
        } else {
            let alt = path.join("ssr/src/main.rs");
            if alt.is_file() {
                alt.canonicalize().unwrap()
            } else {
                continue;
            }
        };
        println!("cargo:rerun-if-changed={}", route_file.display());
        modules.push((name.clone(), route_file));

        let hooks_file = path.join("ssr/render_hooks.rs");
        if hooks_file.is_file() {
            let hooks_file = hooks_file.canonicalize().unwrap();
            println!("cargo:rerun-if-changed={}", hooks_file.display());
            render_hooks.push((name, hooks_file));
        }
    }

    modules.sort();
    render_hooks.sort();

    // Generate the route dispatcher and per-module render-hook submodule includes.
    let mut source = String::new();

    for (module, path) in &modules {
        let escaped = path.display().to_string().replace('\\', "\\\\");
        source.push_str(&format!("#[path = \"{escaped}\"]\nmod {module};\n"));
    }

    for (module, path) in &render_hooks {
        let escaped = path.display().to_string().replace('\\', "\\\\");
        source.push_str(&format!("#[path = \"{escaped}\"]\nmod {module}_hooks;\n"));
    }

    source.push_str("\npub(crate) fn route(ctx: &RequestContext) -> ResponsePayload {\n");
    for (module, _) in &modules {
        source.push_str(&format!(
            "\tif let Some(response) = {module}::route(ctx) {{\n\t\treturn response;\n\t}}\n"
        ));
    }
    source.push_str(
		"\tcrate::shared::html_response_with_status(\n\t\t404,\n\t\t\"404\",\n\t\tcrate::site_ui::error_page(crate::shared::current_user(ctx), &ctx.path, 404, \"Not found\"),\n\t)\n}\n",
	);

    fs::write(generated, source).unwrap();
}
