#ifndef ND_SEVEN_ROOM_H
#define ND_SEVEN_ROOM_H

/*
 * seven-room.h -- deterministic Zelda-like room generator and collision.
 *
 * Pure C, header-only, no I/O and no XY/corm/axil: it is compiled into the
 * browser UX (WASM) and into the native unit tests from this one source, so
 * what is drawn and what blocks the player can never disagree.
 *
 * A room is a function of the cell's world coordinates (x, y, z, world) and
 * of which exits exist. The same inputs give a byte-identical room on every
 * visit, client and database; neighbouring cells look unrelated because every
 * coordinate is mixed through an avalanche hash.
 *
 * Geometry: 20 x 15 cells of 16 logical units (one unit = one source pixel of
 * the seven-ways art; the canvas shows it at 2x, 640x480).
 */

#include <stdint.h>
#include <string.h>

#define NDS_COLS      20
#define NDS_ROWS      15
#define NDS_CELLS     (NDS_COLS * NDS_ROWS)
#define NDS_CELL_PX   16
#define NDS_ROOM_W    (NDS_COLS * NDS_CELL_PX)   /* 320 */
#define NDS_ROOM_H    (NDS_ROWS * NDS_CELL_PX)   /* 240 */
#define NDS_MAX_OBJ   40

/* Exit bits, as nd_map_tile_t.exits (W=1 N=2 E=8 S=16). */
#define NDS_EXIT_W    0x01
#define NDS_EXIT_N    0x02
#define NDS_EXIT_E    0x08
#define NDS_EXIT_S    0x10

/* Ground layer. */
enum { NDS_G_GRASS = 0, NDS_G_PATH, NDS_G_WALL };

/* Solid objects standing on the ground. Footprint is on the base row. */
enum {
	NDS_O_TREE = 1,     /* 2 cells wide, tall canopy above */
	NDS_O_BUSH,         /* 1 cell */
	NDS_O_BIGROCK,      /* 2 cells wide */
	NDS_O_ROCK          /* 1 cell */
};

typedef struct {
	uint8_t type;       /* NDS_O_* */
	uint8_t x, y;       /* left-most base cell */
	uint8_t w;          /* footprint width in cells (1 or 2) */
	uint8_t variant;    /* hash byte; the renderer picks the picture from it */
} nds_obj_t;

typedef struct {
	uint8_t ground[NDS_CELLS];   /* NDS_G_* */
	uint8_t decor[NDS_CELLS];    /* 0 = none, else 1 + decoration variant */
	uint8_t block[NDS_CELLS];    /* 1 = the player cannot stand here */
	nds_obj_t obj[NDS_MAX_OBJ];  /* sorted by base row (painter's order) */
	uint8_t nobj;
	uint8_t exits;
} nds_room_t;

/* --- hashing ------------------------------------------------------------ */

typedef struct { uint32_t s; } nds_rng_t;

static inline uint32_t nds_mix(uint32_t h)
{
	h ^= h >> 16; h *= 0x85EBCA6Bu;
	h ^= h >> 13; h *= 0xC2B2AE35u;
	h ^= h >> 16;
	return h;
}

static inline uint32_t nds_seed(const int16_t pos[4])
{
	uint32_t h = 0x5A454C44u; /* 'ZELD' */
	for (int i = 0; i < 4; i++)
		h = nds_mix(h ^ ((uint32_t)(uint16_t)pos[i] * 0x9E3779B1u + (uint32_t)i));
	return h;
}

static inline uint32_t nds_next(nds_rng_t *r)
{
	r->s += 0x9E3779B9u;
	return nds_mix(r->s);
}

static inline uint32_t nds_below(nds_rng_t *r, uint32_t n)
{
	return nds_next(r) % n;
}

/* --- generation --------------------------------------------------------- */

static inline int nds_in(int x, int y)
{
	return x >= 0 && y >= 0 && x < NDS_COLS && y < NDS_ROWS;
}

/* Cell columns/rows of the 2-wide gates and of the central plaza. */
#define NDS_GATE_X0 9   /* N/S gates: columns 9..10 */
#define NDS_GATE_Y0 7   /* W/E gates: rows 7..8 */
#define NDS_HUB_X0  8   /* plaza: columns 8..11, rows 6..9 */
#define NDS_HUB_X1  11
#define NDS_HUB_Y0  6
#define NDS_HUB_Y1  9

static inline void nds_set_path(nds_room_t *r, int x, int y)
{
	if (nds_in(x, y)) {
		r->ground[y * NDS_COLS + x] = NDS_G_PATH;
		r->block[y * NDS_COLS + x] = 0;
	}
}

