#ifndef ND_SEVEN_WORLD_H
#define ND_SEVEN_WORLD_H

/*
 * seven-world.h -- the continuous 2D world of the "seven" stage.
 *
 * Pure C, header-only, no I/O and no XY/corm/axil: it is compiled into the
 * browser UX (WASM) and into the native unit tests from this one source, so
 * what is drawn and what blocks the player can never disagree.
 *
 * The world is the NeverDark map made walkable. Every map tile is one small
 * block of NDW_TILE_W x NDW_TILE_H units (one unit = one source pixel of the
 * seven-ways art; the canvas shows it at 2x). Blocks touch each other with no
 * walls, gates or roads in between: the lamb walks from one into the next and
 * the camera simply scrolls.
 *
 * What a block contains comes from the map tile:
 *   - the biome picks the ground (desert is sand, ice is snow, ...);
 *   - the three map cells (left, centre, right) become the objects of the
 *     block: a plant glyph is that plant family (x deciduous, t tall tree,
 *     a fir, i cactus; lower case is small, upper case is big), '~' a pond,
 *     '$' a shop, '<' '>' stairs, anything else a mark;
 *   - only tiles flagged as a room get walls, and only where an exit is
 *     missing (a gate gap where it is open).
 * Placement is a pure function of the tile's absolute world coordinates
 * (x, y, z, world), so the same tile looks the same from every neighbour and
 * after every re-centring of the view.
 */

#include <stdint.h>
#include <string.h>

#define NDW_COLS      10
#define NDW_ROWS      8
#define NDW_CELL_PX   16
#define NDW_TILE_W    (NDW_COLS * NDW_CELL_PX)   /* 160 */
#define NDW_TILE_H    (NDW_ROWS * NDW_CELL_PX)   /* 128 */
#define NDW_MAX_OBJ   12
#define NDW_MAX_WALL  8

/* Exit bits, as nd_map_tile_t.exits (W=1 N=2 E=8 S=16). */
#define NDW_EXIT_W    0x01
#define NDW_EXIT_N    0x02
#define NDW_EXIT_E    0x08
#define NDW_EXIT_S    0x10

/* Tile flags, as nd_map_tile_t.flags. */
#define NDW_F_ROOM    0x01
#define NDW_F_PLAYER  0x02
#define NDW_F_ENTITY  0x04

/* Where the lamb arrives when the world is (re)centred on a tile. */
#define NDW_CENTER_X  (NDW_TILE_W / 2)
#define NDW_CENTER_Y  (NDW_TILE_H / 2)

/* The lamb's feet: a box 10 wide, 5 tall, ending on (fx, fy). */
#define NDW_FOOT_HW   5
#define NDW_FOOT_H    5

/* One map tile, reduced to what the world needs. */
typedef struct {
	uint8_t biome;      /* enum biome, 0..18 */
	uint8_t exits;      /* NDW_EXIT_* */
	uint8_t flags;      /* NDW_F_* */
	uint8_t glyph[3];   /* left, centre, right map cell glyph; 0 = blank */
	uint8_t fg[3];      /* ANSI colour (0..15) of each glyph */
} ndw_tile_t;

/* --- ground ------------------------------------------------------------- */

/* There is deliberately no path/road ground: only terrain. */
enum { NDW_G_GRASS = 0, NDW_G_SAND, NDW_G_SNOW, NDW_G_TUNDRA, NDW_G_WATER, NDW_G_ASH };

typedef struct {
	uint8_t kind;       /* NDW_G_* */
	uint8_t r, g, b;    /* tint, 128 = unchanged */
} ndw_ground_t;

