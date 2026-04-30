use std::env;
use std::fs;
use std::path::PathBuf;

fn main() {
	let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
	let mods_dir = manifest_dir.join("../..");
	let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());
	let generated = out_dir.join("generated_routes.rs");
	let mut modules = Vec::new();

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
		modules.push((name, route_file));
	}

	modules.sort();

	let mut source = String::new();

	for (module, path) in &modules {
		let path = path.display().to_string().replace('\\', "\\\\");
		source.push_str(&format!(
			"#[path = \"{path}\"]\nmod {module};\n"
		));
	}

	source.push_str("\npub(crate) fn route(ctx: &RequestContext) -> ResponsePayload {\n");
	for (module, _) in &modules {
		source.push_str(&format!(
			"\tif let Some(response) = {module}::route(ctx) {{\n\t\treturn response;\n\t}}\n"
		));
	}
	source.push_str(
		"\tcrate::shared::html_response_with_status(
\t\t404,
\t\t\"404\",
\t\tcrate::shared::error_page(crate::shared::current_user(ctx), &ctx.path, 404, \"Not found\"),
\t)\n}\n",
	);

	source.push_str("\npub(crate) fn dispatch_item(module: &str, action: &str, id: &str, ctx: &RequestContext) -> Option<ResponsePayload> {\n");
	source.push_str("\tmatch (module, action) {\n");
	for (module, _) in &modules {
		if module == "auth" || module == "index" {
			continue;
		}
		source.push_str(&format!(
			"\t\t(\"{module}\", \"detail\") => Some({module}::render_detail(ctx, id)),\n"
		));
		source.push_str(&format!(
			"\t\t(\"{module}\", \"edit\") => Some({module}::render_edit(ctx, id)),\n"
		));
		source.push_str(&format!(
			"\t\t(\"{module}\", \"delete\") => Some(crate::render_delete_confirm(ctx, \"{module}\", id)),\n"
		));
	}
	source.push_str("\t\t_ => None,\n");
	source.push_str("\t}\n}\n");

	fs::write(generated, source).unwrap();
}
