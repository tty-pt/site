#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>

#include <ttypt/axil.h>
#include <ttypt/xy-mod.h>
#include <ttypt/corm.h>
#include <ttypt/sepal.h>
#include <ttypt/lemma.h>

#include <json-c/json.h>

#include "ttypt/axil-embed-search.h"

static embed_search_state_t g_state;

static void
respond_error_json(socket_t fd, int code, const char *msg)
{
	struct json_object *obj = json_object_new_object();
	if (obj) {
		json_object_object_add(obj, "ok", json_object_new_boolean(0));
		json_object_object_add(obj, "error", json_object_new_string(msg ? msg : "unknown error"));
		const char *s = json_object_to_json_string(obj);
		axil_respond_json(fd, code, s);
		json_object_put(obj);
	} else {
		axil_respond_plain(fd, code, msg ? msg : "internal server error");
	}
}

int
embed_search_init(const char *url, const char *model, const char *db_path)
{
	int err = 0;
	const char *env_url = getenv("AUREN_EMBED_URL");
	if (!env_url || !env_url[0])
		env_url = getenv("CORM_SEPAL_EMBED_URL");
	if (!env_url || !env_url[0])
		env_url = url;
	if (!env_url || !env_url[0])
		env_url = AXIL_EMBED_SEARCH_DEFAULT_URL;

	const char *env_model = getenv("AUREN_EMBED_MODEL");
	if (!env_model || !env_model[0])
		env_model = getenv("CORM_SEPAL_EMBED_MODEL");
	if (!env_model || !env_model[0])
		env_model = model;
	if (!env_model || !env_model[0])
		env_model = AXIL_EMBED_SEARCH_DEFAULT_MODEL;

	const char *env_db = getenv("SEPAL_DB_PATH");
	if (!env_db || !env_db[0])
		env_db = getenv("EMBED_SEARCH_DB");
	if (!env_db || !env_db[0])
		env_db = db_path;

	snprintf(g_state.url, sizeof(g_state.url), "%s", env_url);
	snprintf(g_state.model, sizeof(g_state.model), "%s", env_model);
	if (env_db && env_db[0])
		snprintf(g_state.db_path, sizeof(g_state.db_path), "%s", env_db);
	else
		g_state.db_path[0] = '\0';

	if (sepal_configure_embeddings(g_state.url, g_state.model, NULL) != 0) {
		fprintf(stderr, "axil-embed-search: failed to configure embeddings (%s, %s)\n",
		        g_state.url, g_state.model);
		return -1;
	}

	g_state.vs = sepal_open(g_state.db_path[0] ? g_state.db_path : NULL, &err);
	if (!g_state.vs) {
		fprintf(stderr, "axil-embed-search: failed to open sepal vector store (err=%d)\n", err);
		return -1;
	}

	g_state.ldb = lemma_open(0);
	if (!g_state.ldb) {
		fprintf(stderr, "axil-embed-search: failed to open lemma database\n");
		sepal_close(g_state.vs);
		g_state.vs = NULL;
		return -1;
	}

	g_state.text_hd = corm_open(NULL, NULL, CM_STR, CM_STR, 1023, 0);
	if (g_state.text_hd == CM_MISS) {
		fprintf(stderr, "axil-embed-search: failed to open corm text storage map\n");
		lemma_close(g_state.ldb);
		g_state.ldb = NULL;
		sepal_close(g_state.vs);
		g_state.vs = NULL;
		return -1;
	}

	g_state.next_ref = 1;
	g_state.initialized = 1;
	return 0;
}

void
embed_search_cleanup(void)
{
	if (!g_state.initialized)
		return;

	if (g_state.vs) {
		sepal_close(g_state.vs);
		g_state.vs = NULL;
	}
	if (g_state.ldb) {
		lemma_close(g_state.ldb);
		g_state.ldb = NULL;
	}
	if (g_state.text_hd != CM_MISS && g_state.text_hd != 0) {
		corm_close(g_state.text_hd);
		g_state.text_hd = 0;
	}
	g_state.initialized = 0;
}

