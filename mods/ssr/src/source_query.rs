use std::collections::HashSet;
use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_uint, c_void};
use std::ptr;

use ndx::prelude::*;
use crate::NDX;
use qmap::Qmap;
use hyle::{FieldStorageType, ModelResult, Row, Source, Value};

/// source_query NDX hook: filter+sort+paginate a dataset.
/// Returns a qmap handle (u32) with row IDs as keys and empty values,
/// plus `__total__` key with total count before pagination.
#[ndx_hook_decl]
pub fn source_query(dataset_id: *const c_char, query_str: *const c_char) -> c_uint {}

/// source_find NDX hook: get a dataset definition by ID (opaque pointer).
/// Kept for callers that need the raw pointer (e.g. site_ui load path).
#[ndx_hook_decl]
pub fn source_find(id: *const c_char) -> *const c_void {}

/// source_get_data_hd: returns the full-data qmap handle for a dataset.
#[ndx_hook_decl]
pub fn source_get_data_hd(id: *const c_char) -> c_uint {}

/// source_get_fields_hd: returns the fields qmap handle for a dataset.
#[ndx_hook_decl]
pub fn source_get_fields_hd(id: *const c_char) -> c_uint {}

/// source_get_schema_hd: returns a fresh QM_STR qmap handle (caller owns it).
/// Key = field name; value = JSON {"t":N[,"s":"target_source"[,"i":"inverse"]]}.
#[ndx_hook_decl]
pub fn source_get_schema_hd(id: *const c_char) -> c_uint {}

// ── Schema parsing ────────────────────────────────────────────────────────────

struct FieldSchema {
	name: String,
	field_type: FieldStorageType,
	target_source: Option<String>,
	inverse_name: Option<String>,
}

fn parse_schema(schema_qmap: &Qmap) -> Vec<FieldSchema> {
	let mut fields = Vec::new();
	let mut cursor = schema_qmap.iter(ptr::null(), 0);
	while let Some((key_ptr, val_ptr)) = cursor.next() {
		let name = unsafe { CStr::from_ptr(key_ptr as *const c_char) }
			.to_string_lossy()
			.into_owned();
		let json_str = unsafe { CStr::from_ptr(val_ptr as *const c_char) }
			.to_str()
			.unwrap_or("{}");
		if let Ok(serde_json::Value::Object(map)) = serde_json::from_str(json_str) {
			let t = map.get("t").and_then(|v| v.as_u64()).unwrap_or(0) as u32;
			let target_source = map
				.get("s")
				.and_then(|v| v.as_str())
				.filter(|s| !s.is_empty())
				.map(|s| s.to_string());
			let inverse_name = map
				.get("i")
				.and_then(|v| v.as_str())
				.filter(|s| !s.is_empty())
				.map(|s| s.to_string());
			fields.push(FieldSchema {
				name,
				field_type: FieldStorageType::from_u32(t),
				target_source,
				inverse_name,
			});
		}
	}
	fields
}

// ── Row builder ───────────────────────────────────────────────────────────────

/// Build a hyle Source from qmap handles, without touching SourceDefC/FieldDefC.
///
/// If `fields_hd == 0`, the dataset stores rows as JSON strings (QM_STR path).
/// Otherwise reads field values from `fields_hd` using field schema from
/// `schema_hd` (obtained via source_get_schema_hd).
fn build_source(
	dataset_id: &str,
	data_hd: u32,
	fields_hd: u32,
	schema_hd: u32,
) -> Source {
	let model_id = dataset_id.strip_suffix(".items").unwrap_or(dataset_id);
	let data_qmap = unsafe { Qmap::from_handle(data_hd) };

	let rows = if fields_hd == 0 {
		build_rows_json(&data_qmap)
	} else {
		let schema_qmap = unsafe { Qmap::from_handle(schema_hd) };
		let fields = parse_schema(&schema_qmap);
		// schema_qmap is owned (fresh from source_get_schema_hd) — let it drop
		let fqmap = unsafe { Qmap::from_handle(fields_hd) };
		let rows = build_rows_struct(&data_qmap, &fqmap, &fields);
		std::mem::forget(fqmap);
		rows
	};

	std::mem::forget(data_qmap);

	let mut mr = ModelResult::many(rows);
	mr.total = mr.rows().len();

	let mut source = Source::new();
	source.insert(model_id.to_string(), mr);
	source
}

/// JSON/QM_STR path: each qmap value is a JSON object string.
fn build_rows_json(data_qmap: &Qmap) -> Vec<Row> {
	let mut rows = Vec::new();
	let mut cursor = data_qmap.iter(ptr::null(), 0);
	while let Some((_key_ptr, val_ptr)) = cursor.next() {
		let c_str = unsafe { CStr::from_ptr(val_ptr as *const c_char) };
		if let Ok(json_str) = c_str.to_str() {
			if let Ok(serde_json::Value::Object(map)) = serde_json::from_str(json_str) {
				let mut row: Row = indexmap::IndexMap::new();
				for (k, v) in map {
					row.insert(k, json_to_value(v));
				}
				rows.push(row);
			}
		}
	}
	rows
}

