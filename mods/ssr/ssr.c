#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <ctype.h>

#include <ttypt/qmap.h>
#include <ttypt/ndx.h>

#include <ttypt/ndc.h>
#include <ttypt/ndx.h>

#include "papi.h"
#include "../common/common.h"
#include "../auth/auth.h"

ndx_t ndx;

#define PROXY_HOST "127.0.0.1"
#define PROXY_PORT 3000

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

static uint32_t modules_db_hd = 0;

static uint32_t
get_modules_db(void)
{
	if (!modules_db_hd) {
		modules_db_hd = qmap_open("./module.db", "hd", QM_STR, QM_STR, 0x7FFF, QM_MIRROR);
	}
	return modules_db_hd;
}

static uint32_t
open_modules_db(const char *doc_root)
{
	(void) doc_root;
	return get_modules_db();
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
        call_json_escape(title, title_esc, sizeof(title_esc));

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

    used += snprintf(out + used, outlen - used, "]");
    return 0;
}

static void
ssr_handler(int fd, char *body);

static void
register_module_routes(void)
{
	uint32_t hd = open_modules_db(NULL);
	if (!hd) {
		fprintf(stderr, "register_module_routes: failed to open module.db\n");
		return;
	}

    uint32_t cur = qmap_iter(hd, NULL, 0);
    const void *key;
    const void *value;
    while (qmap_next(&key, &value, cur)) {
        const char *k = (const char *)key;
        const char *v = (const char *)value;
        fprintf(stderr, "register_module_routes: key=%s, value=%s\n", k, v);
        char routes[512] = { 0 };
        if (get_field(v, "routes", routes, sizeof(routes)) != 0) {
            fprintf(stderr, "register_module_routes: no routes field for %s\n", k);
            continue;
        }
        fprintf(stderr, "register_module_routes: %s has routes=%s\n", k, routes);

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
}

/* Helpers to safely build an outgoing HTTP request into a buffer. */
static int
proxy_req_init(char *buf, size_t bufcap, size_t *len, const char *method, const char *path, const char *host, int port)
{
    int n = snprintf(buf, bufcap,
        "%s %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n",
        method, path, host, port);
    if (n < 0 || (size_t)n >= bufcap)
        return -1;
    *len = (size_t)n;
    return 0;
}

static int
proxy_req_add_header(char *buf, size_t bufcap, size_t *len, const char *name, const char *value)
{
    if (!value || !*value)
        return 0; /* nothing to add */
    /* reject CR/LF in header values to prevent injection */
    for (const char *p = value; *p; ++p) {
        if (*p == '\r' || *p == '\n')
            return -1;
    }
    /* cap header value length to avoid huge headers */
    size_t max_val = 4096;
    size_t vlen = strlen(value);
    size_t use_len = vlen > max_val ? max_val : vlen;
    size_t remain = bufcap - *len;
    int n = snprintf(buf + *len, remain, "%s: %.*s\r\n", name, (int)use_len, value);
    if (n < 0 || (size_t)n >= remain)
        return -1;
    *len += (size_t)n;
    return 0;
}

static int
proxy_req_finish(char *buf, size_t bufcap, size_t *len)
{
    size_t remain = bufcap - *len;
    int n = snprintf(buf + *len, remain, "\r\n");
    if (n < 0 || (size_t)n >= remain)
        return -1;
    *len += (size_t)n;
    return 0;
}

static int
proxy_request(int client_fd, const char *path, const char *remote_user, const char *modules_header, const char *error_msg)
{
    int proxy_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (proxy_fd < 0) {
        ndc_header(client_fd, "Content-Type", "text/plain");
        ndc_head(client_fd, 502);
        ndc_body(client_fd, "Proxy error");
        return 1;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PROXY_PORT);
    inet_pton(AF_INET, PROXY_HOST, &serv_addr.sin_addr);

    if (connect(proxy_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
                ndc_header(client_fd, "Content-Type", "text/html");
                ndc_head(client_fd, 502);
                ndc_body(client_fd, "<html><body><h1>Could not connect to Deno</h1></body></html>");
                close(proxy_fd);
                ndc_close(client_fd);
                return 1;
            }

            char req[2048];
            size_t req_len = 0;
            if (proxy_req_init(req, sizeof(req), &req_len, "GET", path, PROXY_HOST, PROXY_PORT) != 0) {
                close(proxy_fd);
                ndc_header(client_fd, "Content-Type", "text/plain");
                ndc_head(client_fd, 502);
                ndc_body(client_fd, "Proxy error");
                return 1;
            }
            if (proxy_req_add_header(req, sizeof(req), &req_len, "X-Remote-User", remote_user) != 0) {
                close(proxy_fd);
                ndc_header(client_fd, "Content-Type", "text/plain");
                ndc_head(client_fd, 502);
                ndc_body(client_fd, "Proxy error");
                return 1;
            }
            if (proxy_req_add_header(req, sizeof(req), &req_len, "X-Modules", modules_header) != 0) {
                close(proxy_fd);
                ndc_header(client_fd, "Content-Type", "text/plain");
                ndc_head(client_fd, 502);
                ndc_body(client_fd, "Proxy error");
                return 1;
            }
            if (proxy_req_add_header(req, sizeof(req), &req_len, "X-Error", error_msg) != 0) {
                close(proxy_fd);
                ndc_header(client_fd, "Content-Type", "text/plain");
                ndc_head(client_fd, 502);
                ndc_body(client_fd, "Proxy error");
                return 1;
            }
            if (proxy_req_add_header(req, sizeof(req), &req_len, "Connection", "close") != 0) {
                close(proxy_fd);
                ndc_header(client_fd, "Content-Type", "text/plain");
                ndc_head(client_fd, 502);
                ndc_body(client_fd, "Proxy error");
                return 1;
            }
            if (proxy_req_finish(req, sizeof(req), &req_len) != 0) {
                close(proxy_fd);
                ndc_header(client_fd, "Content-Type", "text/plain");
                ndc_head(client_fd, 502);
                ndc_body(client_fd, "Proxy error");
                return 1;
            }

            write(proxy_fd, req, req_len);

    /* Read response from proxy, parse status and headers, then forward body.
       We accumulate the response head until CRLFCRLF is seen, then parse it
       and call ndc_head/ndc_header for a canonical forwarding. */
    char buf[4096];
    ssize_t n;
    int headers_done = 0;

    char headbuf[16384];
    size_t head_used = 0;

    while ((n = read(proxy_fd, buf, sizeof(buf))) > 0) {
        size_t rn = (size_t)n;

        if (!headers_done) {
            /* append to head buffer until we find CRLFCRLF */
            size_t to_copy = rn;
            if (head_used + to_copy >= sizeof(headbuf))
                to_copy = sizeof(headbuf) - 1 - head_used;
            memcpy(headbuf + head_used, buf, to_copy);
            head_used += to_copy;
            headbuf[head_used] = '\0';

            char *sep = strstr(headbuf, "\r\n\r\n");
            if (!sep) {
                /* not yet found, continue reading */
                /* if we truncated because headbuf filled, bail out */
                if (head_used == sizeof(headbuf) - 1) {
                    /* header too large */
                    ndc_header(client_fd, "Content-Type", "text/plain");
                    ndc_head(client_fd, 502);
                    ndc_body(client_fd, "Upstream header too large");
                    close(proxy_fd);
                    ndc_close(client_fd);
                    return 1;
                }
                continue;
            }

            /* We have the complete header block in headbuf. Determine how much
               of the latest buf was not copied into headbuf (if any) and
               treat that as body remainder. */
            size_t header_len = (size_t)(sep + 4 - headbuf);

            /* parse status line and headers */
            /* Save the byte at header_len before null-terminating for parsing,
               so we can restore it before writing the body. */
            char saved_byte = headbuf[header_len];
            headbuf[header_len] = '\0';
            char *save = NULL;
            char *line = strtok_r(headbuf, "\r\n", &save);
            int status = 200;
            if (line) {
                char proto[32];
                if (sscanf(line, "%31s %d", proto, &status) != 2) {
                    status = 200;
                }
            }

            /* iterate remaining header lines */
            while ((line = strtok_r(NULL, "\r\n", &save)) != NULL) {
                if (*line == '\0') break;
                char *colon = strchr(line, ':');
                if (!colon) continue;
                *colon = '\0';
                char *key = line;
                char *val = colon + 1;
                /* trim spaces */
                while (*val == ' ') val++;
                /* Skip hop-by-hop headers that should not be forwarded */
                if (!strcasecmp(key, "Connection") || !strcasecmp(key, "Keep-Alive") ||
                    !strcasecmp(key, "Proxy-Connection") || !strcasecmp(key, "Transfer-Encoding")) {
                    continue;
                }
                ndc_header(client_fd, key, val);
            }

            /* Send status line and accumulated response headers.
               ndc_head() now sends all headers immediately. */
            ndc_head(client_fd, status);

            /* Restore the saved byte before writing body */
            headbuf[header_len] = saved_byte;

            headers_done = 1;

            /* compute remainder body: some bytes of the current "buf" may
               not have been copied into headbuf if headbuf had extra room.
               The remainder is everything after the CRLFCRLF in headbuf, but
               it could be part of the current buf or already in headbuf.
               We'll calculate using header_len and head_used. */
            size_t sent_from_head = head_used - header_len;
            if (sent_from_head > 0) {
                /* write the remainder that was captured in headbuf */
                ndc_write(client_fd, headbuf + header_len, sent_from_head);
            }

            /* if there are additional bytes in the current read beyond what we
               appended to headbuf, send them as well */
            if (to_copy < rn) {
                size_t extra_off = to_copy;
                size_t extra_len = rn - to_copy;
                ndc_write(client_fd, buf + extra_off, extra_len);
            }
        } else {
            /* headers already processed; forward body chunks */
            ndc_write(client_fd, buf, rn);
        }
    }

    close(proxy_fd);
    ndc_close(client_fd);
    return 0;
}

/* Forward declarations */
static void ssr_handler(int fd, char *body);

/* Adapter: render an error template via the Deno SSR proxy */
NDX_DECL(int, ssr_render_error, int, fd, char *, template, char *, error_msg);
NDX_DEF(int, ssr_render_error, int, fd, char *, template, char *, error_msg);

int ssr_render_error(int fd, char *template, char *error_msg)
{
    const char *remote_user = NULL;
    char cookie[256] = { 0 };
    char token[128] = { 0 };

    ndc_env_get(fd, cookie, "HTTP_COOKIE");
    call_get_cookie(cookie, token, sizeof(token));

    if (*token) {
        remote_user = call_get_session_user(token);
    }

    char modules_json[4096] = { 0 };
    char modules_header[8192] = { 0 };
    char doc_root[256] = { 0 };
    ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");

    if (build_modules_json(doc_root, modules_json, sizeof(modules_json)) == 0
        && strcmp(modules_json, "[]") != 0) {
        call_url_encode(modules_json, modules_header, sizeof(modules_header));
    }

    proxy_request(fd, template, remote_user, modules_header, error_msg);
    return 0;
}

/* Main SSR handler: proxy request path to the Deno SSR server */
static void
ssr_handler(int fd, char *body)
{
    (void)body;
    char path[1024] = {0};
    ndc_env_get(fd, path, "DOCUMENT_URI");
    if (!path[0]) ndc_env_get(fd, path, "PATH_INFO");
    if (!path[0]) strcpy(path, "/");
    fprintf(stderr, "ssr_handler: path=%s\n", path);
    
    char doc_root[256] = { 0 };
    ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");

    char cookie[256] = { 0 };
    ndc_env_get(fd, cookie, "HTTP_COOKIE");

    char token[128] = { 0 };
    call_get_cookie(cookie, token, sizeof(token));

    const char *remote_user = NULL;
    if (*token) {
        remote_user = call_get_session_user(token);
    }

    char modules_json[4096] = { 0 };
    char modules_header[8192] = { 0 };
    if (build_modules_json(doc_root, modules_json, sizeof(modules_json)) == 0
        && strcmp(modules_json, "[]") != 0) {
        call_url_encode(modules_json, modules_header, sizeof(modules_header));
    }

    /* Obtain path from ndc env */
    ndc_env_get(fd, path, "DOCUMENT_URI");
    if (!path[0]) ndc_env_get(fd, path, "PATH_INFO");
    if (!path[0]) strcpy(path, "/");

    proxy_request(fd, path, remote_user, modules_header, NULL);
}

/* JSON list of modules API handler */
static void
api_modules_handler(int fd, char *body)
{
    (void)body;
    char doc_root[256] = { 0 };
    ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");

    uint32_t hd = open_modules_db(doc_root);

    if (!hd) {
        ndc_header(fd, "Content-Type", "application/json");
        ndc_head(fd, 500);
        ndc_body(fd, "[]");
        return;
    }

    ndc_header(fd, "Content-Type", "application/json");
    ndc_header(fd, "Access-Control-Allow-Origin", "*");
    ndc_head(fd, 200);
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
        call_json_escape(title, title_esc, sizeof(title_esc));

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
	ndx_load("./mods/common/common");
	ndx_load("./mods/auth/auth");
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
}

MODULE_API ndx_t *
get_ndx_ptr(void)
{
    return &ndx;
}
