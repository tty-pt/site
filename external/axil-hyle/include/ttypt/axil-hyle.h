#ifndef AXIL_HYLE_H
#define AXIL_HYLE_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Generic Axil-Hyle HTTP Bridge
 * Mounts standard REST CRUD endpoints (/api/dataset/...) and picker fragment
 * hot-swap endpoint (/pick/:id/options) for any Axil web application.
 */
void axil_hyle_install_routes(void);

typedef struct {
	const char *action;        /* "add", "remove", "replace", "update" */
	const char *module;        /* entity module name, e.g. "gig", "grp" */
	const char *parent_id;     /* parent item key */
	const char *partition_id;  /* partition source id, e.g. "gig.songs", "grp.songs" */
	int row_index;             /* 0-based row index in partition, or -1 */
	const char *item_key;      /* child item key */
	int fd;
	void *user_data;
} axil_hyle_partition_change_ctx_t;

typedef int (*axil_hyle_partition_change_fn)(const axil_hyle_partition_change_ctx_t *ctx);

typedef struct {
	const char *field;           /* schema field name, e.g. "transpose" */
	const char *aliases[4];      /* alternate parameter names, e.g. {"key", "t", NULL} */
} axil_hyle_param_alias_t;

typedef int (*axil_hyle_access_check_fn)(
	int fd, const char *module, const char *parent_id,
	const char *username, void *user_data);

typedef struct {
	const char *module;            /* e.g. "gig", "grp" */
	const char *child_resource;    /* route slug for single child, e.g. "song" */
	const char *children_resource; /* route slug for collection, e.g. "songs" (optional, defaults to child_resource + "s") */
	const char *partition_source;  /* dataset id, e.g. "gig.songs", "grp.songs" */
	const char *primary_field;     /* child identifier field in schema, e.g. "song" */
	const char *pin_field;         /* field name to set to "1" on manual add/update, or NULL */
	int positional;                /* 1 if routes use :n (e.g. gig), 0 if routes use :child_id (e.g. grp) */
	const char *update_action;     /* action slug for key-based update route, e.g. "key" or "update" (default "key") */
	/*
	 * Optional redirect pattern for form submissions, e.g. "/%s/%s" (formats with module, parent_id)
	 * or "/gig/%s" (formats with parent_id). If NULL, responds with JSON {"ok":true,"index":...}.
	 */
	const char *redirect_pattern;
	const axil_hyle_param_alias_t *aliases; /* NULL-terminated or field-matching alias table */
	axil_hyle_access_check_fn check_access; /* optional pluggable authorization callback */
	axil_hyle_partition_change_fn on_change;
	void *user_data;
} axil_hyle_partition_routes_spec_t;

/*
 * Layer 1: Execution engine - executes a partition sub-resource action against
 * a Hyle ordered dataset without registering HTTP routes.
 */
int axil_hyle_partition_execute(
	int fd, char *body,
	const char *action,
	const axil_hyle_partition_routes_spec_t *spec,
	const char *parent_id);

/*
 * Layer 2: Declarative route mounter - mounts standard sub-resource routes
 * in Axil using axil_hyle_partition_execute.
 */
int axil_hyle_register_partition_routes(const axil_hyle_partition_routes_spec_t *spec);

#ifdef __cplusplus
}
#endif

#endif /* AXIL_HYLE_H */