/// Raw-struct path: read each field value from fields_hd using its encoded key.
fn build_rows_struct(data_qmap: &Qmap, fqmap: &Qmap, fields: &[FieldSchema]) -> Vec<Row> {
	let mut rows = Vec::new();
	let mut cursor = data_qmap.iter(ptr::null(), 0);
	while let Some((key_ptr, _)) = cursor.next() {
		let item_id = unsafe { CStr::from_ptr(key_ptr as *const c_char) }
			.to_string_lossy()
			.into_owned();

		let mut row: Row = indexmap::IndexMap::new();
		row.insert("id".to_string(), Value::String(item_id.clone()));

		for f in fields {
			if f.name == "id" {
				continue;
			}
			let value = read_field_value(&item_id, fqmap, f);
			row.insert(f.name.clone(), value);
		}
		rows.push(row);
	}
	rows
}

fn read_field_value(item_id: &str, fqmap: &Qmap, f: &FieldSchema) -> Value {
	let qry = format!("{}:{}", item_id, f.name);
	match f.field_type {
		FieldStorageType::Reference => {
			let c_qry = match CString::new(qry) {
				Ok(s) => s,
				Err(_) => return Value::Null,
			};
			let ptr = fqmap.get(c_qry.as_ptr() as *const std::ffi::c_void);
			if ptr.is_null() {
				return Value::Null;
			}
			let pos = unsafe { *(ptr as *const u32) };
			if pos == u32::MAX {
				return Value::Null;
			}
			resolve_id_at_pos(f.target_source.as_deref(), pos)
		}
		FieldStorageType::MultiReference => {
			let s = fqmap.get_str(&qry).unwrap_or_default();
			let items = s
				.split('\n')
				.filter(|x| !x.is_empty())
				.map(|pos_str| {
					let pos: u32 = pos_str.parse().unwrap_or(u32::MAX);
					if pos == u32::MAX {
						Value::Null
					} else {
						resolve_id_at_pos(f.target_source.as_deref(), pos)
					}
				})
				.collect();
			Value::Array(items)
		}
		FieldStorageType::Inverse => {
			let Some(ref target_id) = f.target_source else {
				return Value::Null;
			};
			let Some(ref inv_name) = f.inverse_name else {
				return Value::Null;
			};
			let id_c = match CString::new(item_id) {
				Ok(s) => s,
				Err(_) => return Value::Null,
			};
			let self_fhd = fqmap.handle();
			let pos = unsafe {
				qmap::qmap_pos(self_fhd, id_c.as_ptr())
			};
			if pos == u32::MAX {
				return Value::Null;
			}
			let target_fhd = get_fields_hd_for(target_id);
			if target_fhd == 0 {
				return Value::Null;
			}
			let inv_c = match CString::new(inv_name.as_str()) {
				Ok(s) => s,
				Err(_) => return Value::Null,
			};
			let mut buf = [0u32; 4096];
			let count = unsafe {
				qmap::qmap_inv_get(
					target_fhd,
					inv_c.as_ptr(),
					pos,
					buf.as_mut_ptr(),
					4096,
				)
			};
			let items = buf[..count]
				.iter()
				.map(|&p| {
					let id_ptr = unsafe { qmap::qmap_get_key(target_fhd, p) };
					if id_ptr.is_null() {
						Value::Null
					} else {
						Value::String(
							unsafe { CStr::from_ptr(id_ptr) }
								.to_string_lossy()
								.into_owned(),
						)
					}
				})
				.collect();
			Value::Array(items)
		}
		FieldStorageType::Int => {
			let s = fqmap.get_str(&qry).unwrap_or("0");
			Value::Int(s.parse().unwrap_or(0))
		}
		FieldStorageType::Bool => {
			let s = fqmap.get_str(&qry).unwrap_or_default();
			Value::Bool(s == "1" || s == "true")
		}
		FieldStorageType::NullableString => match fqmap.get_str(&qry) {
			Some(s) if !s.is_empty() => Value::String(s.to_string()),
			_ => Value::Null,
		},
		FieldStorageType::String => {
			Value::String(fqmap.get_str(&qry).unwrap_or_default().to_string())
		}
	}
}

/// Resolve a position in a target source's fields_hd to its item ID string.
fn resolve_id_at_pos(target_source: Option<&str>, pos: u32) -> Value {
	let Some(target_id) = target_source else {
		return Value::Null;
	};
	let thd = get_fields_hd_for(target_id);
	if thd == 0 {
		return Value::Null;
	}
	let id_ptr = unsafe { qmap::qmap_get_key(thd, pos) };
	if id_ptr.is_null() {
		Value::Null
	} else {
		Value::String(
			unsafe { CStr::from_ptr(id_ptr) }
				.to_string_lossy()
				.into_owned(),
		)
	}
}

