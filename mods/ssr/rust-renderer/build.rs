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
		if route_file.is_file() {
			println!("cargo:rerun-if-changed={}", route_file.display());
			modules.push((name, route_file.canonicalize().unwrap()));
		}
	}

	modules.sort();

	let mut source = String::from(
		"use crate::{RequestContext, ResponsePayload, current_user, error_page, html_response_with_status};\n",
	);

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
		"\thtml_response_with_status(\n\t\t404,\n\t\t\"404\",\n\t\terror_page(current_user(ctx), &ctx.path, 404, \"Not found\"),\n\t)\n}\n",
	);

	fs::write(generated, source).unwrap();
}