/* Biome numbers follow enum biome in nd-world's world-types.h. */
static inline ndw_ground_t
ndw_biome_style(unsigned biome)
{
	static const ndw_ground_t t[19] = {
		/* 0  water               */ { NDW_G_WATER, 128, 128, 128 },
		/* 1  permanent ice       */ { NDW_G_SNOW,  128, 128, 128 },
		/* 2  tundra              */ { NDW_G_TUNDRA, 128, 128, 128 },
		/* 3  tundra2             */ { NDW_G_TUNDRA, 118, 128, 134 },
		/* 4  tundra3             */ { NDW_G_TUNDRA, 128, 122, 118 },
		/* 5  tundra4             */ { NDW_G_TUNDRA, 112, 126, 126 },
		/* 6  cold desert         */ { NDW_G_SAND,  104, 116, 128 },
		/* 7  shrubland           */ { NDW_G_GRASS, 150, 128, 84 },
		/* 8  coniferous forest   */ { NDW_G_GRASS, 80, 110, 96 },
		/* 9  boreal forest       */ { NDW_G_GRASS, 86, 116, 106 },
		/* 10 temperate grassland */ { NDW_G_GRASS, 128, 128, 128 },
		/* 11 woodland            */ { NDW_G_GRASS, 116, 128, 100 },
		/* 12 temperate seasonal  */ { NDW_G_GRASS, 132, 126, 98 },
		/* 13 temperate rainforest*/ { NDW_G_GRASS, 90, 118, 102 },
		/* 14 desert              */ { NDW_G_SAND,  128, 128, 128 },
		/* 15 savannah            */ { NDW_G_GRASS, 196, 152, 70 },
		/* 16 tropical seasonal   */ { NDW_G_GRASS, 106, 136, 96 },
		/* 17 tropical rainforest */ { NDW_G_GRASS, 70, 116, 86 },
		/* 18 volcanic            */ { NDW_G_ASH,   128, 128, 128 },
	};
	return biome < 19 ? t[biome] : t[10];
}

/* --- hashing ------------------------------------------------------------ */

typedef struct { uint32_t s; } ndw_rng_t;

static inline uint32_t
ndw_mix(uint32_t h)
{
	h ^= h >> 16; h *= 0x85EBCA6Bu;
	h ^= h >> 13; h *= 0xC2B2AE35u;
	h ^= h >> 16;
	return h;
}

/* pos = absolute (x, y, z, world) of the tile. */
static inline uint32_t
ndw_seed(const int16_t pos[4])
{
	uint32_t h = 0x57524C44u; /* 'WRLD' */
	for (int i = 0; i < 4; i++)
		h = ndw_mix(h ^ ((uint32_t)(uint16_t)pos[i] * 0x9E3779B1u + (uint32_t)i));
	return h;
}

static inline uint32_t
ndw_next(ndw_rng_t *r)
{
	r->s += 0x9E3779B9u;
	return ndw_mix(r->s);
}

static inline uint32_t
ndw_below(ndw_rng_t *r, uint32_t n)
{
	return ndw_next(r) % n;
}

/* Stable per-cell hash (ground variation), cx/cy are the cell inside the tile. */
static inline uint32_t
ndw_cell_hash(const int16_t pos[4], int cx, int cy)
{
	return ndw_mix(ndw_seed(pos) ^ ((uint32_t)(cy * NDW_COLS + cx) * 0x9E3779B1u));
}

