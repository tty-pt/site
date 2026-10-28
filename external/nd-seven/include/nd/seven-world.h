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
	NDW_K_STONE,        /* * (small stone) */
	NDW_K_ROCK,         /* o (medium rock) */
	NDW_K_BOULDER,      /* O (big boulder) */
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

typedef struct {
	char glyph;
	uint8_t kind;
	uint8_t is_plant;
	uint8_t is_big;
	uint8_t hw, hh;
} ndw_content_entry_t;

static inline const ndw_content_entry_t *
ndw_find_content(int glyph)
{
	static const ndw_content_entry_t table[] = {
		{ 'x', NDW_K_DECID, 1, 0, 5, 6 },
		{ 'X', NDW_K_DECID, 1, 1, 6, 6 },
		{ 't', NDW_K_TALL,  1, 0, 5, 6 },
		{ 'T', NDW_K_TALL,  1, 1, 6, 6 },
		{ 'a', NDW_K_FIR,   1, 0, 5, 6 },
		{ 'A', NDW_K_FIR,   1, 1, 6, 6 },
		{ 'i', NDW_K_CACTUS,1, 0, 4, 6 },
		{ 'I', NDW_K_CACTUS,1, 1, 5, 6 },
		{ '*', NDW_K_STONE, 0, 0, 4, 3 },
		{ 'o', NDW_K_ROCK,  0, 0, 6, 4 },
		{ 'O', NDW_K_BOULDER, 0, 1, 8, 6 },
	};
	for (size_t i = 0; i < sizeof(table)/sizeof(table[0]); i++) {
		if (table[i].glyph == (char)glyph)
			return &table[i];
	}
	return NULL;
}