int
handle_post_embeddings(socket_t cfd, char *body)
{
	if (!g_state.initialized) {
		respond_error_json(cfd, 500, "embed-search module not initialized");
		return 0;
	}

	if (!body || !body[0]) {
		respond_error_json(cfd, 400, "empty request body");
		return 0;
	}

	struct json_object *req = json_tokener_parse(body);
	if (!req || !json_object_is_type(req, json_type_object)) {
		if (req)
			json_object_put(req);
		respond_error_json(cfd, 400, "invalid JSON payload");
		return 0;
	}

	struct json_object *val_obj = json_object_object_get(req, "prompt");
	if (!val_obj)
		val_obj = json_object_object_get(req, "text");
	if (!val_obj)
		val_obj = json_object_object_get(req, "input");

	if (!val_obj || !json_object_is_type(val_obj, json_type_string)) {
		json_object_put(req);
		respond_error_json(cfd, 400, "missing or invalid 'prompt' (expected string)");
		return 0;
	}

	const char *prompt_str = json_object_get_string(val_obj);
	if (!prompt_str || !prompt_str[0]) {
		json_object_put(req);
		respond_error_json(cfd, 400, "'prompt' must not be empty");
		return 0;
	}

	if (strlen(prompt_str) > 32768) {
		json_object_put(req);
		respond_error_json(cfd, 400, "'prompt' exceeds maximum supported length (32KB)");
		return 0;
	}

	uint32_t ref = 0;
	struct json_object *ref_obj = json_object_object_get(req, "ref");
	if (!ref_obj)
		ref_obj = json_object_object_get(req, "id");
	if (ref_obj) {
		if (json_object_is_type(ref_obj, json_type_int)) {
			int64_t v = json_object_get_int64(ref_obj);
			if (v > 0)
				ref = (uint32_t)v;
		} else if (json_object_is_type(ref_obj, json_type_string)) {
			const char *s = json_object_get_string(ref_obj);
			if (s && s[0]) {
				long v = strtol(s, NULL, 10);
				if (v > 0)
					ref = (uint32_t)v;
			}
		}
	}

	if (ref == 0) {
		ref = g_state.next_ref++;
	} else if (ref >= g_state.next_ref) {
		g_state.next_ref = ref + 1;
	}

	float *vec = NULL;
	size_t dim = 0;
	int fetch_rc = sepal_embed_fetch(prompt_str, &vec, &dim);
	if (fetch_rc != 0 || !vec || dim == 0) {
		json_object_put(req);
		char errbuf[768];
		snprintf(errbuf, sizeof(errbuf), "failed to fetch embedding from %s", g_state.url);
		respond_error_json(cfd, 502, errbuf);
		return 0;
	}

	int put_rc = sepal_put(g_state.vs, ref, vec, dim);
	free(vec);
	if (put_rc != 0) {
		json_object_put(req);
		respond_error_json(cfd, 500, "failed to persist vector to sepal");
		return 0;
	}

	lemma_unindex_ref(g_state.ldb, "text", ref);
	lemma_index_ref(g_state.ldb, "text", ref, prompt_str);

	char kbuf[32];
	snprintf(kbuf, sizeof(kbuf), "%u", ref);
	corm_put(g_state.text_hd, kbuf, prompt_str);

	struct json_object *resp = json_object_new_object();
	if (resp) {
		json_object_object_add(resp, "ok", json_object_new_boolean(1));
		json_object_object_add(resp, "ref", json_object_new_int64(ref));
		json_object_object_add(resp, "dim", json_object_new_int64(dim));
		json_object_object_add(resp, "prompt", json_object_new_string(prompt_str));
		const char *resp_str = json_object_to_json_string(resp);
		axil_respond_json(cfd, 200, resp_str);
		json_object_put(resp);
	} else {
		axil_respond_json(cfd, 200, "{\"ok\":true}");
	}

	json_object_put(req);
	return 0;
}

static int
compare_hits_desc(const void *a, const void *b)
{
	const struct json_object * const *ja = (const struct json_object * const *)a;
	const struct json_object * const *jb = (const struct json_object * const *)b;
	struct json_object *sa = json_object_object_get(*ja, "score");
	struct json_object *sb = json_object_object_get(*jb, "score");
	double da = sa ? json_object_get_double(sa) : 0.0;
	double db = sb ? json_object_get_double(sb) : 0.0;
	if (da < db)
		return 1;
	if (da > db)
		return -1;
	return 0;
}

