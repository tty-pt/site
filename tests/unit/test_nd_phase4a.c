#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/select.h>

#include <ttypt/corm.h>
#include <ttypt/axil.h>
#include <nd/law-types.h>

/* ── Instrumented Corm & Axil stubs to prove ZERO corm_get per nd_write ── */
static unsigned s_corm_get_calls = 0;
static unsigned s_corm_put_calls = 0;
static unsigned s_corm_del_calls = 0;
static unsigned s_axil_write_calls = 0;
static int s_fd_flags[FD_SETSIZE];

uint32_t corm_open(const char *path, const char *name, uint32_t kt, uint32_t vt, uint32_t mask, uint32_t flags)
{
	(void)path; (void)name; (void)kt; (void)vt; (void)mask; (void)flags;
	static uint32_t next_hd = 1;
	return next_hd++;
}
const void *corm_get(uint32_t hd, const void *const key)
{
	(void)hd; (void)key;
	s_corm_get_calls++;
	return NULL;
}
uint32_t corm_put(uint32_t hd, const void *const key, const void *const val)
{
	(void)hd; (void)key; (void)val;
	s_corm_put_calls++;
	return 0;
}
void corm_del(uint32_t hd, const void *const key)
{
	(void)hd; (void)key;
	s_corm_del_calls++;
}
void corm_save(void) {}

int axil_flags(socket_t fd)
{
	return (fd >= 0 && fd < FD_SETSIZE) ? s_fd_flags[fd] : 0;
}
int axil_write(socket_t fd, void *buf, size_t len)
{
	(void)fd; (void)buf;
	s_axil_write_calls++;
	return (int)len;
}
void axil_close(socket_t fd) { (void)fd; }
void nd_store_set_hd(unsigned idx, unsigned val) { (void)idx; (void)val; }
int nd_is_unmodded(void) { return 0; }
void nd_bare_motd(socket_t fd) { (void)fd; }

/* Include law_io.c directly with XY_IMPL mapped to plain C functions */
#define XY_H
#define TTYPT_XY_H
#define XY_IMPL_V(ret, name, v) ret name(v)
#define XY_IMPL_1(ret, name, t1, a1) ret name(t1 a1)
#define XY_IMPL_2(ret, name, t1, a1, t2, a2) ret name(t1 a1, t2 a2)
#define XY_IMPL_3(ret, name, t1, a1, t2, a2, t3, a3) ret name(t1 a1, t2 a2, t3 a3)
#define XY_IMPL_GET(_1,_2,_3,_4,_5,_6,_7,_8,NAME,...) NAME
#define XY_IMPL(ret, name, ...) \
	XY_IMPL_GET(ret, name, ##__VA_ARGS__, \
	            XY_IMPL_3, _err, XY_IMPL_2, _err, XY_IMPL_1, XY_IMPL_V, _err)(ret, name, ##__VA_ARGS__)

#include "../../external/axil-nd/src/law_io.c"

static void test_law_io_ram_map(void)
{
	nd_io_init();
	s_corm_get_calls = 0;

	/* Attach 200 players including deliberate hash collisions mod 2048 */
	for (unsigned i = 1; i <= 200; i++) {
		unsigned fd = 10 + i;
		unsigned pref = i * 2048u + 7u; /* exercises probe chains */
		s_fd_flags[fd] = (i & 1u) ? DF_WEBSOCKET : 0;
		nd_io_attach(fd, pref);
	}
	assert(s_corm_get_calls == 0);

	/* Verify O(1) RAM lookups and writes execute 0 corm_get calls */
	for (unsigned i = 1; i <= 200; i++) {
		unsigned fd = 10 + i;
		unsigned pref = i * 2048u + 7u;
		assert(fd_player(fd) == pref);
		assert(nd_player_fd(pref) == (int)fd);
		assert(nd_player_is_ws(pref) == (int)(i & 1u));

		char msg[] = "hello\n";
		assert(nd_write(pref, msg, sizeof(msg) - 1) == 1);
		assert(nd_wwrite(pref, msg, sizeof(msg) - 1) == (int)(i & 1u));
		assert(nd_twrites(pref, msg, sizeof(msg) - 1) == (int)(!(i & 1u)));
		assert(nd_flush(pref) == 0);
	}
	assert(s_corm_get_calls == 0);

	/* Re-attach existing player to new fd: old fd must detach cleanly */
	unsigned p_re = 5u * 2048u + 7u;
	nd_io_attach(500, p_re);
	assert(fd_player(15) == 0);
	assert(fd_player(500) == p_re);
	assert(nd_player_fd(p_re) == 500);
	assert(s_corm_get_calls == 0);

	/* Detach every odd entry (tests backward-shift deletion across collision chains) */
	for (unsigned i = 1; i <= 200; i += 2) {
		unsigned fd = (i == 5) ? 500 : (10 + i);
		unsigned pref = i * 2048u + 7u;
		nd_io_detach(fd);
		assert(fd_player(fd) == 0);
		assert(nd_player_fd(pref) == -1);
		assert(nd_player_is_ws(pref) == 0);
	}
	/* Every even entry must still be intact after backward-shift deletes */
	for (unsigned i = 2; i <= 200; i += 2) {
		unsigned fd = 10 + i;
		unsigned pref = i * 2048u + 7u;
		assert(fd_player(fd) == pref);
		assert(nd_player_fd(pref) == (int)fd);
	}
	assert(s_corm_get_calls == 0);

	/* Clean up remaining entries */
	for (unsigned i = 2; i <= 200; i += 2)
		nd_io_detach(10 + i);
	assert(s_corm_get_calls == 0);

	printf("PASS: law_io in-RAM map (200 sessions, collision chains, backward-shift detach, 0 corm_get)\n");
}

int main(void)
{
	test_law_io_ram_map();
	printf("ALL PHASE 4A SERVER UNIT TESTS PASSED\n");
	return 0;
}