/* Plant family of a map glyph: an NDW_K_* plant kind, else 0. */
static inline int
ndw_plant_kind(int glyph)
{
	const ndw_content_entry_t *ce = ndw_find_content(glyph);
	if (ce && ce->is_plant)
		return ce->kind;
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
		const ndw_content_entry_t *ce;

		if (s == 1 && g == '!')
			continue;   /* the server's player marker; the client restores the real glyph underneath */
		if (g == 0 || g == ' ')
			continue;
		ce = ndw_find_content(g);
		if (!ce && unknown)
			continue;

		if (ce && ce->is_plant) {
			int big = ce->is_big;
			int count = big ? 2 + (int)(ndw_next(&rng) & 1) : 1;
			ndw_obj_t placed[3];
			int np = 0;

			for (int c = 0; c < count && n < NDW_MAX_OBJ; c++) {
				for (int attempt = 0; attempt < 14; attempt++) {
					ndw_obj_t o;
					int xlo, xhi, ylo, yhi, ok = 1;
					ndw_rect_t r;

					memset(&o, 0, sizeof(o));
					o.kind = ce->kind; o.big = (uint8_t)big;
					o.fg = t->fg[s]; o.glyph = (uint8_t)g;
					ndw_plant_box(ce->kind, big, &o.hw, &o.hh);
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

		if (ce && !ce->is_plant) {
			int count = ce->is_big ? 1 + (int)(ndw_next(&rng) & 1) : 1;
			ndw_obj_t placed[2];
			int np = 0;

			for (int c = 0; c < count && n < NDW_MAX_OBJ; c++) {
				for (int attempt = 0; attempt < 14; attempt++) {
					ndw_obj_t o;
					int xlo, xhi, ylo, yhi, ok = 1;
					ndw_rect_t r;

					memset(&o, 0, sizeof(o));
					o.kind = ce->kind; o.big = ce->is_big;
					o.fg = t->fg[s]; o.glyph = (uint8_t)g;
					o.hw = ce->hw; o.hh = ce->hh;
					xlo = third_lo + 4; xhi = third_hi - 4;
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
						if (dx < 18 && dy < 10)
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

/* --- organic ground borders ---------------------------------------------- */

/*
 * Biome borders used to be the tile edges: square. A border is now a smooth,
 * organic contour, and it is crisp: every ground point belongs to exactly ONE
 * tile, so the two grounds never mix into a blur, each keeps its own pixels.
 *
 * Where the contour runs is one global function of the ABSOLUTE world point:
 *   - the point is displaced by a four-octave value-noise warp (64 / 32 / 16 / 8
 *     units, smooth, no speckle) of its absolute position and the (z, world) of
 *     the tile, which bends the border into bays and capes;
 *   - every tile is a box with its convex corners rounded (radius NDW_CORNER_R:
 *     a corner is round where both neighbours across it hold another ground),
 *     so an isolated tile is a smooth oval and no corner stays square;
 *   - the owner of the displaced point is the tile whose rounded box it lies
 *     deepest inside (a signed distance); several tiles of one ground count as
 *     one, so they merge without notch or seam.
 * The same function runs from every tile, after every re-centring, and in Seven
 * and in the lamina minimap, so there is no seam anywhere, corners included.
 *
 * The warp is sampled on a grid of nodes NDW_WARP_STEP units apart (shared by
 * neighbouring tiles; the nodes on a tile edge are identical) and interpolated
 * bilinearly, so evaluating it per output pixel is cheap. Positions inside the
 * tile are fixed point: NDW_SUB steps per unit ("q").
 */
#define NDW_SUB            4      /* q steps per unit */
#define NDW_BLEND_AMP      8      /* max border displacement per axis, units */
#define NDW_CORNER_RX      64     /* corner horizontal rounding radius, units */
#define NDW_CORNER_RY      64     /* corner vertical rounding radius, units */
#define NDW_CORNER_R       NDW_CORNER_RX
#define NDW_EDGE_BAND      (NDW_BLEND_AMP + 4)                  /* the border lies this close to a straight edge */
#define NDW_BLEND_BAND     (NDW_CORNER_RX + NDW_BLEND_AMP + 2)  /* points within this distance of an edge may not be the tile's own */
#define NDW_WARP_STEP      2      /* node spacing, units */
#define NDW_WARP_NX        (NDW_TILE_W / NDW_WARP_STEP + 1)
#define NDW_WARP_NY        (NDW_TILE_H / NDW_WARP_STEP + 1)
#define NDW_WARP_EDGE      ((NDW_EDGE_BAND + 2) / NDW_WARP_STEP + 1)    /* node strip filled along each edge */
#define NDW_WARP_CORNER    ((NDW_CORNER_RX + NDW_EDGE_BAND + 2) / NDW_WARP_STEP + 1)  /* node square filled at each corner */

/* Bit of tile offset (i, j), each -1..0..1, in a 3x3 "may not blend" mask. */
#define NDW_OWN_BIT(i, j)  (1u << (((j) + 1) * 3 + ((i) + 1)))

#define NDW_FINE           256    /* fixed point of the border maths: 1/256 unit */

/* Warp of the nodes of one tile, in q (1/NDW_SUB unit), x and y. */
typedef struct {
	int8_t dx[NDW_WARP_NY][NDW_WARP_NX];
	int8_t dy[NDW_WARP_NY][NDW_WARP_NX];
	int32_t bx, by;      /* world unit of the tile's top-left corner */
	uint32_t seed;       /* (z, world) seed of the edge detail */
} ndw_warp_grid_t;

/* One lattice value, 0..255. */
static inline int
ndw_lattice(uint32_t seed, int gx, int gy)
{
	return (int)(ndw_mix(ndw_mix(seed ^ ((uint32_t)gx * 0x9E3779B1u))
	                     ^ ((uint32_t)gy * 0x85EBCA77u)) & 0xFFu);
}

/* Smooth value noise, 0..255, lattice spacing 1 << shift. */
static inline int
ndw_vnoise(uint32_t seed, int x, int y, int shift)
{
	int cell = 1 << shift;
	int gx = ndw_fdiv(x, cell), gy = ndw_fdiv(y, cell);
	int fx = ((x - gx * cell) * 256) >> shift;   /* 0..255 */
	int fy = ((y - gy * cell) * 256) >> shift;
	int sx = (fx * fx * (768 - 2 * fx)) >> 16;   /* smoothstep, 0..255 */
	int sy = (fy * fy * (768 - 2 * fy)) >> 16;
	int a = ndw_lattice(seed, gx, gy),     b = ndw_lattice(seed, gx + 1, gy);
	int c = ndw_lattice(seed, gx, gy + 1), d = ndw_lattice(seed, gx + 1, gy + 1);
	int top = a + (b - a) * sx / 256;
	int bot = c + (d - c) * sx / 256;

	return top + (bot - top) * sy / 256;
}

/*
 * Warp along one axis (salt picks x or y) at the world unit point (wx, wy), in q.
 * Four octaves, 64 / 32 / 16 / 8 units; each octave sheared a little differently
 * so the lattice never shows.
 */
static inline int
ndw_warp_axis(const int16_t pos[4], int wx, int wy, uint32_t salt)
{
	uint32_t seed = ndw_mix(0x424F5244u ^ salt
	                        ^ ((uint32_t)(uint16_t)pos[2] * 0x9E3779B1u)
	                        ^ ((uint32_t)(uint16_t)pos[3] * 0x85EBCA6Bu));
	int x = wx * NDW_SUB, y = wy * NDW_SUB;
	int n0 = ndw_vnoise(seed, x + y / 4, y - x / 4, 8);
	int n1 = ndw_vnoise(seed ^ 0x68E31DA4u, x - y / 4, y + x / 4, 7);
	int n2 = ndw_vnoise(seed ^ 0xB5297A4Du, x + y / 5, y - x / 4, 6);
	int n3 = ndw_vnoise(seed ^ 0x1B56C4E9u, x - y / 3, y + x / 5, 5);
	int n4 = ndw_vnoise(seed ^ 0x9C4E2B18u, x + y / 2, y - x / 3, 4);
	int n5 = ndw_vnoise(seed ^ 0x3F8A65D1u, x - y / 2, y + x / 2, 3);
	int n = (n0 * 4 + n1 * 3 + n2 * 5 + n3 * 4 + n4 * 2 + n5) / 19;
	int v = (n - 128) * 3 / 2;                   /* reach full amplitude */

	if (v < -128) v = -128;
	if (v > 127) v = 127;
	return v * (NDW_BLEND_AMP * NDW_SUB) / 128;
}

/* Fill the nodes the border can read (a strip along each edge, a square at each corner); others are 0. */
static inline void
ndw_warp_grid_fill(ndw_warp_grid_t *g, const int16_t pos[4])
{
	int bx = (int)pos[0] * NDW_TILE_W;
	int by = -(int)pos[1] * NDW_TILE_H;   /* y grows south on screen */

	memset(g, 0, sizeof(*g));
	g->bx = bx;
	g->by = by;
	g->seed = ndw_mix(0x45444745u ^ ((uint32_t)(uint16_t)pos[2] * 0x9E3779B1u)
	                  ^ ((uint32_t)(uint16_t)pos[3] * 0x85EBCA6Bu));
	for (int ny = 0; ny < NDW_WARP_NY; ny++) {
		int ey = ny < NDW_WARP_EDGE || ny >= NDW_WARP_NY - NDW_WARP_EDGE;
		int cy = ny < NDW_WARP_CORNER || ny >= NDW_WARP_NY - NDW_WARP_CORNER;
		int wy = by + ny * NDW_WARP_STEP;

		for (int nx = 0; nx < NDW_WARP_NX; nx++) {
			int ex = nx < NDW_WARP_EDGE || nx >= NDW_WARP_NX - NDW_WARP_EDGE;
			int cx = nx < NDW_WARP_CORNER || nx >= NDW_WARP_NX - NDW_WARP_CORNER;
			int wx;

			if (!(ex || ey || (cx && cy)))
				continue;
			wx = bx + nx * NDW_WARP_STEP;
			g->dx[ny][nx] = (int8_t)ndw_warp_axis(pos, wx, wy, 0x1u);
			g->dy[ny][nx] = (int8_t)ndw_warp_axis(pos, wx, wy, 0x2u);
		}
	}
}

/*
 * The warp at tile-local q position (qx, qy), 0 <= qx <= NDW_TILE_W * NDW_SUB
 * and 0 <= qy <= NDW_TILE_H * NDW_SUB, bilinear between nodes, in 1/256 unit.
 */
static inline void
ndw_warp_at(const ndw_warp_grid_t *g, int qx, int qy, int *dx, int *dy)
{
	const int node_q = NDW_WARP_STEP * NDW_SUB;
	int nx = qx / node_q, fx = qx % node_q;
	int ny = qy / node_q, fy = qy % node_q;
	int w00, w10, w01, w11;

	if (nx >= NDW_WARP_NX - 1) { nx = NDW_WARP_NX - 2; fx = node_q; }
	if (ny >= NDW_WARP_NY - 1) { ny = NDW_WARP_NY - 2; fy = node_q; }
	w00 = (node_q - fx) * (node_q - fy);
	w10 = fx * (node_q - fy);
	w01 = (node_q - fx) * fy;
	w11 = fx * fy;

	/* sum of weights = node_q^2 = 64 with the default constants: q * 64 = 1/256 unit */
	*dx = g->dx[ny][nx] * w00 + g->dx[ny][nx + 1] * w10 + g->dx[ny + 1][nx] * w01 + g->dx[ny + 1][nx + 1] * w11;
	*dy = g->dy[ny][nx] * w00 + g->dy[ny][nx + 1] * w10 + g->dy[ny + 1][nx] * w01 + g->dy[ny + 1][nx + 1] * w11;
}

/*
 * Displacement (dx, dy), each within +-NDW_BLEND_AMP, at tile-local unit
 * (lx, ly) of the tile at absolute `pos`. lx, ly may lie outside the tile.
 */
static inline void
ndw_border_warp(const int16_t pos[4], int lx, int ly, int *dx, int *dy)
{
	int wx = (int)pos[0] * NDW_TILE_W + lx;
	int wy = -(int)pos[1] * NDW_TILE_H + ly;

	*dx = ndw_warp_axis(pos, wx, wy, 0x1u) / NDW_SUB;
	*dy = ndw_warp_axis(pos, wx, wy, 0x2u) / NDW_SUB;
}

/* 0..256 smoothstep of t (0..256). */
static inline int
ndw_smooth256(int t)
{
	if (t <= 0) return 0;
	if (t >= 256) return 256;
	return (t * t * (768 - 2 * t)) >> 16;
}

static inline int
ndw_isqrt(int n)
{
	if (n <= 0) return 0;
	int x = n, c = 0, d = 1 << 30;
	while (d > n) d >>= 2;
	while (d != 0) {
		if (x >= c + d) {
			x -= c + d;
			c = (c >> 1) + d;
		} else {
			c >>= 1;
		}
		d >>= 2;
	}
	return c;
}

/* Does the tile at 3x3 offset (i, j) have another, blendable ground at (i + di, j + dj)? */
static inline int
ndw_differs_at(const uint8_t grp[9], unsigned no_blend, int i, int j, int di, int dj)
{
	int ni = i + di, nj = j + dj;

	if (ni < -1 || ni > 1 || nj < -1 || nj > 1)
		return 0;   /* unknown (far from the tile): square */
	if (no_blend & NDW_OWN_BIT(ni, nj))
		return 0;   /* rooms stay square, so nothing rounds against them */
	return grp[(nj + 1) * 3 + (ni + 1)] != grp[(j + 1) * 3 + (i + 1)];
}

/*
 * How far inside the rounded box of the tile at 3x3 offset (i, j) the point
 * (px, py) lies (1/NDW_FINE unit, relative to the 3x3 block, negative outside).
 * A corner is round when both neighbours across it hold another ground.
 */
static inline int
ndw_tile_depth(const uint8_t grp[9], unsigned no_blend, int i, int j, int px, int py)
{
	const int hx = NDW_TILE_W * NDW_FINE / 2, hy = NDW_TILE_H * NDW_FINE / 2;
	int cx = i * NDW_TILE_W * NDW_FINE + hx, cy = j * NDW_TILE_H * NDW_FINE + hy;
	int ax = px - cx, ay = py - cy;
	int sx = ax < 0 ? -1 : 1, sy = ay < 0 ? -1 : 1;
	int rx = NDW_CORNER_RX * NDW_FINE;
	int ry = NDW_CORNER_RY * NDW_FINE;
	int diff_x = ndw_differs_at(grp, no_blend, i, j, sx, 0);
	int diff_y = ndw_differs_at(grp, no_blend, i, j, 0, sy);
	int qx, qy;

	if (!diff_x && !diff_y) {
		int dx = (ax < 0 ? -ax : ax) - hx;
		int dy = (ay < 0 ? -ay : ay) - hy;
		return -(dx > dy ? dx : dy);
	}
	qx = diff_x ? (ax < 0 ? -ax : ax) : 0;
	qy = diff_y ? (ay < 0 ? -ay : ay) : 0;

	if (qx > 0 && qy > 0) {
		int a = qx / 16;
		int b = (int)(((int64_t)qy * rx / ry) / 16);
		return rx - ndw_isqrt(a * a + b * b) * 16;
	}
	if (qx > 0) return rx - qx;
	if (qy > 0) return rx - (int)((int64_t)qy * rx / ry);
	return rx;
}

/*
 * Who owns the ground at tile-local q position (qx, qy) of the tile: exactly
 * one of the 3x3 tiles around it; w[(j + 1) * 3 + (i + 1)] is 256 for the owner
 * at offset (i, j) (east and south positive, w[4] the tile itself) and 0 for the
 * others, so the owner's ground shows as it is, never mixed with another.
 * `grp` gives the ground of each of the 3x3 tiles (equal ids = same ground); a
 * tile whose ground matches the tile's own never takes a point from it, they
 * count as one. `no_blend` is a 3x3 mask of NDW_OWN_BIT tiles that must not own
 * anything: rooms (square, walled) and neighbours the caller knows nothing
 * about. If the tile itself is in the mask every point stays its own.
 * Returns 1 when the point is not the tile's own.
 */
static inline int
ndw_ground_weights(const ndw_warp_grid_t *g, int qx, int qy, const uint8_t grp[9], unsigned no_blend, uint16_t w[9])
{
	int px = qx * (NDW_FINE / NDW_SUB), py = qy * (NDW_FINE / NDW_SUB);
	const int hx = NDW_TILE_W * NDW_FINE / 2, hy = NDW_TILE_H * NDW_FINE / 2;
	int dx, dy;

	for (int k = 0; k < 9; k++)
		w[k] = 0;
	w[4] = 256;
	if (no_blend & NDW_OWN_BIT(0, 0))
		return 0;

	ndw_warp_at(g, qx, qy, &dx, &dy);
	int px_w = px + dx;
	int py_w = py + dy;

	/* Determine grid cell containing the warped point in 3x3 block */
	int cell_i = px_w < 0 ? -1 : (px_w >= NDW_TILE_W * NDW_FINE ? 1 : 0);
	int cell_j = py_w < 0 ? -1 : (py_w >= NDW_TILE_H * NDW_FINE ? 1 : 0);
	int cand_k = (cell_j + 1) * 3 + (cell_i + 1);

	/* Local position inside cell (cell_i, cell_j) relative to its center (hx, hy) */
	int ax = px_w - (cell_i * NDW_TILE_W * NDW_FINE + hx);
	int ay = py_w - (cell_j * NDW_TILE_H * NDW_FINE + hy);
	int sx = ax < 0 ? -1 : 1;
	int sy = ay < 0 ? -1 : 1;

	int winning_k = cand_k;

	/* Check if cell cand_k has an exterior convex corner in quadrant (sx, sy) */
	if (!(no_blend & NDW_OWN_BIT(cell_i, cell_j))) {
		int diff_x = ndw_differs_at(grp, no_blend, cell_i, cell_j, sx, 0);
		int diff_y = ndw_differs_at(grp, no_blend, cell_i, cell_j, 0, sy);
		int k_x = (cell_j + 1) * 3 + (cell_i + sx + 1);
		int k_y = (cell_j + sy + 1) * 3 + (cell_i + 1);

		/* A corner rounds into a surrounding biome ONLY IF both orthogonal neighbours
		 * belong to the SAME surrounding biome (grp[k_x] == grp[k_y]).
		 * If grp[k_x] != grp[k_y], it is a 3-way biome intersection (e.g. water meeting
		 * two different land biomes): no pinwheel corner distortion! */
		if (diff_x && diff_y && grp[k_x] == grp[k_y]) {
			int rx = NDW_CORNER_RX * NDW_FINE;
			int ry = NDW_CORNER_RY * NDW_FINE;
			int qx_c = (ax < 0 ? -ax : ax) - (hx - rx);
			int qy_c = (ay < 0 ? -ay : ay) - (hy - ry);
			if (qx_c > 0 && qy_c > 0) {
				int a = qx_c / 16;
				int b = (int)(((int64_t)qy_c * rx / ry) / 16);
				int dist = ndw_isqrt(a * a + b * b) * 16;
				if (dist > rx) {
					/* Point was rounded away from cell cand_k: yields cleanly to the surrounding biome */
					winning_k = k_x;
				}
			}
		}
	}

	if (winning_k == 4)
		return 0;

	/* Fold back into self if same ground or unblendable */
	int wi = (winning_k % 3) - 1;
	int wj = (winning_k / 3) - 1;
	if (grp[winning_k] == grp[4] || (no_blend & NDW_OWN_BIT(wi, wj)))
		return 0;

	w[4] = 0;
	w[winning_k] = 256;
	return 1;
}

/* Dominant owner for callers or tests wanting a hard decision. */
static inline int
ndw_ground_owner(const int16_t pos[4], int lx, int ly, unsigned no_blend, int *oi, int *oj)
{
	ndw_warp_grid_t g;
	uint16_t w[9];
	int max_k = 4, max_w, any;
	uint8_t grp[9];

	for (int k = 0; k < 9; k++)
		grp[k] = 4;
	if (lx >= NDW_TILE_W - NDW_BLEND_BAND) {
		grp[2] = 1; grp[5] = 1; grp[8] = 1;
	}
	if (lx < NDW_BLEND_BAND) {
		grp[0] = 0; grp[3] = 0; grp[6] = 0;
	}
	if (ly < NDW_BLEND_BAND) {
		grp[0] = 2; grp[1] = 2; grp[2] = 2;
	}
	if (ly >= NDW_TILE_H - NDW_BLEND_BAND) {
		grp[6] = 3; grp[7] = 3; grp[8] = 3;
	}

	ndw_warp_grid_fill(&g, pos);
	any = ndw_ground_weights(&g, lx * NDW_SUB + NDW_SUB / 2, ly * NDW_SUB + NDW_SUB / 2, grp, no_blend, w);
	max_w = w[4];
	for (int k = 0; k < 9; k++) {
		if (w[k] > max_w) {
			max_w = w[k];
			max_k = k;
		}
	}
	*oi = (max_k % 3) - 1;
	*oj = (max_k / 3) - 1;
	return any && (*oi != 0 || *oj != 0);
}

/* Blend nine ABGR pixels by weights (summing to 256); the result is opaque. */
static inline uint32_t
ndw_blend_px(const uint32_t c[9], const uint16_t w[9])
{
	uint32_t r = 0, g = 0, b = 0;

	for (int k = 0; k < 9; k++) {
		if (!w[k])
			continue;
		r += (c[k] & 0xFFu) * w[k];
		g += ((c[k] >> 8) & 0xFFu) * w[k];
		b += ((c[k] >> 16) & 0xFFu) * w[k];
	}
	return 0xFF000000u | (((b + 128) >> 8) << 16) | (((g + 128) >> 8) << 8) | ((r + 128) >> 8);
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
