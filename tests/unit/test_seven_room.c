#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <nd/seven-room.h>

/* FNV-1a over the room's observable content. */
static uint32_t room_hash(const nds_room_t *r)
{
	uint32_t h = 2166136261u;
	const uint8_t *parts[3] = { r->ground, r->decor, r->block };
	for (int p = 0; p < 3; p++)
		for (int i = 0; i < NDS_CELLS; i++)
			h = (h ^ parts[p][i]) * 16777619u;
	for (int i = 0; i < r->nobj; i++) {
		const uint8_t *o = (const uint8_t *)&r->obj[i];
		for (size_t k = 0; k < sizeof(r->obj[i]); k++)
			h = (h ^ o[k]) * 16777619u;
	}
	return h;
}

/* Walk the foot box from the plaza towards a gate, one unit at a time. */
static int walk(const nds_room_t *r, int dx, int dy, int *steps)
{
	int fx, fy;
	nds_entry_point(0, &fx, &fy);
	assert(!nds_foot_blocked(r, fx, fy));
	for (int i = 0; i < 1000; i++) {
		fx += dx; fy += dy;
		int key = nds_gate_exit(r, fx, fy);
		if (key) { *steps = i; return key; }
		if (nds_foot_blocked(r, fx, fy)) { *steps = i; return 0; }
	}
	*steps = 1000;
	return 0;
}

int main(void)
{
	printf("=== ND seven room generator, collision and gates ===\n");

	int16_t a[4] = { -1, 1, 0, 0 }, b[4] = { -2, 1, 0, 0 }, c[4] = { -1, 2, 0, 0 };
	nds_room_t ra, ra2, rb, rc;

	/* 1. Pure function of coordinates. */
	nds_room_build(&ra, a, 0x1B);
	nds_room_build(&ra2, a, 0x1B);
	assert(room_hash(&ra) == room_hash(&ra2));
	assert(memcmp(&ra, &ra2, sizeof(ra)) == 0);
	printf("PASS: same coordinates give a byte-identical room\n");

	/* 2. Different coordinates give different rooms (x, y, z and world each). */
	nds_room_build(&rb, b, 0x1B);
	nds_room_build(&rc, c, 0x1B);
	assert(room_hash(&ra) != room_hash(&rb));
	assert(room_hash(&ra) != room_hash(&rc));
	assert(room_hash(&rb) != room_hash(&rc));
	int16_t z1[4] = { -1, 1, 1, 0 }, w1[4] = { -1, 1, 0, 1 };
	nds_room_build(&rb, z1, 0x1B); assert(room_hash(&ra) != room_hash(&rb));
	nds_room_build(&rb, w1, 0x1B); assert(room_hash(&ra) != room_hash(&rb));
	printf("PASS: neighbouring cells, floors and worlds look different\n");

	/* 3. Golden value: layouts must not drift unless changed on purpose. */
	printf("room(-1,1,0,0,0x1B) hash = 0x%08X\n", room_hash(&ra));
	assert(room_hash(&ra) == SEVEN_GOLDEN);
	printf("PASS: golden layout for (-1,1,0,0) unchanged\n");

	/* 4. Structure over many rooms and all 16 exit combinations. */
	int colseen[NDS_COLS] = {0}, rowseen[NDS_ROWS] = {0};
	int trees = 0, rocks = 0, rooms = 0;
	for (int x = -8; x <= 0; x++)
	for (int y = 1; y <= 8; y++)
	for (int ex = 0; ex < 16; ex++) {
		uint8_t exits = (uint8_t)((ex & 1 ? NDS_EXIT_W : 0) | (ex & 2 ? NDS_EXIT_N : 0) |
		                          (ex & 4 ? NDS_EXIT_E : 0) | (ex & 8 ? NDS_EXIT_S : 0));
		int16_t p[4] = { (int16_t)x, (int16_t)y, 0, 0 };
		nds_room_t r;
		nds_room_build(&r, p, exits);
		rooms++;
		uint8_t occ[NDS_CELLS]; memset(occ, 0, sizeof(occ));
		assert(r.nobj <= NDS_MAX_OBJ);
		for (int i = 0; i < r.nobj; i++) {
			const nds_obj_t *o = &r.obj[i];
			assert(o->w == 1 || o->w == 2);
			for (int k = 0; k < o->w; k++) {
				int idx = o->y * NDS_COLS + o->x + k;
				assert(o->x + k < NDS_COLS);
				assert(r.ground[idx] == NDS_G_GRASS);   /* never on a road or wall */
				assert(r.block[idx]);                   /* and always solid */
				assert(!occ[idx]);                      /* no two objects overlap */
				occ[idx] = 1;
			}
			if (o->type == NDS_O_TREE) trees++;
			if (o->type == NDS_O_ROCK || o->type == NDS_O_BIGROCK) rocks++;
			colseen[o->x] = rowseen[o->y] = 1;
			if (i) assert(r.obj[i - 1].y < o->y || (r.obj[i - 1].y == o->y && r.obj[i - 1].x <= o->x));
		}
		/* every open gate is walkable and leaves in the right direction;
		 * closed gates are solid. */
		int s;
		int want[4]  = { exits & NDS_EXIT_W ? 'h' : 0, exits & NDS_EXIT_E ? 'l' : 0,
		                 exits & NDS_EXIT_N ? 'k' : 0, exits & NDS_EXIT_S ? 'j' : 0 };
		int got[4];
		got[0] = walk(&r, -1, 0, &s);
		got[1] = walk(&r,  1, 0, &s);
		got[2] = walk(&r, 0, -1, &s);
		got[3] = walk(&r, 0,  1, &s);
		for (int k = 0; k < 4; k++) assert(got[k] == want[k]);
		/* entering from any side lands on free ground inside the room */
		for (const int *kp = (const int[]){ 0, 'h', 'j', 'k', 'l', -1 }; *kp != -1; kp++) {
			int fx, fy;
			nds_entry_point(*kp, &fx, &fy);
			assert(fx > 0 && fx < NDS_ROOM_W && fy > 0 && fy < NDS_ROOM_H);
			int need = *kp == 'l' ? NDS_EXIT_W : *kp == 'h' ? NDS_EXIT_E :
			           *kp == 'k' ? NDS_EXIT_S : *kp == 'j' ? NDS_EXIT_N : 0;
			if (*kp == 0 || (exits & need))
				assert(!nds_foot_blocked(&r, fx, fy));
		}
	}
	assert(trees > rooms * 2 && rocks > rooms * 2);
	int cols = 0, rows = 0;
	for (int i = 0; i < NDS_COLS; i++) cols += colseen[i];
	for (int i = 0; i < NDS_ROWS; i++) rows += rowseen[i];
	assert(cols >= 14 && rows >= 9);   /* scattered over the room, not on a grid */
	printf("PASS: %d rooms: objects on grass only, no overlap, gates walkable, "
	       "%d trees %d rocks over %d columns x %d rows\n", rooms, trees, rocks, cols, rows);

	/* 5. Walls and objects block; open grass does not. */
	assert(nds_foot_blocked(&ra, 8, 8));                 /* inside the wall ring */
	assert(!nds_foot_blocked(&ra, 10 * 16, 8 * 16));      /* plaza */
	printf("PASS: collision mask agrees with the layout\n");

	printf("ALL SEVEN ROOM TESTS PASSED\n");
	return 0;
}