int
handle_get_search(socket_t cfd, char *body)
{
	if (!g_state.initialized) {
		respond_error_json(cfd, 500, "embed-search module not initialized");
		return 0;
	}

	char qbuf[AXIL_EMBED_SEARCH_MAX_QUERY_LEN] = {0};
	if (axil_req_param(cfd, body, "q", qbuf, sizeof(qbuf)) < 0) {
		if (axil_req_param(cfd, body, "query", qbuf, sizeof(qbuf)) < 0)
			qbuf[0] = '\0';
	}

	if (!qbuf[0]) {
		respond_error_json(cfd, 400, "missing search query parameter 'q' or 'query'");
		return 0;
	}

	char kbuf[32] = {0};
	size_t limit = AXIL_EMBED_SEARCH_DEFAULT_LIMIT;
	if (axil_req_param(cfd, body, "k", kbuf, sizeof(kbuf)) >= 0 ||
	    axil_req_param(cfd, body, "limit", kbuf, sizeof(kbuf)) >= 0)
	{
		long v = strtol(kbuf, NULL, 10);
		if (v > 0) {
			limit = (size_t)v;
			if (limit > AXIL_EMBED_SEARCH_MAX_HITS)
				limit = AXIL_EMBED_SEARCH_MAX_HITS;
		}
	}

	char mbuf[32] = {0};
	search_mode_t mode = SEARCH_MODE_SEPAL;
	if (axil_req_param(cfd, body, "mode", mbuf, sizeof(mbuf)) >= 0) {
		if (strcasecmp(mbuf, "lemma") == 0)
			mode = SEARCH_MODE_LEMMA;
		else if (strcasecmp(mbuf, "hybrid") == 0)
			mode = SEARCH_MODE_HYBRID;
		else
			mode = SEARCH_MODE_SEPAL;
	}

	char simbuf[32] = {0};
	float min_sim = 0.0f;
	if (axil_req_param(cfd, body, "min_sim", simbuf, sizeof(simbuf)) >= 0) {
		float f = strtof(simbuf, NULL);
		if (f > 0.0f)
			min_sim = f;
	}

	struct json_object *resp = json_object_new_object();
	struct json_object *hits_arr = json_object_new_array();
	if (!resp || !hits_arr) {
		if (resp) json_object_put(resp);
		if (hits_arr) json_object_put(hits_arr);
		respond_error_json(cfd, 500, "memory allocation error");
		return 0;
	}

	if (mode == SEARCH_MODE_SEPAL || mode == SEARCH_MODE_HYBRID) {
		float *qvec = NULL;
		size_t qdim = 0;
		int fetch_rc = sepal_embed_fetch(qbuf, &qvec, &qdim);
		if (fetch_rc != 0 || !qvec || qdim == 0) {
			if (qvec) {
				free(qvec);
				qvec = NULL;
			}
			json_object_put(resp);
			json_object_put(hits_arr);
			char errbuf[768];
			snprintf(errbuf, sizeof(errbuf), "failed to embed query via %s", g_state.url);
			respond_error_json(cfd, 502, errbuf);
			return 0;
		}

		sepal_hit_t hits[AXIL_EMBED_SEARCH_MAX_HITS];
		size_t nhits = sepal_search(g_state.vs, qvec, qdim, limit, min_sim, 0, hits);
		free(qvec);
		qvec = NULL;

		uint32_t lemma_matches[AXIL_EMBED_SEARCH_MAX_HITS];
		size_t n_lemma = 0;
		if (mode == SEARCH_MODE_HYBRID) {
			uint32_t out_hd = corm_open(NULL, NULL, CM_STR, CM_STR, 255, 0);
			if (out_hd != CM_MISS) {
				int handled = 0;
				lemma_query(g_state.ldb, "text", qbuf, out_hd, &handled);
				if (handled) {
					uint32_t cur = corm_iter(out_hd, NULL, 0);
					const void *k_ptr, *v_ptr;
					while (corm_next(&k_ptr, &v_ptr, cur) && n_lemma < AXIL_EMBED_SEARCH_MAX_HITS) {
						lemma_matches[n_lemma++] = (uint32_t)strtoul((const char *)k_ptr, NULL, 10);
					}
					corm_fin(cur);
				}
				corm_close(out_hd);
			}
		}

		for (size_t i = 0; i < nhits; i++) {
			struct json_object *hit_obj = json_object_new_object();
			if (!hit_obj)
				continue;

			float score = hits[i].score;
			int lexical_match = 0;
			if (mode == SEARCH_MODE_HYBRID) {
				for (size_t j = 0; j < n_lemma; j++) {
					if (lemma_matches[j] == hits[i].ref) {
						lexical_match = 1;
						break;
					}
				}
				if (lexical_match)
					score += 0.15f; /* Hybrid boost */
			}

			char kbuf[32];
			snprintf(kbuf, sizeof(kbuf), "%u", hits[i].ref);
			const char *stored_text = (const char *)corm_get(g_state.text_hd, kbuf);

			json_object_object_add(hit_obj, "ref", json_object_new_int64(hits[i].ref));
			json_object_object_add(hit_obj, "score", json_object_new_double((double)score));
			json_object_object_add(hit_obj, "prompt",
			                       json_object_new_string(stored_text ? stored_text : ""));
			if (mode == SEARCH_MODE_HYBRID) {
				json_object_object_add(hit_obj, "lexical_match",
				                       json_object_new_boolean(lexical_match));
			}
			json_object_array_add(hits_arr, hit_obj);
		}

		if (mode == SEARCH_MODE_HYBRID && json_object_array_length(hits_arr) < limit) {
			for (size_t j = 0; j < n_lemma && json_object_array_length(hits_arr) < limit; j++) {
				uint32_t lref = lemma_matches[j];
				int already_added = 0;
				for (size_t i = 0; i < nhits; i++) {
					if (hits[i].ref == lref) {
						already_added = 1;
						break;
					}
				}
				if (!already_added) {
					struct json_object *hit_obj = json_object_new_object();
					if (hit_obj) {
						char kbuf[32];
						snprintf(kbuf, sizeof(kbuf), "%u", lref);
						const char *stored_text = (const char *)corm_get(g_state.text_hd, kbuf);

						json_object_object_add(hit_obj, "ref", json_object_new_int64(lref));
						json_object_object_add(hit_obj, "score", json_object_new_double(0.5));
						json_object_object_add(hit_obj, "prompt",
						                       json_object_new_string(stored_text ? stored_text : ""));
						json_object_object_add(hit_obj, "lexical_match", json_object_new_boolean(1));
						json_object_array_add(hits_arr, hit_obj);
					}
				}
			}
		}

		if (mode == SEARCH_MODE_HYBRID) {
			json_object_array_sort(hits_arr, compare_hits_desc);
		}
	} else if (mode == SEARCH_MODE_LEMMA) {
		uint32_t out_hd = corm_open(NULL, NULL, CM_STR, CM_STR, 255, 0);
		if (out_hd != CM_MISS) {
			int handled = 0;
			lemma_query(g_state.ldb, "text", qbuf, out_hd, &handled);
			if (handled) {
				uint32_t cur = corm_iter(out_hd, NULL, 0);
				const void *k_ptr, *v_ptr;
				size_t count = 0;
				while (corm_next(&k_ptr, &v_ptr, cur) && count < limit) {
					uint32_t ref = (uint32_t)strtoul((const char *)k_ptr, NULL, 10);
					struct json_object *hit_obj = json_object_new_object();
					if (hit_obj) {
						char kbuf[32];
						snprintf(kbuf, sizeof(kbuf), "%u", ref);
						const char *stored_text = (const char *)corm_get(g_state.text_hd, kbuf);

						json_object_object_add(hit_obj, "ref", json_object_new_int64(ref));
						json_object_object_add(hit_obj, "score", json_object_new_double(1.0));
						json_object_object_add(hit_obj, "prompt",
						                       json_object_new_string(stored_text ? stored_text : ""));
						json_object_array_add(hits_arr, hit_obj);
						count++;
					}
				}
				corm_fin(cur);
			}
			corm_close(out_hd);
		}
	}

	const char *mode_str = (mode == SEARCH_MODE_LEMMA) ? "lemma" :
	                       (mode == SEARCH_MODE_HYBRID) ? "hybrid" : "sepal";

	json_object_object_add(resp, "ok", json_object_new_boolean(1));
	json_object_object_add(resp, "query", json_object_new_string(qbuf));
	json_object_object_add(resp, "mode", json_object_new_string(mode_str));
	json_object_object_add(resp, "count", json_object_new_int64(json_object_array_length(hits_arr)));
	json_object_object_add(resp, "hits", hits_arr);

	const char *resp_str = json_object_to_json_string(resp);
	axil_respond_json(cfd, 200, resp_str);
	json_object_put(resp);
	return 0;
}