/// Call source_get_fields_hd for a target dataset to resolve references.
fn get_fields_hd_for(dataset_id: &str) -> u32 {
	let Ok(id_c) = CString::new(dataset_id) else {
		return 0;
	};
	unsafe { source_get_fields_hd(id_c.as_ptr()) }
}

fn json_to_value(v: serde_json::Value) -> Value {
	match v {
		serde_json::Value::String(s) => Value::String(s),
		serde_json::Value::Number(n) => {
			if let Some(i) = n.as_i64() {
				Value::Int(i)
			} else if let Some(f) = n.as_f64() {
				Value::Float(f)
			} else {
				Value::Int(0)
			}
		}
		serde_json::Value::Bool(b) => Value::Bool(b),
		serde_json::Value::Null => Value::Null,
		serde_json::Value::Array(arr) => {
			Value::Array(arr.into_iter().map(json_to_value).collect())
		}
		serde_json::Value::Object(obj) => {
			Value::Map(obj.into_iter().map(|(k, v)| (k, json_to_value(v))).collect())
		}
	}
}

// ── Public API ────────────────────────────────────────────────────────────────

/// Query a dataset via C-side hooks, returning a hyle Source filtered to a page.
///
/// If `default_per_page` is `Some(n)` and the query string lacks `per_page=`,
/// injects `page=1&per_page=N` so C-side applies pagination.
pub fn query_source(dataset_id: &str, qs: &str, default_per_page: Option<usize>) -> Option<Source> {
	let effective_qs = match default_per_page {
		Some(pp) if !qs.contains("per_page=") => {
			if qs.is_empty() {
				format!("page=1&per_page={}", pp)
			} else {
				format!("page=1&per_page={}&{}", pp, qs)
			}
		}
		_ => qs.to_owned(),
	};

	let id_c = CString::new(dataset_id).ok()?;
	let qs_c = CString::new(effective_qs).ok()?;

	// Get source data handle (fails if dataset not registered)
	let data_hd = unsafe { source_get_data_hd(id_c.as_ptr()) };
	if data_hd == 0 {
		return None;
	}
	let fields_hd = unsafe { source_get_fields_hd(id_c.as_ptr()) };
	let schema_hd = if fields_hd != 0 {
		unsafe { source_get_schema_hd(id_c.as_ptr()) }
	} else {
		0
	};

	// Call source_query — returns qmap handle with filtered row IDs + __total__
	let result_hd = unsafe { source_query(id_c.as_ptr(), qs_c.as_ptr()) };
	if result_hd == 0 {
		return None;
	}
	let result_qmap = unsafe { Qmap::from_handle(result_hd) };

	// Extract total count from __total__ key
	let total = result_qmap
		.get_str("__total__")
		.and_then(|s| s.parse::<usize>().ok())
		.unwrap_or(0);

	// Collect matching row IDs (skip __total__)
	let mut ids: HashSet<String> = HashSet::new();
	{
		let mut cursor = result_qmap.iter(ptr::null(), 0);
		while let Some((key_ptr, _)) = cursor.next() {
			let key = unsafe { CStr::from_ptr(key_ptr as *const c_char) }
				.to_string_lossy()
				.into_owned();
			if key != "__total__" {
				ids.insert(key);
			}
		}
	}
	std::mem::forget(result_qmap);

	// Build full source from all data, then filter to the page IDs
	let full_source = build_source(dataset_id, data_hd, fields_hd, schema_hd);

	let model_id = dataset_id.strip_suffix(".items").unwrap_or(dataset_id);
	let mr = match full_source.get(model_id) {
		Some(mr) => {
			let rows: Vec<Row> = mr
				.rows()
				.into_iter()
				.filter(|row| {
					row.get("id")
						.and_then(|v| {
							if let Value::String(s) = v {
								Some(s.as_str())
							} else {
								None
							}
						})
						.map_or(false, |id| ids.contains(id))
				})
				.collect();
			let mut result = ModelResult::many(rows);
			result.total = total;
			result
		}
		None => ModelResult::many(vec![]),
	};

	let mut source = Source::new();
	source.insert(model_id.to_string(), mr);

	Some(source)
}

/// Load a full source (all rows, no pagination) for a dataset.
/// Used by QmapProvider::load_source (site_ui's load_typed_item path).
pub fn load_source_full(dataset_id: &str) -> Option<Source> {
	let id_c = CString::new(dataset_id).ok()?;
	let data_hd = unsafe { source_get_data_hd(id_c.as_ptr()) };
	if data_hd == 0 {
		return None;
	}
	let fields_hd = unsafe { source_get_fields_hd(id_c.as_ptr()) };
	let schema_hd = if fields_hd != 0 {
		unsafe { source_get_schema_hd(id_c.as_ptr()) }
	} else {
		0
	};
	Some(build_source(dataset_id, data_hd, fields_hd, schema_hd))
}
