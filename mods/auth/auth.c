#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <crypt.h>

#include <ttypt/ndc.h>
#include <ttypt/ndx.h>
#include <ttypt/qmap.h>

#include "papi.h"

ndx_t ndx;
static uint32_t users, sess;

struct user { char active; char hash[64]; char email[128]; char rcode[64]; };

static void
tok(char *b, size_t l)
{
	FILE *f = fopen("/dev/urandom", "r");
	if (!f) { snprintf(b, l, "%llx", (unsigned long long)time(NULL)); return; }
	for (size_t i = 0; i + 1 < l; ) {
		int c = fgetc(f);
		if (c == EOF) break;
		b[i++] = "0123456789abcdef"[(c>>4)&15];
		b[i++] = "0123456789abcdef"[c&15];
	}
	b[l-1] = 0;
	fclose(f);
}

static void
qp(char *qs, const char *k, char *o, size_t ol)
{
	if (!o || !ol) return;
	o[0] = 0;
	if (!qs) return;
	size_t kl = strlen(k);
	for (char *p = qs; *p; ) {
		while (*p == '&') p++;
		if (!strncmp(p, k, kl) && p[kl] == '=') {
			char *v = p + kl + 1;
			size_t n = 0;
			while (v[n] && v[n] != '&') n++;
			if (n >= ol) n = ol - 1;
			memcpy(o, v, n);
			o[n] = 0;
			size_t j = 0;
			for (size_t i = 0; o[i]; i++) {
				if (o[i] == '+') o[j++] = ' ';
				else if (o[i] == '%' && o[i+1] && o[i+2]) {
					int c; sscanf(o+i+1, "%2x", &c);
					o[j++] = c; i += 2;
				} else o[j++] = o[i];
			}
			o[j] = 0;
			return;
		}
		while (*p && *p != '&') p++;
	}
}

static void
rsp(int fd, int c, const char *h, const char *b)
{
	ndc_writef(fd, "HTTP/1.1 %d %s\r\n%s\r\n%s", c,
		c==200?"OK":c==303?"See Other":c==400?"Bad Request":c==401?"Unauthorized":"Error",
		h?h:"", b?b:"");
	ndc_close(fd);
}

static void
hs(int fd, char *body)
{
	(void)body;
	char c[256] = {0}, t[64] = {0};
	ndc_env_get(fd, c, "HTTP_COOKIE");
	qp(c, "QSESSION", t, sizeof(t));
	const char *u = qmap_get(sess, t);
	rsp(fd, 200, "Content-Type: text/plain\r\n", u ?: "");
}

static void
hl(int fd, char *body)
{
	char u[64] = {0}, p[64] = {0}, r[256] = {0};
	qp(body, "username", u, sizeof(u));
	qp(body, "password", p, sizeof(p));
	qp(body, "ret", r, sizeof(r));
	if (!*u || !*p) { rsp(fd, 400, "Content-Type: text/plain\r\n", "Missing"); return; }
	struct user *x = (struct user *)qmap_get(users, u);
	if (!x) { rsp(fd, 400, "Content-Type: text/plain\r\n", "No user"); return; }
	char salt[30] = {0};
	strncpy(salt, x->hash, 29);
	char *h = crypt(p, salt);
	if (!h || strcmp(h, x->hash)) { rsp(fd, 401, "Content-Type: text/plain\r\n", "Invalid"); return; }
	if (!x->active) { rsp(fd, 400, "Content-Type: text/plain\r\n", "Not confirmed"); return; }
	char t[64], hd[256];
	tok(t, sizeof(t));
	qmap_put(sess, t, u);
	snprintf(hd, sizeof(hd), "Set-Cookie: QSESSION=%s\r\nLocation: %s\r\n", t, *r ? r : "/");
	rsp(fd, 303, hd, NULL);
}

static void
ho(int fd, char *body)
{
	(void)body;
	char c[256] = {0}, q[256] = {0}, t[64] = {0}, r[256] = {0};
	ndc_env_get(fd, c, "HTTP_COOKIE");
	ndc_env_get(fd, q, "QUERY_STRING");
	qp(c, "QSESSION", t, sizeof(t));
	qp(q, "ret", r, sizeof(r));
	if (*t) qmap_del(sess, t);
	char hd[256];
	snprintf(hd, sizeof(hd), "Set-Cookie: QSESSION=\r\nLocation: %s\r\n", *r ? r : "/");
	rsp(fd, 303, hd, NULL);
}