static inline void
nds_room_build(nds_room_t *r, const int16_t pos[4], uint8_t exits)
{
	nds_rng_t rng = { nds_seed(pos) };
	uint8_t keep[NDS_CELLS];   /* cells objects may not touch */
	int x, y;

	memset(r, 0, sizeof(*r));
	r->exits = exits & (NDS_EXIT_W | NDS_EXIT_N | NDS_EXIT_E | NDS_EXIT_S);
	memset(keep, 0, sizeof(keep));

	/* Stone wall around the room. */
	for (y = 0; y < NDS_ROWS; y++)
		for (x = 0; x < NDS_COLS; x++)
			if (x == 0 || y == 0 || x == NDS_COLS - 1 || y == NDS_ROWS - 1) {
				r->ground[y * NDS_COLS + x] = NDS_G_WALL;
				r->block[y * NDS_COLS + x] = 1;
			}

	/* Plaza in the middle: arrival point and the junction of all roads. */
	for (y = NDS_HUB_Y0; y <= NDS_HUB_Y1; y++)
		for (x = NDS_HUB_X0; x <= NDS_HUB_X1; x++)
			nds_set_path(r, x, y);

	/* A two cell wide road from each open gate to the plaza. */
	if (r->exits & NDS_EXIT_N)
		for (y = 0; y < NDS_HUB_Y0; y++) { nds_set_path(r, NDS_GATE_X0, y); nds_set_path(r, NDS_GATE_X0 + 1, y); }
	if (r->exits & NDS_EXIT_S)
		for (y = NDS_HUB_Y1 + 1; y < NDS_ROWS; y++) { nds_set_path(r, NDS_GATE_X0, y); nds_set_path(r, NDS_GATE_X0 + 1, y); }
	if (r->exits & NDS_EXIT_W)
		for (x = 0; x < NDS_HUB_X0; x++) { nds_set_path(r, x, NDS_GATE_Y0); nds_set_path(r, x, NDS_GATE_Y0 + 1); }
	if (r->exits & NDS_EXIT_E)
		for (x = NDS_HUB_X1 + 1; x < NDS_COLS; x++) { nds_set_path(r, x, NDS_GATE_Y0); nds_set_path(r, x, NDS_GATE_Y0 + 1); }

	/* Objects keep one cell clear of every path (and of the walls). */
	for (y = 0; y < NDS_ROWS; y++)
		for (x = 0; x < NDS_COLS; x++) {
			int near_path = 0;
			for (int dy = -1; dy <= 1; dy++)
				for (int dx = -1; dx <= 1; dx++)
					if (nds_in(x + dx, y + dy) &&
					    r->ground[(y + dy) * NDS_COLS + x + dx] == NDS_G_PATH)
						near_path = 1;
			if (near_path || x < 1 || y < 1 || x > NDS_COLS - 2 || y > NDS_ROWS - 2)
				keep[y * NDS_COLS + x] = 1;
		}

	/* Scatter trees, bushes and stones: random-looking, never touching. */
	int ntree = 0, nrock = 0, nbush = 0;
	for (int attempt = 0; attempt < 140 && r->nobj < NDS_MAX_OBJ; attempt++) {
		uint32_t roll = nds_below(&rng, 100);
		uint8_t type, w;
		if (roll < 38)       { type = NDS_O_TREE;    w = 2; }
		else if (roll < 54)  { type = NDS_O_BUSH;    w = 1; }
		else if (roll < 66)  { type = NDS_O_BIGROCK; w = 2; }
		else                 { type = NDS_O_ROCK;    w = 1; }
		int ox = 1 + (int)nds_below(&rng, NDS_COLS - 2);
		int oy = 2 + (int)nds_below(&rng, NDS_ROWS - 3);
		uint8_t variant = (uint8_t)(nds_next(&rng) & 0xFF);

		if ((type == NDS_O_TREE && ntree >= 7) ||
		    (type == NDS_O_BUSH && nbush >= 5) ||
		    ((type == NDS_O_BIGROCK || type == NDS_O_ROCK) && nrock >= 7))
			continue;

		int ok = 1;
		for (int i = 0; i < w && ok; i++)
			if (!nds_in(ox + i, oy) || keep[oy * NDS_COLS + ox + i] || r->block[oy * NDS_COLS + ox + i])
				ok = 0;
		if (!ok)
			continue;

		nds_obj_t *o = &r->obj[r->nobj++];
		o->type = type; o->x = (uint8_t)ox; o->y = (uint8_t)oy;
		o->w = w; o->variant = variant;
		if (type == NDS_O_TREE) ntree++;
		else if (type == NDS_O_BUSH) nbush++;
		else nrock++;

		for (int i = 0; i < w; i++)
			r->block[oy * NDS_COLS + ox + i] = 1;
		/* reserve a one cell margin so objects never touch each other */
		for (int dy = -1; dy <= 1; dy++)
			for (int dx = -1; dx <= w; dx++)
				if (nds_in(ox + dx, oy + dy))
					keep[(oy + dy) * NDS_COLS + ox + dx] = 1;
	}

	/* Painter's order: back to front by base row, then by column. */
	for (int i = 1; i < r->nobj; i++) {
		nds_obj_t t = r->obj[i];
		int j = i - 1;
		while (j >= 0 && (r->obj[j].y > t.y || (r->obj[j].y == t.y && r->obj[j].x > t.x))) {
			r->obj[j + 1] = r->obj[j];
			j--;
		}
		r->obj[j + 1] = t;
	}

	/* Ground decoration: sparse flowers on free grass. */
	for (y = 1; y < NDS_ROWS - 1; y++)
		for (x = 1; x < NDS_COLS - 1; x++) {
			int idx = y * NDS_COLS + x;
			uint32_t h = nds_next(&rng);
			if (r->ground[idx] == NDS_G_GRASS && !r->block[idx] && (h % 100) < 9)
				r->decor[idx] = (uint8_t)(1 + ((h >> 8) & 3));
		}
}

