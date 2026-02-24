#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <ttypt/ndc.h>
#include <ttypt/ndx.h>
#include <ttypt/qmap.h>

#include "papi.h"

static void
api_index_handler(int fd, char *body)
{
	(void)body;
	
	char doc_root[256] = { 0 };
	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
	
	char db_path[512];
	snprintf(db_path, sizeof(db_path), "%s/items/index-en.db", doc_root[0] ? doc_root : ".");
	
	uint32_t hd = qmap_open(db_path, "hd", QM_STR, QM_STR, 0xFF, 0);
	if (!hd) {
		ndc_header(fd, "Content-Type", "application/json");
		ndc_head(fd, 500);
		ndc_body(fd, "{\"error\":\"Failed to open db\"}");
		return;
	}

	ndc_header(fd, "Content-Type", "application/json");
	ndc_header(fd, "Access-Control-Allow-Origin", "*");
	ndc_head(fd, 200);
	ndc_write(fd, "[", 1);
	
	uint32_t cur = qmap_iter(hd, NULL, 0);
	const void *key, *value;
	int first = 1;
	
	while (qmap_next(&key, &value, cur)) {
		if (!first) ndc_writef(fd, ",");
		first = 0;
		
		const char *k = (const char *)key;
		const char *v = (const char *)value;
		
		char title[256] = { 0 };
		const char *sp = strchr(v, ' ');
		if (sp) {
			strncpy(title, sp + 1, sizeof(title) - 1);
		}
		
		ndc_writef(fd, "{\"link\":\"%s\",\"title\":\"%s\"}", k, title);
	}
	qmap_fin(cur);
	
	ndc_writef(fd, "]");
	qmap_close(hd);
	ndc_close(fd);
}

MODULE_API void
ndx_install(void)
{
	ndc_register_handler("GET:/api/index", api_index_handler);
}

MODULE_API void
ndx_open(void)
{
}

MODULE_API ndx_t *
get_ndx_ptr(void)
{
	static ndx_t ndx;
	return &ndx;
}