static inline int
ndw_fdiv(int a, int b)
{
	int q = a / b;
	return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

/* --- objects ------------------------------------------------------------ */

enum {
	NDW_K_DECID = 1,    /* x / X */
	NDW_K_TALL,         /* t / T */
	NDW_K_FIR,          /* a / A */
	NDW_K_CACTUS,       /* i / I */
	NDW_K_POND,         /* ~ */
	NDW_K_SHOP,         /* $ */
	NDW_K_STAIRS_DOWN,  /* < */
	NDW_K_STAIRS_UP,    /* > */
	NDW_K_MARK          /* anything else (entities ...) */
};

typedef struct {
	uint8_t kind;       /* NDW_K_* */
	uint8_t big;        /* upper-case plant glyph */
	uint8_t fg;         /* ANSI colour of the map glyph */
	uint8_t variant;    /* hash byte; the renderer picks the picture from it */
	uint8_t glyph;      /* the map glyph */
	uint8_t hw, hh;     /* blocking box half width / height (0 = walkable) */
	int16_t x, y;       /* foot point, tile local units */
} ndw_obj_t;

/* Plant family of a map glyph: an NDW_K_* plant kind, else 0. */
static inline int
ndw_plant_kind(int glyph)
{
	switch (glyph | 0x20) {
	case 'x': return NDW_K_DECID;
	case 't': return NDW_K_TALL;
	case 'a': return NDW_K_FIR;
	case 'i': return NDW_K_CACTUS;
	}
	return 0;
}

static inline int
ndw_is_big(int glyph)
{
	return glyph >= 'A' && glyph <= 'Z';
}

typedef struct { int16_t x0, y0, x1, y1; } ndw_rect_t;   /* half open */

#define NDW_MARGIN       12   /* objects keep this far from every tile edge */
#define NDW_ROOM_MARGIN  20   /* ... and this far inside a walled room */
#define NDW_WALL_T       16   /* wall band thickness */
#define NDW_GATE         64   /* gate gap width in a room wall */

/* The clear box around the centre, where the lamb may arrive. */
#define NDW_CLEAR_X0  (NDW_CENTER_X - 30)
#define NDW_CLEAR_X1  (NDW_CENTER_X + 30)
#define NDW_CLEAR_Y0  (NDW_CENTER_Y - 26)
#define NDW_CLEAR_Y1  (NDW_CENTER_Y + 26)

static inline int
ndw_rect_hit(const ndw_rect_t *a, const ndw_rect_t *b)
{
	return a->x0 < b->x1 && b->x0 < a->x1 && a->y0 < b->y1 && b->y0 < a->y1;
}

static inline ndw_rect_t
ndw_obj_rect(const ndw_obj_t *o)
{
	ndw_rect_t r = { (int16_t)(o->x - o->hw), (int16_t)(o->y - o->hh),
	                 (int16_t)(o->x + o->hw), (int16_t)o->y };
	return r;
}

/*
 * Walls of a tile (rooms only): a band on every closed edge, two half bands
 * leaving a NDW_GATE wide gap on an open one. Returns the number of rects.
 */
static inline int
ndw_walls(const ndw_tile_t *t, ndw_rect_t out[NDW_MAX_WALL])
{
	static const struct { uint8_t bit; } e[4] = {
		{ NDW_EXIT_N }, { NDW_EXIT_S }, { NDW_EXIT_W }, { NDW_EXIT_E }
	};
	int n = 0;

	if (!(t->flags & NDW_F_ROOM))
		return 0;
	for (int i = 0; i < 4; i++) {
		int horiz = i < 2;             /* N and S bands run along x */
		int len = horiz ? NDW_TILE_W : NDW_TILE_H;
		int lo = (len - NDW_GATE) / 2, hi = lo + NDW_GATE;
		int near_edge = (i == 0 || i == 2);
		int a0 = near_edge ? 0 : (horiz ? NDW_TILE_H : NDW_TILE_W) - NDW_WALL_T;
		int a1 = a0 + NDW_WALL_T;
		int seg[2][2];
		int nseg;

		if (t->exits & e[i].bit) {
			seg[0][0] = 0;  seg[0][1] = lo;
			seg[1][0] = hi; seg[1][1] = len;
			nseg = 2;
		} else {
			seg[0][0] = 0;  seg[0][1] = len;
			nseg = 1;
		}
		for (int k = 0; k < nseg; k++) {
			ndw_rect_t r;
			if (horiz) { r.x0 = (int16_t)seg[k][0]; r.x1 = (int16_t)seg[k][1]; r.y0 = (int16_t)a0; r.y1 = (int16_t)a1; }
			else       { r.y0 = (int16_t)seg[k][0]; r.y1 = (int16_t)seg[k][1]; r.x0 = (int16_t)a0; r.x1 = (int16_t)a1; }
			out[n++] = r;
		}
	}
	return n;
}

static inline void
ndw_plant_box(int kind, int big, uint8_t *hw, uint8_t *hh)
{
	switch (kind) {
	case NDW_K_CACTUS: *hw = (uint8_t)(big ? 5 : 4); break;
	default:           *hw = (uint8_t)(big ? 6 : 5); break;
	}
	*hh = 6;
}

/*
 * The objects of one tile, back to front. `pos` is the tile's ABSOLUTE
 * (x, y, z, world). `unknown` marks a tile the map does not describe (outside
 * the 7x7 view): only plants are generated for it, so the world fades out in
 * the biome's own flora. Returns the number of objects.
 */
static inline int
ndw_tile_objects(const ndw_tile_t *t, const int16_t pos[4], int unknown,
                 ndw_obj_t out[NDW_MAX_OBJ])
{
	ndw_rng_t rng = { ndw_seed(pos) };
	ndw_rect_t walls[NDW_MAX_WALL];
	int nwall = ndw_walls(t, walls);
	int margin = (t->flags & NDW_F_ROOM) ? NDW_ROOM_MARGIN : NDW_MARGIN;
	int n = 0;

	for (int s = 0; s < 3 && n < NDW_MAX_OBJ; s++) {
		int g = t->glyph[s];
		int third_lo = s * NDW_TILE_W / 3, third_hi = (s + 1) * NDW_TILE_W / 3;
		int cx = (third_lo + third_hi) / 2;
		int pk;

		if (s == 1 && g == '!')
			continue;   /* the server's player marker; the client restores the real glyph underneath */
		if (g == 0 || g == ' ')
			continue;
		pk = ndw_plant_kind(g);
		if (!pk && unknown)
			continue;

		if (pk) {
			int big = ndw_is_big(g);
			int count = big ? 2 + (int)(ndw_next(&rng) & 1) : 1;
			ndw_obj_t placed[3];
			int np = 0;

			for (int c = 0; c < count && n < NDW_MAX_OBJ; c++) {
				for (int attempt = 0; attempt < 14; attempt++) {
					ndw_obj_t o;
					int xlo, xhi, ylo, yhi, ok = 1;
					ndw_rect_t r;

					memset(&o, 0, sizeof(o));
					o.kind = (uint8_t)pk; o.big = (uint8_t)big;
					o.fg = t->fg[s]; o.glyph = (uint8_t)g;
					ndw_plant_box(pk, big, &o.hw, &o.hh);
					xlo = third_lo + 6; xhi = third_hi - 6;
					if (xlo < margin + o.hw) xlo = margin + o.hw;
					if (xhi > NDW_TILE_W - margin - o.hw) xhi = NDW_TILE_W - margin - o.hw;
					if (xhi <= xlo) xhi = xlo + 1;
					ylo = margin + o.hh; yhi = NDW_TILE_H - margin;
					o.x = (int16_t)(xlo + (int)ndw_below(&rng, (uint32_t)(xhi - xlo)));
					o.y = (int16_t)(ylo + (int)ndw_below(&rng, (uint32_t)(yhi - ylo)));
					o.variant = (uint8_t)(ndw_next(&rng) & 0xFF);

					r = ndw_obj_rect(&o);
					{
						ndw_rect_t clear = { NDW_CLEAR_X0, NDW_CLEAR_Y0, NDW_CLEAR_X1, NDW_CLEAR_Y1 };
						if (ndw_rect_hit(&r, &clear))
							ok = 0;
					}
					for (int w = 0; ok && w < nwall; w++)
						if (ndw_rect_hit(&r, &walls[w]))
							ok = 0;
					for (int p = 0; ok && p < np; p++) {
						int dx = o.x - placed[p].x, dy = o.y - placed[p].y;
						if (dx < 0) dx = -dx;
						if (dy < 0) dy = -dy;
						if (dx < 22 && dy < 12)
							ok = 0;
					}
					if (!ok)
						continue;
					placed[np++] = o;
					out[n++] = o;
					break;
				}
			}
			continue;
		}

		{
			ndw_obj_t o;
			memset(&o, 0, sizeof(o));
			o.fg = t->fg[s]; o.glyph = (uint8_t)g;
			o.variant = (uint8_t)(ndw_next(&rng) & 0xFF);
			o.x = (int16_t)cx;
			o.y = (int16_t)(NDW_CENTER_Y + 14);
			switch (g) {
			case '~': o.kind = NDW_K_POND; o.hw = 22; o.hh = 12; o.x = (int16_t)cx; o.y = (int16_t)(NDW_CENTER_Y + 12); break;
			case '$': o.kind = NDW_K_SHOP; o.hw = 18; o.hh = 12; o.y = (int16_t)(NDW_CENTER_Y + 18); break;
			case '<': o.kind = NDW_K_STAIRS_DOWN; break;
			case '>': o.kind = NDW_K_STAIRS_UP; break;
			default:
				o.kind = NDW_K_MARK;
				o.x = (int16_t)(cx + (int)(o.variant & 15) - 8);
				o.y = (int16_t)(NDW_CENTER_Y + 8 + (int)((o.variant >> 4) & 15));
				break;
			}
			out[n++] = o;
		}
	}

	/* Painter's order: back to front by foot row, then by column. */
	for (int i = 1; i < n; i++) {
		ndw_obj_t k = out[i];
		int j = i - 1;
		while (j >= 0 && (out[j].y > k.y || (out[j].y == k.y && out[j].x > k.x))) {
			out[j + 1] = out[j];
			j--;
		}
		out[j + 1] = k;
	}
	return n;
}

/* --- collision ---------------------------------------------------------- */

/*
 * Tile lookup supplied by the caller. (ti, tj) is the tile offset from the
 * origin tile (east and south are positive). Fill *t and the tile's absolute
 * pos[4]; return 0 when the lamb may not enter that tile (not described yet,
 * or too far from the origin), 1 for a map tile, 2 for an unknown tile.
 */
typedef int (*ndw_lookup_fn)(void *ctx, int ti, int tj, ndw_tile_t *t, int16_t pos[4]);

/* 1 if the foot box ending on (fx, fy), in origin tile units, is blocked. */
static inline int
ndw_blocked(ndw_lookup_fn lookup, void *ctx, int fx, int fy)
{
	int x0 = fx - NDW_FOOT_HW, x1 = fx + NDW_FOOT_HW;   /* half open */
	int y0 = fy - NDW_FOOT_H,  y1 = fy;
	int ti0 = ndw_fdiv(x0, NDW_TILE_W), ti1 = ndw_fdiv(x1 - 1, NDW_TILE_W);
	int tj0 = ndw_fdiv(y0, NDW_TILE_H), tj1 = ndw_fdiv(y1 - 1, NDW_TILE_H);

	for (int tj = tj0; tj <= tj1; tj++)
		for (int ti = ti0; ti <= ti1; ti++) {
			ndw_tile_t t;
			int16_t pos[4];
			ndw_obj_t obj[NDW_MAX_OBJ];
			ndw_rect_t walls[NDW_MAX_WALL];
			int kind = lookup(ctx, ti, tj, &t, pos);
			int ox = ti * NDW_TILE_W, oy = tj * NDW_TILE_H;
			ndw_rect_t foot = { (int16_t)(x0 - ox), (int16_t)(y0 - oy),
			                    (int16_t)(x1 - ox), (int16_t)(y1 - oy) };

			if (!kind)
				return 1;
			for (int w = ndw_walls(&t, walls) - 1; w >= 0; w--)
				if (ndw_rect_hit(&foot, &walls[w]))
					return 1;
			int n = ndw_tile_objects(&t, pos, kind == 2, obj);
			for (int i = 0; i < n; i++) {
				ndw_rect_t r;
				if (!obj[i].hw)
					continue;
				r = ndw_obj_rect(&obj[i]);
				if (ndw_rect_hit(&foot, &r))
					return 1;
			}
		}
	return 0;
}

#endif /* ND_SEVEN_WORLD_H */