int
handle_get_ui(socket_t cfd, char *body)
{
	(void)body;
	static const char ui_html[] =
		"<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>Axil Embed Search</title><style>"
		"body{font-family:sans-serif;max-width:800px;margin:2rem auto;padding:0 1rem;background:#111;color:#eee}"
		"h1{color:#38bdf8}label{display:block;margin:0.5rem 0 0.2rem;font-weight:bold}"
		"textarea,input,select{width:100%;box-sizing:border-box;padding:0.5rem;background:#222;color:#fff;border:1px solid #444;border-radius:4px}"
		"button{margin-top:0.75rem;padding:0.6rem 1.2rem;background:#38bdf8;color:#000;border:none;border-radius:4px;font-weight:bold;cursor:pointer}"
		".card{background:#1a1a1a;border:1px solid #333;border-radius:8px;padding:1.2rem;margin-bottom:1.5rem}"
		".hit{background:#222;border:1px solid #444;border-radius:4px;padding:0.75rem;margin-top:0.5rem}"
		".badge{background:#38bdf8;color:#000;padding:2px 6px;border-radius:3px;font-size:0.75rem;font-weight:bold}"
		"</style></head><body>"
		"<h1>⚡ Axil Embed Search</h1>"
		"<div class=\"card\"><h2>Ingest Document</h2>"
		"<form id=\"ingest-form\"><label>Prompt / Text</label><textarea id=\"prompt\" rows=\"3\" required></textarea>"
		"<label>Custom ID / Ref (optional)</label><input type=\"number\" id=\"ref\">"
		"<button type=\"submit\">Ingest</button></form><pre id=\"ingest-res\" style=\"color:#38bdf8;margin-top:0.5rem\"></pre></div>"
		"<div class=\"card\"><h2>Search Documents</h2>"
		"<form id=\"search-form\"><label>Query</label><input type=\"text\" id=\"q\" required>"
		"<label>Mode</label><select id=\"mode\"><option value=\"sepal\">Semantic (Sepal)</option><option value=\"lemma\">Lexical (Lemma)</option><option value=\"hybrid\" selected>Hybrid</option></select>"
		"<label>Limit</label><input type=\"number\" id=\"k\" value=\"10\">"
		"<button type=\"submit\">Search</button></form><div id=\"hits\"></div></div>"
		"<script>"
		"document.getElementById('ingest-form').onsubmit=async e=>{"
		"e.preventDefault();const p=document.getElementById('prompt').value,r=document.getElementById('ref').value,b={prompt:p};"
		"if(r)b.ref=parseInt(r,10);"
		"const res=await fetch('/embeddings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)});"
		"const d=await res.json();document.getElementById('ingest-res').textContent=JSON.stringify(d,null,2);if(d.ok)document.getElementById('prompt').value='';"
		"};"
		"document.getElementById('search-form').onsubmit=async e=>{"
		"e.preventDefault();const q=document.getElementById('q').value,m=document.getElementById('mode').value,k=document.getElementById('k').value||10,div=document.getElementById('hits');"
		"div.innerHTML='Searching...';"
		"const res=await fetch('/search?q='+encodeURIComponent(q)+'&mode='+m+'&k='+k);"
		"const d=await res.json();if(!d.hits||!d.hits.length){div.innerHTML='No hits';return;}"
		"div.innerHTML=d.hits.map(h=>'<div class=\"hit\"><div><span class=\"badge\">Ref '+h.ref+'</span> Score: '+(h.score?h.score.toFixed(4):'')+ '</div><div style=\"margin-top:0.3rem\">'+(h.prompt||'')+'</div></div>').join('');"
		"};"
		"</script></body></html>\n";

	axil_header_set(cfd, "Content-Type", "text/html; charset=utf-8");
	axil_respond(cfd, 200, ui_html);
	return 0;
}

XY_MODULE_API void
xy_install(void)
{
	if (embed_search_init(NULL, NULL, NULL) != 0) {
		fprintf(stderr, "axil-embed-search: xy_install initialization failed\n");
		return;
	}

	axil_register_handler("POST:/embeddings", handle_post_embeddings);
	axil_register_handler("POST:/api/embeddings", handle_post_embeddings);
	axil_register_handler("GET:/search", handle_get_search);
	axil_register_handler("GET:/api/search", handle_get_search);
	axil_register_handler("GET:/", handle_get_ui);
	axil_register_handler("GET:/embed-search", handle_get_ui);
	fprintf(stderr, "axil-embed-search: mounted /embeddings, /search, and / (UI form) endpoints\n");
}

XY_MODULE_API void
xy_shutdown(void)
{
	embed_search_cleanup();
}
