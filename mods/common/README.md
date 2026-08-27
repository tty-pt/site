# common — shared utilities

Shared C utilities used by every site module. Not a standalone XY module —
`common.c` `#include`s five sub-files and only registers `mods/mpfd` on install.

## Sub-files

| File | Purpose |
|------|---------|
| `common_encoding.c` | `str_trim` (XY_IMPL) |
| `common_response.c` | HTTP response helpers, CSRF, `site_ui_respond_*` page renderers |
| `common_storage.c` | File I/O, path building, item CRUD, `slurp_file`, `write_file_path` |
| `common_strlist.c` | Comma-separated list helpers: `str_list_contains`, `str_list_append`, `str_list_normalize`, `str_list_for_each` |
| `ux/site_ui.c` | Page layout, `site_ui_action_form`, `site_ui_item_row`, `escape_html_into`, CSS/cache-bust, `parse_transpose_qs` |
| `ux/site_forms.c` | Declarative form generation (`site_ui_form_from_desc`), string-first pickers (`site_ui_picker`, `site_ui_row_replace_picker`) |

## Public API (via XY_DECL in `common.h`)

Other modules call these via `call_<name>()`. Key grps:

**Responses:** `respond_html`, `respond_json`, `respond_error`, `bad_request`,
`server_error`, `not_found`, `redirect_to_item`.

**Auth:** `require_user`, `csrf_check_mpfd`, `csrf_check_query`, `csrf_setup`.

**Storage:** `write_file_path`, `slurp_file`, `read_meta_file`, `write_meta_file`,
`meta_fields_read`, `meta_fields_write`, `write_item_child_file`,
`item_remove_path_recursive`, `ensure_dir_path`, `get_doc_root`, `resolve_doc_root`.

**Path building:** `item_path_build`, `item_path_build_root`, `module_path_build`,
`module_items_path_build`, `item_child_path`, `user_path_build`, `build_owner_path`.

**Page rendering:** `site_ui_respond_page`, `site_ui_respond_form_page`,
`site_ui_respond_add_page`, `site_ui_respond_edit_page`.

**Declarative UI Builders:** `site_ui_form_from_desc`, `site_ui_picker`,
`site_ui_row_replace_picker`, `site_ui_action_form`, `site_ui_item_row`.

## Related docs

- `docs/ARCHITECTURE.md` — module load order, XY contract.
- `docs/CONVENTIONS.md` — handler patterns, form parsing.