static void
hr(int fd, char *body)
{
	char u[64] = {0}, p[64] = {0}, p2[64] = {0}, e[128] = {0};
	qp(body, "username", u, sizeof(u));
	qp(body, "password", p, sizeof(p));
	qp(body, "password2", p2, sizeof(p2));
	qp(body, "email", e, sizeof(e));
	size_t ul = strlen(u);
	if (ul < 2 || ul > 32) { rsp(fd, 400, "Content-Type: text/plain\r\n", "Bad user"); return; }
	for (char *x = u; *x; x++) if (!((*x>='a'&&*x<='z')||(*x>='A'&&*x<='Z')||(*x>='0'&&*x<='9')||*x=='_'||*x=='-')) { rsp(fd, 400, "Content-Type: text/plain\r\n", "Bad user"); return; }
	if (strlen(p) < 4) { rsp(fd, 400, "Content-Type: text/plain\r\n", "Short pw"); return; }
	if (strcmp(p, p2)) { rsp(fd, 400, "Content-Type: text/plain\r\n", "Mismatch"); return; }
	if (qmap_get(users, u)) { rsp(fd, 400, "Content-Type: text/plain\r\n", "Exists"); return; }
	struct user x = {0};
	char *h = crypt(p, "$2b$12$abcdefghijklmnopqrstuu");
	if (!h) { rsp(fd, 400, "Content-Type: text/plain\r\n", "Hash err"); return; }
	strncpy(x.hash, h, sizeof(x.hash)-1);
	strncpy(x.email, e, sizeof(x.email)-1);
	tok(x.rcode, sizeof(x.rcode));
	qmap_put(users, u, &x);
	fprintf(stderr, "Register: /confirm?u=%s&r=%s\n", u, x.rcode);
	rsp(fd, 303, "Location: /welcome\r\n", NULL);
}

static void
hc(int fd, char *body)
{
	(void)body;
	char q[256] = {0}, u[64] = {0}, r[64] = {0};
	ndc_env_get(fd, q, "QUERY_STRING");
	qp(q, "u", u, sizeof(u));
	qp(q, "r", r, sizeof(r));
	if (!*u || !*r) { rsp(fd, 400, "Content-Type: text/plain\r\n", "Missing"); return; }
	struct user *xp = (struct user *)qmap_get(users, u);
	if (!xp || xp->active) { rsp(fd, 400, "Content-Type: text/plain\r\n", "Invalid"); return; }
	if (strcmp(r, xp->rcode)) { rsp(fd, 400, "Content-Type: text/plain\r\n", "Wrong code"); return; }
	struct user x;
	memcpy(&x, xp, sizeof(x));
	x.active = 1;
	qmap_put(users, u, &x);
	char t[64], hd[256];
	tok(t, sizeof(t));
	qmap_put(sess, t, u);
	snprintf(hd, sizeof(hd), "Set-Cookie: QSESSION=%s\r\nLocation: /\r\n", t);
	rsp(fd, 303, hd, NULL);
}

MODULE_API void
ndx_install(void)
{
	users = qmap_open("auth.qmap", "users", QM_STR, qmap_reg(sizeof(struct user)), 0xFF, 0);
	sess = qmap_open("auth.qmap", "sess", QM_STR, QM_STR, 0xFF, 0);
	ndc_register_handler("/api/session", hs);
	ndc_register_handler("POST:/login", hl);
	ndc_register_handler("/logout", ho);
	ndc_register_handler("POST:/register", hr);
	ndc_register_handler("/confirm", hc);
}

MODULE_API void
ndx_open(void)
{
	ndc_register_handler("/api/session", hs);
	ndc_register_handler("/logout", ho);
	ndc_register_handler("/confirm", hc);
}

MODULE_API ndx_t *
get_ndx_ptr(void)
{
	return &ndx;
}