/* --- collision & gates -------------------------------------------------- */

/* The lamb's feet: a box 10 wide, 5 tall, centred on (fx, fy) = ground point. */
#define NDS_FOOT_HW 5
#define NDS_FOOT_H  5

static inline int
nds_cell_blocked(const nds_room_t *r, int cx, int cy)
{
	if (!nds_in(cx, cy))
		return 1;
	return r->block[cy * NDS_COLS + cx];
}

/* 1 if the foot box centred on (fx, fy) overlaps a blocked cell. Points
 * outside the room are blocked unless they pass through an open gate; use
 * nds_gate_exit() to detect that crossing first. */
static inline int
nds_foot_blocked(const nds_room_t *r, int fx, int fy)
{
	int x0 = fx - NDS_FOOT_HW, x1 = fx + NDS_FOOT_HW - 1;
	int y0 = fy - NDS_FOOT_H,  y1 = fy - 1;
	int cx0 = x0 < 0 ? -1 : x0 / NDS_CELL_PX, cx1 = x1 < 0 ? -1 : x1 / NDS_CELL_PX;
	int cy0 = y0 < 0 ? -1 : y0 / NDS_CELL_PX, cy1 = y1 < 0 ? -1 : y1 / NDS_CELL_PX;

	for (int cy = cy0; cy <= cy1; cy++)
		for (int cx = cx0; cx <= cx1; cx++)
			if (nds_cell_blocked(r, cx, cy))
				return 1;
	return 0;
}

/*
 * If the foot box has left the room through an open gate, return the
 * NeverDark move key ('h' west, 'k' north, 'l' east, 'j' south); else 0.
 * Test this BEFORE nds_foot_blocked(): cells outside the room are solid.
 * Only gate cells are open in the wall ring, so leaving the box this way is
 * only possible through a gate. (Screen y grows downwards; north is up.)
 */
static inline int
nds_gate_exit(const nds_room_t *r, int fx, int fy)
{
	if (fx - NDS_FOOT_HW < 0 && (r->exits & NDS_EXIT_W)) return 'h';
	if (fx + NDS_FOOT_HW > NDS_ROOM_W && (r->exits & NDS_EXIT_E)) return 'l';
	if (fy - NDS_FOOT_H < 0 && (r->exits & NDS_EXIT_N)) return 'k';
	if (fy >= NDS_ROOM_H && (r->exits & NDS_EXIT_S)) return 'j';
	return 0;
}

/* Where the lamb stands after walking in through the gate opposite `key`
 * (the key that was pressed in the previous room). key==0: the plaza. */
static inline void
nds_entry_point(int key, int *fx, int *fy)
{
	*fx = (NDS_GATE_X0 + 1) * NDS_CELL_PX;       /* between the two gate columns */
	*fy = (NDS_GATE_Y0 + 1) * NDS_CELL_PX + 4;   /* between the two gate rows */
	switch (key) {
	case 'l': *fx = NDS_CELL_PX + NDS_FOOT_HW + 2; break;                      /* came east: enter at west */
	case 'h': *fx = NDS_ROOM_W - NDS_CELL_PX - NDS_FOOT_HW - 2; break;
	case 'k': *fy = NDS_ROOM_H - NDS_CELL_PX - 1; break;                       /* went north: enter at south */
	case 'j': *fy = NDS_CELL_PX + NDS_FOOT_H + 2; break;
	}
}

#endif /* ND_SEVEN_ROOM_H */
