#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <ttypt/xy-mod.h>
#include <nd/bcp.h>

#define CHECK(label, condition)                                                \
	do {                                                                   \
		if (condition)                                                 \
			printf("PASS: %s\n", label);                           \
		else {                                                         \
			printf("FAIL: %s (line %d)\n", label, __LINE__);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static int failures = 0;

extern int g_custom_called;

int main(void)
{
	printf("=== Testing ND BCP protocol decoding ===\n");

	nd_client_state_t st;
	memset(&st, 0, sizeof(st));

	/* 1. Plain text / ANSI stream should return 0 */
	{
		const char *ansi = "\033[31mWelcome to NeverDark\033[0m\r\n";
		int ret = nd_bcp_decode((const uint8_t *)ansi, strlen(ansi), &st);
		CHECK("plain text is ignored by BCP decoder", ret == 0);
	}

	/* 2. BCP HP packet */
	{
		uint8_t pkt[7];
		pkt[0] = '#';
		pkt[1] = 'b';
		pkt[2] = 10; /* mock tenant bar channel -> gauge "hp" */
		pkt[3] = 45; pkt[4] = 0;   /* val: 45 */
		pkt[5] = 100; pkt[6] = 0;  /* max: 100 */

		int ret = nd_bcp_decode(pkt, sizeof(pkt), &st);
		CHECK("HP packet returns 1", ret == 1);
		CHECK("HP val unpacked", nd_client_gauge_get(&st, "hp") && nd_client_gauge_get(&st, "hp")->val == 45);
		CHECK("HP max unpacked", nd_client_gauge_get(&st, "hp") && nd_client_gauge_get(&st, "hp")->max == 100);
	}

	/* 3. BCP MP packet */
	{
		uint8_t pkt[7];
		pkt[0] = '#';
		pkt[1] = 'b';
		pkt[2] = 13; /* mock tenant bar channel -> gauge "mp" */
		pkt[3] = 20; pkt[4] = 0;   /* val: 20 */
		pkt[5] = 50; pkt[6] = 0;   /* max: 50 */

		int ret = nd_bcp_decode(pkt, sizeof(pkt), &st);
		CHECK("MP packet returns 1", ret == 1);
		CHECK("MP val unpacked", nd_client_gauge_get(&st, "mp") && nd_client_gauge_get(&st, "mp")->val == 20);
		CHECK("MP max unpacked", nd_client_gauge_get(&st, "mp") && nd_client_gauge_get(&st, "mp")->max == 50);
	}

	/* 4. BCP Action packet */
	{
		/* id = 42, label = "fight", icon = "⚔️" */
		uint8_t pkt[32];
		pkt[0] = '#';
		pkt[1] = 'b';
		pkt[2] = ND_BCP_ACTION;
		pkt[3] = 42; pkt[4] = 0; pkt[5] = 0; pkt[6] = 0; /* id = 42 */
		strcpy((char *)&pkt[7], "fight");
		size_t l_len = strlen("fight") + 1;
		strcpy((char *)&pkt[7 + l_len], "⚔️");
		size_t total = 7 + l_len + strlen("⚔️") + 1;

		int ret = nd_bcp_decode(pkt, total, &st);
		CHECK("Action packet returns 1", ret == 1);
		CHECK("Action count incremented", st.action_count == 1);
		CHECK("Action id matches", st.actions[0].id == 42);
		CHECK("Action label matches", strcmp(st.actions[0].label, "fight") == 0);
		CHECK("Action icon matches", strcmp(st.actions[0].icon, "⚔️") == 0);
	}

	/* 5. BCP Auth success */
	{
		uint8_t pkt[7];
		pkt[0] = '#';
		pkt[1] = 'b';
		pkt[2] = ND_BCP_AUTH_SUCCESS;
		pkt[3] = 123; pkt[4] = 0; pkt[5] = 0; pkt[6] = 0; /* me = 123 */

		int ret = nd_bcp_decode(pkt, sizeof(pkt), &st);
		CHECK("Auth success returns 1", ret == 1);
		CHECK("Player ref set", st.me == 123);
		CHECK("Auth failed cleared", st.auth_failed == 0);
	}

	/* 6. Extensible sub-channel unpacker */
	{
		g_custom_called = 0;
		uint8_t pkt[8];
		pkt[0] = '#';
		pkt[1] = 'b';
		pkt[2] = 12;
		pkt[3] = 1; pkt[4] = 2; pkt[5] = 3; pkt[6] = 4; pkt[7] = 5;

		int ret = nd_bcp_decode(pkt, sizeof(pkt), &st);
		CHECK("Modular channel dispatched", ret == 1);
		CHECK("Modular unpacker invoked", g_custom_called == 1);
	}

	/* 7. BCP Room packet */
	{
		uint8_t pkt[128];
		size_t pos = 0;
		pkt[pos++] = '#';
		pkt[pos++] = 'b';
		pkt[pos++] = ND_BCP_ROOM;
		/* ref = 42 */
		pkt[pos++] = 42; pkt[pos++] = 0; pkt[pos++] = 0; pkt[pos++] = 0;
		/* name = "Grand Chamber" */
		strcpy((char *)&pkt[pos], "Grand Chamber");
		pos += strlen("Grand Chamber") + 1;
		/* desc = "A majestic stone hall." */
		strcpy((char *)&pkt[pos], "A majestic stone hall.");
		pos += strlen("A majestic stone hall.") + 1;
		/* exits = 7 */
		pkt[pos++] = 7; pkt[pos++] = 0; pkt[pos++] = 0; pkt[pos++] = 0;

		int ret = nd_bcp_decode(pkt, pos, &st);
		CHECK("Room packet returns 1", ret == 1);
		CHECK("Room ref unpacked", st.room.ref == 42);
		CHECK("Room name unpacked", strcmp(st.room.name, "Grand Chamber") == 0);
		CHECK("Room desc unpacked", strcmp(st.room.desc, "A majestic stone hall.") == 0);
		CHECK("Room exits unpacked", st.room.exits == 7);
	}

	/* 8. BCP Item packet */
	{
		uint8_t pkt[128];
		size_t pos = 0;
		pkt[pos++] = '#';
		pkt[pos++] = 'b';
		pkt[pos++] = ND_BCP_ITEM;
		/* ref = 99 */
		pkt[pos++] = 99; pkt[pos++] = 0; pkt[pos++] = 0; pkt[pos++] = 0;
		/* location = 42 */
		pkt[pos++] = 42; pkt[pos++] = 0; pkt[pos++] = 0; pkt[pos++] = 0;
		/* dynflags = 3 */
		pkt[pos++] = 3;
		/* type = 5 */
		pkt[pos++] = 5; pkt[pos++] = 0; pkt[pos++] = 0; pkt[pos++] = 0;
		/* actions = 1 */
		pkt[pos++] = 1; pkt[pos++] = 0; pkt[pos++] = 0; pkt[pos++] = 0;
		/* name = "Iron Sword" */
		strcpy((char *)&pkt[pos], "Iron Sword");
		pos += strlen("Iron Sword") + 1;
		/* unparse = "a sharp iron sword" */
		strcpy((char *)&pkt[pos], "a sharp iron sword");
		pos += strlen("a sharp iron sword") + 1;
		/* fg = 7, flags = 0, ch = '/' */
		pkt[pos++] = 7; pkt[pos++] = 0;
		pkt[pos++] = 0; pkt[pos++] = 0;
		pkt[pos++] = '/';
		/* art = "sword.png" */
		strcpy((char *)&pkt[pos], "sword.png");
		pos += strlen("sword.png") + 1;

		int ret = nd_bcp_decode(pkt, pos, &st);
		CHECK("Item packet returns 1", ret == 1);
		CHECK("Entity count incremented", st.entity_count == 1);
		CHECK("Entity ref matches", st.entities[0].ref == 99);
		CHECK("Entity location matches", st.entities[0].location == 42);
		CHECK("Entity name matches", strcmp(st.entities[0].name, "Iron Sword") == 0);
		CHECK("Entity ch glyph matches", st.entities[0].ch == '/');
		CHECK("Entity art matches", strcmp(st.entities[0].art, "sword.png") == 0);
	}

	if (failures == 0) {
		printf("ALL TESTS PASSED\n");
		return 0;
	}
	printf("TESTS FAILED: %d\n", failures);
	return 1;
}
