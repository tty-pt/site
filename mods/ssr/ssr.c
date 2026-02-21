#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <ctype.h>

#include <ttypt/qmap.h>

#include <ttypt/ndc.h>
#include <ttypt/ndx.h>

#include "papi.h"

ndx_t ndx;

#define PROXY_HOST "127.0.0.1"
#define PROXY_PORT 3000

static void
get_cookie(const char *cookie, char *token, size_t len)
{
	token[0] = '\0';
	if (!cookie || !*cookie)
		return;
	const char *p = cookie;
	while (*p) {
		while (*p == ' ' || *p == '\t') p++;
		if (!strncmp(p, "QSESSION=", 9)) {
			p += 9;
			const char *amp = strchr(p, '&');
			size_t tlen = amp ? (amp - p) : strlen(p);
			if (tlen >= len) tlen = len - 1;
			strncpy(token, p, tlen);
			token[tlen] = '\0';
			return;
		}
		while (*p && *p != '&') p++;
		if (*p == '&') p++;
	}
}

static void
url_encode(const char *in, char *out, size_t outlen)
{
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 4 < outlen; i++) {
        unsigned char c = (unsigned char)in[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out[j++] = c;
        } else {
            j += snprintf(out + j, outlen - j, "%%%02X", c);
        }
    }
    out[j] = '\0';
}

static void
json_escape(const char *in, char *out, size_t outlen)
{
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 2 < outlen; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == '"' || c == '\\') {
            if (j + 2 >= outlen) break;
            out[j++] = '\\';
            out[j++] = c;
        } else if (c == '\n') {
            if (j + 2 >= outlen) break;
            out[j++] = '\\';
            out[j++] = 'n';
        } else if (c == '\r') {
            if (j + 2 >= outlen) break;
            out[j++] = '\\';
            out[j++] = 'r';
        } else if (c == '\t') {
            if (j + 2 >= outlen) break;
            out[j++] = '\\';
            out[j++] = 't';
        } else if (c < 0x20) {
            if (j + 6 >= outlen) break;
            j += snprintf(out + j, outlen - j, "\\u%04x", c);
        } else {
            out[j++] = c;
        }
    }
    out[j] = '\0';
}

static int
get_field(const char *value, const char *key, char *out, size_t outlen)
{
    size_t keylen = strlen(key);
    const char *p = value;
    while (p && *p) {
        while (*p == ' ') p++;
        if (!strncmp(p, key, keylen) && p[keylen] == '=') {
            p += keylen + 1;
            const char *end = strchr(p, ';');
            size_t len = end ? (size_t)(end - p) : strlen(p);
            if (len >= outlen) len = outlen - 1;
            strncpy(out, p, len);
            out[len] = '\0';
            return 0;
        }
        p = strchr(p, ';');
        if (p) p++;
    }
    return -1;
}

static uint32_t
open_modules_db(const char *doc_root)
{
	(void) doc_root;
	return qmap_open("./module.db", "hd", QM_STR, QM_STR, 0x7FFF, QM_MIRROR);
}

static int
build_modules_json(char *doc_root, char *out, size_t outlen)
{
	uint32_t hd = open_modules_db(doc_root);

    size_t used = 0;
    used += snprintf(out + used, outlen - used, "[");

    uint32_t cur = qmap_iter(hd, NULL, 0);
    const void *key;
    const void *value;
    int first = 1;
    while (qmap_next(&key, &value, cur)) {
        const char *k = (const char *)key;
        const char *v = (const char *)value;
        char title[256] = { 0 };
        char routes[512] = { 0 };
        char ssr[256] = { 0 };

        get_field(v, "title", title, sizeof(title));
        get_field(v, "routes", routes, sizeof(routes));
        get_field(v, "ssr", ssr, sizeof(ssr));

        char title_esc[256];
        json_escape(title, title_esc, sizeof(title_esc));

        if (!first) {
            used += snprintf(out + used, outlen - used, ",");
        }
        first = 0;

        used += snprintf(out + used, outlen - used,
            "{\"id\":\"%s\",\"title\":\"%s\",\"routes\":[", k, title_esc);

        int route_first = 1;
        char *save = NULL;
        char *tok = strtok_r(routes, ",", &save);
        while (tok) {
            if (!route_first) {
                used += snprintf(out + used, outlen - used, ",");
            }
            route_first = 0;
            used += snprintf(out + used, outlen - used, "\"%s\"", tok);
            tok = strtok_r(NULL, ",", &save);
        }

        used += snprintf(out + used, outlen - used,
            "],\"ssr\":\"%s\"}", ssr);

        if (used + 4 >= outlen)
            break;
    }
	qmap_close(hd);

    used += snprintf(out + used, outlen - used, "]");
    return 0;
}

