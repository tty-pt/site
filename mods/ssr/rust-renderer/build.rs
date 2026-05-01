use std::env;
use std::fs;
use std::path::PathBuf;

fn main() {
	let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
	let mods_dir = manifest_dir.join("../..");
	let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());
	let generated = out_dir.join("generated_routes.rs");
	let mut modules: Vec<(String, PathBuf)> = Vec::new();
	let mut ffi_modules: Vec<(String, PathBuf)> = Vec::new();

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

		let ffi_file = path.join("ssr/render_ffi.rs");
		if ffi_file.is_file() {
			let ffi_file = ffi_file.canonicalize().unwrap();
			println!("cargo:rerun-if-changed={}", ffi_file.display());
			ffi_modules.push((name, ffi_file));
		}
	}

	modules.sort();
	ffi_modules.sort();

	// Generate ssr_ffi.h — scan lib.rs plus every module's render_ffi.rs so that
	// module-specific #[repr(C)] structs (defined there) appear in the header.
	let header_out = manifest_dir.join("../ssr_ffi.h");
	let config = cbindgen::Config::from_file(manifest_dir.join("cbindgen.toml"))
		.expect("cbindgen.toml not found");
	let mut builder = cbindgen::Builder::new()
		.with_src(manifest_dir.join("src/lib.rs"));
	for (_, ffi_path) in &ffi_modules {
		builder = builder.with_src(ffi_path);
	}
	builder
		.with_config(config)
		.generate()
		.expect("cbindgen failed to generate ssr_ffi.h")
		.write_to_file(&header_out);

	// Generate the route dispatcher and per-module FFI submodule includes.
	let mut source = String::new();

	for (module, path) in &modules {
		let escaped = path.display().to_string().replace('\\', "\\\\");
		source.push_str(&format!(
			"#[path = \"{escaped}\"]\nmod {module};\n"
		));
	}

	for (module, path) in &ffi_modules {
		let escaped = path.display().to_string().replace('\\', "\\\\");
		source.push_str(&format!(
			"#[path = \"{escaped}\"]\nmod {module}_ffi;\n"
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

	fs::write(generated, source).unwrap();
}