static void
ssr_handler(int fd, char *body);

static void
register_module_routes(void)
{
	uint32_t hd = open_modules_db(NULL);

    uint32_t cur = qmap_iter(hd, NULL, 0);
    const void *key;
    const void *value;
    while (qmap_next(&key, &value, cur)) {
        const char *v = (const char *)value;
        char routes[512] = { 0 };
        if (get_field(v, "routes", routes, sizeof(routes)) != 0)
            continue;

        char *save = NULL;
        char *tok = strtok_r(routes, ",", &save);
        while (tok) {
            char route_buf[300];
            snprintf(route_buf, sizeof(route_buf), "GET:%s", tok);
            ndc_register_handler(route_buf, ssr_handler);

            size_t tok_len = strlen(tok);
            if (tok_len > 1 && tok[tok_len - 1] != '/' && !strchr(tok, ':')) {
                snprintf(route_buf, sizeof(route_buf), "GET:%s/", tok);
                ndc_register_handler(route_buf, ssr_handler);
            }

            if (strchr(tok, ':')) {
                char *slash = strchr(tok, ':');
                if (slash) {
                    *slash = '\0';
                    snprintf(route_buf, sizeof(route_buf), "GET:%s", tok);
                    ndc_register_handler(route_buf, ssr_handler);
                }
            }
            tok = strtok_r(NULL, ",", &save);
        }
    }
	qmap_close(hd);
}

static int
proxy_request(int client_fd, const char *path, const char *remote_user, const char *modules_header)
{
    int proxy_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (proxy_fd < 0) {
        ndc_head(client_fd, 502);
        ndc_header(client_fd, "Content-Type", "text/plain");
        ndc_body(client_fd, "Proxy error");
        return 1;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PROXY_PORT);
    inet_pton(AF_INET, PROXY_HOST, &serv_addr.sin_addr);

    if (connect(proxy_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        ndc_head(client_fd, 502);
        ndc_header(client_fd, "Content-Type", "text/plain");
        ndc_body(client_fd, "Could not connect to Deno");
        close(proxy_fd);
        ndc_close(client_fd);
        return 1;
    }

    char req[2048];
    int req_len = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: 127.0.0.1:%d\r\n",
        path, PROXY_PORT);
    if (remote_user && *remote_user) {
        req_len += snprintf(req + req_len, sizeof(req) - req_len,
            "X-Remote-User: %s\r\n",
            remote_user);
    }
    if (modules_header && *modules_header) {
        req_len += snprintf(req + req_len, sizeof(req) - req_len,
            "X-Modules: %s\r\n",
            modules_header);
    }
    snprintf(req + req_len, sizeof(req) - req_len,
        "Connection: close\r\n\r\n");

    write(proxy_fd, req, strlen(req));

    char buf[4096];
    ssize_t n;
    int headers_done = 0;
    
    while ((n = read(proxy_fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        
        if (!headers_done) {
            char *body = strstr(buf, "\r\n\r\n");
            if (body) {
                headers_done = 1;
                size_t body_len = n - (body + 4 - buf);
                ndc_head(client_fd, 200);
                ndc_header(client_fd, "Content-Type", "text/html");
                ndc_header(client_fd, "Connection", "close");
                if (body_len > 0) {
                    ndc_write(client_fd, body + 4, body_len);
                }
            }
        } else {
            ndc_write(client_fd, buf, n);
        }
    }

    close(proxy_fd);
    ndc_close(client_fd);
    return 0;
}

static const char *
get_session_user(const char *token)
{
    static char user[64];
    if (!token || !*token)
        return NULL;
    
    char path[256];
    snprintf(path, sizeof(path), "./sessions/%s", token);
    
    FILE *fp = fopen(path, "r");
    if (!fp)
        return NULL;
    
    if (fgets(user, sizeof(user), fp)) {
        size_t len = strlen(user);
        if (len > 0 && user[len-1] == '\n')
            user[len-1] = '\0';
        fclose(fp);
        if (*user)
            return user;
    }
    fclose(fp);
    return NULL;
}

static void
ssr_handler(int fd, char *body);

static void
ssr_handler(int fd, char *body)
{
    char path[256] = { 0 };
    ndc_env_get(fd, path, "DOCUMENT_URI");
    char doc_root[256] = { 0 };
    ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
    
    if (!path || !*path || strcmp(path, "/") == 0) {
        strcpy(path, "/");
    }

    char cookie[256] = { 0 };
    ndc_env_get(fd, cookie, "HTTP_COOKIE");

    char token[128] = { 0 };
    get_cookie(cookie, token, sizeof(token));

    const char *remote_user = NULL;
    if (*token) {
        remote_user = get_session_user(token);
    }

    char modules_json[4096] = { 0 };
    char modules_header[8192] = { 0 };
    if (build_modules_json(doc_root, modules_json, sizeof(modules_json)) == 0
        && strcmp(modules_json, "[]") != 0) {
        url_encode(modules_json, modules_header, sizeof(modules_header));
    }

    proxy_request(fd, path, remote_user, NULL);
}

static void
api_modules_handler(int fd, char *body)
{
	(void)body;
	char doc_root[256] = { 0 };
	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");

	uint32_t hd = open_modules_db(doc_root);

	if (!hd) {
		ndc_head(fd, 500);
		ndc_header(fd, "Content-Type", "application/json");
		ndc_body(fd, "[]");
		return;
	}

	ndc_head(fd, 200);
	ndc_header(fd, "Content-Type", "application/json");
	ndc_header(fd, "Access-Control-Allow-Origin", "*");
	ndc_write(fd, "[", 1);

	uint32_t cur = qmap_iter(hd, NULL, 0);
	const void *key;
	const void *value;
	int first = 1;
	while (qmap_next(&key, &value, cur)) {
		const char *k = (const char *)key;
		const char *v = (const char *)value;
		char title[256] = { 0 };
		char routes[512] = { 0 };
		char ssr[256] = { 0 };
		char be[256] = { 0 };

		get_field(v, "title", title, sizeof(title));
		get_field(v, "routes", routes, sizeof(routes));
		get_field(v, "ssr", ssr, sizeof(ssr));
		get_field(v, "be", be, sizeof(be));

		if (!first) ndc_writef(fd, ",");
		first = 0;

		char title_esc[256];
		json_escape(title, title_esc, sizeof(title_esc));

		ndc_writef(fd, "{\"id\":\"%s\",\"title\":\"%s\",\"routes\":[", k, title_esc);
		int rf = 1;
		char *save = NULL;
		char *tok = strtok_r(routes, ",", &save);
		while (tok) {
			if (!rf) ndc_writef(fd, ",");
			rf = 0;
			ndc_writef(fd, "\"%s\"", tok);
			tok = strtok_r(NULL, ",", &save);
		}
		ndc_writef(fd, "],\"ssr\":\"%s\",\"be\":\"%s\"}", ssr, be);
	}
	qmap_close(hd);

	ndc_writef(fd, "]");
	ndc_close(fd);
}

MODULE_API void
ndx_install(void)
{
    ndc_config.default_handler = ssr_handler;
    ndc_register_handler("GET:/", ssr_handler);
    ndc_register_handler("GET:/login", ssr_handler);
    ndc_register_handler("GET:/register", ssr_handler);
    ndc_register_handler("GET:/welcome", ssr_handler);
    ndc_register_handler("GET:/api/modules", api_modules_handler);
    register_module_routes();
}

MODULE_API void
ndx_open(void)
{
    ndx_install();
}

MODULE_API ndx_t *
get_ndx_ptr(void)
{
    return &ndx;
}
