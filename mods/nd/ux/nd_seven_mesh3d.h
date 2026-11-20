/*
 * nd_seven_mesh3d.h — Pure-C 3D Procedural Botanical & Geological Mesh Engine
 * + Orthographic Pixel-Art Albedo & Tangent-Space Normal Map Baker.
 *
 * Provides:
 *   1. Option A (Live 3D WebGL2 Instanced Rendering):
 *      Real 3D triangle meshes with analytical 3D vertex normals, ambient occlusion,
 *      material specular/foliage tags, and wind flexibility weights for all 64 canonical
 *      plant templates (16 species x 2 growth stages x 2 variants) and all 51 stone/mineral
 *      templates (NDS_SP_COUNT = 51 across pebbles, rocks, boulders, crystals, and ores).
 *   2. Option B (Orthographic 55-deg Diorama Pixel-Art Sprite & Normal Map Baker):
 *      Deterministic software Z-buffer rasterizer that bakes any 3D mesh template (or
 *      extended botanical species config) into clean, lossless 32-bit RGBA pixel-art
 *      albedo sprites + tangent-space normal maps (_n.png) with zero background bleed.
 */

#ifndef ND_SEVEN_MESH3D_H
#define ND_SEVEN_MESH3D_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include <nd/plant.h>

#define ND_MESH3D_NONE        0u
#define ND_MESH3D_PLANT_BASE  1u
#define ND_MESH3D_PLANT_COUNT 128u  /* 16 species * 2 stages (small/big) * 4 variants (1..4) */
#define ND_MESH3D_STONE_BASE  129u
#define ND_MESH3D_STONE_COUNT 51u   /* NDS_SP_COUNT = 51 stone/mineral specimens */
#define ND_MESH3D_TOTAL       180u  /* 0..179 inclusive */

#define ND_MESH3D_MAX_VERTS   262144u
#define ND_MESH3D_MAX_INDICES 1048576u

/* Material class encoded in nd_mesh3d_vert_t.col[3]:
 *   0.00      = Bark / woody stem
 *   0.185     = Solid succulent apex cap (ND_MAT3D_FOLIAGE_CORE)
 *   0.200..0.238 = Pectinate Conifer Needle Bough Card (v = (mat - 0.200) / 0.038 in [0,1])
 *   0.240..0.278 = Serrated Broadleaf Ovate Shoot Card (v = (mat - 0.240) / 0.038 in [0,1])
 *   0.280..0.318 = Lobed/Palmate Broadleaf Shoot Card  (v = (mat - 0.280) / 0.038 in [0,1])
 *   0.320..0.350 = Parallel-Veined Sword/Herb Blade    (v = (mat - 0.320) / 0.030 in [0,1])
 *   0.50      = Floral blossom / fruit / cone / spadix accent
 *   0.75      = Matte geological rock / boulder
 *   1.00      = Crystalline / metallic ore mineral */
#define ND_MAT3D_BARK           0.00f
#define ND_MAT3D_FOLIAGE_CORE   0.185f
#define ND_MAT3D_FOL_CONIFER    0.200f
#define ND_MAT3D_FOL_OVATE      0.240f
#define ND_MAT3D_FOL_LOBED      0.280f
#define ND_MAT3D_FOL_BLADE      0.320f
#define ND_MAT3D_FOLIAGE        0.250f
#define ND_MAT3D_FOLIAGE_FRINGE 0.320f
#define ND_MAT3D_BLOSSOM        0.50f
#define ND_MAT3D_STONE_MATTE    0.75f
#define ND_MAT3D_CRYSTAL_ORE    1.00f

/* Canonical 3D World-Unit Scale Constants:
 * Standing Lamb body height = 18.0 world units (LAMB_FOOT_Y=25 minus head top y=7) = 0.40 m (40 cm).
 * Room tile = 160 x 128 world units; normal camera distance = 400.0 wu (331 wu vertical frustum span).
 * Dimensions come directly from s_nd_plant_specs[sp_idx].genes (loaded from data/plants.dsv). */
#define ND_MESH3D_LAMB_HEIGHT_M  0.40f
#define ND_MESH3D_LAMB_BODY_WU   18.0f
#define ND_MESH3D_WU_PER_METER   45.0f

static inline uint32_t
nd_m3d_pack_rgb3(const uint8_t rgb[3])
{
	return 0xFF000000u | ((uint32_t)rgb[2] << 16) | ((uint32_t)rgb[1] << 8) | (uint32_t)rgb[0];
}

/* Data-driven world-unit dimensions read directly from s_nd_plant_specs[sp_idx].genes */
static inline void
nd_m3d_plant_metric_dims(unsigned sp_idx, unsigned is_big,
                         float *out_w_wu, float *out_h_wu, float *out_ay_wu)
{
	unsigned idx = (sp_idx < ND_PLANT_MAX) ? sp_idx : 10u;
	const nd_plant_genes_t *g = &s_nd_plant_specs[idx].genes;
	float w_wu = (float)(is_big ? g->big_w : g->small_w);
	float h_wu = (float)(is_big ? g->big_h : g->small_h);
	float ay_wu = fmaxf(1.5f, fminf(9.0f, h_wu * 0.06f));
	if (out_w_wu)  *out_w_wu  = w_wu;
	if (out_h_wu)  *out_h_wu  = h_wu;
	if (out_ay_wu) *out_ay_wu = ay_wu;
}

/* Canonical world-unit dimensions for geological tiers relative to the 18.0 wu (40 cm) Lamb:
 *   kind 9  (NDW_K_STONE):   hand specimens, crystals, pebbles (13..16 wu wide, 10..13 wu tall, < 0.75x Lamb)
 *   kind 10 (NDW_K_ROCK):    outcrop rocks & large crystal clusters (25..31 wu wide, 21..26 wu tall, ~1.3x Lamb)
 *   kind 11 (NDW_K_BOULDER): monolithic boulders (48..58 wu wide, 40..48 wu tall, ~2.4x Lamb) */
static inline void
nd_m3d_stone_metric_dims(uint8_t kind, uint8_t variant,
                         float *out_w_wu, float *out_h_wu, float *out_ay_wu)
{
	float v_mod = 0.90f + 0.065f * (float)(variant & 3u);
	float w_wu = 15.0f * v_mod;
	float h_wu = 11.5f * v_mod;
	if (kind == 11u || kind == 6u) {        /* NDW_K_BOULDER */
		w_wu = 52.0f * v_mod;
		h_wu = 44.0f * v_mod;
	} else if (kind == 10u || kind == 5u) { /* NDW_K_ROCK */
		w_wu = 28.0f * v_mod;
		h_wu = 23.0f * v_mod;
	}
	if (out_w_wu)  *out_w_wu  = w_wu;
	if (out_h_wu)  *out_h_wu  = h_wu;
	if (out_ay_wu) *out_ay_wu = fmaxf(2.0f, h_wu * 0.22f);
}

/* 48 bytes (12 floats) per 3D template vertex (matches locations 0, 1, 2 in s_vs_mesh3d) */
typedef struct {
	float pos[4];  /* xyz: local 3D pos in world units (x=East, y=South, z=Up from ground), w: wind weight [0..1] */
	float norm[4]; /* xyz: unit 3D surface normal in local frame, w: ambient occlusion [0.15..1.0] */
	float col[4];  /* rgb: continuous 24-bit physical albedo [0..1], a: ND_MAT3D_* material class */
} nd_mesh3d_vert_t;

/* 112 bytes (28 floats) per 3D mesh instance (matches locations 3..9 in s_vs_mesh3d) */
typedef struct {
	float base_scale[4]; /* xyz: torus world base_pos relative to cur_origin, w: horizontal scale_xy */
	float axis_e[4];     /* xyz: rotated East tangent basis, w: vertical scale_z */
	float axis_s[4];     /* xyz: rotated South tangent basis, w: wind sway_sx */
	float axis_n[4];     /* xyz: torus Zenith normal basis, w: wind sway_sy */
	float tint[4];       /* rgba: instance tint & distance fade alpha */
	float param[4];      /* x: world wx, y: world wy, z: ground wz, w: flags (+0 dry, +16 inWaterTile) */
	float terr[4];       /* x: terrain slope dz/dx, y: terrain slope dz/dy, z: curvature, w: shadow flags (bit0=sun, bit1=lamb) */
} nd_seven_gl_mesh3d_inst_t;

typedef struct {
	uint32_t first_vert;
	uint32_t vert_count;
	uint32_t first_idx;
	uint32_t idx_count;
	float    mesh_w_wu;
	float    mesh_h_wu;
	uint16_t sprite_w;
	uint16_t sprite_h;
	uint8_t  anchor_y;
	uint8_t  shadow_rx;
	uint8_t  shadow_ry;
	uint8_t  pad0;
} nd_mesh3d_slice_t;

/* Botanical archetype for 3D procedural plant generation (supports all 16 runtime species + 41 disk species) */
typedef enum {
	ND_BOT3D_PINE = 0,       /* Pinus sylvestris / Pinus mugo: tall bare orange-red upper trunk, tiered dome needle crowns */
	ND_BOT3D_FIR_CONIFER,    /* Pseudotsuga menziesii / Abies alba / Picea / Larix: pyramidal whorled conifer tiers */
	ND_BOT3D_BIRCH,          /* Betula pendula / pubescens / nana: white chalky bark with dark diamond lenticels, airy crown */
	ND_BOT3D_BROADLEAF_OAK,  /* Quercus robur / Fagus / Castanea / Ulmus: stout buttressed trunk, heavy spreading lobes */
	ND_BOT3D_MAPLE_SYCAMORE, /* Acer pseudoplatanus / Tilia / Fraxinus / Alnus / Populus: multi-tiered broad dome */
	ND_BOT3D_WILLOW,         /* Salix babylonica / arctica / herbacea: gnarled trunk with cascading weeping curtains */
	ND_BOT3D_ACACIA,         /* Acacia senegal / tortilis / Boswellia: forked lean trunk with flat umbrella tabletop */
	ND_BOT3D_OLIVE,          /* Olea europaea / Quercus ilex / Arbutus: twisted gnarled trunk, silvery-green lobes */
	ND_BOT3D_CACTUS,         /* Arthrocereus rondonianus / Opuntia / Aloe: ribbed fluted cactus columns + blossom */
	ND_BOT3D_HERB_FLAX,      /* Linum usitatissimum / Lavandula / Rosmarinus: slender upright stems + sky-blue/violet flowers */
	ND_BOT3D_HERB_UMBEL,     /* Daucus carota / Dryas / Rubus: feathery basal rosette + flat white floral umbels */
	ND_BOT3D_HERB_TOMATO,    /* Solanum lycopersicum / Vaccinium / Empetrum: bushy leafy vine + glossy red berries */
	ND_BOT3D_HERB_DAISY,     /* Bellis perennis / Ranunculus: basal rosette + white-petalled golden-center capitula */
	ND_BOT3D_REED_CATTAIL,   /* Typha latifolia / Carex / Eriophorum: sword blades + dark brown cylindrical spadix spikes */
	ND_BOT3D_PALM_TROPICAL,  /* Euterpe / Mauritia / Bertholletia / Hevea / Ceiba / Swietenia / Ficus / Rhizophora */
} nd_bot3d_kind_t;

typedef struct {
	nd_bot3d_kind_t kind;
	uint32_t bark_dark, bark_mid, bark_light;
	uint32_t leaf_deep, leaf_mid, leaf_lit, leaf_high;
	uint32_t accent_dark, accent_lit;
	uint16_t small_w, small_h, big_w, big_h;
	uint8_t  has_accent;
	uint8_t  sub_variant; /* e.g. 1 = Abies alba upright cones & pectinate sprays, 1 = Betula pendula weeping twigs */
} nd_bot3d_profile_t;

typedef struct {
	nd_mesh3d_vert_t *verts;
	uint32_t          vert_count;
	uint32_t          vert_cap;
	uint32_t         *indices;
	uint32_t          idx_count;
	uint32_t          idx_cap;
} nd_mesh3d_builder_t;

static inline uint32_t
nd_m3d_hash(uint32_t x)
{
	x ^= x >> 16;
	x *= 0x7FEB352Du;
	x ^= x >> 15;
	x *= 0x846CA68Bu;
	x ^= x >> 16;
	return x;
}

static inline float
nd_m3d_rand01(uint32_t seed)
{
	return (float)(nd_m3d_hash(seed) & 0xFFFFFFu) / 16777215.0f;
}

static inline float
nd_m3d_rand_signed(uint32_t seed)
{
	return nd_m3d_rand01(seed) * 2.0f - 1.0f;
}

static inline void
nd_m3d_norm3(float v[3])
{
	float l2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
	if (l2 > 1e-12f) {
		float inv = 1.0f / sqrtf(l2);
		v[0] *= inv;
		v[1] *= inv;
		v[2] *= inv;
	} else {
		v[0] = 0.0f;
		v[1] = 0.0f;
		v[2] = 1.0f;
	}
}

static inline void
nd_m3d_unpack_rgb(uint32_t c, float out_rgb[3])
{
	/* Preserve full 24-bit continuous botanical and mineralogical color gradients */
	uint32_t r = (c & 0xFFu);
	uint32_t g = ((c >> 8) & 0xFFu);
	uint32_t b = ((c >> 16) & 0xFFu);
	out_rgb[0] = (float)r / 255.0f;
	out_rgb[1] = (float)g / 255.0f;
	out_rgb[2] = (float)b / 255.0f;
}

static inline uint32_t
nd_m3d_lerp_col(uint32_t c0, uint32_t c1, float t)
{
	if (t <= 0.0f) return c0;
	if (t >= 1.0f) return c1;
	int r0 = (int)(c0 & 0xFFu), g0 = (int)((c0 >> 8) & 0xFFu), b0 = (int)((c0 >> 16) & 0xFFu);
	int r1 = (int)(c1 & 0xFFu), g1 = (int)((c1 >> 8) & 0xFFu), b1 = (int)((c1 >> 16) & 0xFFu);
	int r = r0 + (int)((float)(r1 - r0) * t + 0.5f);
	int g = g0 + (int)((float)(g1 - g0) * t + 0.5f);
	int b = b0 + (int)((float)(b1 - b0) * t + 0.5f);
	if (r < 0) r = 0; else if (r > 255) r = 255;
	if (g < 0) g = 0; else if (g > 255) g = 255;
	if (b < 0) b = 0; else if (b > 255) b = 255;
	return 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

static inline uint32_t
nd_m3d_push_vert(nd_mesh3d_builder_t *b,
                 float x, float y, float z, float wind_w,
                 float nx, float ny, float nz, float ao,
                 uint32_t col, float mat_class)
{
	if (b->vert_count >= b->vert_cap)
		return (uint32_t)(b->vert_count ? b->vert_count - 1u : 0u);
	uint32_t idx = b->vert_count++;
	nd_mesh3d_vert_t *v = &b->verts[idx];
	float n[3] = { nx, ny, nz };
	nd_m3d_norm3(n);
	v->pos[0] = x;
	v->pos[1] = y;
	v->pos[2] = z;
	/* Note: wind_w > 1.0f encodes joint floor(cardU * 250.0f) + fract(wind_w) for UV branchlet cards */
	v->pos[3] = wind_w < 0.0f ? 0.0f : wind_w;
	v->norm[0] = n[0];
	v->norm[1] = n[1];
	v->norm[2] = n[2];
	v->norm[3] = ao < 0.12f ? 0.12f : (ao > 1.0f ? 1.0f : ao);
	nd_m3d_unpack_rgb(col, v->col);
	v->col[3] = mat_class;
	return idx;
}

static inline void
nd_m3d_push_tri(nd_mesh3d_builder_t *b, uint32_t i0, uint32_t i1, uint32_t i2)
{
	if (b->idx_count + 3u > b->idx_cap)
		return;
	b->indices[b->idx_count++] = i0;
	b->indices[b->idx_count++] = i1;
	b->indices[b->idx_count++] = i2;
}

/* Emit a flat-shaded / faceted triangle with analytical geometric normal and quantized palette color */
static inline void
nd_m3d_emit_faceted_tri(nd_mesh3d_builder_t *b,
                        const float p0[3], const float p1[3], const float p2[3],
                        float wind0, float wind1, float wind2,
                        float ao0, float ao1, float ao2,
                        uint32_t col0, uint32_t col1, uint32_t col2,
                        float mat_class)
{
	float e1[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
	float e2[3] = { p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2] };
	float n[3] = {
		e1[1] * e2[2] - e1[2] * e2[1],
		e1[2] * e2[0] - e1[0] * e2[2],
		e1[0] * e2[1] - e1[1] * e2[0]
	};
	nd_m3d_norm3(n);
	uint32_t v0 = nd_m3d_push_vert(b, p0[0], p0[1], p0[2], wind0, n[0], n[1], n[2], ao0, col0, mat_class);
	uint32_t v1 = nd_m3d_push_vert(b, p1[0], p1[1], p1[2], wind1, n[0], n[1], n[2], ao1, col1, mat_class);
	uint32_t v2 = nd_m3d_push_vert(b, p2[0], p2[1], p2[2], wind2, n[0], n[1], n[2], ao2, col2, mat_class);
	nd_m3d_push_tri(b, v0, v1, v2);
}

/* 3D Tapered Bark Tube / Trunk / Branch with root buttress flutes, continuous physical bark shading,
 * twisted olive cordons, papery birch periderm with dark lenticel bands, and Scots pine salmon upper bark */
static inline void
nd_m3d_add_trunk_tube_ex(nd_mesh3d_builder_t *b,
                         float x0, float y0, float z0, float r0,
                         float x1, float y1, float z1, float r1,
                         int sides, int rings,
                         uint32_t col_dark, uint32_t col_mid, uint32_t col_lit,
                         uint8_t bark_type, float crook_mag, float total_h, uint32_t seed)
{
	if (sides < 5) sides = 5;
	if (sides > 10) sides = 10;
	if (rings < 1) rings = 1;
	if (rings > 7) rings = 7;

	float dx = x1 - x0, dy = y1 - y0, dz = z1 - z0;
	float len = sqrtf(dx * dx + dy * dy + dz * dz);
	if (len < 1e-4f) return;
	float axis[3] = { dx / len, dy / len, dz / len };
	float up_ref[3] = { 0.0f, 1.0f, 0.0f };
	if (fabsf(axis[1]) > 0.85f) {
		up_ref[0] = 1.0f; up_ref[1] = 0.0f; up_ref[2] = 0.0f;
	}
	float u_vec[3] = {
		up_ref[1] * axis[2] - up_ref[2] * axis[1],
		up_ref[2] * axis[0] - up_ref[0] * axis[2],
		up_ref[0] * axis[1] - up_ref[1] * axis[0]
	};
	nd_m3d_norm3(u_vec);
	float v_vec[3] = {
		axis[1] * u_vec[2] - axis[2] * u_vec[1],
		axis[2] * u_vec[0] - axis[0] * u_vec[2],
		axis[0] * u_vec[1] - axis[1] * u_vec[0]
	};
	nd_m3d_norm3(v_vec);

	float bow_u = crook_mag * len * nd_m3d_rand_signed(seed + 17u);
	float bow_v = crook_mag * 0.65f * len * nd_m3d_rand_signed(seed + 53u);
	float s_curve = crook_mag * 0.35f * len * nd_m3d_rand_signed(seed + 97u);

	uint32_t ring_verts[8][11];
	for (int ir = 0; ir <= rings; ir++) {
		float t = (float)ir / (float)rings;
		float arch = sinf(t * 3.14159265f);
		float s_arch = sinf(t * 6.2831853f);
		float du = bow_u * arch + s_curve * s_arch;
		float dv = bow_v * arch - s_curve * 0.5f * s_arch;
		float cx = x0 + dx * t + u_vec[0] * du + v_vec[0] * dv;
		float cy = y0 + dy * t + u_vec[1] * du + v_vec[1] * dv;
		float cz = z0 + dz * t + u_vec[2] * du + v_vec[2] * dv;
		float rad = r0 + (r1 - r0) * t;

		/* Root buttress flare near ground z=0 */
		float flare_h = fmaxf(5.0f, total_h * 0.08f);
		float flare = (cz < flare_h && z0 < 1.5f)
			? (1.0f + 0.46f * (1.0f - cz / flare_h) * (1.0f - cz / flare_h))
			: 1.0f;
		float wind_w = (total_h > 1.0f) ? fminf(0.45f, (cz / total_h) * 0.38f) : 0.1f;
		float h_norm = (total_h > 1.0f) ? fminf(1.0f, fmaxf(0.0f, cz / total_h)) : t;
		/* Deep sub-canopy shadow occlusion where trunk enters the lower crown */
		float canopy_occl = (h_norm > 0.35f) ? (0.68f - 0.18f * sinf((h_norm - 0.35f) * 4.5f)) : (0.55f + 0.35f * (h_norm / 0.35f));

		for (int is = 0; is < sides; is++) {
			float ang = ((float)is / (float)sides) * 6.2831853f;
			/* Helical twist for ancient Mediterranean olive / gnarled oak bark */
			float twist_ang = (bark_type == ND_PLANT_BARK_FLUTED) ? (ang + t * 1.45f) : ang;
			float ca = cosf(ang), sa = sinf(ang);
			float rib = 1.0f;
			if (bark_type == ND_PLANT_BARK_RIBBED) {
				/* Fluted cactus ribs */
				rib = 1.0f + 0.16f * cosf(ang * (float)sides);
			} else if (bark_type == ND_PLANT_BARK_FLUTED) {
				/* Twisted corded olive trunk flutes */
				rib = 1.0f + 0.18f * cosf(twist_ang * 3.0f + (float)(seed & 7u));
			} else if (flare > 1.02f) {
				rib = 1.0f + 0.24f * cosf(ang * 4.0f + (float)(seed & 7u)) * (flare - 1.0f);
			} else {
				rib = 1.0f + 0.05f * cosf(ang * 3.0f + t * 3.14f);
			}
			float rr = rad * flare * rib;
			float nx = u_vec[0] * ca + v_vec[0] * sa;
			float ny = u_vec[1] * ca + v_vec[1] * sa;
			float nz = u_vec[2] * ca + v_vec[2] * sa;
			float vx = cx + nx * rr;
			float vy = cy + ny * rr;
			float vz = cz + nz * rr;

			/* Rotation-invariant cylindrical bark shading (no baked compass sun that spins with yaw) */
			float sky_bias = 0.50f + 0.50f * nz;
			uint32_t h = nd_m3d_hash(seed + (uint32_t)(ir * 37 + is * 17));
			float grain = (((float)(h & 0xFFu) / 255.0f) - 0.5f) * 0.22f;
			float shade_t = fminf(1.0f, fmaxf(0.0f, 0.48f + 0.24f * sky_bias + 0.14f * h_norm + grain));
			uint32_t col = (shade_t < 0.42f)
				? nd_m3d_lerp_col(col_dark, col_mid, 0.35f + 0.65f * (shade_t / 0.42f))
				: nd_m3d_lerp_col(col_mid, col_lit, (shade_t - 0.42f) / 0.58f);

			if (bark_type == ND_PLANT_BARK_BIRCH) {
				/* Dark fissured basal chevron bark + horizontal lenticel bands on chalk-white periderm */
				if (h_norm < 0.14f || (h & 7u) == 0u) {
					col = nd_m3d_lerp_col(col_dark, col_mid, 0.32f);
				} else {
					col = nd_m3d_lerp_col(col_mid, col_lit, fminf(1.0f, shade_t * 1.15f));
				}
			} else if (bark_type == ND_PLANT_BARK_SCOTS) {
				/* Scots pine: dark furrowed lower plates transitioning to flaky salmon-ochre upper trunk */
				if (h_norm > 0.36f) {
					float up_t = fminf(1.0f, (h_norm - 0.36f) / 0.35f);
					uint32_t upper_c = nd_m3d_lerp_col(col_mid, col_lit, 0.35f + 0.65f * shade_t);
					col = nd_m3d_lerp_col(col, upper_c, up_t * 0.85f);
				} else {
					col = nd_m3d_lerp_col(col_dark, col_mid, 0.30f + shade_t * 0.70f);
				}
			}
			ring_verts[ir][is] = nd_m3d_push_vert(b, vx, vy, vz, wind_w, nx, ny, nz, canopy_occl, col, ND_MAT3D_BARK);
		}
	}
	for (int ir = 0; ir < rings; ir++) {
		for (int is = 0; is < sides; is++) {
			int next_s = (is + 1) % sides;
			uint32_t a = ring_verts[ir][is];
			uint32_t b0 = ring_verts[ir][next_s];
			uint32_t c = ring_verts[ir + 1][next_s];
			uint32_t d = ring_verts[ir + 1][is];
			nd_m3d_push_tri(b, a, b0, c);
			nd_m3d_push_tri(b, a, c, d);
		}
	}
}

/* Continuous Catmull-Rom Parallel-Transported Multi-Joint Bark Axis Tube with Welded Branch-Collar Socket:
 * Subdivides the axis polyline with smooth Catmull-Rom curvature across all branch orders, then sweeps
 * the entire limb as a single seamless, twist-free 3D tubular mesh using a Rotation-Minimizing
 * (Bishop parallel-transport) frame (u_vec, v_vec). Every joint shares a single welded vertex ring
 * (zero open polygon gaps or angular zigzag elbows), and child branches (is_lateral != 0) flare into
 * a swollen branch-collar socket embedded cleanly inside the parent limb's bark cylinder. */
static inline void
nd_m3d_add_axis_spline_tube(nd_mesh3d_builder_t *b,
                            const float (*in_pts)[3],
                            const float *in_radii,
                            int in_n_pts,
                            int sides,
                            int is_lateral,
                            uint32_t col_dark, uint32_t col_mid, uint32_t col_lit,
                            uint8_t bark_type, float total_h, uint32_t seed)
{
	if (!in_pts || !in_radii || in_n_pts < 2) return;
	if (sides < 4) sides = 4;
	if (sides > 10) sides = 10;
	if (in_n_pts > 24) in_n_pts = 24;

	/* Catmull-Rom midpoint refinement when there are >= 3 control points so limbs bend smoothly
	 * instead of forming sharp angular polylines at internode junctions. */
	float pts[48][3];
	float radii[48];
	int n_pts = 0;
	if (in_n_pts >= 3 && in_n_pts <= 20) {
		for (int i = 0; i < in_n_pts - 1 && n_pts < 46; i++) {
			const float *p0 = in_pts[(i > 0) ? (i - 1) : 0];
			const float *p1 = in_pts[i];
			const float *p2 = in_pts[i + 1];
			const float *p3 = in_pts[(i + 2 < in_n_pts) ? (i + 2) : (in_n_pts - 1)];
			float r1 = in_radii[i], r2 = in_radii[i + 1];
			pts[n_pts][0] = p1[0]; pts[n_pts][1] = p1[1]; pts[n_pts][2] = p1[2];
			radii[n_pts] = r1;
			n_pts++;
			/* Insert smooth Catmull-Rom midpoint (t = 0.5) on long trunk / primary bough segments */
			float sdx = p2[0] - p1[0], sdy = p2[1] - p1[1], sdz = p2[2] - p1[2];
			float slen = sqrtf(sdx * sdx + sdy * sdy + sdz * sdz);
			int is_collar_seg = (is_lateral && i == 0);
			int is_cap_seg = (i == in_n_pts - 2 && r2 < 0.15f);
			if ((!is_lateral || sides >= 6) && !is_collar_seg && !is_cap_seg &&
			    slen > fmaxf(r1 * 2.5f, 8.0f) && n_pts < 46) {
				/* Catmull-Rom at t=0.5: 0.5 * (-0.125*p0 + 1.125*p1 + 1.125*p2 - 0.125*p3) */
				pts[n_pts][0] = 0.5625f * (p1[0] + p2[0]) - 0.0625f * (p0[0] + p3[0]);
				pts[n_pts][1] = 0.5625f * (p1[1] + p2[1]) - 0.0625f * (p0[1] + p3[1]);
				pts[n_pts][2] = 0.5625f * (p1[2] + p2[2]) - 0.0625f * (p0[2] + p3[2]);
				radii[n_pts]  = 0.5f * (r1 + r2);
				n_pts++;
			}
		}
		pts[n_pts][0] = in_pts[in_n_pts - 1][0];
		pts[n_pts][1] = in_pts[in_n_pts - 1][1];
		pts[n_pts][2] = in_pts[in_n_pts - 1][2];
		radii[n_pts]  = in_radii[in_n_pts - 1];
		n_pts++;
	} else {
		n_pts = in_n_pts;
		for (int i = 0; i < n_pts; i++) {
			pts[i][0] = in_pts[i][0]; pts[i][1] = in_pts[i][1]; pts[i][2] = in_pts[i][2];
			radii[i] = in_radii[i];
		}
	}

	float tang[48][3];
	for (int i = 0; i < n_pts; i++) {
		int i0 = (i > 0) ? (i - 1) : 0;
		int i1 = (i + 1 < n_pts) ? (i + 1) : (n_pts - 1);
		float tx = pts[i1][0] - pts[i0][0];
		float ty = pts[i1][1] - pts[i0][1];
		float tz = pts[i1][2] - pts[i0][2];
		float len = sqrtf(tx * tx + ty * ty + tz * tz);
		if (len < 1e-4f) {
			tang[i][0] = 0.0f; tang[i][1] = 0.0f; tang[i][2] = 1.0f;
		} else {
			tang[i][0] = tx / len; tang[i][1] = ty / len; tang[i][2] = tz / len;
		}
	}

	/* Initial cross-section frame at base node i = 0 */
	float t_prev[3] = { tang[0][0], tang[0][1], tang[0][2] };
	float up_ref[3] = { 0.0f, 1.0f, 0.0f };
	if (fabsf(t_prev[1]) > 0.85f) {
		up_ref[0] = 1.0f; up_ref[1] = 0.0f; up_ref[2] = 0.0f;
	}
	float u_vec[3] = {
		up_ref[1] * t_prev[2] - up_ref[2] * t_prev[1],
		up_ref[2] * t_prev[0] - up_ref[0] * t_prev[2],
		up_ref[0] * t_prev[1] - up_ref[1] * t_prev[0]
	};
	nd_m3d_norm3(u_vec);
	float v_vec[3] = {
		t_prev[1] * u_vec[2] - t_prev[2] * u_vec[1],
		t_prev[2] * u_vec[0] - t_prev[0] * u_vec[2],
		t_prev[0] * u_vec[1] - t_prev[1] * u_vec[0]
	};
	nd_m3d_norm3(v_vec);

	uint32_t ring_verts[48][11];
	for (int i = 0; i < n_pts; i++) {
		if (i > 0) {
			/* Parallel-transport (u_vec, v_vec) from t_prev to tang[i] without axial torsion */
			float *t_cur = tang[i];
			float dot_u = u_vec[0] * t_cur[0] + u_vec[1] * t_cur[1] + u_vec[2] * t_cur[2];
			u_vec[0] -= dot_u * t_cur[0];
			u_vec[1] -= dot_u * t_cur[1];
			u_vec[2] -= dot_u * t_cur[2];
			nd_m3d_norm3(u_vec);
			v_vec[0] = t_cur[1] * u_vec[2] - t_cur[2] * u_vec[1];
			v_vec[1] = t_cur[2] * u_vec[0] - t_cur[0] * u_vec[2];
			v_vec[2] = t_cur[0] * u_vec[1] - t_cur[1] * u_vec[0];
			nd_m3d_norm3(v_vec);
			t_prev[0] = t_cur[0]; t_prev[1] = t_cur[1]; t_prev[2] = t_cur[2];
		}

		float t = (float)i / (float)(n_pts - 1);
		float cx = pts[i][0], cy = pts[i][1], cz = pts[i][2];
		float rad = radii[i];

		/* Subtle root-collar swell near ground (for trunk) or branch-collar ridge at lateral fork socket (i == 0) */
		float flare_h = fmaxf(4.0f, total_h * 0.065f);
		float flare = 1.0f;
		if (!is_lateral && cz < flare_h && pts[0][2] < 1.5f) {
			float f0 = 1.0f - cz / flare_h;
			flare = 1.0f + 0.22f * f0 * f0;
		} else if (is_lateral && i == 0) {
			/* Swollen woody branch-collar socket where lateral limb welds into parent bark */
			flare = 1.18f;
		} else if (is_lateral && i == 1) {
			flare = 1.04f;
		}

		float wind_w = (total_h > 1.0f) ? fminf(0.45f, (cz / total_h) * 0.38f) : 0.1f;
		float h_norm = (total_h > 1.0f) ? fminf(1.0f, fmaxf(0.0f, cz / total_h)) : t;
		float canopy_occl = (h_norm > 0.35f)
			? (0.76f - 0.10f * sinf((h_norm - 0.35f) * 4.5f))
			: (0.68f + 0.26f * (h_norm / 0.35f));

		for (int is = 0; is < sides; is++) {
			float ang = ((float)is / (float)sides) * 6.2831853f;
			float twist_ang = (bark_type == ND_PLANT_BARK_FLUTED) ? (ang + t * 1.45f) : ang;
			float ca = cosf(ang), sa = sinf(ang);
			float rib = 1.0f;
			float rib_wave = 0.0f, rib_deriv = 0.0f;
			if (bark_type == ND_PLANT_BARK_RIBBED) {
				/* Deep 8-ribbed cereoid cactus fluting with 3D normal perturbation across rib crests & valleys */
				rib_wave  = cosf(ang * 8.0f);
				rib_deriv = -sinf(ang * 8.0f);
				rib = 1.0f + 0.20f * rib_wave;
			} else if (bark_type == ND_PLANT_BARK_FLUTED) {
				rib = 1.0f + 0.16f * cosf(twist_ang * 3.0f + (float)(seed & 7u));
			} else if (!is_lateral && flare > 1.02f) {
				rib = 1.0f + 0.14f * cosf(ang * 4.0f + (float)(seed & 7u)) * (flare - 1.0f);
			} else {
				rib = 1.0f + 0.04f * cosf(ang * 3.0f + t * 3.14f);
			}
			float rr = rad * flare * rib;
			float tan_x = -u_vec[0] * sa + v_vec[0] * ca;
			float tan_y = -u_vec[1] * sa + v_vec[1] * ca;
			float tan_z = -u_vec[2] * sa + v_vec[2] * ca;
			float nx = u_vec[0] * ca + v_vec[0] * sa - tan_x * (0.42f * rib_deriv);
			float ny = u_vec[1] * ca + v_vec[1] * sa - tan_y * (0.42f * rib_deriv);
			float nz = u_vec[2] * ca + v_vec[2] * sa - tan_z * (0.42f * rib_deriv);
			float nlen = sqrtf(nx * nx + ny * ny + nz * nz);
			if (nlen > 1e-4f) { nx /= nlen; ny /= nlen; nz /= nlen; }
			float vx = cx + (u_vec[0] * ca + v_vec[0] * sa) * rr;
			float vy = cy + (u_vec[1] * ca + v_vec[1] * sa) * rr;
			float vz = cz + (u_vec[2] * ca + v_vec[2] * sa) * rr;
			/* Clamp basal trunk collar ring flush to the ground plane so tilted trunks never poke underground */
			if (!is_lateral && i == 0 && pts[0][2] < 1.5f) {
				vz = 0.0f;
			} else if (vz < 0.0f) {
				vz = 0.0f;
			}

			/* Rotation-invariant bark albedo variation (live solar directional shading is computed in s_fs_mesh3d) */
			float sky_bias = 0.50f + 0.50f * nz;
			uint32_t h = nd_m3d_hash(seed + (uint32_t)(i * 37 + is * 17));
			float grain = (((float)(h & 0xFFu) / 255.0f) - 0.5f) * 0.22f;
			float shade_t = fminf(1.0f, fmaxf(0.0f, 0.48f + 0.24f * sky_bias + 0.14f * h_norm + grain));
			uint32_t col = (shade_t < 0.42f)
				? nd_m3d_lerp_col(col_dark, col_mid, 0.35f + 0.65f * (shade_t / 0.42f))
				: nd_m3d_lerp_col(col_mid, col_lit, (shade_t - 0.42f) / 0.58f);

			if (bark_type == ND_PLANT_BARK_RIBBED) {
				/* Cactus rib valley vs sunlit glaucous rib crest with pale spine areoles */
				col = (rib_wave > 0.25f)
					? nd_m3d_lerp_col(col_mid, col_lit, 0.45f + 0.55f * rib_wave)
					: nd_m3d_lerp_col(col_dark, col_mid, 0.50f + 0.50f * (rib_wave + 1.0f));
			} else if (bark_type == ND_PLANT_BARK_BIRCH) {
				if ((!is_lateral && i == 0 && pts[0][2] < 1.5f) || (is_lateral && i == 0) || ((h & 15u) == 0u)) {
					col = nd_m3d_lerp_col(col_dark, col_mid, 0.38f);
				} else {
					col = nd_m3d_lerp_col(col_mid, col_lit, fminf(1.0f, 0.45f + shade_t * 0.65f));
				}
			} else if (bark_type == ND_PLANT_BARK_SCOTS) {
				if (h_norm > 0.36f) {
					float up_t = fminf(1.0f, (h_norm - 0.36f) / 0.35f);
					uint32_t upper_c = nd_m3d_lerp_col(col_mid, col_lit, 0.35f + 0.65f * shade_t);
					col = nd_m3d_lerp_col(col, upper_c, up_t * 0.85f);
				} else {
					col = nd_m3d_lerp_col(col_dark, col_mid, 0.30f + shade_t * 0.70f);
				}
			}
			ring_verts[i][is] = nd_m3d_push_vert(b, vx, vy, vz, wind_w, nx, ny, nz, canopy_occl, col, ND_MAT3D_BARK);
		}
	}
	for (int i = 0; i < n_pts - 1; i++) {
		for (int is = 0; is < sides; is++) {
			int next_s = (is + 1) % sides;
			uint32_t a  = ring_verts[i][is];
			uint32_t b0 = ring_verts[i][next_s];
			uint32_t c  = ring_verts[i + 1][next_s];
			uint32_t d  = ring_verts[i + 1][is];
			nd_m3d_push_tri(b, a, b0, c);
			nd_m3d_push_tri(b, a, c, d);
		}
	}
}

static inline void
nd_m3d_add_trunk_tube(nd_mesh3d_builder_t *b,
                      float x0, float y0, float z0, float r0,
                      float x1, float y1, float z1, float r1,
                      int sides, int rings,
                      uint32_t col_dark, uint32_t col_mid, uint32_t col_lit,
                      int is_birch, int is_pine, float total_h, uint32_t seed)
{
	uint8_t bt = is_birch ? ND_PLANT_BARK_BIRCH : (is_pine ? ND_PLANT_BARK_SCOTS : ND_PLANT_BARK_FISSURED);
	nd_m3d_add_trunk_tube_ex(b, x0, y0, z0, r0, x1, y1, z1, r1, sides, rings,
	                         col_dark, col_mid, col_lit, bt, 0.11f, total_h, seed);
}

/* 3D Sculpted Foliage Cluster / Leaf Crown Lobe:
 * Builds a multi-ring polyhedral dome with deeply scalloped leaf-bunch relief,
 * canopy-centroid spherical normal blending, internal self-occlusion,
 * and porous fringe tagging (ND_MAT3D_FOLIAGE_CORE vs ND_MAT3D_FOLIAGE_FRINGE)
 * so s_fs_mesh3d can alpha-cutout realistic individual leaf tips around the silhouette. */
static inline void
nd_m3d_add_foliage_lobe(nd_mesh3d_builder_t *b,
                        float cx, float cy, float cz,
                        float rx, float ry, float rz,
                        int lon_segs, int lat_rings,
                        uint32_t col_deep, uint32_t col_mid, uint32_t col_lit, uint32_t col_high,
                        float total_h, uint32_t seed, float mat_class)
{
	if (lon_segs < 5) lon_segs = 5;
	if (lon_segs > 12) lon_segs = 12;
	if (lat_rings < 2) lat_rings = 2;
	if (lat_rings > 6) lat_rings = 6;

	float crown_z_ref = total_h * 0.56f;
	uint32_t grid[7][13];
	for (int ilat = 0; ilat <= lat_rings; ilat++) {
		/* theta from -0.42*PI (tucked shaded underside) to +0.50*PI (crown apex) */
		float v = (float)ilat / (float)lat_rings;
		float theta = (-0.42f + 0.92f * v) * 3.14159265f;
		float ct = cosf(theta), st = sinf(theta);

		for (int ilon = 0; ilon < lon_segs; ilon++) {
			float u = (float)ilon / (float)lon_segs;
			float phi = u * 6.2831853f + (float)(ilat & 1) * (3.14159265f / (float)lon_segs);
			float cp = cosf(phi), sp = sinf(phi);

			/* Deep multi-harmonic leaf-bunch billowing & cleft relief */
			uint32_t h = nd_m3d_hash(seed + (uint32_t)(ilat * 41 + ilon * 19));
			float harmonic = 0.14f * cosf(phi * 3.0f + (float)(seed & 7u)) +
			                 0.11f * sinf(phi * 5.0f + theta * 2.5f) +
			                 0.07f * cosf(phi * 7.0f - theta * 3.0f);
			float puff = 0.82f + 0.30f * ((float)(h & 0xFFu) / 255.0f) + harmonic;
			/* Scalloped lower & equatorial foliage fringe */
			if (ilat <= 1)
				puff *= (ilon & 1) ? 1.15f : 0.84f;

			float nx = ct * cp;
			float ny = ct * sp;
			float nz = st;

			float px = cx + nx * rx * puff;
			float py = cy + ny * ry * puff;
			float pz = cz + (st >= 0.0f ? st * rz : st * rz * 0.48f) * puff;

			/* Canopy-centroid spherical normal blending */
			float gdx = px, gdy = py, gdz = (pz - crown_z_ref) * 1.15f;
			float glen = sqrtf(gdx * gdx + gdy * gdy + gdz * gdz);
			if (glen > 1e-4f) { gdx /= glen; gdy /= glen; gdz /= glen; }
			float jx = (((float)((h >> 8) & 0xFFu) / 255.0f) - 0.5f) * 0.22f;
			float jy = (((float)((h >> 16) & 0xFFu) / 255.0f) - 0.5f) * 0.22f;
			float fnx = nx * 0.58f + gdx * 0.42f + jx;
			float fny = ny * 0.58f + gdy * 0.42f + jy;
			float fnz = nz * 0.58f + gdz * 0.42f + 0.22f;

			float h_frac = fminf(1.0f, fmaxf(0.0f, pz / fmaxf(1.0f, total_h)));
			float r_horiz = sqrtf(px * px + py * py);
			float r_ref = fmaxf(8.0f, total_h * 0.36f);
			float radial_exposure = fminf(1.0f, r_horiz / r_ref);
			/* Rotation-invariant chlorophyll exposure gradient (live sun direction is evaluated in s_fs_mesh3d) */
			float canopy_metric = fnz * 0.62f + radial_exposure * 0.28f + h_frac * 0.20f - 0.14f;
			uint32_t col;
			if (st < -0.14f || canopy_metric < -0.10f) {
				col = col_deep;
			} else if (canopy_metric < 0.22f) {
				float lt = (canopy_metric + 0.10f) / 0.32f;
				col = nd_m3d_lerp_col(col_deep, col_mid, lt);
			} else if (canopy_metric < 0.56f) {
				float lt = (canopy_metric - 0.22f) / 0.34f;
				col = nd_m3d_lerp_col(col_mid, col_lit, lt);
			} else {
				float lt = fminf(1.0f, (canopy_metric - 0.56f) / 0.36f);
				col = nd_m3d_lerp_col(col_lit, col_high, lt);
			}

			/* Da Vinci sub-canopy interior occlusion: deep inner/lower bough cavities are shaded, outer shell is bright */
			float ao = (0.34f + 0.30f * v + 0.18f * h_frac + 0.18f * radial_exposure);
			if (st < 0.0f) ao *= 0.72f;
			float wind_w = 0.45f + 0.55f * h_frac;

			/* Tag outer/equatorial skirt rings as porous leaf-tip fringe so s_fs_mesh3d carves leaf-cluster silhouettes */
			float v_mat = mat_class;
			if (mat_class >= 0.18f && mat_class <= 0.35f) {
				if (ilat == 0 || puff > 1.03f || (ilat == 1 && ((ilon + ilat) & 1)))
					v_mat = ND_MAT3D_FOLIAGE_FRINGE;
				else if (ilat < lat_rings)
					v_mat = ND_MAT3D_FOLIAGE;
			}
			grid[ilat][ilon] = nd_m3d_push_vert(b, px, py, pz, wind_w, fnx, fny, fnz, ao, col, v_mat);
		}
	}

	for (int ilat = 0; ilat < lat_rings; ilat++) {
		for (int ilon = 0; ilon < lon_segs; ilon++) {
			int next_lon = (ilon + 1) % lon_segs;
			uint32_t v00 = grid[ilat][ilon];
			uint32_t v10 = grid[ilat][next_lon];
			uint32_t v11 = grid[ilat + 1][next_lon];
			uint32_t v01 = grid[ilat + 1][ilon];
			nd_m3d_push_tri(b, v00, v10, v11);
			nd_m3d_push_tri(b, v00, v11, v01);
		}
	}
}

/* Pack longitudinal branchlet coordinate u in [0, 1] and wind flexibility w in [0.01, 0.99] into pos[3] */
static inline float
nd_m3d_pack_card_u_wind(float u, float wind_w)
{
	float uc = fminf(1.0f, fmaxf(0.0f, u));
	float wc = fminf(0.98f, fmaxf(0.02f, wind_w));
	float u_idx = floorf(uc * 249.0f + 1.0f); /* 1..250 so pos[3] > 1.0f always flags a UV card */
	return u_idx + wc;
}

/* Pack botanical foliage archetype base (ND_MAT3D_FOL_*) and lateral card coordinate v in [0, 1] into col[3] */
static inline float
nd_m3d_pack_card_mat_v(float fol_base, float v)
{
	float vc = fminf(1.0f, fmaxf(0.0f, v));
	float span = (fol_base >= 0.319f) ? 0.030f : 0.038f;
	return fol_base + vc * span;
}

/* SpeedTree / Creation Engine (Skyrim)–Style 3D Branchlet Sheet with UV Encoding & Outward Dome-Transferred Normals:
 * Emits a 3-segment x 2-wing doubly-curved 3D botanical branchlet card (9 vertices, 8 triangles) with:
 *   - Explicit (u, v) card coordinates packed in pos[3] and col[3] for per-pixel needle/leaf cutouts,
 *   - Axial roll (roll_rad), gravitropic bow, and dihedral V-cupping,
 *   - Outward dome-transferred normals blended (72% spherical crown dome normal from (dome_cx, dome_cy, dome_cz),
 *     28% cambered sheet normal) so crossed branchlet sheets shade as a cohesive, lush 3D volume,
 *   - Unbaked physical chlorophyll gradient (no hardcoded sun vector baked into vertex RGB). */
static inline void
nd_m3d_add_branchlet_card(nd_mesh3d_builder_t *b,
                          float x0, float y0, float z0,
                          float dir_x, float dir_y, float dir_z,
                          float length, float height_arc, float drop_end,
                          float base_half_w, float cup_dz, float roll_rad,
                          float dome_cx, float dome_cy, float dome_cz,
                          float fol_base,
                          uint32_t col_deep, uint32_t col_mid, uint32_t col_lit, uint32_t col_high,
                          float total_h)
{
	float fwd[3] = { dir_x, dir_y, dir_z };
	nd_m3d_norm3(fwd);
	float up_ref[3] = { 0.0f, 0.0f, 1.0f };
	if (fabsf(fwd[2]) > 0.92f) { up_ref[0] = 1.0f; up_ref[2] = 0.0f; }
	float side0[3] = {
		fwd[1] * up_ref[2] - fwd[2] * up_ref[1],
		fwd[2] * up_ref[0] - fwd[0] * up_ref[2],
		fwd[0] * up_ref[1] - fwd[1] * up_ref[0]
	};
	nd_m3d_norm3(side0);
	float up0[3] = {
		side0[1] * fwd[2] - side0[2] * fwd[1],
		side0[2] * fwd[0] - side0[0] * fwd[2],
		side0[0] * fwd[1] - side0[1] * fwd[0]
	};
	nd_m3d_norm3(up0);

	/* Apply axial roll around the branchlet growth axis */
	float cr = cosf(roll_rad), sr = sinf(roll_rad);
	float side[3] = {
		side0[0] * cr + up0[0] * sr,
		side0[1] * cr + up0[1] * sr,
		side0[2] * cr + up0[2] * sr
	};
	float card_up[3] = {
		-side0[0] * sr + up0[0] * cr,
		-side0[1] * sr + up0[1] * cr,
		-side0[2] * sr + up0[2] * cr
	};

	float mat_left  = nd_m3d_pack_card_mat_v(fol_base, 0.02f);
	float mat_mid   = nd_m3d_pack_card_mat_v(fol_base, 0.50f);
	float mat_right = nd_m3d_pack_card_mat_v(fol_base, 0.98f);

	uint32_t v_left[3], v_mid[3], v_right[3];
	for (int s = 0; s <= 2; s++) {
		float u = (float)s * 0.5f;
		float width_prof = (s == 0) ? 0.44f : ((s == 1) ? 1.08f : 0.34f);
		float hw = base_half_w * width_prof;
		float reach = length * u;
		float arc_z = height_arc * sinf(u * 2.15f) - drop_end * u * u;
		float cx = x0 + fwd[0] * reach;
		float cy = y0 + fwd[1] * reach;
		float cz = fmaxf(0.4f, z0 + fwd[2] * reach + arc_z);

		float wx_l = cx - side[0] * hw - card_up[0] * (cup_dz * width_prof);
		float wy_l = cy - side[1] * hw - card_up[1] * (cup_dz * width_prof);
		float wz_l = fmaxf(0.4f, cz - side[2] * hw - card_up[2] * (cup_dz * width_prof));

		float wx_r = cx + side[0] * hw - card_up[0] * (cup_dz * width_prof);
		float wy_r = cy + side[1] * hw - card_up[1] * (cup_dz * width_prof);
		float wz_r = fmaxf(0.4f, cz + side[2] * hw - card_up[2] * (cup_dz * width_prof));

		/* Pure physical chlorophyll gradient from interior attachment base to fresh young shoot tip */
		uint32_t c_base_mid = nd_m3d_lerp_col(col_deep, col_mid, 0.58f);
		uint32_t c_sp = (s == 0) ? c_base_mid : ((s == 1) ? col_mid : col_lit);
		uint32_t c_wg = (s == 0) ? col_mid    : ((s == 1) ? col_lit : col_high);
		float h_frac = fminf(1.0f, fmaxf(0.0f, cz / fmaxf(1.0f, total_h)));
		float r_dist = sqrtf(cx * cx + cy * cy);
		float r_frac = fminf(1.0f, r_dist / fmaxf(4.0f, total_h * 0.28f));
		float crown_exp = fminf(1.0f, 0.28f + 0.40f * h_frac + 0.32f * r_frac);
		float ao = fminf(1.0f, (0.36f + 0.46f * crown_exp) * (0.62f + 0.38f * u) + 0.18f * u * crown_exp);
		float raw_ww = fminf(0.98f, fmaxf(0.14f, h_frac) * (0.40f + 0.60f * u));
		float pw_mid  = nd_m3d_pack_card_u_wind(u, raw_ww * 0.90f);
		float pw_wing = nd_m3d_pack_card_u_wind(u, raw_ww);

		/* Outward dome-transferred normal blended with local cambered sheet normal */
		float dm[3] = { cx - dome_cx, cy - dome_cy, (cz - dome_cz) * 1.15f + 2.0f };
		nd_m3d_norm3(dm);
		float nm[3] = {
			dm[0] * 0.72f + card_up[0] * 0.28f,
			dm[1] * 0.72f + card_up[1] * 0.28f,
			dm[2] * 0.72f + card_up[2] * 0.28f
		};
		float nl[3] = {
			nm[0] - side[0] * 0.28f,
			nm[1] - side[1] * 0.28f,
			nm[2] - side[2] * 0.28f
		};
		float nr[3] = {
			nm[0] + side[0] * 0.28f,
			nm[1] + side[1] * 0.28f,
			nm[2] + side[2] * 0.28f
		};

		v_left[s]  = nd_m3d_push_vert(b, wx_l, wy_l, wz_l, pw_wing,
		                              nl[0], nl[1], nl[2], ao * 0.94f,
		                              c_wg, mat_left);
		v_mid[s]   = nd_m3d_push_vert(b, cx, cy, cz, pw_mid,
		                              nm[0], nm[1], nm[2], ao,
		                              c_sp, mat_mid);
		v_right[s] = nd_m3d_push_vert(b, wx_r, wy_r, wz_r, pw_wing,
		                              nr[0], nr[1], nr[2], ao * 0.94f,
		                              c_wg, mat_right);
	}
	for (int s = 0; s < 2; s++) {
		nd_m3d_push_tri(b, v_left[s],  v_mid[s],   v_mid[s + 1]);
		nd_m3d_push_tri(b, v_left[s],  v_mid[s + 1], v_left[s + 1]);
		nd_m3d_push_tri(b, v_mid[s],   v_right[s], v_right[s + 1]);
		nd_m3d_push_tri(b, v_mid[s],   v_right[s + 1], v_mid[s + 1]);
	}
}

/* 3D Cupped Leaf-Frond Spray / Plumose Needle Bough (wrapper around nd_m3d_add_branchlet_card) */
static inline void
nd_m3d_add_leaf_frond_spray(nd_mesh3d_builder_t *b,
                            float x0, float y0, float z0,
                            float dir_x, float dir_y,
                            float length, float height_arc, float drop_end,
                            float base_half_w, float cup_dz,
                            uint32_t col_deep, uint32_t col_mid, uint32_t col_lit, uint32_t col_high,
                            float total_h)
{
	nd_m3d_add_branchlet_card(b, x0, y0, z0,
	                          dir_x, dir_y, 0.08f,
	                          length, height_arc, drop_end,
	                          base_half_w, cup_dz, 0.0f,
	                          x0 - dir_x * length * 0.25f, y0 - dir_y * length * 0.25f, z0 - drop_end * 0.4f,
	                          ND_MAT3D_FOL_OVATE,
	                          col_deep, col_mid, col_lit, col_high, total_h);
}

/* 3D Scalloped Conifer Whorl Tier (Skyrim Falkreath / Riverwood / Haafingar Pine, Douglas Fir, Silver Fir):
 * Builds discrete radiating whorled needle-comb boughs with crossed pitched secondary lateral fanlets
 * and zero spherical blobs, leaving real inter-bough sky gaps around the trunk. */
static inline void
nd_m3d_add_conifer_tier(nd_mesh3d_builder_t *b,
                        float cx, float cy, float z_base, float z_apex,
                        float r_skirt, int points,
                        uint32_t col_deep, uint32_t col_mid, uint32_t col_lit, uint32_t col_high,
                        float total_h, uint32_t seed)
{
	if (points < 4) points = 4;
	if (points > 7) points = 7;

	float dz = z_apex - z_base;
	float dome_cz = z_base + dz * 0.35f;
	for (int i = 0; i < points; i++) {
		uint32_t h = nd_m3d_hash(seed + (uint32_t)(i * 73));
		float ang_j = (((float)(h & 0xFFu) / 255.0f) - 0.5f) * (2.1f / (float)points);
		float ang = ((float)i / (float)points) * 6.2831853f + ang_j;
		float ca = cosf(ang), sa = sinf(ang);
		float len_j = 0.86f + 0.28f * ((float)((h >> 8) & 0xFFu) / 255.0f);
		float b_len = r_skirt * len_j;
		float z_start = z_base + dz * (0.42f + 0.18f * ((float)((h >> 16) & 0x3Fu) / 63.0f));
		float drop = dz * (0.38f + 0.16f * (float)(i & 1));
		float roll_main = ((i & 1) ? 0.26f : -0.26f);

		/* Primary sweeping conifer needle-comb bough sheet */
		nd_m3d_add_branchlet_card(b,
		                          cx + ca * r_skirt * 0.05f, cy + sa * r_skirt * 0.05f, z_start,
		                          ca, sa, -0.10f,
		                          b_len, dz * 0.14f, drop,
		                          b_len * 0.48f, dz * 0.15f, roll_main,
		                          cx, cy, dome_cz,
		                          ND_MAT3D_FOL_CONIFER,
		                          col_deep, col_mid, col_lit, col_high, total_h);

		/* Crossed pitched secondary pectinate lateral branchlet sheets on either side of the main bough */
		float sang_l = ang - 0.52f;
		float sang_r = ang + 0.52f;
		float mx = cx + ca * (b_len * 0.34f);
		float my = cy + sa * (b_len * 0.34f);
		float mz = z_start - drop * 0.22f;
		nd_m3d_add_branchlet_card(b, mx, my, mz,
		                          cosf(sang_l), sinf(sang_l), -0.14f,
		                          b_len * 0.66f, dz * 0.10f, drop * 0.74f,
		                          b_len * 0.34f, dz * 0.10f, -0.58f,
		                          cx, cy, dome_cz,
		                          ND_MAT3D_FOL_CONIFER,
		                          col_deep, col_mid, col_lit, col_high, total_h);
		nd_m3d_add_branchlet_card(b, mx, my, mz,
		                          cosf(sang_r), sinf(sang_r), -0.12f,
		                          b_len * 0.62f, dz * 0.10f, drop * 0.72f,
		                          b_len * 0.32f, dz * 0.10f, 0.58f,
		                          cx, cy, dome_cz,
		                          ND_MAT3D_FOL_CONIFER,
		                          col_deep, col_mid, col_lit, col_high, total_h);
	}
}

/* 3D Arching Monocot Sword-Blade (Typha latifolia / Graminoid Reed Blade) */
static inline void
nd_m3d_add_arching_blade(nd_mesh3d_builder_t *b,
                         float x0, float y0, float z0,
                         float dir_x, float dir_y,
                         float length, float height_arc, float drop_end,
                         float base_half_w,
                         uint32_t col_dark, uint32_t col_mid, uint32_t col_lit,
                         float total_h)
{
	float pdir[2] = { -dir_y, dir_x };
	float pl = sqrtf(pdir[0] * pdir[0] + pdir[1] * pdir[1]);
	if (pl > 1e-5f) { pdir[0] /= pl; pdir[1] /= pl; }

	uint32_t v_left[4], v_right[4];
	for (int s = 0; s <= 3; s++) {
		float t = (float)s / 3.0f;
		float reach = length * t;
		float z = fmaxf(0.4f, z0 + height_arc * sinf(t * 2.05f) - drop_end * t * t);
		float hw = base_half_w * (1.0f - 0.82f * t * t);
		float cx = x0 + dir_x * reach;
		float cy = y0 + dir_y * reach;

		float nx = dir_x * 0.42f;
		float ny = dir_y * 0.42f;
		float nz = 0.90f - 0.35f * t;

		uint32_t col = (s == 0) ? col_dark : ((s == 1) ? col_mid : col_lit);
		float ao = 0.50f + 0.50f * t;
		float ww = fminf(0.95f, fmaxf(0.12f, z / fmaxf(1.0f, total_h)) * (0.35f + 0.65f * t));

		v_left[s]  = nd_m3d_push_vert(b, cx - pdir[0] * hw, cy - pdir[1] * hw, z, ww,
		                              nx - pdir[0] * 0.30f, ny - pdir[1] * 0.30f, nz, ao, col, ND_MAT3D_FOLIAGE_CORE);
		v_right[s] = nd_m3d_push_vert(b, cx + pdir[0] * hw, cy + pdir[1] * hw, z, ww,
		                              nx + pdir[0] * 0.30f, ny + pdir[1] * 0.30f, nz, ao, col, ND_MAT3D_FOLIAGE_CORE);
	}
	for (int s = 0; s < 3; s++) {
		nd_m3d_push_tri(b, v_left[s], v_right[s], v_right[s + 1]);
		nd_m3d_push_tri(b, v_left[s], v_right[s + 1], v_left[s + 1]);
	}
}

/* 3D 5-Petal Radial Flower Corolla with Contrasting Central Disc (Daisy, Flax, Umbel Floret) */
static inline void
nd_m3d_add_blossom_star(nd_mesh3d_builder_t *b,
                        float cx, float cy, float cz,
                        float r_outer, float r_height,
                        uint32_t col_petal_lit, uint32_t col_petal_shade, uint32_t col_center,
                        float total_h)
{
	float ww = fminf(0.95f, cz / fmaxf(1.0f, total_h));
	float h_cup = fminf(r_height, r_outer * 0.34f);
	float pitch_tilt = 0.26f; /* Slight camera-facing tilt so petals read from both 55-deg and side views */
	uint32_t v_top = nd_m3d_push_vert(b, cx, cy, cz + h_cup * 0.65f, ww,
	                                  0.0f, 0.22f, 0.97f, 0.99f, col_center, ND_MAT3D_BLOSSOM);
	uint32_t v_bot = nd_m3d_push_vert(b, cx, cy, fmaxf(0.4f, cz - h_cup * 0.45f), ww,
	                                  0.0f, 0.0f, -1.0f, 0.62f, col_petal_shade, ND_MAT3D_BLOSSOM);
	uint32_t disc[5], tips[5], clefts[5];
	for (int i = 0; i < 5; i++) {
		float ang_tip   = ((float)i / 5.0f) * 6.2831853f;
		float ang_cleft = ang_tip + 0.6283185f;
		float ct = cosf(ang_tip),   st = sinf(ang_tip);
		float cc = cosf(ang_cleft), sc = sinf(ang_cleft);
		float r_disc  = r_outer * 0.32f;
		float r_cleft = r_outer * 0.40f;
		float z_disc  = cz + h_cup * 0.50f - st * r_disc * pitch_tilt;
		float z_tip   = cz + h_cup * 0.25f - st * r_outer * pitch_tilt;
		float z_cleft = cz + h_cup * 0.32f - sc * r_cleft * pitch_tilt;
		disc[i] = nd_m3d_push_vert(b, cx + ct * r_disc, cy + st * r_disc, fmaxf(0.4f, z_disc), ww,
		                           ct * 0.25f, st * 0.25f + 0.20f, 0.94f, 0.96f, col_center, ND_MAT3D_BLOSSOM);
		tips[i] = nd_m3d_push_vert(b, cx + ct * r_outer, cy + st * r_outer, fmaxf(0.4f, z_tip), ww,
		                           ct * 0.38f, st * 0.38f + 0.20f, 0.90f, 0.96f, col_petal_lit, ND_MAT3D_BLOSSOM);
		clefts[i] = nd_m3d_push_vert(b, cx + cc * r_cleft, cy + sc * r_cleft, fmaxf(0.4f, z_cleft), ww,
		                             cc * 0.30f, sc * 0.30f + 0.20f, 0.92f, 0.85f, col_petal_shade, ND_MAT3D_BLOSSOM);
	}
	for (int i = 0; i < 5; i++) {
		int next = (i + 1) % 5;
		int prev = (i + 4) % 5;
		/* Central golden/contrasting disc pentagon */
		nd_m3d_push_tri(b, disc[i], disc[next], v_top);
		/* 5 distinct ray petals radiating from central disc */
		nd_m3d_push_tri(b, disc[i], tips[i], clefts[i]);
		nd_m3d_push_tri(b, disc[i], clefts[prev], tips[i]);
		nd_m3d_push_tri(b, disc[i], disc[next], clefts[i]);
		/* Underside calyx cup */
		nd_m3d_push_tri(b, clefts[i], tips[i], v_bot);
		nd_m3d_push_tri(b, tips[next], clefts[i], v_bot);
	}
}

/* 3D Globose Berry / Drupe / Tomato Fruit with Spherical Shading & Apical Calyx */
static inline void
nd_m3d_add_fruit_berry(nd_mesh3d_builder_t *b,
                       float cx, float cy, float cz,
                       float rad, uint32_t col_ripe, uint32_t col_shade,
                       float total_h)
{
	float ww = fminf(0.92f, cz / fmaxf(1.0f, total_h));
	uint32_t v_top = nd_m3d_push_vert(b, cx, cy, cz + rad * 0.88f, ww,
	                                  0.0f, 0.0f, 1.0f, 0.95f, col_ripe, ND_MAT3D_BLOSSOM);
	uint32_t v_bot = nd_m3d_push_vert(b, cx, cy, fmaxf(0.4f, cz - rad * 0.88f), ww,
	                                  0.0f, 0.0f, -1.0f, 0.55f, col_shade, ND_MAT3D_BLOSSOM);
	uint32_t ring[6];
	for (int i = 0; i < 6; i++) {
		float ang = ((float)i / 6.0f) * 6.2831853f;
		float ca = cosf(ang), sa = sinf(ang);
		ring[i] = nd_m3d_push_vert(b, cx + ca * rad, cy + sa * rad, fmaxf(0.4f, cz), ww,
		                           ca * 0.86f, sa * 0.86f, 0.35f, 0.90f, col_ripe, ND_MAT3D_BLOSSOM);
	}
	for (int i = 0; i < 6; i++) {
		int next = (i + 1) % 6;
		nd_m3d_push_tri(b, ring[i], ring[next], v_top);
		nd_m3d_push_tri(b, ring[next], ring[i], v_bot);
	}
}

/* Lookup botanical 3D profile by species slug (supports all 16 runtime species + all 41 disk species) */
static inline nd_bot3d_profile_t
nd_m3d_get_botanical_profile(const char *slug)
{
	nd_bot3d_profile_t p;
	memset(&p, 0, sizeof(p));
	/* Default temperate broadleaf tree */
	p.kind = ND_BOT3D_BROADLEAF_OAK;
	p.bark_dark   = 0xFF222E3Eu;
	p.bark_mid    = 0xFF364960u;
	p.bark_light  = 0xFF4E6682u;
	p.leaf_deep   = 0xFF1D4020u;
	p.leaf_mid    = 0xFF2D632Eu;
	p.leaf_lit    = 0xFF448A3Cu;
	p.leaf_high   = 0xFF64B050u;
	p.accent_dark = 0xFF285878u;
	p.accent_lit  = 0xFF4288B0u;
	p.small_w = 56; p.small_h = 68;
	p.big_w   = 96; p.big_h   = 112;
	p.has_accent = 0;

	if (!slug) return p;

	if (!strcmp(slug, "pinus_sylvestris") || !strcmp(slug, "pinus_mugo")) {
		p.kind = ND_BOT3D_PINE;
		p.bark_dark  = 0xFF222838u;
		p.bark_mid   = 0xFF344C78u;
		p.bark_light = 0xFF4474B4u; /* Flaky orange-reddish upper Scots pine bark */
		p.leaf_deep  = 0xFF223C24u;
		p.leaf_mid   = 0xFF305C36u;
		p.leaf_lit   = 0xFF447E4Cu;
		p.leaf_high  = 0xFF60A066u;
		p.small_w = 52; p.small_h = 74;
		p.big_w   = 88; p.big_h   = 128;
		if (!strcmp(slug, "pinus_mugo")) {
			p.small_w = 48; p.small_h = 44;
			p.big_w   = 72; p.big_h   = 60;
		}
	} else if (!strcmp(slug, "pseudotsuga_menziesii") || !strcmp(slug, "picea_abies") ||
	           !strcmp(slug, "picea_mariana") || !strcmp(slug, "larix_decidua")) {
		p.kind = ND_BOT3D_FIR_CONIFER;
		p.bark_dark  = 0xFF1E2634u;
		p.bark_mid   = 0xFF303E52u;
		p.bark_light = 0xFF465870u;
		p.leaf_deep  = 0xFF1A3822u;
		p.leaf_mid   = 0xFF285A34u;
		p.leaf_lit   = 0xFF3A7E46u;
		p.leaf_high  = 0xFF56A45Eu;
		if (!strcmp(slug, "larix_decidua")) {
			p.leaf_lit  = 0xFF429652u;
			p.leaf_high = 0xFF68BE64u;
		}
		p.small_w = 52; p.small_h = 84;
		p.big_w   = 86; p.big_h   = 148;
	} else if (!strcmp(slug, "abies_alba")) {
		p.kind = ND_BOT3D_FIR_CONIFER;
		p.bark_dark  = 0xFF2E343Cu;
		p.bark_mid   = 0xFF4C5662u;
		p.bark_light = 0xFF707C8Au; /* Silvery-grey fir bark */
		p.leaf_deep  = 0xFF1E3A2Cu;
		p.leaf_mid   = 0xFF2E5C46u;
		p.leaf_lit   = 0xFF468266u;
		p.leaf_high  = 0xFF6CA88Cu; /* Silvery-emerald fir needles */
		p.small_w = 52; p.small_h = 80;
		p.big_w   = 88; p.big_h   = 144;
		p.sub_variant = 1; /* Flat-pectinate tiers + upright cylindrical cones */
	} else if (!strcmp(slug, "betula_pendula") || !strcmp(slug, "betula_pubescens") || !strcmp(slug, "betula_nana")) {
		p.kind = ND_BOT3D_BIRCH;
		p.bark_dark  = 0xFF1C1E24u; /* Dark diamond lenticels & basal fissures */
		p.bark_mid   = 0xFF8C96A4u;
		p.bark_light = 0xFFE2EAF2u; /* Chalk-white papery birch bark */
		if (!strcmp(slug, "betula_pendula")) {
			p.leaf_deep = 0xFF1E4A24u;
			p.leaf_mid  = 0xFF347434u;
			p.leaf_lit  = 0xFF52A244u;
			p.leaf_high = 0xFF7CD05Cu;
			p.sub_variant = 1; /* Weeping pendulous secondary twiglets */
		} else {
			p.bark_light = 0xFFC8D0D8u; /* Matte greyish-white Downy birch bark */
			p.leaf_deep = 0xFF204428u;
			p.leaf_mid  = 0xFF346A3Eu;
			p.leaf_lit  = 0xFF4C9254u;
			p.leaf_high = 0xFF70B872u;
		}
		p.small_w = 50; p.small_h = 72;
		p.big_w   = 84; p.big_h   = 120;
		if (!strcmp(slug, "betula_nana")) {
			p.small_w = 36; p.small_h = 36;
			p.big_w   = 52; p.big_h   = 52;
		}
	} else if (!strcmp(slug, "linum_usitatissimum") || !strcmp(slug, "lavandula_angustifolia") ||
	           !strcmp(slug, "rosmarinus_officinalis")) {
		p.kind = ND_BOT3D_HERB_FLAX;
		p.bark_dark  = 0xFF224424u;
		p.bark_mid   = 0xFF346834u;
		p.bark_light = 0xFF4A8C46u;
		p.leaf_deep  = 0xFF244C26u;
		p.leaf_mid   = 0xFF387438u;
		p.leaf_lit   = 0xFF529E4Eu;
		p.leaf_high  = 0xFF72C268u;
		if (!strcmp(slug, "lavandula_angustifolia")) {
			p.accent_dark = 0xFF783864u;
			p.accent_lit  = 0xFFC468A8u;
		} else {
			p.accent_dark = 0xFF9E5424u; /* Rich azure-periwinkle blue (BGR in uint32_t) */
			p.accent_lit  = 0xFFF2A452u;
		}
		p.has_accent = 1;
		p.small_w = 24; p.small_h = 28;
		p.big_w   = 32; p.big_h   = 40;
	} else if (!strcmp(slug, "arthrocereus_rondonianus") || !strcmp(slug, "opuntia_ficus_indica") ||
	           !strcmp(slug, "aloe_vera")) {
		p.kind = ND_BOT3D_CACTUS;
		p.bark_dark  = 0xFF2A4030u;
		p.bark_mid   = 0xFF486A4Au;
		p.bark_light = 0xFF78A074u;
		p.leaf_deep  = 0xFF20442Au;
		p.leaf_mid   = 0xFF326A40u;
		p.leaf_lit   = 0xFF489458u;
		p.leaf_high  = 0xFF6CC07Cu;
		p.accent_dark = 0xFF5C4298u; /* Rose-white nocturnal cactus bloom */
		p.accent_lit  = 0xFFD4C4F8u;
		p.has_accent = 1;
		p.small_w = 32; p.small_h = 44;
		p.big_w   = 48; p.big_h   = 68;
	} else if (!strcmp(slug, "acacia_senegal") || !strcmp(slug, "acacia_tortilis") ||
	           !strcmp(slug, "boswellia_sacra")) {
		p.kind = ND_BOT3D_ACACIA;
		p.bark_dark  = 0xFF202A3Au;
		p.bark_mid   = 0xFF384A64u;
		p.bark_light = 0xFF567094u;
		p.leaf_deep  = 0xFF1E3E22u;
		p.leaf_mid   = 0xFF366832u;
		p.leaf_lit   = 0xFF569442u;
		p.leaf_high  = 0xFF7EC058u;
		p.small_w = 66; p.small_h = 48;
		p.big_w   = 116; p.big_h  = 84;
	} else if (!strcmp(slug, "daucus_carota") || !strcmp(slug, "dryas_octopetala") ||
	           !strcmp(slug, "rubus_chamaemorus")) {
		p.kind = ND_BOT3D_HERB_UMBEL;
		p.bark_dark  = 0xFF204422u;
		p.bark_mid   = 0xFF366C34u;
		p.bark_light = 0xFF4E9448u;
		p.leaf_deep  = 0xFF1E4820u;
		p.leaf_mid   = 0xFF347434u;
		p.leaf_lit   = 0xFF4EA24Au;
		p.leaf_high  = 0xFF72CA68u;
		p.accent_dark = 0xFFA8BEC4u;
		p.accent_lit  = 0xFFEBF6F8u; /* Creamy lace-white compound umbels */
		p.has_accent = 1;
		p.small_w = 24; p.small_h = 26;
		p.big_w   = 34; p.big_h   = 38;
	} else if (!strcmp(slug, "solanum_lycopersicum") || !strcmp(slug, "vaccinium_myrtillus") ||
	           !strcmp(slug, "vaccinium_uliginosum") || !strcmp(slug, "empetrum_nigrum")) {
		p.kind = ND_BOT3D_HERB_TOMATO;
		p.bark_dark  = 0xFF1E4220u;
		p.bark_mid   = 0xFF326832u;
		p.bark_light = 0xFF488E46u;
		p.leaf_deep  = 0xFF1C4620u;
		p.leaf_mid   = 0xFF307232u;
		p.leaf_lit   = 0xFF489E48u;
		p.leaf_high  = 0xFF68C662u;
		p.accent_dark = 0xFF1C2498u; /* Ripe crimson-scarlet fruit */
		p.accent_lit  = 0xFF3448ECu;
		p.has_accent = 1;
		p.small_w = 26; p.small_h = 28;
		p.big_w   = 38; p.big_h   = 42;
	} else if (!strcmp(slug, "quercus_robur") || !strcmp(slug, "fagus_sylvatica") ||
	           !strcmp(slug, "castanea_sativa") || !strcmp(slug, "ulmus_glabra")) {
		p.kind = ND_BOT3D_BROADLEAF_OAK;
		p.bark_dark  = 0xFF202834u;
		p.bark_mid   = 0xFF364458u;
		p.bark_light = 0xFF50647Eu;
		p.leaf_deep  = 0xFF1A3E1Eu;
		p.leaf_mid   = 0xFF2C662Eu;
		p.leaf_lit   = 0xFF449240u;
		p.leaf_high  = 0xFF68BE58u;
		p.small_w = 68; p.small_h = 66;
		p.big_w   = 114; p.big_h  = 108;
	} else if (!strcmp(slug, "salix_babylonica") || !strcmp(slug, "salix_arctica") ||
	           !strcmp(slug, "salix_herbacea")) {
		p.kind = ND_BOT3D_WILLOW;
		p.bark_dark  = 0xFF222C38u;
		p.bark_mid   = 0xFF3A4A5Cu;
		p.bark_light = 0xFF566C82u;
		p.leaf_deep  = 0xFF224A28u;
		p.leaf_mid   = 0xFF38783Eu;
		p.leaf_lit   = 0xFF56A856u;
		p.leaf_high  = 0xFF80D472u;
		p.small_w = 66; p.small_h = 64;
		p.big_w   = 110; p.big_h  = 104;
	} else if (!strcmp(slug, "acer_pseudoplatanus") || !strcmp(slug, "tilia_cordata") ||
	           !strcmp(slug, "fraxinus_excelsior") || !strcmp(slug, "alnus_glutinosa") ||
	           !strcmp(slug, "populus_tremula")) {
		p.kind = ND_BOT3D_MAPLE_SYCAMORE;
		p.bark_dark  = 0xFF26303Cu;
		p.bark_mid   = 0xFF405064u;
		p.bark_light = 0xFF5E728Au;
		p.leaf_deep  = 0xFF1C4220u;
		p.leaf_mid   = 0xFF306E32u;
		p.leaf_lit   = 0xFF4A9C46u;
		p.leaf_high  = 0xFF72C860u;
		p.small_w = 62; p.small_h = 68;
		p.big_w   = 104; p.big_h  = 114;
	} else if (!strcmp(slug, "olea_europaea") || !strcmp(slug, "quercus_ilex") ||
	           !strcmp(slug, "arbutus_unedo")) {
		p.kind = ND_BOT3D_OLIVE;
		p.bark_dark  = 0xFF283038u;
		p.bark_mid   = 0xFF44505Cu;
		p.bark_light = 0xFF667686u;
		p.leaf_deep  = 0xFF2A4232u;
		p.leaf_mid   = 0xFF446A50u;
		p.leaf_lit   = 0xFF649472u;
		p.leaf_high  = 0xFF9AC8A8u; /* Silvery sage-green olive foliage with bright underside sheen */
		p.small_w = 56; p.small_h = 52;
		p.big_w   = 96; p.big_h   = 90;
	} else if (!strcmp(slug, "bellis_perennis") || !strcmp(slug, "ranunculus_acris")) {
		p.kind = ND_BOT3D_HERB_DAISY;
		p.bark_dark  = 0xFF204822u;
		p.bark_mid   = 0xFF347036u;
		p.bark_light = 0xFF4C9A4Cu;
		p.leaf_deep  = 0xFF1E4620u;
		p.leaf_mid   = 0xFF327034u;
		p.leaf_lit   = 0xFF4A9C4Au;
		p.leaf_high  = 0xFF6AC466u;
		p.accent_dark = 0xFF24A8E8u; /* Golden disc center */
		p.accent_lit  = 0xFFF2F6FAu; /* Crisp white ray petals */
		p.has_accent = 1;
		p.small_w = 22; p.small_h = 22;
		p.big_w   = 30; p.big_h   = 30;
	} else if (!strcmp(slug, "typha_latifolia") || !strcmp(slug, "carex_acuta") ||
	           !strcmp(slug, "eriophorum_vaginatum")) {
		p.kind = ND_BOT3D_REED_CATTAIL;
		p.bark_dark  = 0xFF18243Eu; /* Dark cigar-brown cattail spadix */
		p.bark_mid   = 0xFF263A62u;
		p.bark_light = 0xFF3A568Au;
		p.leaf_deep  = 0xFF1E4424u;
		p.leaf_mid   = 0xFF326C38u;
		p.leaf_lit   = 0xFF4A984Eu;
		p.leaf_high  = 0xFF6AC06Au;
		p.small_w = 26; p.small_h = 38;
		p.big_w   = 42; p.big_h   = 62;
	} else {
		/* Tropical / emergent / palm species */
		p.kind = ND_BOT3D_PALM_TROPICAL;
	}
	return p;
}

/* SpeedTree / Creation Engine (Skyrim)–Style 3D Multi-Sheet Botanical Bough Cluster:
 * Replaces closed ellipsoid spheres with an interlocking 3D rosette of 7 pitched, rolled,
 * cambered branchlet cards (4 lower spreading skirt sheets + 3 upper crossed crown-cap sheets)
 * sharing a common sub-crown centroid (cx, cy, cz - rz * 0.15) for smooth outward dome-transferred
 * normals and crisp UV-space per-leaf / per-needle cutouts. */
static inline void
nd_m3d_add_foliage_bough_cluster_ex(nd_mesh3d_builder_t *b,
                                    float cx, float cy, float cz,
                                    float rx, float ry, float rz,
                                    float fol_base,
                                    uint32_t col_deep, uint32_t col_mid, uint32_t col_lit, uint32_t col_high,
                                    float total_h, uint32_t seed)
{
	uint32_t hk = nd_m3d_hash(seed + 137u);
	float ang0 = ((float)(hk & 0xFFu) / 255.0f) * 6.2831853f;
	float dome_cz = cz - rz * 0.18f;

	/* Tier 1: 3 lower spreading skirt branchlet sheets pitched outward at 120 deg with alternating axial roll */
	for (int s = 0; s < 3; s++) {
		uint32_t hs = nd_m3d_hash(seed + (uint32_t)(s * 179 + 43));
		float jitter = (((float)(hs & 0xFFu) / 255.0f) - 0.5f) * 0.42f;
		float sang = ang0 + (float)s * 2.0943951f + jitter;
		float sca = cosf(sang), ssa = sinf(sang);
		float roll = ((s & 1) ? 0.48f : -0.48f) + jitter * 0.35f;
		float sx = cx + sca * rx * 0.08f;
		float sy = cy + ssa * ry * 0.08f;
		float sz = cz - rz * 0.08f + ((float)(s & 1) * rz * 0.10f);
		float card_len = fmaxf(rx, ry) * (0.98f + 0.14f * ((float)((hs >> 8) & 0x3Fu) / 63.0f));
		float card_hw  = card_len * 0.62f;
		nd_m3d_add_branchlet_card(b, sx, sy, sz,
		                          sca, ssa, 0.14f,
		                          card_len, rz * 0.34f, rz * 0.46f,
		                          card_hw, rz * 0.18f, roll,
		                          cx, cy, dome_cz,
		                          fol_base,
		                          col_deep, col_mid, col_lit, col_high, total_h);
	}

	/* Tier 2: Pitched crown-cap branchlet sheet intersecting across the sub-crown apex */
	{
		uint32_t hf = nd_m3d_hash(seed + 71u);
		float jitter = (((float)(hf & 0xFFu) / 255.0f) - 0.5f) * 0.45f;
		float fang = ang0 + 1.0471976f + jitter;
		float fca = cosf(fang), fsa = sinf(fang);
		float fx = cx - fca * rx * 0.14f;
		float fy = cy - fsa * ry * 0.14f;
		float fz = cz + rz * 0.22f;
		float card_len = fmaxf(rx, ry) * 0.96f;
		float card_hw  = card_len * 0.64f;
		nd_m3d_add_branchlet_card(b, fx, fy, fz,
		                          fca, fsa, 0.26f,
		                          card_len, rz * 0.38f, rz * 0.38f,
		                          card_hw, rz * 0.16f, 0.32f,
		                          cx, cy, dome_cz,
		                          fol_base,
		                          col_deep, col_mid, col_lit, col_high, total_h);
	}
}

static inline void
nd_m3d_add_foliage_bough_cluster(nd_mesh3d_builder_t *b,
                                 float cx, float cy, float cz,
                                 float rx, float ry, float rz,
                                 uint32_t col_deep, uint32_t col_mid, uint32_t col_lit, uint32_t col_high,
                                 float total_h, uint32_t seed)
{
	nd_m3d_add_foliage_bough_cluster_ex(b, cx, cy, cz, rx, ry, rz,
	                                    ND_MAT3D_FOL_OVATE,
	                                    col_deep, col_mid, col_lit, col_high,
	                                    total_h, seed);
}

/* Branch-Aligned Shingled Bough Fan (Skyrim / SpeedTree Style):
 * Anchors all 4 branchlet cards' woody stems (u = 0) directly into the terminal shoot fork
 * (ax, ay, az) and fans them outward along the branch growth direction (dx, dy) in layered,
 * cambered tiers (1 central leader shoot + 2 lateral V-fan wings at +/-48 deg + 1 upper
 * crown-shingle cap). Blends local sub-crown and global tree-crown centroids for seamless
 * dome-normal lighting across the entire canopy without floating mid-air polygon intersections. */
static inline void
nd_m3d_add_foliage_bough_fan_ex(nd_mesh3d_builder_t *b,
                                float ax, float ay, float az,
                                float dx, float dy,
                                float rx, float ry, float rz,
                                float fol_base,
                                uint32_t col_deep, uint32_t col_mid, uint32_t col_lit, uint32_t col_high,
                                float total_h, uint32_t seed)
{
	float dlen = sqrtf(dx * dx + dy * dy);
	float dir_x = (dlen > 1e-4f) ? (dx / dlen) : 1.0f;
	float dir_y = (dlen > 1e-4f) ? (dy / dlen) : 0.0f;
	float base_ang = atan2f(dir_y, dir_x);

	/* Blend local bough center (55%) with global tree crown core (45%) for unified Skyrim dome shading */
	float loc_cx = ax + dir_x * (rx * 0.42f);
	float loc_cy = ay + dir_y * (ry * 0.42f);
	float loc_cz = az - rz * 0.14f;
	float dome_cx = loc_cx * 0.55f;
	float dome_cy = loc_cy * 0.55f;
	float dome_cz = loc_cz * 0.55f + (total_h * 0.56f) * 0.45f;

	float r_base = fmaxf(rx, ry);
	uint32_t h0 = nd_m3d_hash(seed + 137u);
	float jit0 = (((float)(h0 & 0xFFu) / 255.0f) - 0.5f) * 0.18f;

	/* Card 0: Central forward leader shoot anchored slightly behind the woody fork tip */
	{
		float ang = base_ang + jit0;
		float ca = cosf(ang), sa = sinf(ang);
		float sx = ax - dir_x * (r_base * 0.42f);
		float sy = ay - dir_y * (r_base * 0.42f);
		float sz = az - rz * 0.04f;
		float card_len = r_base * 1.38f;
		float card_hw  = card_len * 0.56f;
		nd_m3d_add_branchlet_card(b, sx, sy, sz,
		                          ca, sa, 0.16f,
		                          card_len, rz * 0.34f, rz * 0.32f,
		                          card_hw, rz * 0.16f, jit0 * 0.8f,
		                          dome_cx, dome_cy, dome_cz,
		                          fol_base,
		                          col_deep, col_mid, col_lit, col_high, total_h);
	}

	/* Cards 1 & 2: Left & right lateral shingled bough fan wings at +/-48 deg with natural dihedral roll */
	for (int s = 0; s < 2; s++) {
		float side_sign = (s == 0) ? 1.0f : -1.0f;
		uint32_t hs = nd_m3d_hash(seed + (uint32_t)(s * 179 + 43));
		float jitter = (((float)(hs & 0xFFu) / 255.0f) - 0.5f) * 0.22f;
		float ang = base_ang + side_sign * 0.84f + jitter;
		float ca = cosf(ang), sa = sinf(ang);
		float sx = ax - dir_x * (r_base * 0.34f);
		float sy = ay - dir_y * (r_base * 0.34f);
		float sz = az - rz * (0.06f + 0.05f * (float)s);
		float card_len = r_base * (1.22f + 0.12f * ((float)((hs >> 8) & 0x3Fu) / 63.0f));
		float card_hw  = card_len * 0.58f;
		float roll = side_sign * 0.36f + jitter * 0.25f;
		nd_m3d_add_branchlet_card(b, sx, sy, sz,
		                          ca, sa, 0.10f,
		                          card_len, rz * 0.28f, rz * 0.40f,
		                          card_hw, rz * 0.17f, roll,
		                          dome_cx, dome_cy, dome_cz,
		                          fol_base,
		                          col_deep, col_mid, col_lit, col_high, total_h);
	}

	/* Card 3: Upper crown-shingle cap anchored above the fork for oblique & top-down silhouette volume */
	{
		uint32_t hf = nd_m3d_hash(seed + 71u);
		float jitter = (((float)(hf & 0xFFu) / 255.0f) - 0.5f) * 0.34f;
		float ang = base_ang + jitter;
		float ca = cosf(ang), sa = sinf(ang);
		float fx = ax - dir_x * (r_base * 0.48f);
		float fy = ay - dir_y * (r_base * 0.48f);
		float fz = az + rz * 0.18f;
		float card_len = r_base * 1.16f;
		float card_hw  = card_len * 0.62f;
		nd_m3d_add_branchlet_card(b, fx, fy, fz,
		                          ca, sa, 0.28f,
		                          card_len, rz * 0.36f, rz * 0.28f,
		                          card_hw, rz * 0.18f, (hf & 1u) ? 0.28f : -0.28f,
		                          dome_cx, dome_cy, dome_cz,
		                          fol_base,
		                          col_deep, col_mid, col_lit, col_high, total_h);
	}
}

/* Skyrim Creation-Engine Style Drooping Conifer Bough (Skirt + Cross-Fin Shingle):
 * Replaces upward-cupped broadleaf fans on conifers (Douglas Fir, Silver Fir, Scots Pine)
 * with gravity-swept, downward-shingled pine/fir boughs:
 *   - Card 0: Central sweeping needle mantle pitched downward (-0.28..-0.40) with upturned distal tip
 *   - Cards 1 & 2: Steeply banked left/right lateral skirt wings (roll = +/-44 deg) that drape over the whorl below
 *   - Card 3: Steeply rolled dorsal/ventral cross-fin (roll = +/-72 deg) for deep 3D side-view & 55-deg diorama volume. */
static inline void
nd_m3d_add_conifer_bough_fan(nd_mesh3d_builder_t *b,
                             float ax, float ay, float az,
                             float dx, float dy,
                             float rx, float ry, float rz,
                             uint32_t col_deep, uint32_t col_mid, uint32_t col_lit, uint32_t col_high,
                             float total_h, uint32_t seed)
{
	float dlen = sqrtf(dx * dx + dy * dy);
	float dir_x = (dlen > 1e-4f) ? (dx / dlen) : 1.0f;
	float dir_y = (dlen > 1e-4f) ? (dy / dlen) : 0.0f;
	float base_ang = atan2f(dir_y, dir_x);

	float r_base = fmaxf(rx, ry);
	float loc_cx = ax + dir_x * (r_base * 0.38f);
	float loc_cy = ay + dir_y * (r_base * 0.38f);
	float loc_cz = az - rz * 0.32f;
	float dome_cx = loc_cx * 0.50f;
	float dome_cy = loc_cy * 0.50f;
	float dome_cz = loc_cz * 0.65f + (total_h * 0.48f) * 0.35f;

	uint32_t h0 = nd_m3d_hash(seed + 137u);
	float jit0 = (((float)(h0 & 0xFFu) / 255.0f) - 0.5f) * 0.16f;

	/* Card 0: Central gravity-swept conifer needle mantle with downward droop and upturned tip */
	{
		float ang = base_ang + jit0;
		float ca = cosf(ang), sa = sinf(ang);
		float sx = ax - dir_x * (r_base * 0.38f);
		float sy = ay - dir_y * (r_base * 0.38f);
		float sz = az + rz * 0.12f;
		float card_len = r_base * 1.42f;
		float card_hw  = card_len * 0.58f;
		nd_m3d_add_branchlet_card(b, sx, sy, sz,
		                          ca, sa, -0.26f,
		                          card_len, -rz * 0.24f, rz * 0.42f,
		                          card_hw, rz * 0.22f, jit0 * 0.9f,
		                          dome_cx, dome_cy, dome_cz,
		                          ND_MAT3D_FOL_CONIFER,
		                          col_deep, col_mid, col_lit, col_high, total_h);
	}

	/* Cards 1 & 2: Left & right steeply pitched lateral skirt wings (roll = +/-0.76 rad = +/-44 deg) */
	for (int s = 0; s < 2; s++) {
		float side_sign = (s == 0) ? 1.0f : -1.0f;
		uint32_t hs = nd_m3d_hash(seed + (uint32_t)(s * 179 + 43));
		float jitter = (((float)(hs & 0xFFu) / 255.0f) - 0.5f) * 0.20f;
		float ang = base_ang + side_sign * 0.62f + jitter;
		float ca = cosf(ang), sa = sinf(ang);
		float sx = ax - dir_x * (r_base * 0.30f);
		float sy = ay - dir_y * (r_base * 0.30f);
		float sz = az + rz * 0.06f;
		float card_len = r_base * (1.26f + 0.12f * ((float)((hs >> 8) & 0x3Fu) / 63.0f));
		float card_hw  = card_len * 0.60f;
		float roll = side_sign * 0.76f + jitter * 0.22f;
		nd_m3d_add_branchlet_card(b, sx, sy, sz,
		                          ca, sa, -0.32f,
		                          card_len, -rz * 0.18f, rz * 0.56f,
		                          card_hw, rz * 0.20f, roll,
		                          dome_cx, dome_cy, dome_cz,
		                          ND_MAT3D_FOL_CONIFER,
		                          col_deep, col_mid, col_lit, col_high, total_h);
	}

	/* Card 3: Steeply rolled dorsal/ventral cross-fin (roll = +/-1.22 rad = +/-70 deg) for side-view fullness */
	{
		uint32_t hf = nd_m3d_hash(seed + 71u);
		float jitter = (((float)(hf & 0xFFu) / 255.0f) - 0.5f) * 0.24f;
		float ang = base_ang + jitter;
		float ca = cosf(ang), sa = sinf(ang);
		float fx = ax - dir_x * (r_base * 0.36f);
		float fy = ay - dir_y * (r_base * 0.36f);
		float fz = az + rz * 0.18f;
		float card_len = r_base * 1.22f;
		float card_hw  = card_len * 0.56f;
		float roll = ((hf & 1u) ? 1.20f : -1.20f) + jitter * 0.20f;
		nd_m3d_add_branchlet_card(b, fx, fy, fz,
		                          ca, sa, -0.22f,
		                          card_len, rz * 0.16f, rz * 0.48f,
		                          card_hw, rz * 0.16f, roll,
		                          dome_cx, dome_cy, dome_cz,
		                          ND_MAT3D_FOL_CONIFER,
		                          col_deep, col_mid, col_lit, col_high, total_h);
	}
}

/* Flat Horizontal Tabular Umbrella Pad (Acacia senegal & Scots Pine Upper Cloud Table):
 * Spreads 4 wide, horizontally stratified branchlet cards with near-zero vertical pitch
 * and shallow vertical thickness so adjacent terminal pads merge into a continuous flat
 * savannah umbrella tabletop (Acacia) or billowy layered upper pine table (Pinus sylvestris). */
static inline void
nd_m3d_add_tabular_umbrella_fan(nd_mesh3d_builder_t *b,
                                float ax, float ay, float az,
                                float dx, float dy,
                                float rx, float ry, float rz,
                                float fol_base,
                                uint32_t col_deep, uint32_t col_mid, uint32_t col_lit, uint32_t col_high,
                                float total_h, uint32_t seed)
{
	float dlen = sqrtf(dx * dx + dy * dy);
	float dir_x = (dlen > 1e-4f) ? (dx / dlen) : 1.0f;
	float dir_y = (dlen > 1e-4f) ? (dy / dlen) : 0.0f;
	float base_ang = atan2f(dir_y, dir_x);

	float r_base = fmaxf(rx, ry);
	float dome_cx = (ax + dir_x * r_base * 0.35f) * 0.45f;
	float dome_cy = (ay + dir_y * r_base * 0.35f) * 0.45f;
	float dome_cz = az - r_base * 0.45f; /* Below the flat table so normals point strongly upward + outward */

	static const float fan_offs[4] = { 0.0f, -1.02f, 1.02f, 3.1415926f };
	static const float fan_roll[4] = { 0.12f, -0.22f, 0.22f, -0.14f };
	static const float fan_len[4]  = { 1.22f, 1.14f, 1.14f, 0.92f };
	for (int s = 0; s < 4; s++) {
		uint32_t hs = nd_m3d_hash(seed + (uint32_t)(s * 163 + 29));
		float jitter = (((float)(hs & 0xFFu) / 255.0f) - 0.5f) * 0.24f;
		float ang = base_ang + fan_offs[s] + jitter;
		float ca = cosf(ang), sa = sinf(ang);
		float sx = ax - ca * (r_base * 0.18f);
		float sy = ay - sa * (r_base * 0.18f);
		float sz = az + ((float)(s - 1) * rz * 0.10f);
		float card_len = r_base * fan_len[s];
		float card_hw  = card_len * 0.64f;
		nd_m3d_add_branchlet_card(b, sx, sy, sz,
		                          ca, sa, 0.03f,
		                          card_len, rz * 0.20f, rz * 0.22f,
		                          card_hw, rz * 0.10f, fan_roll[s] + jitter * 0.2f,
		                          dome_cx, dome_cy, dome_cz,
		                          fol_base,
		                          col_deep, col_mid, col_lit, col_high, total_h);
	}
}

/* Leonardo da Vinci Recursive Vascular Tree Architecture (Notebook M / Treatise on Painting):
 *   1. Cross-Sectional Area Conservation at Every Fork:
 *      "All the branches of a tree at every stage of its height when put together are equal
 *       in thickness to the trunk [below them]." -> r_parent^alpha = sum(r_child_i^alpha), alpha = 2.12.
 *   2. Angular Torque & Deflection Balance:
 *      At each bifurcation, the thicker leader continuation deflects by a smaller angle theta_1
 *      while the thinner lateral branch deflects by a wider angle theta_2 such that
 *      r_1^alpha * sin(theta_1) = r_2^alpha * sin(theta_2), driven by the species `apical_dom` gene.
 *   3. Golden-Angle Phyllotaxis (137.508 deg = 2.3999632 rad) & Centrifugal Phototropism:
 *      Successive bifurcation planes rotate around the parent limb axis by the golden angle and
 *      steer outward away from the shaded trunk core toward open daylight, creating natural
 *      crown fenestrations (sky windows) where interior structural forks remain visible.
 *   4. Terminal Shoot Foliage Only:
 *      Foliage grows only on terminal Order-2 and Order-3 shoots (plus cascading weeping curtains
 *      when weeping_droop is active), never as disconnected blobs on bare lower trunks. */
typedef struct {
	float w_total;
	float h_total;
	float apical;
	float pitch_norm;
	float gnarl_norm;
	float weep_norm;
	float flatness;
	float crook_mag;
	uint8_t fol_type;
	uint8_t bark_type;
	int is_big;
	uint32_t c_bark_dark, c_bark_mid, c_bark_light;
	uint32_t c_leaf_deep, c_leaf_mid, c_leaf_lit, c_leaf_high;
} nd_m3d_davinci_ctx_t;

static inline void
nd_m3d_build_davinci_limb(nd_mesh3d_builder_t *b,
                          const nd_m3d_davinci_ctx_t *ctx,
                          float x0, float y0, float z0,
                          float dx, float dy, float dz,
                          float len, float r0,
                          int order, int max_order,
                          float roll_az, uint32_t seed)
{
	/* Normalize incoming limb growth direction */
	float d[3] = { dx, dy, dz };
	nd_m3d_norm3(d);

	/* Centrifugal phototropism + gravitropic droop for branches (order >= 1) */
	if (order >= 1) {
		float r_horiz = sqrtf(x0 * x0 + y0 * y0);
		float out_x = (r_horiz > 1.5f) ? (x0 / r_horiz) : cosf(roll_az);
		float out_y = (r_horiz > 1.5f) ? (y0 / r_horiz) : sinf(roll_az);
		/* Steer outward away from the central crown shadow and modulate vertical pitch */
		float outward_pull = 0.24f + 0.10f * (float)order;
		d[0] += out_x * outward_pull;
		d[1] += out_y * outward_pull;
		/* Phototropic skyward curl at outer tips vs gravitropic pitch & weeping droop */
		float target_dz = (ctx->flatness < 0.42f)
			? (0.28f - 0.06f * (float)order) /* Umbrella acacia / Scots pine table crown */
			: (0.58f - 0.52f * ctx->pitch_norm - 0.24f * ctx->weep_norm);
		d[2] = d[2] * 0.62f + target_dz * 0.38f;
		nd_m3d_norm3(d);
	}

	float x1 = x0 + d[0] * len;
	float y1 = y0 + d[1] * len;
	float z1 = z0 + d[2] * len;
	/* Keep branch joints above ground and within canonical height envelope */
	float min_z = (order == 0) ? (ctx->h_total * 0.14f) : (ctx->h_total * 0.18f);
	if (z1 < min_z) z1 = min_z;
	if (z1 > ctx->h_total * 0.93f) z1 = ctx->h_total * 0.93f;

	/* Da Vinci mild internode taper (most cross-sectional reduction happens at fork junctions) */
	float r1 = r0 * ((order == 0) ? 0.86f : ((order == 1) ? 0.80f : 0.72f));
	int sides = (order == 0) ? 8 : ((order == 1) ? 6 : 5);
	int rings = (order == 0) ? 4 : ((order == 1) ? 3 : 2);
	nd_m3d_add_trunk_tube_ex(b, x0, y0, z0, r0,
	                         x1, y1, z1, r1,
	                         sides, rings,
	                         ctx->c_bark_dark, ctx->c_bark_mid, ctx->c_bark_light,
	                         ctx->bark_type,
	                         ctx->crook_mag * (0.75f + 0.22f * (float)order),
	                         ctx->h_total, seed);

	/* Terminal shoot: emit compound porous sub-crown foliage cloud + optional weeping curtains */
	if (order >= max_order) {
		float size_scale = (max_order >= 3) ? 0.96f : 1.12f;
		float flat_boost = (ctx->flatness < 0.42f) ? 1.24f : 1.0f;
		float lobe_rx = ctx->w_total * (0.175f + 0.035f * nd_m3d_rand01(seed + 19u)) * size_scale * flat_boost;
		float lobe_ry = lobe_rx * 0.92f;
		float lobe_rz = lobe_rx * fmaxf(0.28f, fminf(0.84f, ctx->flatness * 0.82f));
		/* Offset leaf cloud slightly outward & upward along terminal shoot tangent so woody fork under-boughs show */
		float fcx = x1 + d[0] * (lobe_rx * 0.22f);
		float fcy = y1 + d[1] * (lobe_ry * 0.22f);
		float fcz = fminf(ctx->h_total * 0.94f, z1 + lobe_rz * 0.24f);
		nd_m3d_add_foliage_bough_cluster(b, fcx, fcy, fcz,
		                                 lobe_rx, lobe_ry, lobe_rz,
		                                 ctx->c_leaf_deep, ctx->c_leaf_mid, ctx->c_leaf_lit, ctx->c_leaf_high,
		                                 ctx->h_total, seed + 307u);

		/* Cascading pendulous weeping curtains hanging from terminal shoots (Salix babylonica, Betula pendula) */
		if (ctx->fol_type == ND_PLANT_FOLIAGE_WEEPING || ctx->weep_norm > 0.35f) {
			int n_curtains = (ctx->weep_norm > 0.72f) ? 2 : 1;
			for (int wc = 0; wc < n_curtains; wc++) {
				float cang = atan2f(y1, x1) + (float)wc * 0.85f - 0.35f;
				float cca = cosf(cang), csa = sinf(cang);
				float drop = ctx->h_total * (0.24f + 0.36f * ctx->weep_norm) * (0.88f + 0.16f * (float)(wc & 1));
				nd_m3d_add_leaf_frond_spray(b, fcx + cca * lobe_rx * 0.30f, fcy + csa * lobe_ry * 0.30f, z1,
				                            cca, csa, ctx->w_total * 0.10f, ctx->h_total * 0.03f, drop,
				                            ctx->w_total * 0.048f, ctx->h_total * 0.02f,
				                            ctx->c_leaf_deep, ctx->c_leaf_mid, ctx->c_leaf_lit, ctx->c_leaf_high,
				                            ctx->h_total);
			}
		}
		return;
	}

	/* Build orthonormal frame (u_perp, v_perp) around parent limb tangent d[3] */
	float up_ref[3] = { 0.0f, 0.0f, 1.0f };
	if (fabsf(d[2]) > 0.88f) {
		up_ref[0] = 1.0f; up_ref[1] = 0.0f; up_ref[2] = 0.0f;
	}
	float u_perp[3] = {
		up_ref[1] * d[2] - up_ref[2] * d[1],
		up_ref[2] * d[0] - up_ref[0] * d[2],
		up_ref[0] * d[1] - up_ref[1] * d[0]
	};
	nd_m3d_norm3(u_perp);
	float v_perp[3] = {
		d[1] * u_perp[2] - d[2] * u_perp[1],
		d[2] * u_perp[0] - d[0] * u_perp[2],
		d[0] * u_perp[1] - d[1] * u_perp[0]
	};
	nd_m3d_norm3(v_perp);

	/* Da Vinci Area-Preserving Bifurcation / Trifurcation:
	 * At Order 0->1 or Order 1->2, strong broadleaves can trifurcate (3 children: 1 leader + 2 phyllotactic laterals);
	 * otherwise bifurcate (2 children) with area weights summing to 1.0: sum(w_i) = 1.0, r_child_i = r1 * w_i^(1/2.12). */
	const float inv_davinci_exp = 1.0f / 2.12f; /* alpha = 2.12 */
	int n_children = (order == 0 || (order == 1 && ctx->is_big && (seed & 1u))) ? 3 : 2;

	float w_child[3];
	if (n_children == 2) {
		/* High apical dominance -> dominant leader (0.64) + lateral (0.36);
		 * Low apical dominance (decurrent oak/olive/acacia) -> nearly balanced fork (0.52 / 0.48) */
		float jitter = (nd_m3d_rand01(seed + 41u) - 0.5f) * 0.08f;
		w_child[0] = fminf(0.70f, fmaxf(0.50f, 0.51f + 0.16f * ctx->apical + jitter));
		w_child[1] = 1.0f - w_child[0];
		w_child[2] = 0.0f;
	} else {
		float w0 = fminf(0.54f, fmaxf(0.36f, 0.36f + 0.18f * ctx->apical));
		float rem = 1.0f - w0;
		w_child[0] = w0;
		w_child[1] = rem * 0.54f;
		w_child[2] = rem * 0.46f;
	}

	/* Base fork opening angle governed by branch_pitch, crown_flatness, and gnarl_norm */
	float base_spread = 0.52f + 0.28f * ctx->pitch_norm + 0.16f * ctx->gnarl_norm +
	                    ((ctx->flatness < 0.42f) ? 0.24f : 0.0f);

	for (int c = 0; c < n_children; c++) {
		uint32_t c_seed = nd_m3d_hash(seed + (uint32_t)(c * 251 + order * 97 + 13));
		/* Exact Da Vinci cross-sectional area conservation: sum(r_c^2.12) == r1^2.12 */
		float r_child = r1 * powf(w_child[c], inv_davinci_exp);

		/* Da Vinci torque-balanced deflection angle: thinner branches deflect wider than the dominant leader */
		float torque_ratio = 1.0f - w_child[c];
		float theta = base_spread * (0.65f + 0.75f * torque_ratio);
		if (c == 0 && ctx->apical > 0.55f)
			theta *= (1.15f - 0.65f * ctx->apical); /* Excurrent leader stays closer to vertical axis */

		/* Golden-angle phyllotaxis rotation around the limb axis (137.508 deg = 2.3999632 rad) */
		float az = roll_az + (float)c * 2.3999632f + ((c == 1) ? 0.55f : 0.0f) +
		           (nd_m3d_rand01(c_seed) - 0.5f) * 0.36f;
		float ca = cosf(az), sa = sinf(az);
		float st = sinf(theta), ct = cosf(theta);

		float lat_x = u_perp[0] * ca + v_perp[0] * sa;
		float lat_y = u_perp[1] * ca + v_perp[1] * sa;
		float lat_z = u_perp[2] * ca + v_perp[2] * sa;

		float cdx = d[0] * ct + lat_x * st;
		float cdy = d[1] * ct + lat_y * st;
		float cdz = d[2] * ct + lat_z * st;

		/* Prevent lateral branches from diving downward into the lower trunk */
		float min_cdz = (ctx->flatness < 0.42f) ? 0.18f : (0.34f - 0.36f * ctx->pitch_norm);
		if (c == 0)
			cdz = fmaxf(cdz, 0.45f + 0.30f * ctx->apical);
		else
			cdz = fmaxf(cdz, min_cdz);

		/* Child segment length scales with area weight and crown spread */
		float len_scale = (order == 0)
			? (0.62f + 0.30f * powf(w_child[c], 0.35f))
			: (0.68f + 0.24f * powf(w_child[c], 0.35f));
		if (ctx->flatness < 0.42f && c > 0)
			len_scale *= 1.18f; /* Wide horizontal reach for umbrella acacia / Scots pine */
		float child_len = len * len_scale * (0.90f + 0.20f * nd_m3d_rand01(c_seed + 71u));

		/* Sparse interior foliage pad at major Order-1->2 fork junctions only when crown is dense */
		if (order == 1 && c == 0 && ctx->flatness >= 0.42f && (c_seed & 1u)) {
			float irx = ctx->w_total * 0.14f;
			float irz = irx * fmaxf(0.32f, fminf(0.75f, ctx->flatness * 0.72f));
			nd_m3d_add_foliage_lobe(b, x1, y1, z1 + irz * 0.35f,
			                        irx, irx * 0.90f, irz,
			                        7, 2, ctx->c_leaf_deep, ctx->c_leaf_mid, ctx->c_leaf_lit, ctx->c_leaf_high,
			                        ctx->h_total, c_seed + 509u, ND_MAT3D_FOLIAGE);
		}

		nd_m3d_build_davinci_limb(b, ctx,
		                          x1, y1, z1,
		                          cdx, cdy, cdz,
		                          child_len, r_child,
		                          order + 1, max_order,
		                          az + 2.3999632f, c_seed);
	}
}

static inline uint32_t
nd_m3d_pack_color4(PlantColor4 c)
{
	int r = (int)(plant_clampf(c.r, 0.0f, 1.0f) * 255.0f + 0.5f);
	int g = (int)(plant_clampf(c.g, 0.0f, 1.0f) * 255.0f + 0.5f);
	int b = (int)(plant_clampf(c.b, 0.0f, 1.0f) * 255.0f + 0.5f);
	return 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

typedef struct {
	nd_mesh3d_builder_t *builder;
	nd_mesh3d_slice_t   *out_slice;
	float                target_w_wu;
	float                target_h_wu;
} PlantGeometryOutput;

/* =========================================================================
 * Stage 4 Geometry Consumer: plant_geometry_build()
 * =========================================================================
 * Pure consumer of a developed PlantInstance (PlantGenome + PlantPhenotype MTG).
 * Traverses:
 *   1. phenotype.axes[] -> phenotype.units[] (first_unit .. next_in_axis)
 *      to sweep continuous Da Vinci tapered bark tubes with root-collar buttress
 *      fluting and branch collar junctions.
 *   2. phenotype.organs[] (PLANT_ORGAN_LEAF, PLANT_ORGAN_FLOWER, PLANT_ORGAN_FRUIT)
 *      to emit species-accurate 3D foliage clusters, cupped leaf blades,
 *      pectinate conifer needle sprays, blossoms, umbels, cones, and berries.
 */
static inline void
plant_geometry_build(const PlantInstance *plant,
                     uint32_t detail_level,
                     void *geometry_output)
{
	if (!plant || !geometry_output)
		return;
	PlantGeometryOutput *out = (PlantGeometryOutput *)geometry_output;
	nd_mesh3d_builder_t *b = out->builder;
	if (!b)
		return;

	uint32_t v_start = b->vert_count;
	uint32_t i_start = b->idx_count;
	const PlantGenome *g = &plant->genome;
	const PlantPhenotype *pheno = &plant->phenotype;

	float wu_per_m = ND_MESH3D_WU_PER_METER;
	float h_total = (out->target_h_wu > 1.0f) ? out->target_h_wu : fmaxf(12.0f, pheno->height_m * wu_per_m);
	float w_total = (out->target_w_wu > 1.0f) ? out->target_w_wu : fmaxf(10.0f, pheno->crown_radius_m * 2.0f * wu_per_m);
	if (pheno->height_m > 1e-3f && out->target_h_wu > 1.0f) {
		wu_per_m = (out->target_h_wu * 0.88f) / pheno->height_m;
	}

	uint32_t c_bark_dark   = nd_m3d_pack_color4(g->allometry.bark_dark);
	uint32_t c_bark_mid    = nd_m3d_pack_color4(g->allometry.bark_mid);
	uint32_t c_bark_light  = nd_m3d_pack_color4(g->allometry.bark_light);
	uint32_t c_leaf_deep   = nd_m3d_pack_color4(g->leaf.adaxial_deep);
	uint32_t c_leaf_mid    = nd_m3d_pack_color4(g->leaf.adaxial_mid);
	uint32_t c_leaf_lit    = nd_m3d_pack_color4(g->leaf.adaxial_lit);
	uint32_t c_leaf_high   = nd_m3d_pack_color4(g->leaf.abaxial_high);
	uint32_t c_fl_prim     = nd_m3d_pack_color4(g->flower.primary_color);
	uint32_t c_fl_sec      = nd_m3d_pack_color4(g->flower.secondary_color);
	uint32_t c_fr_imm      = nd_m3d_pack_color4(g->fruit.immature_color);
	uint32_t c_fr_mat      = nd_m3d_pack_color4(g->fruit.mature_color);

	uint8_t bark_class = g->allometry.bark_class;
	float crook_mag = 0.03f + 0.14f * g->architecture.tortuosity;
	uint8_t max_order_draw = (detail_level >= 2u) ? 1u : ((detail_level == 0u) ? 3u : 2u);
	uint32_t bough_clusters_emitted = 0u;
	uint32_t inst_seed = (uint32_t)(plant->instance_id ^ (plant->genome.genetic_seed >> 17));
	bool is_massart_conifer = (g->leaf.complexity == PLANT_LEAF_NEEDLE &&
	                           g->architecture.model == PLANT_ARCH_MASSART);
	bool is_scots_pine = (g->leaf.complexity == PLANT_LEAF_NEEDLE &&
	                      !is_massart_conifer);
	bool is_acacia = (g->leaf.complexity == PLANT_LEAF_BIPINNATE);
	bool is_birch  = (bark_class == ND_PLANT_BARK_BIRCH);
	bool is_olive  = (bark_class == ND_PLANT_BARK_FLUTED);
	bool is_willow = (g->architecture.orthotropy < -0.55f);
	bool is_cactus = (bark_class == ND_PLANT_BARK_RIBBED ||
	                  g->leaf.complexity == PLANT_LEAF_SCALE);
	if (is_cactus) {
		bark_class = ND_PLANT_BARK_RIBBED;
		/* Columnar cactus leaf genes represent tan spine color; photosynthetic stem is glaucous green */
		c_leaf_deep = 0xFF1A4222u;
		c_leaf_mid  = 0xFF286434u;
		c_leaf_lit  = 0xFF3A8446u;
		c_leaf_high = 0xFF58A664u;
	}
	bool is_graminoid = (g->leaf.shape == PLANT_LEAF_SHAPE_LINEAR &&
	                     g->leaf.venation_type == PLANT_VENATION_PARALLELODROMOUS);
	bool is_herb = (!is_graminoid &&
	                !is_cactus &&
	                g->architecture.max_branch_order <= 2u &&
	                g->physiology.mature_height_m < 1.5f);
	float tree_fol_base = (g->leaf.complexity == PLANT_LEAF_NEEDLE)
		? ND_MAT3D_FOL_CONIFER
		: ((g->leaf.complexity == PLANT_LEAF_BIPINNATE)
			? ND_MAT3D_FOL_BLADE
			: ((g->leaf.shape == PLANT_LEAF_SHAPE_PINNATELY_LOBED ||
			    g->leaf.shape == PLANT_LEAF_SHAPE_PALMATELY_LOBED)
				? ND_MAT3D_FOL_LOBED
				: ND_MAT3D_FOL_OVATE));

	/* Normalize pipe-model trunk base radius to authentic forestry DBH-to-height slenderness ratios
	 * while preserving Da Vinci relative child-to-parent taper across all branch orders:
	 *   - Birches (Betula pendula / pubescens): DBH = 3.8% of H (slender chalk-white trunks)
	 *   - Conifers (Pseudotsuga, Abies, Pinus): DBH = 4.6%..5.0% of H (tall forest spires)
	 *   - Acacia senegal:                       DBH = 5.6% of H (lean savannah bole)
	 *   - Maple / Willow:                       DBH = 6.6%..7.2% of H
	 *   - Ancient Oak / Twisted Olive:          DBH = 8.4%..9.0% of H
	 *   - Columnar Cactus (Arthrocereus):       DBH = 11.5% of H (slender ribbed xerophytic column) */
	float pheno_r0_wu = (pheno->unit_count > 0u)
		? fmaxf(0.15f, pheno->units[0].base_radius_m * wu_per_m)
		: 1.0f;
	float target_trunk_r0_wu = pheno_r0_wu;
	if (!is_herb && !is_graminoid) {
		float target_dbh_ratio = 0.068f;
		if (is_birch)                target_dbh_ratio = 0.038f;
		else if (is_massart_conifer) target_dbh_ratio = 0.046f;
		else if (is_scots_pine)      target_dbh_ratio = 0.050f;
		else if (is_acacia)          target_dbh_ratio = 0.056f;
		else if (is_willow)          target_dbh_ratio = 0.072f;
		else if (is_olive)           target_dbh_ratio = 0.088f;
		else if (is_cactus)          target_dbh_ratio = 0.115f;
		else if (g->leaf.shape == PLANT_LEAF_SHAPE_PINNATELY_LOBED)
			target_dbh_ratio = 0.084f; /* Quercus robur */
		else
			target_dbh_ratio = 0.066f; /* Acer pseudoplatanus */
		target_trunk_r0_wu = fmaxf(1.2f, h_total * (0.5f * target_dbh_ratio));
	}

	/* 0. Ground-hugging basal rosette for herbaceous wildflowers/crops (Linum, Daucus, Solanum, Bellis)
	 *    and sweeping basal wetland sword-blades for graminoid reeds (Typha latifolia). */
	if (is_herb) {
		int n_rosette = (detail_level == 0u) ? 7 : 5;
		for (int i = 0; i < n_rosette; i++) {
			float ang = ((float)i / (float)n_rosette) * 6.2831853f + (float)(inst_seed & 7u) * 0.25f;
			float ca = cosf(ang), sa = sinf(ang);
			nd_m3d_add_arching_blade(b, 0.0f, 0.0f, 0.6f, ca, sa,
			                         w_total * (0.36f + 0.05f * (float)(i & 1)),
			                         h_total * ((g->architecture.model == PLANT_ARCH_HOLTTUM) ? 0.20f : 0.26f),
			                         h_total * 0.10f,
			                         w_total * 0.105f,
			                         c_leaf_deep, c_leaf_mid, c_leaf_lit, h_total);
		}
		if (g->fruit.fruit_type == PLANT_FRUIT_BERRY) {
			/* Dense leafy bush rosette of intersecting branchlet cards for fruiting Solanum vine */
			nd_m3d_add_foliage_bough_cluster_ex(b, 0.0f, 0.0f, h_total * 0.44f,
			                                    w_total * 0.36f, w_total * 0.34f, h_total * 0.32f,
			                                    ND_MAT3D_FOL_OVATE,
			                                    c_leaf_deep, c_leaf_mid, c_leaf_lit, c_leaf_high,
			                                    h_total, inst_seed + 173u);
		}
	} else if (is_graminoid) {
		int n_blades = (detail_level == 0u) ? 8 : 6;
		for (int i = 0; i < n_blades; i++) {
			float ang = ((float)i / (float)n_blades) * 6.2831853f + (float)(inst_seed & 7u) * 0.22f;
			float ca = cosf(ang), sa = sinf(ang);
			nd_m3d_add_arching_blade(b, ca * w_total * 0.07f, sa * w_total * 0.07f, 0.0f,
			                         ca, sa,
			                         w_total * (0.28f + 0.07f * (float)(i & 1)),
			                         h_total * (0.64f + 0.10f * (float)(i % 3)),
			                         h_total * 0.16f,
			                         w_total * 0.068f,
			                         c_leaf_deep, c_leaf_mid, (i & 1) ? c_leaf_high : c_leaf_lit,
			                         h_total);
		}
		/* Erect wetland cattail culms topped with velvety dark-chocolate cylindrical female spadix
		 * and slender golden-tan apical male staminate spike (Typha latifolia) */
		int n_culms = (detail_level == 0u) ? 3 : 2;
		uint32_t c_spadix_dark = 0xFF101C34u;
		uint32_t c_spadix_mid  = 0xFF1A2E52u;
		uint32_t c_spadix_lit  = 0xFF284472u;
		uint32_t c_male_spike  = 0xFF4288B4u;
		for (int c = 0; c < n_culms; c++) {
			float c_ang = ((float)c / (float)n_culms) * 6.2831853f + 0.55f + (float)(inst_seed & 3u) * 0.25f;
			float cx0 = cosf(c_ang) * (w_total * 0.06f);
			float cy0 = sinf(c_ang) * (w_total * 0.06f);
			float cx1 = cosf(c_ang) * (w_total * (0.11f + 0.03f * (float)c));
			float cy1 = sinf(c_ang) * (w_total * (0.11f + 0.03f * (float)c));
			float ch  = h_total * (0.94f - 0.05f * (float)c);
			float sp_z0 = ch * 0.62f;
			float sp_z1 = ch * 0.86f;
			float sp_x0 = cx0 + (cx1 - cx0) * 0.62f;
			float sp_y0 = cy0 + (cy1 - cy0) * 0.62f;
			float sp_x1 = cx0 + (cx1 - cx0) * 0.86f;
			float sp_y1 = cy0 + (cy1 - cy0) * 0.86f;
			float culm_r = fmaxf(0.8f, w_total * 0.016f);
			float spad_r = fmaxf(1.8f, w_total * 0.044f);
			/* Green cylindrical reed culm */
			nd_m3d_add_trunk_tube_ex(b, cx0, cy0, 0.0f, culm_r * 1.15f,
			                         sp_x0, sp_y0, sp_z0, culm_r,
			                         5, 2, c_leaf_deep, c_leaf_mid, c_leaf_lit,
			                         ND_PLANT_BARK_HERB, 0.01f, h_total, inst_seed + (uint32_t)c * 59u);
			/* Plush dark-brown cylindrical cattail spadix ("cigar" head) */
			nd_m3d_add_trunk_tube_ex(b, sp_x0, sp_y0, sp_z0, spad_r,
			                         sp_x1, sp_y1, sp_z1, spad_r * 0.95f,
			                         7, 2, c_spadix_dark, c_spadix_mid, c_spadix_lit,
			                         ND_PLANT_BARK_HERB, 0.004f, h_total, inst_seed + (uint32_t)c * 83u);
			/* Slender apical male staminate spike above the brown spadix */
			nd_m3d_add_trunk_tube_ex(b, sp_x1, sp_y1, sp_z1, culm_r * 0.90f,
			                         cx1, cy1, ch, culm_r * 0.35f,
			                         5, 1, c_spadix_mid, c_male_spike, c_male_spike,
			                         ND_PLANT_BARK_HERB, 0.004f, h_total, inst_seed + (uint32_t)c * 107u);
		}
	}

	/* Count primary trunk nodes & identify true terminal shoots (units with no drawn woody child branches) */
	uint32_t trunk_node_count = 0u;
	bool has_woody_child[PLANT_MAX_UNITS];
	memset(has_woody_child, 0, sizeof(has_woody_child));
	uint8_t woody_order_cap = (is_massart_conifer && max_order_draw > 2u) ? 2u : max_order_draw;
	for (uint32_t u_idx = 0u; u_idx < pheno->unit_count; u_idx++) {
		const PlantGrowthUnit *u = &pheno->units[u_idx];
		if (u->branch_order == 0u &&
		    (uint32_t)(u->node_index + 1u) > trunk_node_count) {
			trunk_node_count = (uint32_t)(u->node_index + 1u);
		}
		if (u->branch_order <= woody_order_cap &&
		    u->parent_unit != PLANT_INVALID_ID &&
		    u->parent_unit < PLANT_MAX_UNITS) {
			has_woody_child[u->parent_unit] = true;
		}
	}
	if (trunk_node_count == 0u)
		trunk_node_count = 1u;

	/* 1. Traverse MTG PlantAxes in ascending branch order (0 -> 1 -> 2 -> 3) to sweep smooth
	 *    Catmull-Rom parallel-transported bark/stem tubes:
	 *    - Recording each unit's exact rendered tip position & radius guarantees child axes
	 *      socket seamlessly into the actual curved parent limb centerline (never floating or
	 *      poking through the far side of a bowed trunk).
	 *    - Every axis shares welded vertex rings along its entire length with zero polygon gaps.
	 *    - Step 2 foliage organs also lock directly onto the recorded rendered twig tips. */
	float max_r_env = w_total * (is_acacia ? 0.48f : 0.44f);
	uint32_t bd = (bark_class == ND_PLANT_BARK_RIBBED) ? c_leaf_deep : c_bark_dark;
	uint32_t bm = (bark_class == ND_PLANT_BARK_RIBBED) ? c_leaf_mid  : c_bark_mid;
	uint32_t bl = (bark_class == ND_PLANT_BARK_RIBBED) ? c_leaf_high : c_bark_light;

	float unit_tip_pos[PLANT_MAX_UNITS][3];
	float unit_tip_rad[PLANT_MAX_UNITS];
	uint8_t unit_tip_valid[PLANT_MAX_UNITS];
	memset(unit_tip_valid, 0, sizeof(unit_tip_valid));

	for (uint8_t pass_ord = 0u; pass_ord <= max_order_draw; pass_ord++) {
		if (is_massart_conifer && pass_ord > 2u)
			continue;
		for (uint32_t a_idx = 0u; a_idx < pheno->axis_count; a_idx++) {
			const PlantAxis *ax = &pheno->axes[a_idx];
			if (!ax->alive || ax->is_root_axis || ax->first_unit == PLANT_INVALID_ID)
				continue;
			if (ax->order != pass_ord)
				continue;
			if ((is_herb || is_graminoid) && ax->order > 0u)
				continue;

			float pts[24][3];
			float radii[24];
			int n_pts = 0;

			/* Delta shift to keep child axis base locked onto its parent's exact curved spine */
			float shift_x = 0.0f, shift_y = 0.0f, shift_z = 0.0f;
			float p_rad_socket = 0.0f;
			if (ax->order >= 1u && ax->parent_unit != PLANT_INVALID_ID &&
			    ax->parent_unit < PLANT_MAX_UNITS && unit_tip_valid[ax->parent_unit]) {
				const PlantGrowthUnit *u0 = &pheno->units[ax->first_unit];
				float raw_x0 = u0->base_pos.x * wu_per_m;
				float raw_y0 = u0->base_pos.y * wu_per_m;
				float raw_z0 = u0->base_pos.z * wu_per_m;
				float rh = sqrtf(raw_x0 * raw_x0 + raw_y0 * raw_y0);
				if (rh > max_r_env * 0.75f && rh > 1e-3f) {
					float ex = (rh - max_r_env * 0.75f) / (max_r_env * 0.25f);
					float rs = max_r_env * (0.75f + 0.25f * tanhf(ex));
					raw_x0 *= rs / rh;
					raw_y0 *= rs / rh;
				}
				shift_x = unit_tip_pos[ax->parent_unit][0] - raw_x0;
				shift_y = unit_tip_pos[ax->parent_unit][1] - raw_y0;
				shift_z = unit_tip_pos[ax->parent_unit][2] - raw_z0;
				p_rad_socket = unit_tip_rad[ax->parent_unit];
			}

			PlantUnitId uid = ax->first_unit;
			uint32_t guard = 0u;
			float prev_x = 0.0f, prev_y = 0.0f, prev_z = 0.0f;
			while (uid != PLANT_INVALID_ID && uid < pheno->unit_count && guard < 20u) {
				const PlantGrowthUnit *u = &pheno->units[uid];
				guard++;

				float x0 = u->base_pos.x * wu_per_m;
				float y0 = u->base_pos.y * wu_per_m;
				float z0 = u->base_pos.z * wu_per_m;
				float x1 = u->tip_pos.x * wu_per_m;
				float y1 = u->tip_pos.y * wu_per_m;
				float z1 = u->tip_pos.z * wu_per_m;

				/* Smooth soft-knee radial envelope matching organ attachment coordinates */
				float r_h0 = sqrtf(x0 * x0 + y0 * y0);
				if (r_h0 > max_r_env * 0.75f && r_h0 > 1e-3f) {
					float excess = (r_h0 - max_r_env * 0.75f) / (max_r_env * 0.25f);
					float r_soft = max_r_env * (0.75f + 0.25f * tanhf(excess));
					x0 *= r_soft / r_h0;
					y0 *= r_soft / r_h0;
				}
				float r_h1 = sqrtf(x1 * x1 + y1 * y1);
				if (r_h1 > max_r_env * 0.75f && r_h1 > 1e-3f) {
					float excess = (r_h1 - max_r_env * 0.75f) / (max_r_env * 0.25f);
					float r_soft = max_r_env * (0.75f + 0.25f * tanhf(excess));
					x1 *= r_soft / r_h1;
					y1 *= r_soft / r_h1;
				}

				/* Propagate parent spine alignment so forks remain 100% welded */
				x0 += shift_x; y0 += shift_y; z0 += shift_z;
				x1 += shift_x; y1 += shift_y; z1 += shift_z;
				if (n_pts > 0) {
					x0 = prev_x; y0 = prev_y; z0 = prev_z;
				}

				/* Species-specific architectural spine shaping */
				if (is_acacia) {
					if (ax->order == 0u) {
						/* Acacia senegal: low sympodial bole terminating at ~34% height before splitting into V-vase */
						float z_cap = h_total * 0.36f;
						if (z1 > z_cap) {
							float t_node = (float)(u->node_index + 1u) / (float)trunk_node_count;
							z1 = h_total * (0.14f + 0.22f * t_node);
							float fork_ang = (float)(inst_seed & 15u) * 0.4f;
							x1 += cosf(fork_ang) * (w_total * 0.03f * t_node);
							y1 += sinf(fork_ang) * (w_total * 0.03f * t_node);
						}
					} else if (ax->order == 1u) {
						/* Ascending V-shaped savannah vase scaffold limbs distributed around the 360-deg compass
						 * and rising uniformly to the flat 78..85% H umbrella tabletop */
						float target_ang = (float)a_idx * 2.3999632f + (float)(inst_seed & 7u) * 0.35f;
						float cur_ang = atan2f(y1, x1);
						float blend_ang = target_ang + 0.25f * sinf(cur_ang - target_ang);
						float step_t = plant_clampf((float)(guard) / 3.0f, 0.33f, 1.0f);
						float target_r = w_total * (0.14f + 0.26f * step_t);
						x1 = cosf(blend_ang) * target_r;
						/* Compress Y depth spread slightly so 55-deg diorama projection reads as a flat horizontal table */
						y1 = sinf(blend_ang) * target_r * 0.58f;
						z1 = h_total * (0.36f + 0.47f * powf(step_t, 0.78f));
					} else {
						/* Secondary/tertiary flat horizontal table twigs fanning across the upper umbrella stratum */
						float sub_ang = (float)(a_idx * 137u + guard * 53u) * 0.045f;
						float step_len = w_total * ((ax->order == 2u) ? 0.11f : 0.08f);
						x1 = x0 + cosf(sub_ang) * step_len;
						y1 = y0 + sinf(sub_ang) * step_len * 0.58f;
						float r_xy = hypotf(x1, y1 / 0.58f);
						if (r_xy > w_total * 0.46f && r_xy > 1e-3f) {
							float sc = (w_total * 0.46f) / r_xy;
							x1 *= sc;
							y1 *= sc;
						}
						z1 = plant_clampf(z0 + h_total * 0.025f, h_total * 0.68f, h_total * 0.89f);
					}
				} else if (is_scots_pine) {
					/* Pinus sylvestris: high bare salmon-ochre lower trunk (first 56% clear of live limbs)
					 * with irregular picturesque crooked upper scaffold crown (56%..94% H) */
					if (ax->order == 0u) {
						float t_node = (float)(u->node_index + 1u) / (float)trunk_node_count;
						if (t_node >= 0.35f) {
							float lift = h_total * (0.54f + 0.36f * ((t_node - 0.35f) / 0.65f));
							z1 = fmaxf(z1, lift);
						}
					} else {
						if (z1 < h_total * 0.56f) {
							z1 = h_total * 0.56f + (z1 - h_total * 0.30f) * 0.55f;
						}
					}
				} else if (is_massart_conifer && ax->order >= 1u) {
					/* Skyrim Pine / Douglas Fir / Silver Fir: sweeping downward-angled conifer boughs
					 * with subtle upturned distal tips */
					float seg_horiz = hypotf(x1 - x0, y1 - y0);
					float droop = (ax->order == 1u) ? (-seg_horiz * 0.24f) : (-seg_horiz * 0.14f);
					z1 = fmaxf(h_total * 0.10f, z0 + droop);
				}

				/* Normalized botanical radius preserving Da Vinci taper across branch orders */
				float r0_raw = fmaxf(0.10f, u->base_radius_m * wu_per_m);
				float r1_raw = fmaxf(0.08f, u->tip_radius_m * wu_per_m);
				float r0, r1;
				if (is_herb || is_graminoid) {
					r0 = fmaxf(0.38f, r0_raw * 0.65f);
					r1 = fmaxf(0.26f, r1_raw * 0.65f);
				} else if (is_cactus) {
					r0 = target_trunk_r0_wu * powf(plant_clampf(r0_raw / pheno_r0_wu, 0.35f, 1.0f), 0.45f);
					r1 = target_trunk_r0_wu * powf(plant_clampf(r1_raw / pheno_r0_wu, 0.32f, 1.0f), 0.45f);
				} else {
					float ratio0 = plant_clampf(r0_raw / pheno_r0_wu, 0.03f, 1.0f);
					float ratio1 = plant_clampf(r1_raw / pheno_r0_wu, 0.02f, 1.0f);
					float exp_t  = (ax->order == 0u) ? 0.74f : 0.86f;
					float ord_atten = (ax->order == 0u) ? 1.0f : ((ax->order == 1u) ? 0.72f : ((ax->order == 2u) ? 0.52f : 0.38f));
					r0 = fmaxf(0.45f, target_trunk_r0_wu * powf(ratio0, exp_t) * ord_atten);
					r1 = fmaxf(0.32f, target_trunk_r0_wu * powf(ratio1, exp_t) * ord_atten);
				}
				uint32_t useed = (uint32_t)(u->topo_hash ^ (u->topo_hash >> 32));

				/* Gentle organic gnarl/bow on trunk and lateral limbs while tracking exact endpoint */
				float seg_dx = x1 - x0, seg_dy = y1 - y0, seg_dz = z1 - z0;
				float seg_len = sqrtf(seg_dx * seg_dx + seg_dy * seg_dy + seg_dz * seg_dz);

				if (n_pts == 0) {
					if (ax->order >= 1u) {
						float p_rad = (p_rad_socket > 0.1f) ? p_rad_socket : (r0 * 1.35f);
						if (p_rad < r0) p_rad = r0;
						/* Clamp child radius strictly inside parent limb radius so forks look natural */
						if (r0 > p_rad * 0.78f) r0 = p_rad * 0.78f;
						if (r1 > r0 * 0.88f)    r1 = r0 * 0.88f;
						/* Tuck child base cleanly inside parent centerline (<= 0.42 * p_rad so it never pokes through back) */
						float tuck = (seg_len > 1e-3f) ? fminf(p_rad * 0.42f, seg_len * 0.22f) : 0.0f;
						float inv_l = (seg_len > 1e-3f) ? (1.0f / seg_len) : 0.0f;
						pts[0][0] = x0 - seg_dx * inv_l * tuck;
						pts[0][1] = y0 - seg_dy * inv_l * tuck;
						pts[0][2] = z0 - seg_dz * inv_l * tuck;
						radii[0]  = fminf(p_rad * 0.86f, r0 * 1.12f);
						n_pts = 1;
						/* Smooth branch-collar transition ring at 20% along primary bough internode */
						if (ax->order == 1u && seg_len > r0 * 1.6f && n_pts < 22) {
							pts[n_pts][0] = x0 + seg_dx * 0.20f;
							pts[n_pts][1] = y0 + seg_dy * 0.20f;
							pts[n_pts][2] = z0 + seg_dz * 0.20f;
							radii[n_pts]  = fminf(p_rad * 0.80f, r0 * 0.96f);
							n_pts++;
						}
					} else {
						pts[0][0] = x0; pts[0][1] = y0; pts[0][2] = z0;
						radii[0]  = r0;
						n_pts = 1;
					}
				}

				/* Subtle organic mid-span curvature on trunk and primary boughs */
				if (ax->order <= 1u && !is_herb && !is_graminoid && n_pts + 2 < 23 && seg_len > r0 * 2.1f) {
					float bow_ang = ((float)(useed & 255u) / 255.0f) * 6.2831853f;
					float bow_scale = (ax->order == 0u)
						? ((is_olive || is_scots_pine || is_acacia) ? 0.36f : 0.22f)
						: 0.22f;
					float bow = seg_len * crook_mag * bow_scale;
					/* Subtle gravitropic sag on conifer boughs or ascending lift on broadleaf limbs */
					float z_arch = (ax->order >= 1u)
						? (is_massart_conifer ? (-seg_len * 0.08f) : (seg_len * 0.04f))
						: 0.0f;
					pts[n_pts][0] = 0.5f * (x0 + x1) + cosf(bow_ang) * bow;
					pts[n_pts][1] = 0.5f * (y0 + y1) + sinf(bow_ang) * bow;
					pts[n_pts][2] = 0.5f * (z0 + z1) + z_arch;
					radii[n_pts]  = 0.5f * (r0 + r1);
					n_pts++;
				}

				if (n_pts < 23) {
					pts[n_pts][0] = x1;
					pts[n_pts][1] = y1;
					pts[n_pts][2] = z1;
					radii[n_pts]  = r1;
					n_pts++;
				}

				/* Record exact rendered unit tip for child-axis sockets and Step 2 foliage anchoring */
				if (uid < PLANT_MAX_UNITS) {
					unit_tip_pos[uid][0] = x1;
					unit_tip_pos[uid][1] = y1;
					unit_tip_pos[uid][2] = z1;
					unit_tip_rad[uid]    = r1;
					unit_tip_valid[uid]  = 1u;
				}
				prev_x = x1; prev_y = y1; prev_z = z1;

				/* Subtle ground-hugging root-collar spurs on ancient buttressed trees (Quercus, Olea, Acer) */
				if (u->branch_order == 0u && u->node_index == 0u && detail_level == 0u &&
				    !is_herb && !is_graminoid && !is_cactus && !is_birch &&
				    g->allometry.root_flare_strength > 0.38f) {
					int n_spurs = 3;
					for (int sp = 0; sp < n_spurs; sp++) {
						float sp_ang = ((float)sp / (float)n_spurs) * 6.2831853f + (float)(useed & 31u) * 0.1f;
						float sp_reach = r0 * (1.28f + 0.32f * g->allometry.root_flare_strength);
						float spts[3][3] = {
							{ x0, y0, z0 + fminf(h_total * 0.045f, (z1 - z0) * 0.32f) },
							{ x0 + cosf(sp_ang) * sp_reach * 0.58f, y0 + sinf(sp_ang) * sp_reach * 0.58f, z0 + fminf(h_total * 0.016f, (z1 - z0) * 0.12f) },
							{ x0 + cosf(sp_ang) * sp_reach, y0 + sinf(sp_ang) * sp_reach, 0.0f }
						};
						float srads[3] = { r0 * 0.38f, r0 * 0.24f, r0 * 0.10f };
						nd_m3d_add_axis_spline_tube(b, spts, srads, 3, 5, 1,
						                            bd, bm, bl, bark_class, h_total, useed + (uint32_t)sp * 41u);
					}
				}

				/* Dead self-pruned lower conifer branch stubs on clear lower bole of Scots Pine & Douglas Fir */
				if (u->branch_order == 0u && u->node_index <= (is_scots_pine ? 2u : 1u) && detail_level == 0u &&
				    g->leaf.complexity == PLANT_LEAF_NEEDLE) {
					for (int st = 0; st < 2; st++) {
						float st_ang = (float)(u->node_index * 2u + (uint32_t)st) * 2.15f + (float)(useed & 15u) * 0.2f;
						float st_z = z0 + (z1 - z0) * (0.35f + 0.40f * (float)st);
						float st_len = r0 * 2.4f;
						float st_pts[3][3] = {
							{ x0 + (x1 - x0) * 0.5f, y0 + (y1 - y0) * 0.5f, st_z },
							{ x0 + cosf(st_ang) * st_len * 0.55f, y0 + sinf(st_ang) * st_len * 0.55f, st_z - st_len * 0.14f },
							{ x0 + cosf(st_ang) * st_len, y0 + sinf(st_ang) * st_len, st_z - st_len * 0.32f }
						};
						float st_rads[3] = { r1 * 0.34f, r1 * 0.20f, r1 * 0.08f };
						nd_m3d_add_axis_spline_tube(b, st_pts, st_rads, 3, 4, 1,
						                            bd, bd, bm, bark_class, h_total, useed + (uint32_t)st * 79u);
					}
				}

				/* Rounded succulent dome apex + sub-apical nocturnal cactus blossom on terminal cactus columns */
				if (is_cactus && u->next_in_axis == PLANT_INVALID_ID) {
					r1 *= 0.78f;
					if (n_pts > 0)
						radii[n_pts - 1] = r1;
					if (n_pts < 22) {
						pts[n_pts][0] = x1;
						pts[n_pts][1] = y1;
						pts[n_pts][2] = z1 + r1 * 0.52f;
						radii[n_pts]  = r1 * 0.48f;
						n_pts++;
						pts[n_pts][0] = x1;
						pts[n_pts][1] = y1;
						pts[n_pts][2] = z1 + r1 * 0.82f;
						radii[n_pts]  = 0.08f;
						n_pts++;
					}
					float c_fl_r = fmaxf(2.2f, r1 * 0.82f);
					float fl_ang = (float)(a_idx * 2u + 1u) * 1.1f;
					nd_m3d_add_blossom_star(b,
					                        x1 + cosf(fl_ang) * (r1 * 0.55f),
					                        y1 + sinf(fl_ang) * (r1 * 0.55f),
					                        z1 + r1 * 0.45f,
					                        c_fl_r, c_fl_r * 0.42f,
					                        0xFFF4F2FCu, 0xFFD0A8E8u, 0xFF38D4F8u, h_total);
				}

				/* Conifer apical leader crown spire + upper sub-apical whorls on the topmost trunk node */
				if (is_massart_conifer && u->branch_order == 0u && u->next_in_axis == PLANT_INVALID_ID) {
					float spire_r = w_total * 0.14f;
					for (int sp = 0; sp < 4; sp++) {
						float sp_ang = (float)sp * 1.5707963f + (float)(useed & 7u) * 0.35f;
						float ca = cosf(sp_ang), sa = sinf(sp_ang);
						nd_m3d_add_branchlet_card(b, x1, y1, z1 - spire_r * 0.25f,
						                          ca * 0.48f, sa * 0.48f, 0.86f,
						                          spire_r * 1.55f, spire_r * 0.14f, spire_r * 0.10f,
						                          spire_r * 0.56f, spire_r * 0.10f, (sp & 1) ? 0.28f : -0.28f,
						                          x1, y1, z1 - spire_r * 0.20f,
						                          ND_MAT3D_FOL_CONIFER,
						                          c_leaf_deep, c_leaf_mid, c_leaf_lit, c_leaf_high, h_total);
					}
				}

				uid = u->next_in_axis;
			}

			/* Close tapered terminal twig apex so branch tips never look like hollow open cylinders */
			if (n_pts >= 2 && n_pts < 24 && bark_class != ND_PLANT_BARK_RIBBED) {
				float tx = pts[n_pts - 1][0] - pts[n_pts - 2][0];
				float ty = pts[n_pts - 1][1] - pts[n_pts - 2][1];
				float tz = pts[n_pts - 1][2] - pts[n_pts - 2][2];
				float tlen = sqrtf(tx * tx + ty * ty + tz * tz);
				if (tlen > 1e-4f) {
					float cap_ext = fmaxf(0.45f, radii[n_pts - 1] * 0.90f);
					pts[n_pts][0] = pts[n_pts - 1][0] + (tx / tlen) * cap_ext;
					pts[n_pts][1] = pts[n_pts - 1][1] + (ty / tlen) * cap_ext;
					pts[n_pts][2] = pts[n_pts - 1][2] + (tz / tlen) * cap_ext;
					radii[n_pts]  = 0.08f;
					n_pts++;
				}
			}

			if (n_pts >= 2) {
				int sides = (ax->order == 0u)
					? ((bark_class == ND_PLANT_BARK_RIBBED) ? 8 : ((is_herb || is_graminoid) ? 5 : 8))
					: ((ax->order == 1u) ? (is_massart_conifer ? 5 : 6) : 4);
				uint32_t aseed = (uint32_t)(ax->topo_hash ^ (ax->topo_hash >> 32));
				nd_m3d_add_axis_spline_tube(b, pts, radii, n_pts, sides,
				                            (ax->order >= 1u) ? 1 : 0,
				                            bd, bm, bl, bark_class, h_total, aseed);
			}
		}
	}

	/* 2. Traverse MTG PlantOrgans to emit species-accurate leaves, needles, flowers, and fruit */
	uint32_t max_clusters = (detail_level == 0u) ? 30u : 15u;
	for (uint32_t o_idx = 0u; o_idx < pheno->organ_count; o_idx++) {
		const PlantOrgan *org = &pheno->organs[o_idx];
		uint32_t oseed = (uint32_t)(org->topo_hash ^ (org->topo_hash >> 32));
		float ax = org->attachment_pos.x * wu_per_m;
		float ay = org->attachment_pos.y * wu_per_m;
		float az = org->attachment_pos.z * wu_per_m;

		/* Lock organ attachment directly to its parent unit's exact curved spine tip when available */
		if (org->parent_unit != PLANT_INVALID_ID && org->parent_unit < PLANT_MAX_UNITS &&
		    unit_tip_valid[org->parent_unit]) {
			ax = unit_tip_pos[org->parent_unit][0];
			ay = unit_tip_pos[org->parent_unit][1];
			az = unit_tip_pos[org->parent_unit][2];
		} else {
			float r_ha = sqrtf(ax * ax + ay * ay);
			if (r_ha > max_r_env * 0.75f && r_ha > 1e-3f) {
				float excess = (r_ha - max_r_env * 0.75f) / (max_r_env * 0.25f);
				float r_soft = max_r_env * (0.75f + 0.25f * tanhf(excess));
				ax *= r_soft / r_ha;
				ay *= r_soft / r_ha;
			}
		}
		az = plant_clampf(az, 0.5f, h_total * 0.96f);

		float olen = fmaxf(2.0f, org->length_m * wu_per_m);
		float owid = fmaxf(1.2f, org->width_m * wu_per_m);
		float dx = org->direction.x;
		float dy = org->direction.y;
		/* Align organ fan direction outward from the trunk along the actual curved branch vector */
		if (hypotf(ax, ay) > 2.5f) {
			float r_xy = hypotf(ax, ay);
			dx = dx * 0.35f + (ax / r_xy) * 0.65f;
			dy = dy * 0.35f + (ay / r_xy) * 0.65f;
		}
		float dlen_xy = sqrtf(dx * dx + dy * dy);
		if (dlen_xy > 1e-4f) {
			dx /= dlen_xy;
			dy /= dlen_xy;
		} else {
			float fallback_ang = (float)(oseed & 0xFFu) * (6.2831853f / 256.0f);
			dx = cosf(fallback_ang);
			dy = sinf(fallback_ang);
		}

		if (org->type == PLANT_ORGAN_LEAF) {
			const PlantGrowthUnit *pu = (org->parent_unit < pheno->unit_count)
				? &pheno->units[org->parent_unit] : NULL;
			bool is_axis_tip = (!pu || pu->next_in_axis == PLANT_INVALID_ID);
			bool is_terminal_shoot = (!pu || (pu->id < PLANT_MAX_UNITS && !has_woody_child[pu->id]));

			if (g->leaf.complexity == PLANT_LEAF_SCALE) {
				/* Columnar cactus spine areoles are already represented on the ribbed stem */
				continue;
			} else if (is_massart_conifer) {
				/* Skyrim Falkreath/Riverwood Conifer Bough Spire (Douglas Fir & Silver Fir):
				 * Emits drooping, shingled skirt + cross-fin boughs along both primary whorl limbs
				 * (order 1) and lateral sub-boughs (order 2), tapering smoothly into a conical spire
				 * so successive whorls overlap and eliminate bare telephone-pole trunk gaps. */
				if (org->sub_index != 0u || org->branch_order == 0u)
					continue;
				if (org->branch_order > woody_order_cap)
					continue;
				if (org->branch_order == 2u && !is_terminal_shoot && !is_axis_tip)
					continue;
				float h_t = plant_clampf(az / fmaxf(1.0f, h_total), 0.0f, 1.0f);
				float cone_taper = powf(fmaxf(0.14f, 1.04f - 0.82f * h_t), 0.78f);
				float rx = w_total * 0.30f * cone_taper;
				float ry = rx * 0.94f;
				float rz = rx * 0.48f;
				if (bough_clusters_emitted < max_clusters) {
					bough_clusters_emitted++;
					nd_m3d_add_conifer_bough_fan(b, ax, ay, az,
					                             dx, dy,
					                             rx, ry, rz,
					                             c_leaf_deep, c_leaf_mid, c_leaf_lit, c_leaf_high,
					                             h_total, oseed);
				}
			} else if (is_scots_pine) {
				/* Pinus sylvestris (Scots Pine):
				 * Lower 55% of the salmon-orange trunk is self-pruned and bare; upper crown (58..95% H)
				 * carries wide, domed/tabular pine needle cloud pads on the crooked upper branches. */
				if (org->sub_index != 0u || !is_terminal_shoot)
					continue;
				if (az < h_total * 0.54f)
					az = h_total * 0.56f + (az * 0.25f);
				float rx = w_total * 0.23f;
				float ry = rx * 0.94f;
				float rz = rx * 0.44f;
				if (bough_clusters_emitted < max_clusters) {
					bough_clusters_emitted++;
					nd_m3d_add_conifer_bough_fan(b, ax, ay, az,
					                             dx, dy,
					                             rx, ry, rz,
					                             c_leaf_deep, c_leaf_mid, c_leaf_lit, c_leaf_high,
					                             h_total, oseed);
				}
			} else if (is_acacia) {
				/* Acacia senegal (Savannah Umbrella Thorn):
				 * Prune all lower/mid-trunk foliage below the V-vase forks and emit wide, thin,
				 * horizontally stratified tabular umbrella pads across the top canopy table (70..94% H)
				 * using fine feathery bipinnate pinnae (ND_MAT3D_FOL_BLADE). */
				if (org->sub_index != 0u || !is_terminal_shoot)
					continue;
				float r_xy = hypotf(ax, ay);
				float r_norm = plant_clampf(r_xy / fmaxf(1.0f, w_total * 0.44f), 0.0f, 1.0f);
				/* Stratified horizontal umbrella table at 74..92% H with slight peripheral rim lift */
				float table_z = h_total * (0.76f + 0.14f * r_norm + ((oseed & 3u) == 0u ? -0.10f : 0.0f));
				az = fmaxf(h_total * 0.64f, 0.25f * az + 0.75f * table_z);
				float rx = w_total * 0.25f;
				float ry = rx * 0.95f;
				float rz = rx * 0.22f;
				if (bough_clusters_emitted < max_clusters) {
					bough_clusters_emitted++;
					nd_m3d_add_tabular_umbrella_fan(b, ax, ay, az,
					                                dx, dy,
					                                rx, ry, rz,
					                                ND_MAT3D_FOL_BLADE,
					                                c_leaf_deep, c_leaf_mid, c_leaf_lit, c_leaf_high,
					                                h_total, oseed);
				}
			} else if (is_graminoid) {
				/* Graminoid basal sword-blades already emitted in step 0; add upper cauline blade on stem tips */
				if (org->sub_index != 0u || !is_axis_tip)
					continue;
				nd_m3d_add_arching_blade(b, ax * 0.45f, ay * 0.45f, az * 0.25f,
				                         dx, dy,
				                         w_total * 0.26f,
				                         fminf(h_total * 0.72f, olen * 1.15f),
				                         olen * 0.20f,
				                         owid * 0.55f,
				                         c_leaf_deep, c_leaf_mid, c_leaf_lit,
				                         h_total);
			} else if (is_herb) {
				/* Cauline stem leaf spray above basal rosette */
				if (org->sub_index != 0u || !is_axis_tip)
					continue;
				float arc_h = olen * ((g->architecture.model == PLANT_ARCH_HOLTTUM) ? 0.22f : 0.34f);
				nd_m3d_add_leaf_frond_spray(b, ax, ay, fminf(az, h_total * 0.58f),
				                            dx, dy,
				                            olen * 0.90f, arc_h, olen * 0.22f,
				                            owid * 0.48f, olen * 0.08f,
				                            c_leaf_deep, c_leaf_mid, c_leaf_lit, c_leaf_high,
				                            h_total);
			} else {
				/* Broadleaf Trees (Birch, Oak, Willow, Sycamore Maple, Olive):
				 * Placed strictly on true terminal shoots (!has_woody_child) with woody stems anchored into
				 * the terminal fork and fanning outward along the branch growth vector (dx, dy). */
				if (org->sub_index != 0u || !is_terminal_shoot)
					continue;
				float crown_scale = is_birch ? 0.19f : (is_olive ? 0.22f : 0.24f);
				float flat_aspect = is_olive ? 0.42f : (is_birch ? 0.54f : 0.62f);
				float rx = w_total * crown_scale;
				float ry = rx * 0.94f;
				float rz = rx * flat_aspect;
				float cz = fminf(h_total * 0.94f, az + rz * 0.18f);

				if (bough_clusters_emitted < max_clusters) {
					bough_clusters_emitted++;
					/* Branch-aligned 4-sheet shingled bough fan anchored directly at the terminal woody twig */
					nd_m3d_add_foliage_bough_fan_ex(b, ax, ay, az,
					                                dx, dy,
					                                rx, ry, rz,
					                                tree_fol_base,
					                                c_leaf_deep, c_leaf_mid, c_leaf_lit, c_leaf_high,
					                                h_total, oseed);
					/* Weeping species (Salix babylonica, Betula pendula): cascading pendulous branchlet curtains
					 * using alpha-cutout compound foliage cards pitched steeply downward along gravity */
					if (g->architecture.orthotropy < -0.25f && (is_willow || (bough_clusters_emitted & 1u))) {
						int n_curtains = is_willow ? 2 : 1;
						for (int wc = 0; wc < n_curtains; wc++) {
							float c_ang = atan2f(dy, dx) + (float)wc * 0.95f - 0.42f;
							float cca = cosf(c_ang), csa = sinf(c_ang);
							float drop_len = is_willow
								? fminf(cz - h_total * 0.08f, h_total * (0.42f + 0.06f * (float)(wc & 1)))
								: fminf(cz - h_total * 0.22f, h_total * 0.24f);
							if (drop_len < h_total * 0.12f)
								drop_len = h_total * 0.12f;
							float card_hw = drop_len * (is_willow ? 0.42f : 0.46f);
							float out_xy  = is_willow ? 0.34f : 0.48f;
							float down_z  = is_willow ? -0.94f : -0.88f;
							nd_m3d_add_branchlet_card(b,
							                          ax + cca * (rx * 0.16f),
							                          ay + csa * (ry * 0.16f),
							                          az + rz * 0.08f,
							                          cca * out_xy, csa * out_xy, down_z,
							                          drop_len, w_total * 0.04f, -w_total * 0.03f,
							                          card_hw, w_total * 0.03f,
							                          (wc & 1) ? 0.32f : -0.32f,
							                          0.0f, 0.0f, h_total * 0.55f,
							                          is_willow ? ND_MAT3D_FOL_BLADE : tree_fol_base,
							                          c_leaf_deep, c_leaf_mid, c_leaf_lit, c_leaf_high,
							                          h_total);
						}
					}
				}
			}
		} else if (org->type == PLANT_ORGAN_FLOWER) {
			const PlantGrowthUnit *pu = (org->parent_unit < pheno->unit_count)
				? &pheno->units[org->parent_unit] : NULL;
			/* Keep flower corollas botanically proportioned relative to overall plant crown width */
			float max_frad = is_cactus
				? (w_total * 0.075f)
				: (is_herb ? (w_total * 0.125f) : (w_total * 0.045f));
			float frad = plant_clampf(org->length_m * 0.45f * wu_per_m, 1.4f, max_frad);
			float fh   = plant_clampf(org->thickness_m * 0.6f * wu_per_m, 0.6f, frad * 0.48f);
			if (is_graminoid || g->flower.inflorescence == PLANT_INFLORESCENCE_SPIKE) {
				/* Cylindrical dark-chocolate velvety cattail spadix spike (Typha latifolia) */
				uint32_t c_spadix_dark = 0xFF12203Au;
				uint32_t c_spadix_mid  = 0xFF1A2E52u;
				uint32_t c_spadix_lit  = 0xFF28426Eu;
				float sp_r = fmaxf(1.1f, w_total * 0.036f);
				nd_m3d_add_trunk_tube_ex(b, ax, ay, h_total * 0.54f, sp_r,
				                         ax, ay, h_total * 0.80f, sp_r * 0.92f,
				                         6, 2, c_spadix_dark, c_spadix_mid, c_spadix_lit,
				                         ND_PLANT_BARK_HERB, 0.005f, h_total, oseed);
			} else if (g->flower.inflorescence == PLANT_INFLORESCENCE_UMBEL) {
				/* Compound flat-topped lacy white wild carrot umbel (Daucus carota) */
				float u_rad = fminf(w_total * 0.068f, frad * 0.65f);
				float fz = fminf(h_total * 0.93f, az + fh * 0.3f);
				nd_m3d_add_blossom_star(b, ax, ay, fz, u_rad, fh * 0.5f,
				                        c_fl_prim, c_fl_sec, c_fl_sec, h_total);
				for (int uf = 0; uf < 3; uf++) {
					float uang = (float)uf * 2.0943951f + (float)(oseed & 7u) * 0.3f;
					nd_m3d_add_blossom_star(b,
					                        ax + cosf(uang) * u_rad * 1.15f,
					                        ay + sinf(uang) * u_rad * 1.15f,
					                        fz - fh * 0.15f,
					                        u_rad * 0.78f, fh * 0.45f,
					                        c_fl_prim, c_fl_sec, c_fl_prim, h_total);
				}
			} else {
				/* Distinct white ray petals + golden central disc for daisy (Bellis perennis) */
				bool is_daisy = (g->architecture.model == PLANT_ARCH_HOLTTUM);
				uint32_t col_petal_lit   = is_daisy ? 0xFFF8FAFCu : c_fl_prim;
				uint32_t col_petal_shade = is_daisy ? 0xFFD4DCE6u : c_fl_sec;
				uint32_t col_center      = is_daisy ? 0xFF10B8F8u : c_fl_sec;
				float fz = fminf(h_total * 0.94f, az + fh * 0.35f);
				if (is_daisy) {
					if (org->sub_index != 0u)
						continue;
					if (pu && pu->next_in_axis != PLANT_INVALID_ID)
						continue;
					frad = fmaxf(frad, w_total * 0.145f);
					fz   = az + fh * 0.25f;
				}
				nd_m3d_add_blossom_star(b, ax, ay, fz,
				                        frad, fh,
				                        col_petal_lit, col_petal_shade, col_center, h_total);
			}
		} else if (org->type == PLANT_ORGAN_FRUIT) {
			float max_frad = is_herb ? (w_total * 0.068f) : (w_total * 0.035f);
			float frad = plant_clampf(org->width_m * 0.45f * wu_per_m, 1.1f, max_frad);
			float flen = plant_clampf(org->length_m * 0.65f * wu_per_m, 1.6f, max_frad * 2.2f);
			if (is_graminoid) {
				/* Cattail spike already emitted by flower/culm */
				continue;
			} else if (g->fruit.fruit_type == PLANT_FRUIT_CONE || g->fruit.fruit_type == PLANT_FRUIT_ACHENE) {
				/* Upright conifer seed cone (Abies alba) or woody pine cone */
				nd_m3d_add_trunk_tube_ex(b, ax, ay, az, frad,
				                         ax, ay, fminf(h_total * 0.95f, az + flen), frad * 0.76f,
				                         5, 1, c_fr_imm, c_fr_mat, c_fr_mat,
				                         ND_PLANT_BARK_FISSURED, 0.01f, h_total, oseed);
			} else {
				/* Plump 3D spherical berry / tomato fruit (Solanum lycopersicum) */
				uint32_t col_berry = ((oseed & 3u) == 0u) ? c_fr_imm : c_fr_mat;
				nd_m3d_add_fruit_berry(b, ax, ay, fminf(h_total * 0.78f, az),
				                       frad, col_berry, c_fr_imm, h_total);
			}
		}
	}

	/* 3. Conifer Inter-Whorl Crown Fill (Douglas Fir & Silver Fir):
	 *    Because Massart conifers develop 6 primary trunk whorls, add intermediate offset
	 *    drooping bough whorls along the upper-middle trunk (22%..86% H) so the conical
	 *    spire has 10+ overlapping Skyrim pine/fir tiers with zero bare pole gaps. */
	if (is_massart_conifer) {
		int n_fill_tiers = (detail_level == 0u) ? 4 : 2;
		for (int ft = 0; ft < n_fill_tiers; ft++) {
			float h_t = 0.24f + 0.60f * ((float)ft / (float)fmaxf(1, n_fill_tiers - 1));
			float tz = h_total * h_t;
			/* Interpolate trunk centerline (x, y) at height tz */
			float tx = 0.0f, ty = 0.0f;
			for (uint32_t u_idx = 0u; u_idx < pheno->unit_count; u_idx++) {
				const PlantGrowthUnit *u = &pheno->units[u_idx];
				if (u->branch_order == 0u && unit_tip_valid[u_idx]) {
					if (unit_tip_pos[u_idx][2] <= tz + h_total * 0.12f) {
						tx = unit_tip_pos[u_idx][0];
						ty = unit_tip_pos[u_idx][1];
					}
				}
			}
			float cone_taper = powf(fmaxf(0.14f, 1.04f - 0.82f * h_t), 0.78f);
			float rx = w_total * 0.30f * cone_taper;
			float ry = rx * 0.94f;
			float rz = rx * 0.48f;
			int n_arms = 3;
			for (int a = 0; a < n_arms; a++) {
				uint32_t fseed = nd_m3d_hash(inst_seed + (uint32_t)(ft * 313 + a * 97 + 19));
				float ang = ((float)a / (float)n_arms) * 6.2831853f + (float)ft * 2.3999632f +
				            (((float)(fseed & 0xFFu) / 255.0f) - 0.5f) * 0.25f;
				float ca = cosf(ang), sa = sinf(ang);
				float ax_f = tx + ca * (rx * 0.22f);
				float ay_f = ty + sa * (rx * 0.22f);
				nd_m3d_add_conifer_bough_fan(b, ax_f, ay_f, tz,
				                             ca, sa,
				                             rx, ry, rz,
				                             c_leaf_deep, c_leaf_mid, c_leaf_lit, c_leaf_high,
				                             h_total, fseed);
			}
		}
	} else if (is_acacia) {
		/* Acacia senegal Continuous Horizontal Savannah Umbrella Stratum:
		 * Crowns the ascending V-vase limbs with a wide, flat, multi-pad horizontal tabletop
		 * (82..90% H) plus a smaller offset lower tier (64..69% H) so both side and 55-deg
		 * diorama views show an unmistakable East African flat-topped umbrella thorn silhouette. */
		float rx = w_total * 0.28f;
		float ry = rx * 0.92f;
		float rz = rx * 0.18f;
		/* Central upper tabletop pad closing the crown roof above the main V-fork */
		nd_m3d_add_tabular_umbrella_fan(b, 0.0f, 0.0f, h_total * 0.86f,
		                                1.0f, 0.0f,
		                                rx * 1.08f, ry * 1.08f, rz,
		                                ND_MAT3D_FOL_BLADE,
		                                c_leaf_deep, c_leaf_mid, c_leaf_lit, c_leaf_high,
		                                h_total, inst_seed + 401u);
		int n_table = (detail_level == 0u) ? 5 : 3;
		for (int tp = 0; tp < n_table; tp++) {
			uint32_t tseed = nd_m3d_hash(inst_seed + (uint32_t)(tp * 229 + 83));
			float ang = ((float)tp / (float)n_table) * 6.2831853f + (float)(inst_seed & 7u) * 0.3f;
			float ca = cosf(ang), sa = sinf(ang);
			float tx = ca * (w_total * 0.25f);
			float ty = sa * (w_total * 0.25f) * 0.56f;
			float tz = h_total * (0.83f + 0.025f * (float)(tp % 3));
			nd_m3d_add_tabular_umbrella_fan(b, tx, ty, tz,
			                                ca, sa,
			                                rx, ry, rz,
			                                ND_MAT3D_FOL_BLADE,
			                                c_leaf_deep, c_leaf_mid, c_leaf_lit, c_leaf_high,
			                                h_total, tseed);
		}
		/* Secondary offset lower savannah tier pad at 66% H */
		float l_ang = (float)(inst_seed & 15u) * 0.42f;
		nd_m3d_add_tabular_umbrella_fan(b,
		                                cosf(l_ang) * (w_total * 0.22f),
		                                sinf(l_ang) * (w_total * 0.22f) * 0.56f,
		                                h_total * 0.66f,
		                                cosf(l_ang), sinf(l_ang),
		                                rx * 0.85f, ry * 0.85f, rz * 0.90f,
		                                ND_MAT3D_FOL_BLADE,
		                                c_leaf_deep, c_leaf_mid, c_leaf_lit, c_leaf_high,
		                                h_total, inst_seed + 709u);
	} else if (is_scots_pine) {
		/* Pinus sylvestris Upper Crown Dome & Cloud Table Fill:
		 * Closes the central upper pine dome (74..92% H) above the high salmon-barked trunk fork
		 * so the 55-deg diorama view shows a full, billowing, layered Scots pine canopy. */
		float rx = w_total * 0.28f;
		float ry = rx * 0.94f;
		float rz = rx * 0.44f;
		int n_caps = (detail_level == 0u) ? 4 : 2;
		for (int cp = 0; cp < n_caps; cp++) {
			uint32_t pseed = nd_m3d_hash(inst_seed + (uint32_t)(cp * 193 + 61));
			float ang = (float)cp * 2.3999632f + (float)(inst_seed & 7u) * 0.25f;
			float ca = cosf(ang), sa = sinf(ang);
			float rad = (cp == 0) ? (w_total * 0.03f) : (w_total * 0.14f);
			float pz  = h_total * ((cp == 0) ? 0.91f : (0.78f + 0.04f * (float)cp));
			nd_m3d_add_conifer_bough_fan(b, ca * rad, sa * rad * 0.68f, pz,
			                             ca, sa,
			                             rx, ry, rz,
			                             c_leaf_deep, c_leaf_mid, c_leaf_lit, c_leaf_high,
			                             h_total, pseed);
		}
	} else if (!is_herb && !is_graminoid && !is_cactus) {
		/* Broadleaf Upper-Crown Dome Roof Fill (Birch, Oak, Willow, Maple, Olive):
		 * Terminal shoots lie around the outer crown perimeter; adding 3 interior upper-crown
		 * bough fans above the central scaffold fork (68..88% H) eliminates the top-down/55-deg
		 * donut hole while preserving natural side-view branch windows below. */
		float crown_scale = is_birch ? 0.21f : (is_olive ? 0.24f : 0.26f);
		float flat_aspect = is_olive ? 0.42f : (is_birch ? 0.54f : 0.62f);
		float rx = w_total * crown_scale;
		float ry = rx * 0.94f;
		float rz = rx * flat_aspect;
		int n_roof = (detail_level == 0u) ? 3 : 2;
		for (int rf = 0; rf < n_roof; rf++) {
			uint32_t rseed = nd_m3d_hash(inst_seed + (uint32_t)(rf * 271 + 113));
			float ang = (float)rf * 2.0943951f + (float)(inst_seed & 7u) * 0.3f;
			float ca = cosf(ang), sa = sinf(ang);
			float rad = (rf == 0) ? (w_total * 0.04f) : (w_total * 0.13f);
			float rz_h = h_total * ((rf == 0) ? 0.84f : (0.72f + 0.05f * (float)rf));
			nd_m3d_add_foliage_bough_fan_ex(b, ca * rad, sa * rad * 0.78f, rz_h,
			                                ca, sa,
			                                rx, ry, rz,
			                                tree_fol_base,
			                                c_leaf_deep, c_leaf_mid, c_leaf_lit, c_leaf_high,
			                                h_total, rseed);
		}
	}

	if (out->out_slice) {
		float max_sp = 136.0f;
		float sp_scale = (w_total > max_sp || h_total > max_sp)
			? (max_sp / fmaxf(w_total, h_total))
			: 1.0f;
		uint16_t sw = (uint16_t)fmaxf(8.0f, fminf(248.0f, floorf(w_total * sp_scale + 0.5f)));
		uint16_t sh = (uint16_t)fmaxf(6.0f, fminf(248.0f, floorf(h_total * sp_scale + 0.5f)));
		out->out_slice->first_vert = v_start;
		out->out_slice->vert_count = b->vert_count - v_start;
		out->out_slice->first_idx  = i_start;
		out->out_slice->idx_count  = b->idx_count - i_start;
		out->out_slice->mesh_w_wu  = w_total;
		out->out_slice->mesh_h_wu  = h_total;
		out->out_slice->sprite_w   = sw;
		out->out_slice->sprite_h   = sh;
		out->out_slice->anchor_y   = (uint8_t)(sh > 64 ? 6u : 3u);
		out->out_slice->shadow_rx  = (uint8_t)(sw * 0.34f);
		out->out_slice->shadow_ry  = (uint8_t)(sw * 0.22f);
		out->out_slice->pad0       = 0;
	}
}

/* Unified Data-Driven 3D Botanical Generator:
 * Evaluates the full 4-stage pipeline:
 *   PlantSpeciesDef -> PlantGenome (sampled per variant allele) ->
 *   PlantPhenotype (developed at juvenile or mature age via plant_simulate_at_time) ->
 *   3D Geometry (via plant_geometry_build). */
static inline void
nd_m3d_build_plant_from_genes(nd_mesh3d_builder_t *b,
                              const nd_plant_spec_t *spec,
                              int is_big, int var_idx, uint32_t seed_salt,
                              nd_mesh3d_slice_t *out_slice)
{
	float w_wu = 56.0f, h_wu = 68.0f, ay_wu = 4.0f;
	unsigned sp_idx = (unsigned)(spec - s_nd_plant_specs);
	if (sp_idx < ND_PLANT_MAX) {
		nd_m3d_plant_metric_dims(sp_idx, (unsigned)is_big, &w_wu, &h_wu, &ay_wu);
	} else {
		w_wu = (float)(is_big ? spec->genes.big_w : spec->genes.small_w);
		h_wu = (float)(is_big ? spec->genes.big_h : spec->genes.small_h);
		sp_idx = 10u;
	}

	/* 1. Initialize biological species definition & priors */
	PlantSpeciesDef species_def;
	plant_species_init_preset(&species_def, sp_idx);

	/* 2. Sample an individual PlantGenome for this genetic variant allele (0..3) */
	unsigned allele = (unsigned)var_idx & 3u;
	uint64_t genetic_seed = plant_mix64(
		((uint64_t)seed_salt << 16) ^
		((uint64_t)sp_idx * 0x9E3779B97F4A7C15ULL) ^
		((uint64_t)allele * 0xBF58476D1CE4E5B9ULL) ^
		(uint64_t)(is_big ? 0xF00DULL : 0x100DULL));
	PlantGenome genome = plant_genome_sample(&species_def, genetic_seed);
	if (allele == 2u && genome.architecture.basal_stem_count < 3u &&
	    species_def.life_form == PLANT_LIFEFORM_TREE) {
		/* Allele 2: multi-stemmed coppice / twin trunk variant */
		genome.architecture.basal_stem_count++;
	}

	/* 3. Develop the chrono-deterministic PlantPhenotype at juvenile (small) or mature (big) age */
	static PlantPhenotypeStorage s_pheno_storage;
	PlantInstance inst;
	memset(&inst, 0, sizeof(inst));
	inst.instance_id = ((uint64_t)sp_idx << 8) | ((uint64_t)is_big << 4) | (uint64_t)allele;
	inst.genome = genome;
	plant_phenotype_bind_storage(&inst.phenotype, &s_pheno_storage);

	PlantEnvironment env = plant_environment_default();
	/* Slight prevailing wind/light vector variation per allele for organic asymmetry */
	static const float s_lean_x[4] = {  0.08f, -0.14f,  0.11f, -0.09f };
	static const float s_lean_y[4] = { -0.05f,  0.07f, -0.08f,  0.06f };
	env.mean_light_dir = plant_v3_norm(plant_v3(s_lean_x[allele], s_lean_y[allele], 0.98f));

	float full_horizon_days = genome.meristem.plastochron_days * 12.0f;
	float target_age_days = is_big ? (full_horizon_days * 1.05f) : (full_horizon_days * 0.74f);
	plant_simulate_at_time(&inst, &env, target_age_days, NULL, 0u);

	/* 4. Build 3D mesh geometry purely from the developed PlantPhenotype */
	PlantGeometryOutput geom_out;
	geom_out.builder     = b;
	geom_out.out_slice   = out_slice;
	geom_out.target_w_wu = w_wu;
	geom_out.target_h_wu = h_wu;
	uint32_t detail_level = is_big ? 0u : 1u;
	plant_geometry_build(&inst, detail_level, &geom_out);
}

/* Build a complete 3D botanical mesh for a given profile, growth stage (is_big), and variant (var_idx 0..3) */
static inline void
nd_m3d_build_plant_from_profile(nd_mesh3d_builder_t *b,
                                const nd_bot3d_profile_t *p,
                                int is_big, int var_idx, uint32_t seed_salt,
                                nd_mesh3d_slice_t *out_slice)
{
	uint32_t v_start = b->vert_count;
	uint32_t i_start = b->idx_count;
	uint16_t sw = is_big ? p->big_w : p->small_w;
	uint16_t sh = is_big ? p->big_h : p->small_h;
	float h_total = (float)sh;
	float w_total = (float)sw;
	uint32_t seed = nd_m3d_hash(seed_salt + (uint32_t)(is_big * 131 + var_idx * 977));
	float lean = (var_idx ? -1.0f : 1.0f) * (is_big ? 2.6f : 1.4f);

	switch (p->kind) {
	case ND_BOT3D_PINE: {
		/* Pinus sylvestris: Self-pruned bare lower trunk (55%+), dark fissured basal bark
		 * transitioning to flaky salmon-orange upper bark, crooked high forks, and tiered flat-topped needle pads */
		float trunk_h = h_total * 0.82f;
		float r_base = w_total * (is_big ? 0.064f : 0.056f);
		nd_m3d_add_trunk_tube(b, 0.0f, 0.0f, 0.0f, r_base,
		                      lean, -lean * 0.35f, trunk_h, r_base * 0.34f,
		                      8, 6, p->bark_dark, p->bark_mid, p->bark_light,
		                      0, 1, h_total, seed);
		/* 5 to 7 picturesque high lateral pine boughs + secondary ascending twigs + flattened needle pads */
		int n_boughs = is_big ? 7 : 5;
		for (int i = 0; i < n_boughs; i++) {
			float t = (float)i / (float)(n_boughs - 1);
			float bz = h_total * (0.46f + 0.38f * t);
			float ang = (float)i * 2.24f + (var_idx ? 1.15f : 0.25f);
			float reach = w_total * (0.35f - 0.09f * t);
			float tx0 = lean * (bz / trunk_h);
			float ty0 = -lean * 0.35f * (bz / trunk_h);
			float bx = tx0 + cosf(ang) * reach;
			float by = ty0 + sinf(ang) * reach * 0.90f;
			nd_m3d_add_trunk_tube(b, tx0, ty0, bz - h_total * 0.04f, r_base * (0.44f - 0.14f * t),
			                      bx, by, bz + h_total * 0.02f, r_base * 0.16f,
			                      6, 3, p->bark_dark, p->bark_mid, p->bark_light,
			                      0, 1, h_total, seed + (uint32_t)i * 43u);
			/* Secondary pine fork */
			float fang = ang + ((i & 1) ? 0.68f : -0.68f);
			float fx = (tx0 + bx) * 0.5f + cosf(fang) * (reach * 0.58f);
			float fy = (ty0 + by) * 0.5f + sinf(fang) * (reach * 0.58f);
			float fz = bz + h_total * 0.045f;
			nd_m3d_add_trunk_tube(b, (tx0 + bx) * 0.5f, (ty0 + by) * 0.5f, bz, r_base * 0.25f,
			                      fx, fy, fz, r_base * 0.11f,
			                      5, 2, p->bark_dark, p->bark_mid, p->bark_light,
			                      0, 1, h_total, seed + (uint32_t)i * 107u);
			nd_m3d_add_foliage_bough_cluster(b, bx, by, bz + h_total * 0.035f,
			                                 w_total * (0.23f - 0.03f * t),
			                                 w_total * (0.21f - 0.03f * t),
			                                 h_total * (0.085f - 0.01f * t),
			                                 p->leaf_deep, p->leaf_mid, p->leaf_lit, p->leaf_high,
			                                 h_total, seed + (uint32_t)i * 71u);
			nd_m3d_add_foliage_lobe(b, fx, fy, fz + h_total * 0.02f,
			                        w_total * (0.16f - 0.02f * t),
			                        w_total * (0.15f - 0.02f * t),
			                        h_total * 0.065f,
			                        8, 4, p->leaf_deep, p->leaf_mid, p->leaf_lit, p->leaf_high,
			                        h_total, seed + (uint32_t)i * 157u, ND_MAT3D_FOLIAGE);
		}
		/* Upper flat-domed Scots pine umbrella crown cluster */
		nd_m3d_add_foliage_bough_cluster(b, lean, -lean * 0.3f, h_total * 0.90f,
		                                 w_total * 0.31f, w_total * 0.28f, h_total * 0.11f,
		                                 p->leaf_deep, p->leaf_mid, p->leaf_lit, p->leaf_high,
		                                 h_total, seed + 503u);
		break;
	}

	case ND_BOT3D_FIR_CONIFER: {
		/* Tall straight monopodial conifer trunk + 6 to 8 layered whorled tiers + radial 3D needle boughs.
		 * Abies alba (sub_variant==1) features horizontal pectinate tiers and upright cones near the apex. */
		float trunk_h = h_total * 0.92f;
		float r_base = w_total * (is_big ? 0.062f : 0.054f);
		nd_m3d_add_trunk_tube(b, 0.0f, 0.0f, 0.0f, r_base,
		                      lean * 0.22f, 0.0f, trunk_h, r_base * 0.18f,
		                      7, 4, p->bark_dark, p->bark_mid, p->bark_light,
		                      0, 0, h_total, seed);
		int tiers = is_big ? 8 : 6;
		for (int i = 0; i < tiers; i++) {
			float t = (float)i / (float)tiers;
			float t_next = (float)(i + 1) / (float)tiers;
			float z0 = h_total * (0.12f + 0.74f * t);
			float z1 = h_total * (0.16f + 0.82f * t_next);
			float r_sk = w_total * (0.46f * (1.0f - (p->sub_variant ? 0.65f : 0.72f) * t));
			float cx = lean * 0.22f * t;
			nd_m3d_add_conifer_tier(b, cx, 0.0f, z0, z1, r_sk,
			                        is_big ? 11 : 9,
			                        p->leaf_deep, p->leaf_mid, p->leaf_lit, p->leaf_high,
			                        h_total, seed + (uint32_t)i * 89u);
			/* Radial 3D needle-bough puffs & sweeping fans emerging from each whorl */
			int n_sprays = (i < tiers - 2) ? 4 : 3;
			for (int s = 0; s < n_sprays; s++) {
				float sang = ((float)s / (float)n_sprays) * 6.2831853f + (float)i * 0.95f;
				float ca = cosf(sang), sa = sinf(sang);
				if (i < tiers - 1) {
					nd_m3d_add_foliage_lobe(b, cx + ca * r_sk * 0.62f, sa * r_sk * 0.62f,
					                        z0 + (z1 - z0) * 0.28f,
					                        r_sk * 0.42f, r_sk * 0.40f, (z1 - z0) * 0.36f,
					                        8, 3, p->leaf_deep, p->leaf_mid, p->leaf_lit, p->leaf_high,
					                        h_total, seed + (uint32_t)(i * 131 + s * 29), ND_MAT3D_FOLIAGE);
				}
				nd_m3d_add_arching_blade(b, cx + ca * r_sk * 0.20f, sa * r_sk * 0.20f, z0 + (z1 - z0) * 0.35f,
				                         ca, sa,
				                         r_sk * 0.95f, (z1 - z0) * 0.08f, (z1 - z0) * 0.28f,
				                         r_sk * 0.30f,
				                         p->leaf_deep, p->leaf_mid, p->leaf_high, h_total);
			}
		}
		if (p->sub_variant == 1 && is_big) {
			/* Abies alba: erect cylindrical seed cones perched on upper whorl branches */
			for (int c = 0; c < 3; c++) {
				float ang = (float)c * 2.094f + 0.5f;
				float cr = w_total * 0.12f;
				float cz = h_total * (0.72f + 0.05f * (float)c);
				nd_m3d_add_trunk_tube(b, cosf(ang) * cr, sinf(ang) * cr, cz, w_total * 0.025f,
				                      cosf(ang) * cr, sinf(ang) * cr, cz + h_total * 0.07f, w_total * 0.020f,
				                      5, 1, 0xFF223048u, 0xFF385074u, 0xFF5474A0u,
				                      0, 0, h_total, seed + 901u + (uint32_t)c);
			}
		}
		break;
	}

	case ND_BOT3D_BIRCH: {
		/* Graceful white birch trunk with dark diamond basal fissures & lenticels,
		 * multi-forked ascending white branches visible through an airy, tiered crown */
		float trunk_h = h_total * 0.80f;
		float r_base = w_total * (is_big ? 0.050f : 0.044f);
		nd_m3d_add_trunk_tube(b, 0.0f, 0.0f, 0.0f, r_base,
		                      lean, 0.0f, trunk_h, r_base * 0.26f,
		                      8, 6, p->bark_dark, p->bark_mid, p->bark_light,
		                      1, 0, h_total, seed);
		if (var_idx == 1 || is_big) {
			float fork_z = h_total * 0.24f;
			nd_m3d_add_trunk_tube(b, lean * 0.3f, 0.0f, fork_z, r_base * 0.68f,
			                      -lean * 1.4f, w_total * 0.09f, h_total * 0.72f, r_base * 0.22f,
			                      7, 4, p->bark_dark, p->bark_mid, p->bark_light,
			                      1, 0, h_total, seed + 199u);
		}
		int n_lobes = is_big ? 6 : 4;
		for (int i = 0; i < n_lobes; i++) {
			float t = (float)i / (float)(n_lobes - 1);
			float ang = (float)i * 2.18f + (var_idx ? 0.8f : 0.0f);
			float rad = (i == n_lobes - 1) ? w_total * 0.06f : w_total * (0.25f - 0.04f * t);
			float z_fork = h_total * (0.32f + 0.34f * t);
			float lx = lean * (0.5f + 0.4f * t) + cosf(ang) * rad;
			float ly = sinf(ang) * rad * 0.88f;
			float lz = h_total * (0.46f + 0.40f * t);
			/* Ascending white secondary birch limb connecting trunk to foliage cluster */
			nd_m3d_add_trunk_tube(b, lean * (z_fork / trunk_h), 0.0f, z_fork, r_base * 0.38f,
			                      lx, ly, lz, r_base * 0.14f,
			                      6, 3, p->bark_dark, p->bark_mid, p->bark_light,
			                      1, 0, h_total, seed + (uint32_t)i * 47u);
			nd_m3d_add_foliage_bough_cluster(b, lx, ly, lz,
			                                 w_total * (0.21f - 0.02f * t),
			                                 w_total * (0.19f - 0.02f * t),
			                                 h_total * (0.135f - 0.01f * t),
			                                 p->leaf_deep, p->leaf_mid, p->leaf_lit, p->leaf_high,
			                                 h_total, seed + (uint32_t)i * 67u);
			if (p->sub_variant == 1 && i < 5) {
				/* Betula pendula: slender pendulous weeping outer twig curtains */
				nd_m3d_add_arching_blade(b, lx, ly, lz, cosf(ang), sinf(ang),
				                         w_total * 0.15f, h_total * 0.02f, h_total * 0.25f,
				                         w_total * 0.042f,
				                         p->leaf_deep, p->leaf_mid, p->leaf_high, h_total);
			}
		}
		break;
	}

	case ND_BOT3D_BROADLEAF_OAK:
	case ND_BOT3D_MAPLE_SYCAMORE:
	case ND_BOT3D_OLIVE: {
		/* Quercus robur (stout buttressed trunk + gnarled primary & secondary boughs + billowing sub-crowns),
		 * Acer pseudoplatanus (ascending scaffold limbs + layered palmate sub-crowns),
		 * Olea europaea (deeply twisted fluted trunk + silvery-green Mediterranean bough pads) */
		int is_oak = (p->kind == ND_BOT3D_BROADLEAF_OAK);
		int is_olive = (p->kind == ND_BOT3D_OLIVE);
		float trunk_h = h_total * (is_olive ? 0.42f : (is_oak ? 0.42f : 0.48f));
		float r_base = w_total * (is_olive ? 0.088f : (is_oak ? 0.084f : 0.068f));
		float twist = is_olive ? lean * 1.7f : (is_oak ? lean * 0.95f : lean * 0.55f);
		nd_m3d_add_trunk_tube(b, 0.0f, 0.0f, 0.0f, r_base,
		                      twist, 0.0f, trunk_h, r_base * 0.62f,
		                      8, 5, p->bark_dark, p->bark_mid, p->bark_light,
		                      0, 0, h_total, seed);
		int n_limbs = is_big ? 6 : 5;
		for (int i = 0; i < n_limbs; i++) {
			float ang = ((float)i / (float)n_limbs) * 6.2831853f + (var_idx ? 0.55f : 0.15f);
			float reach = w_total * ((is_oak ? 0.29f : 0.25f) + 0.04f * (float)(i & 1));
			float lz = trunk_h + h_total * ((is_oak ? 0.12f : 0.15f) + 0.06f * (float)(i % 3));
			float lx = twist + cosf(ang) * reach;
			float ly = sinf(ang) * reach * 0.90f;
			/* Primary scaffold bough */
			nd_m3d_add_trunk_tube(b, twist * 0.85f, 0.0f, trunk_h * 0.72f, r_base * 0.48f,
			                      lx, ly, lz, r_base * 0.22f,
			                      7, 4, p->bark_dark, p->bark_mid, p->bark_light,
			                      0, 0, h_total, seed + (uint32_t)i * 53u);
			/* Secondary bifurcating limb (Da Vinci area-preserving fork) */
			float sang = ang + ((i & 1) ? 0.70f : -0.70f);
			float sx = (twist + lx) * 0.5f + cosf(sang) * (reach * 0.64f);
			float sy = ly * 0.5f + sinf(sang) * (reach * 0.64f);
			float sz = lz + h_total * 0.12f;
			nd_m3d_add_trunk_tube(b, (twist + lx) * 0.52f, ly * 0.52f, (trunk_h * 0.72f + lz) * 0.5f, r_base * 0.30f,
			                      sx, sy, sz, r_base * 0.14f,
			                      6, 3, p->bark_dark, p->bark_mid, p->bark_light,
			                      0, 0, h_total, seed + (uint32_t)i * 149u);
			/* Rich 3D bough clusters at primary and secondary branch tips */
			nd_m3d_add_foliage_bough_cluster(b, lx, ly, lz + h_total * 0.04f,
			                                 w_total * (is_oak ? 0.23f : 0.21f),
			                                 w_total * (is_oak ? 0.21f : 0.19f),
			                                 h_total * (is_olive ? 0.13f : 0.15f),
			                                 p->leaf_deep, p->leaf_mid, p->leaf_lit, p->leaf_high,
			                                 h_total, seed + (uint32_t)i * 101u);
			nd_m3d_add_foliage_bough_cluster(b, sx, sy, sz + h_total * 0.03f,
			                                 w_total * (is_oak ? 0.20f : 0.18f),
			                                 w_total * (is_oak ? 0.18f : 0.17f),
			                                 h_total * (is_olive ? 0.12f : 0.14f),
			                                 p->leaf_deep, p->leaf_mid, p->leaf_lit, p->leaf_high,
			                                 h_total, seed + (uint32_t)i * 193u);
		}
		/* High central leader bough & billowing apex crown cluster */
		nd_m3d_add_trunk_tube(b, twist, 0.0f, trunk_h * 0.88f, r_base * 0.42f,
		                      twist * 0.6f, 0.0f, h_total * 0.78f, r_base * 0.16f,
		                      6, 3, p->bark_dark, p->bark_mid, p->bark_light,
		                      0, 0, h_total, seed + 601u);
		nd_m3d_add_foliage_bough_cluster(b, twist * 0.6f, 0.0f, h_total * 0.82f,
		                                 w_total * (is_oak ? 0.28f : 0.25f),
		                                 w_total * (is_oak ? 0.26f : 0.23f),
		                                 h_total * (is_olive ? 0.15f : 0.18f),
		                                 p->leaf_deep, p->leaf_mid, p->leaf_lit, p->leaf_high,
		                                 h_total, seed + 773u);
		break;
	}

	case ND_BOT3D_WILLOW: {
		/* Gnarled leaning willow trunk + arching fountain limbs + porous crown + cascading 3D weeping curtains */
		float trunk_h = h_total * 0.52f;
		float r_base = w_total * 0.070f;
		nd_m3d_add_trunk_tube(b, 0.0f, 0.0f, 0.0f, r_base,
		                      lean * 1.3f, 0.0f, trunk_h, r_base * 0.54f,
		                      8, 5, p->bark_dark, p->bark_mid, p->bark_light,
		                      0, 0, h_total, seed);
		for (int l = 0; l < 5; l++) {
			float lang = ((float)l / 5.0f) * 6.2831853f + 0.4f;
			float lx = lean + cosf(lang) * w_total * 0.23f;
			float ly = sinf(lang) * w_total * 0.21f;
			float lz = h_total * (0.72f + 0.04f * (float)(l & 1));
			nd_m3d_add_trunk_tube(b, lean * 1.1f, 0.0f, trunk_h * 0.84f, r_base * 0.42f,
			                      lx, ly, lz, r_base * 0.16f,
			                      6, 3, p->bark_dark, p->bark_mid, p->bark_light,
			                      0, 0, h_total, seed + (uint32_t)l * 59u);
			nd_m3d_add_foliage_bough_cluster(b, lx, ly, lz + h_total * 0.03f,
			                                 w_total * 0.22f, w_total * 0.20f, h_total * 0.15f,
			                                 p->leaf_deep, p->leaf_mid, p->leaf_lit, p->leaf_high,
			                                 h_total, seed + (uint32_t)l * 83u);
		}
		/* Upper central fountain dome */
		nd_m3d_add_foliage_bough_cluster(b, lean * 0.8f, 0.0f, h_total * 0.84f,
		                                 w_total * 0.25f, w_total * 0.23f, h_total * 0.14f,
		                                 p->leaf_deep, p->leaf_mid, p->leaf_lit, p->leaf_high,
		                                 h_total, seed + 557u);
		/* Weeping curtain streamers descending almost to ground level */
		int n_curtains = is_big ? 18 : 12;
		for (int i = 0; i < n_curtains; i++) {
			float ang = ((float)i / (float)n_curtains) * 6.2831853f + (var_idx ? 0.3f : 0.0f);
			float ca = cosf(ang), sa = sinf(ang);
			float r_start = w_total * (0.18f + 0.15f * (float)(i & 1));
			float sx = lean + ca * r_start;
			float sy = sa * r_start * 0.88f;
			float sz = h_total * (0.68f + 0.08f * (float)(i % 3));
			float drop = h_total * (0.54f + 0.08f * (float)(i & 1));
			nd_m3d_add_arching_blade(b, sx, sy, sz, ca, sa,
			                         w_total * 0.10f, h_total * 0.04f, drop,
			                         w_total * 0.050f,
			                         p->leaf_deep, p->leaf_mid, (i & 1) ? p->leaf_high : p->leaf_lit,
			                         h_total);
		}
		break;
	}

	case ND_BOT3D_ACACIA: {
		/* Savannah umbrella thorn: forked leaning trunk + wide flat horizontal tabletop tiers */
		float fork_z = h_total * 0.42f;
		float r_base = w_total * 0.054f;
		nd_m3d_add_trunk_tube(b, 0.0f, 0.0f, 0.0f, r_base,
		                      lean, 0.0f, fork_z, r_base * 0.64f,
		                      7, 4, p->bark_dark, p->bark_mid, p->bark_light,
		                      0, 0, h_total, seed);
		/* 4 sweeping umbrella scaffold branches + secondary struts */
		float lx = -w_total * 0.29f + lean;
		float rx_pos = w_total * 0.31f + lean;
		float top_z = h_total * 0.78f;
		nd_m3d_add_trunk_tube(b, lean, 0.0f, fork_z, r_base * 0.56f,
		                      lx, -w_total * 0.06f, top_z, r_base * 0.22f,
		                      6, 3, p->bark_dark, p->bark_mid, p->bark_light,
		                      0, 0, h_total, seed + 41u);
		nd_m3d_add_trunk_tube(b, lean, 0.0f, fork_z, r_base * 0.56f,
		                      rx_pos, w_total * 0.06f, top_z * 0.94f, r_base * 0.22f,
		                      6, 3, p->bark_dark, p->bark_mid, p->bark_light,
		                      0, 0, h_total, seed + 83u);
		nd_m3d_add_trunk_tube(b, lean, 0.0f, fork_z, r_base * 0.48f,
		                      lean * 0.5f, w_total * 0.16f, h_total * 0.86f, r_base * 0.20f,
		                      6, 3, p->bark_dark, p->bark_mid, p->bark_light,
		                      0, 0, h_total, seed + 113u);
		nd_m3d_add_trunk_tube(b, lean, 0.0f, fork_z, r_base * 0.46f,
		                      lean * 0.3f, -w_total * 0.18f, h_total * 0.82f, r_base * 0.18f,
		                      6, 3, p->bark_dark, p->bark_mid, p->bark_light,
		                      0, 0, h_total, seed + 149u);
		/* Wide flat porous tabletop pads */
		nd_m3d_add_foliage_bough_cluster(b, lx, -w_total * 0.05f, top_z + h_total * 0.05f,
		                                 w_total * 0.27f, w_total * 0.22f, h_total * 0.075f,
		                                 p->leaf_deep, p->leaf_mid, p->leaf_lit, p->leaf_high,
		                                 h_total, seed + 127u);
		nd_m3d_add_foliage_bough_cluster(b, rx_pos, w_total * 0.05f, top_z * 0.94f + h_total * 0.05f,
		                                 w_total * 0.26f, w_total * 0.21f, h_total * 0.075f,
		                                 p->leaf_deep, p->leaf_mid, p->leaf_lit, p->leaf_high,
		                                 h_total, seed + 251u);
		nd_m3d_add_foliage_bough_cluster(b, lean * 0.5f, w_total * 0.06f, h_total * 0.89f,
		                                 w_total * 0.33f, w_total * 0.25f, h_total * 0.075f,
		                                 p->leaf_deep, p->leaf_mid, p->leaf_lit, p->leaf_high,
		                                 h_total, seed + 397u);
		break;
	}

	case ND_BOT3D_CACTUS: {
		/* Fluted ribbed columnar cactus stems + golden spine ribs + nocturnal flower crown */
		int n_stems = is_big ? 3 : 2;
		for (int s = 0; s < n_stems; s++) {
			float ox = (s == 0) ? 0.0f : ((s == 1) ? -w_total * 0.24f : w_total * 0.24f);
			float oy = (s == 0) ? 0.0f : ((s == 1) ? w_total * 0.08f : -w_total * 0.08f);
			float z_base = (s == 0) ? 0.0f : h_total * (0.20f + 0.08f * (float)s);
			float z_top  = (s == 0) ? h_total * 0.92f : h_total * (0.68f + 0.10f * (float)(s & 1));
			float rad    = w_total * ((s == 0) ? 0.17f : 0.13f);
			if (s > 0) {
				/* Elbow joint from central stem */
				nd_m3d_add_trunk_tube(b, 0.0f, 0.0f, z_base, rad * 0.85f,
				                      ox, oy, z_base + h_total * 0.06f, rad * 0.95f,
				                      6, 1, p->leaf_deep, p->leaf_mid, p->leaf_lit,
				                      0, 0, h_total, seed + (uint32_t)s * 19u);
			}
			/* 8-sided ribbed cactus column */
			nd_m3d_add_trunk_tube(b, ox, oy, z_base, rad,
			                      ox, oy, z_top, rad * 0.88f,
			                      8, 4, p->leaf_deep, p->leaf_mid, p->leaf_high,
			                      0, 0, h_total, seed + (uint32_t)s * 61u);
			/* Rounded cactus cap */
			nd_m3d_add_foliage_lobe(b, ox, oy, z_top,
			                        rad * 0.92f, rad * 0.92f, rad * 0.75f,
			                        8, 2, p->leaf_deep, p->leaf_mid, p->leaf_lit, p->leaf_high,
			                        h_total, seed + (uint32_t)s * 97u, ND_MAT3D_FOLIAGE);
			if (p->has_accent && (s == 0 || is_big)) {
				nd_m3d_add_blossom_star(b, ox + (s == 0 ? rad * 0.35f : 0.0f), oy, z_top + rad * 0.55f,
				                        w_total * 0.13f, h_total * 0.06f,
				                        p->accent_lit, p->accent_dark, 0xFF38D0F8u, h_total);
			}
		}
		break;
	}

	case ND_BOT3D_HERB_FLAX:
	case ND_BOT3D_HERB_UMBEL:
	case ND_BOT3D_HERB_TOMATO:
	case ND_BOT3D_HERB_DAISY: {
		/* Basal rosette of 3D arching leaves + upright branching stems + 3D flowers/umbels/fruits */
		int n_leaves = is_big ? 8 : 6;
		for (int i = 0; i < n_leaves; i++) {
			float ang = ((float)i / (float)n_leaves) * 6.2831853f + (var_idx ? 0.4f : 0.0f);
			float ca = cosf(ang), sa = sinf(ang);
			nd_m3d_add_arching_blade(b, 0.0f, 0.0f, 0.8f, ca, sa,
			                         w_total * (0.38f + 0.06f * (float)(i & 1)),
			                         h_total * 0.28f, h_total * 0.10f,
			                         w_total * 0.11f,
			                         p->leaf_deep, p->leaf_mid, p->leaf_lit, h_total);
		}
		if (p->kind == ND_BOT3D_HERB_TOMATO) {
			/* Bushy leafy tomato vine mound */
			nd_m3d_add_foliage_lobe(b, 0.0f, 0.0f, h_total * 0.46f,
			                        w_total * 0.38f, w_total * 0.36f, h_total * 0.36f,
			                        8, 3, p->leaf_deep, p->leaf_mid, p->leaf_lit, p->leaf_high,
			                        h_total, seed + 173u, ND_MAT3D_FOLIAGE);
		}
		int n_stems = is_big ? 5 : 3;
		for (int s = 0; s < n_stems; s++) {
			float ang = ((float)s / (float)n_stems) * 6.2831853f + (var_idx ? 0.7f : 0.2f);
			float rad = (s == 0) ? 0.0f : w_total * 0.24f;
			float tx = cosf(ang) * rad;
			float ty = sinf(ang) * rad;
			float tz = h_total * ((s == 0) ? 0.86f : (0.68f + 0.08f * (float)(s & 1)));
			nd_m3d_add_trunk_tube(b, tx * 0.25f, ty * 0.25f, 0.0f, w_total * 0.045f,
			                      tx, ty, tz, w_total * 0.030f,
			                      5, 2, p->bark_dark, p->bark_mid, p->bark_light,
			                      0, 0, h_total, seed + (uint32_t)s * 37u);
			if (p->has_accent) {
				float br = (p->kind == ND_BOT3D_HERB_UMBEL) ? w_total * 0.22f : w_total * 0.17f;
				float bh = (p->kind == ND_BOT3D_HERB_TOMATO) ? w_total * 0.14f : h_total * 0.08f;
				uint32_t center_c = (p->kind == ND_BOT3D_HERB_DAISY) ? p->accent_dark :
				                    ((p->kind == ND_BOT3D_HERB_FLAX) ? 0xFF58E0F8u : p->accent_lit);
				nd_m3d_add_blossom_star(b, tx, ty, tz, br, bh,
				                        p->accent_lit, p->accent_dark, center_c, h_total);
			}
		}
		break;
	}

	case ND_BOT3D_REED_CATTAIL: {
		/* Sweeping wetland sword blades + tall cylindrical stalks + dark cigar-brown spadix spikes */
		int n_blades = is_big ? 9 : 6;
		for (int i = 0; i < n_blades; i++) {
			float ang = ((float)i / (float)n_blades) * 6.2831853f + (var_idx ? 0.35f : 0.0f);
			float ca = cosf(ang), sa = sinf(ang);
			nd_m3d_add_arching_blade(b, ca * w_total * 0.08f, sa * w_total * 0.08f, 0.0f,
			                         ca, sa,
			                         w_total * (0.28f + 0.08f * (float)(i & 1)),
			                         h_total * (0.72f + 0.14f * (float)(i % 3)),
			                         h_total * 0.16f,
			                         w_total * 0.075f,
			                         p->leaf_deep, p->leaf_mid, (i & 1) ? p->leaf_high : p->leaf_lit,
			                         h_total);
		}
		int n_spikes = is_big ? 3 : 2;
		for (int s = 0; s < n_spikes; s++) {
			float ox = (s == 0) ? -w_total * 0.08f : ((s == 1) ? w_total * 0.11f : 0.0f);
			float oy = (s == 0) ? 0.0f : ((s == 1) ? w_total * 0.06f : -w_total * 0.08f);
			float top_z = h_total * (0.88f - 0.08f * (float)s);
			/* Green stem */
			nd_m3d_add_trunk_tube(b, ox * 0.5f, oy * 0.5f, 0.0f, w_total * 0.035f,
			                      ox, oy, top_z, w_total * 0.028f,
			                      5, 2, p->leaf_deep, p->leaf_mid, p->leaf_lit,
			                      0, 0, h_total, seed + (uint32_t)s * 23u);
			/* Velvety brown cylindrical cattail spadix */
			nd_m3d_add_trunk_tube(b, ox, oy, top_z * 0.58f, w_total * 0.085f,
			                      ox, oy, top_z * 0.86f, w_total * 0.082f,
			                      6, 2, p->bark_dark, p->bark_mid, p->bark_light,
			                      0, 0, h_total, seed + (uint32_t)s * 59u);
		}
		break;
	}

	case ND_BOT3D_PALM_TROPICAL:
	default: {
		/* Curved ringed trunk + crown of arching tropical fronds */
		float trunk_h = h_total * 0.72f;
		float r_base = w_total * 0.058f;
		nd_m3d_add_trunk_tube(b, 0.0f, 0.0f, 0.0f, r_base,
		                      lean * 1.4f, 0.0f, trunk_h, r_base * 0.45f,
		                      7, 5, p->bark_dark, p->bark_mid, p->bark_light,
		                      0, 0, h_total, seed);
		int n_fronds = is_big ? 9 : 7;
		for (int i = 0; i < n_fronds; i++) {
			float ang = ((float)i / (float)n_fronds) * 6.2831853f + (var_idx ? 0.4f : 0.0f);
			float ca = cosf(ang), sa = sinf(ang);
			nd_m3d_add_arching_blade(b, lean * 1.4f, 0.0f, trunk_h,
			                         ca, sa,
			                         w_total * 0.42f, h_total * 0.24f, h_total * 0.22f,
			                         w_total * 0.11f,
			                         p->leaf_deep, p->leaf_mid, (i & 1) ? p->leaf_high : p->leaf_lit,
			                         h_total);
		}
		break;
	}
	}

	if (out_slice) {
		out_slice->first_vert = v_start;
		out_slice->vert_count = b->vert_count - v_start;
		out_slice->first_idx  = i_start;
		out_slice->idx_count  = b->idx_count - i_start;
		out_slice->mesh_w_wu  = w_total;
		out_slice->mesh_h_wu  = h_total;
		out_slice->sprite_w   = sw;
		out_slice->sprite_h   = sh;
		out_slice->anchor_y   = (uint8_t)(sh > 64 ? 6u : 3u);
		out_slice->shadow_rx  = (uint8_t)(sw * 0.34f);
		out_slice->shadow_ry  = (uint8_t)(sw * 0.22f);
		out_slice->pad0       = 0;
	}
}

/* =========================================================================
 * 3D Faceted Geological & Mineral Generator (All 51 NDS_SP_* Specimens)
 * =========================================================================
 * Crystallographic termination modes:
 *   term_mode == 0: Pointed rhombohedral / pyramidal apex (Quartz SiO2, Obsidian shards, Ore crystals)
 *   term_mode == 1: Flat basal pinacoid {0001} termination (Beryl/Emerald Be3Al2Si6O18, Halite NaCl cubes)
 *   term_mode == 2: Trigonal ditrigonal prism with rounded Reuleaux 3-fold symmetry (Tourmaline)
 */
static inline void
nd_m3d_add_crystal_prism(nd_mesh3d_builder_t *b,
                         float x0, float y0, float z0,
                         float dir_x, float dir_y, float dir_z,
                         float length, float radius, int sides, int term_mode,
                         uint32_t col_deep, uint32_t col_mid, uint32_t col_lit, uint32_t col_spec,
                         float mat_class)
{
	if (sides < 3) sides = 3;
	if (sides > 8) sides = 8;
	float axis[3] = { dir_x, dir_y, dir_z };
	nd_m3d_norm3(axis);
	float up_ref[3] = { 0.0f, 1.0f, 0.0f };
	if (fabsf(axis[1]) > 0.85f) { up_ref[0] = 1.0f; up_ref[1] = 0.0f; }
	float u_vec[3] = {
		up_ref[1] * axis[2] - up_ref[2] * axis[1],
		up_ref[2] * axis[0] - up_ref[0] * axis[2],
		up_ref[0] * axis[1] - up_ref[1] * axis[0]
	};
	nd_m3d_norm3(u_vec);
	float v_vec[3] = {
		axis[1] * u_vec[2] - axis[2] * u_vec[1],
		axis[2] * u_vec[0] - axis[0] * u_vec[2],
		axis[0] * u_vec[1] - axis[1] * u_vec[0]
	};
	nd_m3d_norm3(v_vec);

	/* Flat basal pinacoid (Emerald/Halite) extends full shaft length; Quartz tapers at 0.68 into rhombohedral pyramid */
	float shaft_len = (term_mode == 1) ? (length * 0.96f) : (length * 0.68f);
	float bx[9], by[9], bz[9];
	float mx[9], my[9], mz[9];
	float rot_off = (sides == 4) ? 0.78539816f : 0.25f; /* Align 4-sided Halite cubes orthogonally to axes */
	for (int i = 0; i < sides; i++) {
		float ang = ((float)i / (float)sides) * 6.2831853f + rot_off;
		float ca = cosf(ang), sa = sinf(ang);
		/* Trigonal Tourmaline (term_mode == 2): Reuleaux 3-fold symmetry modulation */
		float r_mod = (term_mode == 2) ? (1.0f + 0.24f * cosf(3.0f * ang)) : 1.0f;
		float r_cur = radius * r_mod;
		float rx = (u_vec[0] * ca + v_vec[0] * sa) * r_cur;
		float ry = (u_vec[1] * ca + v_vec[1] * sa) * r_cur;
		float rz = (u_vec[2] * ca + v_vec[2] * sa) * r_cur;
		bx[i] = x0 + rx;
		by[i] = y0 + ry;
		bz[i] = (term_mode == 1 && sides == 4) ? z0 : fmaxf(0.0f, z0 + rz * 0.4f);
		float taper = (term_mode == 1) ? ((sides == 4) ? 1.0f : 0.94f) : 0.90f;
		mx[i] = x0 + axis[0] * shaft_len + rx * taper;
		my[i] = y0 + axis[1] * shaft_len + ry * taper;
		mz[i] = z0 + axis[2] * shaft_len + rz * taper;
	}
	float apex[3] = {
		x0 + axis[0] * ((term_mode == 1) ? shaft_len : length),
		y0 + axis[1] * ((term_mode == 1) ? shaft_len : length),
		z0 + axis[2] * ((term_mode == 1) ? shaft_len : length)
	};

	for (int i = 0; i < sides; i++) {
		int next = (i + 1) % sides;
		float p0[3] = { bx[i], by[i], bz[i] };
		float p1[3] = { bx[next], by[next], bz[next] };
		float p2[3] = { mx[next], my[next], mz[next] };
		float p3[3] = { mx[i], my[i], mz[i] };

		/* Pick crisp facet color from analytical face normal */
		float fn[3] = {
			(p1[1] - p0[1]) * (p2[2] - p0[2]) - (p1[2] - p0[2]) * (p2[1] - p0[1]),
			(p1[2] - p0[2]) * (p2[0] - p0[0]) - (p1[0] - p0[0]) * (p2[2] - p0[2]),
			(p1[0] - p0[0]) * (p2[1] - p0[1]) - (p1[1] - p0[1]) * (p2[0] - p0[0])
		};
		nd_m3d_norm3(fn);
		float sun = fn[0] * (-0.50f) + fn[1] * (-0.52f) + fn[2] * 0.69f;
		uint32_t c_shaft = (sun > 0.45f) ? col_spec : ((sun > 0.10f) ? col_lit : ((sun > -0.22f) ? col_mid : col_deep));
		uint32_t c_tip   = (term_mode == 1) ? col_spec : ((sun > 0.25f) ? col_spec : ((sun > -0.10f) ? col_lit : col_mid));

		nd_m3d_emit_faceted_tri(b, p0, p1, p2, 0.0f, 0.0f, 0.0f, 0.55f, 0.55f, 0.88f, col_deep, c_shaft, c_shaft, mat_class);
		nd_m3d_emit_faceted_tri(b, p0, p2, p3, 0.0f, 0.0f, 0.0f, 0.55f, 0.88f, 0.88f, col_deep, c_shaft, c_shaft, mat_class);
		nd_m3d_emit_faceted_tri(b, p3, p2, apex, 0.0f, 0.0f, 0.0f, 0.88f, 0.88f, 1.00f, c_tip, c_tip, col_spec, mat_class);
	}
}

/* Emit a 3D cleaved polyhedral boulder / rock mass with crisp geological fracture planes */
static inline void
nd_m3d_add_cleaved_boulder(nd_mesh3d_builder_t *b,
                           float cx, float cy, float cz,
                           float rx, float ry, float rz,
                           int lon_segs, int lat_rings,
                           float angularity, int stratified,
                           uint32_t col_deep, uint32_t col_mid, uint32_t col_lit, uint32_t col_high,
                           int has_moss, uint32_t col_moss_dark, uint32_t col_moss_lit,
                           uint32_t seed, float mat_class)
{
	if (lon_segs < 5) lon_segs = 5;
	if (lon_segs > 10) lon_segs = 10;
	if (lat_rings < 3) lat_rings = 3;
	if (lat_rings > 5) lat_rings = 5;

	float pts[6][11][3];
	for (int ilat = 0; ilat <= lat_rings; ilat++) {
		float v = (float)ilat / (float)lat_rings;
		/* From ground contact skirt (theta = -0.18*PI) to summit (theta = +0.50*PI) */
		float theta = (-0.15f + 0.65f * v) * 3.14159265f;
		float ct = cosf(theta), st = sinf(theta);

		for (int ilon = 0; ilon < lon_segs; ilon++) {
			float u = (float)ilon / (float)lon_segs;
			float phi = u * 6.2831853f + (float)(ilat & 1) * (1.57f / (float)lon_segs);
			float cp = cosf(phi), sp = sinf(phi);

			uint32_t h = nd_m3d_hash(seed + (uint32_t)(ilat * 53 + ilon * 29));
			float facet_mod = 1.0f + angularity * ((((float)(h & 0xFFu) / 255.0f) - 0.5f) * 0.52f);
			if (stratified) {
				/* Sedimentary / laterite horizontal step shelves */
				facet_mod *= (ilat & 1) ? 1.12f : 0.92f;
			}
			float px = cx + ct * cp * rx * facet_mod;
			float py = cy + ct * sp * ry * facet_mod;
			float pz = (ilat == 0) ? cz : (cz + fmaxf(0.2f, (st + 0.15f) * rz * facet_mod));
			if (ilat == lat_rings) {
				/* Narrow summit ridge */
				px = cx + cp * rx * 0.18f;
				py = cy + sp * ry * 0.18f;
				pz = cz + rz * (0.92f + 0.12f * ((float)(h & 0x3Fu) / 63.0f));
			}
			pts[ilat][ilon][0] = px;
			pts[ilat][ilon][1] = py;
			pts[ilat][ilon][2] = pz;
		}
	}

	for (int ilat = 0; ilat < lat_rings; ilat++) {
		for (int ilon = 0; ilon < lon_segs; ilon++) {
			int next = (ilon + 1) % lon_segs;
			const float *p00 = pts[ilat][ilon];
			const float *p10 = pts[ilat][next];
			const float *p11 = pts[ilat + 1][next];
			const float *p01 = pts[ilat + 1][ilon];

			float fn[3] = {
				(p10[1] - p00[1]) * (p11[2] - p00[2]) - (p10[2] - p00[2]) * (p11[1] - p00[1]),
				(p10[2] - p00[2]) * (p11[0] - p00[0]) - (p10[0] - p00[0]) * (p11[2] - p00[2]),
				(p10[0] - p00[0]) * (p11[1] - p00[1]) - (p10[1] - p00[1]) * (p11[0] - p00[0])
			};
			nd_m3d_norm3(fn);
			float sun = fn[0] * (-0.50f) + fn[1] * (-0.52f) + fn[2] * 0.69f;
			uint32_t c_face = (sun > 0.42f) ? col_high : ((sun > 0.08f) ? col_lit : ((sun > -0.22f) ? col_mid : col_deep));
			uint32_t c_top  = c_face;

			if (has_moss && (ilat >= lat_rings - 2) && fn[2] > 0.35f) {
				c_top  = (sun > 0.15f) ? col_moss_lit : col_moss_dark;
				if (ilat == lat_rings - 1) c_face = c_top;
			}
			float ao0 = (ilat == 0) ? 0.42f : (0.55f + 0.40f * ((float)ilat / (float)lat_rings));
			float ao1 = 0.55f + 0.45f * ((float)(ilat + 1) / (float)lat_rings);

			nd_m3d_emit_faceted_tri(b, p00, p10, p11, 0.0f, 0.0f, 0.0f, ao0, ao0, ao1, c_face, c_face, c_top, mat_class);
			nd_m3d_emit_faceted_tri(b, p00, p11, p01, 0.0f, 0.0f, 0.0f, ao0, ao1, ao1, c_face, c_top, c_top, mat_class);
		}
	}
}

/* Build a complete 3D geological/mineral mesh for specimen index `sp` (0..50) */
static inline void
nd_m3d_build_stone_spec(nd_mesh3d_builder_t *b,
                        unsigned sp, uint16_t sw, uint16_t sh,
                        nd_mesh3d_slice_t *out_slice)
{
	uint32_t v_start = b->vert_count;
	uint32_t i_start = b->idx_count;
	float w = (float)sw;
	float h = (float)sh;
	uint32_t seed = nd_m3d_hash(0x510E5u + sp * 4099u);

	/* Classify the 51 canonical NDS_SP_* specimens by geological family & palette */
	uint32_t c_deep = 0xFF282C32u, c_mid = 0xFF48505Au, c_lit = 0xFF6E7A88u, c_high = 0xFF98A6B6u;
	uint32_t c_vein_dark = 0xFF305068u, c_vein_lit = 0xFF5090B8u, c_vein_spec = 0xFF90D8F8u;
	int is_crystal = 0, is_cluster = 0, is_ore = 0, is_pebble = 0, is_boulder = 0;
	int is_stratified = 0, has_moss = 0, has_snow = 0;
	uint32_t c_moss_dark = 0xFF225026u, c_moss_lit = 0xFF468E44u;
	float angularity = 0.55f;
	float mat_class = ND_MAT3D_STONE_MATTE;

	switch (sp) {
	case 0:  /* CristalQuartzo_solo */
	case 1:  /* CristaisQuartzo */
		is_crystal = 1; is_cluster = (sp == 1);
		c_deep = 0xFF685848u; c_mid = 0xFFA89888u; c_lit = 0xFFE4D8C8u; c_high = 0xFFFBF8F4u;
		mat_class = ND_MAT3D_CRYSTAL_ORE;
		break;
	case 2:  /* CristalTurmalina_solo */
	case 3:  /* ClusterTurmalina */
		is_crystal = 1; is_cluster = (sp == 3);
		c_deep = 0xFF26381Cu; c_mid = 0xFF426A2Eu; c_lit = 0xFF68A64Au; c_high = 0xFFA2E482u;
		mat_class = ND_MAT3D_CRYSTAL_ORE;
		break;
	case 4:  /* SoloEsmeralda */
	case 5:  /* ClusterEsmeralda */
		is_crystal = 1; is_cluster = (sp == 5);
		c_deep = 0xFF244C18u; c_mid = 0xFF3A8828u; c_lit = 0xFF56CA42u; c_high = 0xFF94F682u;
		mat_class = ND_MAT3D_CRYSTAL_ORE;
		break;
	case 6:  /* SoloObsidiana */
	case 7:  /* ClusterObsidiana */
		is_crystal = 1; is_cluster = (sp == 7);
		c_deep = 0xFF140E10u; c_mid = 0xFF281C22u; c_lit = 0xFF523A46u; c_high = 0xFFA88898u;
		mat_class = ND_MAT3D_CRYSTAL_ORE;
		break;
	case 8:  /* SoloJade */
		is_crystal = 1; is_cluster = 0;
		c_deep = 0xFF2E4A28u; c_mid = 0xFF4E8244u; c_lit = 0xFF78B86Cu; c_high = 0xFFB2E6A6u;
		mat_class = ND_MAT3D_CRYSTAL_ORE;
		break;
	case 9:  /* SoloAmbar */
		is_crystal = 1; is_cluster = 0;
		c_deep = 0xFF12366Eu; c_mid = 0xFF1C62B8u; c_lit = 0xFF2E9AE8u; c_high = 0xFF72D8FCu;
		mat_class = ND_MAT3D_CRYSTAL_ORE;
		break;
	case 10: /* SoloSal (cubic/cleaved halite rock salt) */
		is_crystal = 1; is_cluster = 1;
		c_deep = 0xFF787C88u; c_mid = 0xFFB2B8C8u; c_lit = 0xFFDCE2F0u; c_high = 0xFFF6F8FCu;
		mat_class = ND_MAT3D_CRYSTAL_ORE;
		break;
	case 11: /* SoloCarvao */
	case 12: /* ClusterCarvao */
		is_ore = 1; is_cluster = (sp == 12);
		c_deep = 0xFF141618u; c_mid = 0xFF24282Cu; c_lit = 0xFF3C4248u; c_high = 0xFF606872u;
		c_vein_dark = 0xFF0E1012u; c_vein_lit = 0xFF2E343Cu; c_vein_spec = 0xFF586270u;
		mat_class = ND_MAT3D_CRYSTAL_ORE;
		break;
	case 13: /* PepitaCobre_solo */
	case 14: /* ClusterCobre */
		is_ore = 1; is_cluster = (sp == 14);
		c_deep = 0xFF243448u; c_mid = 0xFF3E5670u; c_lit = 0xFF5A7898u; c_high = 0xFF7A9ABAu;
		c_vein_dark = 0xFF244678u; c_vein_lit = 0xFF3E78C4u; c_vein_spec = 0xFF6EB4F4u;
		mat_class = ND_MAT3D_CRYSTAL_ORE;
		break;
	case 15: /* PedacoFerro_solo */
	case 16: /* ClusterFerro */
		is_ore = 1; is_cluster = (sp == 16);
		c_deep = 0xFF22262Cu; c_mid = 0xFF3C444Eu; c_lit = 0xFF5A6674u; c_high = 0xFF8290A0u;
		c_vein_dark = 0xFF26344Au; c_vein_lit = 0xFF546C88u; c_vein_spec = 0xFF98B0C8u;
		mat_class = ND_MAT3D_CRYSTAL_ORE;
		break;
	case 17: /* SoloPrata */
	case 18: /* ClusterPrata */
		is_ore = 1; is_cluster = (sp == 18);
		c_deep = 0xFF2C323Au; c_mid = 0xFF4C5662u; c_lit = 0xFF728090u; c_high = 0xFF9AA8B8u;
		c_vein_dark = 0xFF687482u; c_vein_lit = 0xFFAEC0D2u; c_vein_spec = 0xFFEBF4FCu;
		mat_class = ND_MAT3D_CRYSTAL_ORE;
		break;
	case 19: /* PepitaOuro_solo */
	case 20: /* PepitasOuro */
		is_ore = 1; is_cluster = (sp == 20);
		c_deep = 0xFF26323Eu; c_mid = 0xFF425466u; c_lit = 0xFF62788Eu; c_high = 0xFF88A0B6u;
		c_vein_dark = 0xFF14689Cu; c_vein_lit = 0xFF24B4E8u; c_vein_spec = 0xFF72ECFCu;
		mat_class = ND_MAT3D_CRYSTAL_ORE;
		break;

	/* Small ground stones & pebbles (21..33) */
	case 21: /* SeixoRio */
	case 22: /* PedraArredondada */
	case 23: /* SeixosRio */
		is_pebble = 1; is_cluster = (sp == 23); angularity = 0.18f;
		c_deep = 0xFF343C44u; c_mid = 0xFF56626Eu; c_lit = 0xFF7C8C9Au; c_high = 0xFFA6B6C4u;
		break;
	case 24: /* PedraMusgo */
	case 25: /* ClusterPedrasMusgo */
		is_pebble = 1; is_cluster = (sp == 25); has_moss = 1; angularity = 0.35f;
		c_deep = 0xFF2E3638u; c_mid = 0xFF4C585Au; c_lit = 0xFF6E7E80u; c_high = 0xFF94A6A8u;
		break;
	case 26: /* CalcareoSolto */
	case 27: /* ClusterCalcareo */
		is_pebble = 1; is_cluster = (sp == 27); is_stratified = 1; angularity = 0.48f;
		c_deep = 0xFF526674u; c_mid = 0xFF7A94A4u; c_lit = 0xFFA4BECDu; c_high = 0xFFCCE2EEu;
		break;
	case 28: /* FragmentoXisto */
	case 29: /* ClusterXisto */
		is_pebble = 1; is_cluster = (sp == 29); is_stratified = 1; angularity = 0.72f;
		c_deep = 0xFF262A32u; c_mid = 0xFF3E4652u; c_lit = 0xFF5A6676u; c_high = 0xFF7E8CA0u;
		break;
	case 30: /* FragmentoBasalto */
	case 31: /* PedraPomes */
	case 32: /* ClusterEscoria */
	case 33: /* ClusterBasalto */
		is_pebble = 1; is_cluster = (sp >= 32); angularity = 0.68f;
		if (sp == 31) {
			c_deep = 0xFF484E54u; c_mid = 0xFF727A82u; c_lit = 0xFF9EA8B0u; c_high = 0xFFC6D0D8u;
		} else {
			c_deep = 0xFF181A20u; c_mid = 0xFF2A2E36u; c_lit = 0xFF424854u; c_high = 0xFF626A7Au;
		}
		break;

	/* Medium rocks & outcrops (34..42) */
	case 34: /* RochaMusgo */
	case 35: /* RochaGranitoMusgo */
		has_moss = 1; angularity = 0.52f;
		c_deep = 0xFF2C3438u; c_mid = 0xFF4E5A60u; c_lit = 0xFF74848Cu; c_high = 0xFF9EB0B8u;
		break;
	case 36: /* RochaGelo */
		has_snow = 1; angularity = 0.65f;
		c_deep = 0xFF48382Cu; c_mid = 0xFF745E4Au; c_lit = 0xFFA48A72u; c_high = 0xFFE4F0F8u;
		break;
	case 37: /* RochaCongelada */
		has_snow = 1; angularity = 0.70f;
		c_deep = 0xFF5E422Au; c_mid = 0xFF926A46u; c_lit = 0xFFC69C72u; c_high = 0xFFEBF6FCu;
		break;
	case 38: /* RochaLaterita */
	case 39: /* RochaErodida */
		is_stratified = 1; angularity = 0.58f;
		c_deep = 0xFF223458u; c_mid = 0xFF345488u; c_lit = 0xFF4C7AB6u; c_high = 0xFF72A4DCu;
		break;
	case 40: /* RochaVulcanica */
		angularity = 0.74f;
		c_deep = 0xFF16181Eu; c_mid = 0xFF282C34u; c_lit = 0xFF424652u; c_high = 0xFF666C7Cu;
		break;
	case 41: /* RochaTropical */
	case 42: /* RochaRaizes */
		has_moss = 1; angularity = 0.50f;
		c_deep = 0xFF242E2Cu; c_mid = 0xFF3E4E4Au; c_lit = 0xFF5C726Cu; c_high = 0xFF829C94u;
		break;

	/* Large monolithic boulders (43..50) */
	case 43: /* PedregulhoFloresta */
	case 44: /* PedregulhoPartido */
		is_boulder = 1; has_moss = (sp == 43); angularity = 0.62f;
		c_deep = 0xFF2A3036u; c_mid = 0xFF48525Cu; c_lit = 0xFF6C7A86u; c_high = 0xFF96A6B4u;
		break;
	case 45: /* ErraticoGlacial */
		is_boulder = 1; has_snow = 1; angularity = 0.68f;
		c_deep = 0xFF3A3632u; c_mid = 0xFF605A54u; c_lit = 0xFF8C867Eu; c_high = 0xFFDCE8F2u;
		break;
	case 46: /* MonolitoCalcario */
		is_boulder = 1; is_stratified = 1; angularity = 0.64f;
		c_deep = 0xFF506270u; c_mid = 0xFF7890A0u; c_lit = 0xFFA2BCCCu; c_high = 0xFFCCE4F0u;
		break;
	case 47: /* AfloramentoSavana */
		is_boulder = 1; is_stratified = 1; angularity = 0.58f;
		c_deep = 0xFF24385Au; c_mid = 0xFF3A5A8Cu; c_lit = 0xFF5682BCu; c_high = 0xFF7CACE2u;
		break;
	case 48: /* BlocoBasalto */
		is_boulder = 1; angularity = 0.78f;
		c_deep = 0xFF14161Cu; c_mid = 0xFF262A32u; c_lit = 0xFF3E4450u; c_high = 0xFF5E6676u;
		break;
	case 49: /* PedregulhoSelva */
	case 50: /* RochaMusgoGigante */
	default:
		is_boulder = 1; has_moss = 1; angularity = 0.54f;
		c_deep = 0xFF242E2Au; c_mid = 0xFF3E4E48u; c_lit = 0xFF5C726Au; c_high = 0xFF829A90u;
		break;
	}

	if (has_snow) {
		has_moss = 1;
		c_moss_dark = 0xFFB8C8D8u;
		c_moss_lit  = 0xFFF0F6FCu;
	}

	if (is_crystal) {
		if (sp == 9) {
			/* SoloAmbar (Amber): Organic rounded botryoidal resin droplet with warm golden glow */
			nd_m3d_add_cleaved_boulder(b, 0.0f, 0.0f, 0.0f,
			                           w * 0.36f, w * 0.32f, h * 0.82f,
			                           8, 4, 0.12f, 0,
			                           c_deep, c_mid, c_lit, c_high,
			                           0, 0, 0, seed, mat_class);
		} else {
			/* Basal host rock bed + faceted crystallographic prisms */
			if (sp != 10) {
				nd_m3d_add_cleaved_boulder(b, 0.0f, 0.0f, 0.0f,
				                           w * 0.34f, w * 0.28f, h * 0.26f,
				                           7, 3, 0.55f, 0,
				                           0xFF242830u, 0xFF3E4450u, 0xFF5C6474u, 0xFF7E8898u,
				                           0, 0, 0, seed, ND_MAT3D_STONE_MATTE);
			}
			int n_prisms = is_cluster ? 5 : 2;
			/* Crystallographic symmetry & termination:
			 *   Emerald (sp 4,5) & Jade (sp 8) & Halite (sp 10): term_mode = 1 (flat basal pinacoid {0001})
			 *   Tourmaline (sp 2,3): term_mode = 2 (trigonal ditrigonal Reuleaux cross-section)
			 *   Quartz (sp 0,1) & Obsidian (sp 6,7): term_mode = 0 (rhombohedral/pyramidal pointed apex) */
			int term_mode = (sp == 4 || sp == 5 || sp == 8 || sp == 10) ? 1 :
			                ((sp == 2 || sp == 3) ? 2 : 0);
			int sides = (sp == 10) ? 4 : 6; /* Halite uses 4-sided orthogonal Hopper cubes; Quartz/Emerald/Tourmaline use 6 */
			for (int i = 0; i < n_prisms; i++) {
				float ang = ((float)i / (float)n_prisms) * 6.2831853f + 0.4f;
				/* Upright primary prism for Emerald/Halite so basal pinacoid has N_z ≈ 1.0 */
				float tilt = (sp == 10) ? 0.0f : ((i == 0) ? ((term_mode == 1) ? 0.0f : 0.12f) : (0.34f + 0.10f * (float)(i & 1)));
				float dx = cosf(ang) * sinf(tilt);
				float dy = sinf(ang) * sinf(tilt);
				float dz = cosf(tilt);
				float ox = (i == 0) ? 0.0f : cosf(ang) * w * 0.15f;
				float oy = (i == 0) ? 0.0f : sinf(ang) * w * 0.12f;
				float len = h * ((i == 0) ? 0.88f : (0.58f + 0.10f * (float)(i & 1)));
				float rad = w * ((i == 0) ? ((sp == 10) ? 0.24f : 0.18f) : 0.14f);
				nd_m3d_add_crystal_prism(b, ox, oy, (sp == 10) ? 0.0f : (h * 0.08f), dx, dy, dz,
				                         len, rad, sides, term_mode,
				                         c_deep, c_mid, c_lit, c_high, mat_class);
			}
		}
	} else if (is_ore) {
		/* Heavy matrix rock embedded with lustrous metallic ore nuggets/veins */
		nd_m3d_add_cleaved_boulder(b, 0.0f, 0.0f, 0.0f,
		                           w * 0.38f, w * 0.32f, h * 0.62f,
		                           7, 3, 0.62f, 0,
		                           c_deep, c_mid, c_lit, c_high,
		                           0, 0, 0, seed, ND_MAT3D_STONE_MATTE);
		int n_nuggets = is_cluster ? 5 : 3;
		for (int i = 0; i < n_nuggets; i++) {
			float ang = ((float)i / (float)n_nuggets) * 6.2831853f + 0.6f;
			float ox = cosf(ang) * w * 0.16f;
			float oy = sinf(ang) * w * 0.14f;
			float oz = h * (0.20f + 0.14f * (float)(i % 3));
			nd_m3d_add_crystal_prism(b, ox, oy, oz,
			                         cosf(ang) * 0.45f, sinf(ang) * 0.45f, 0.88f,
			                         h * 0.44f, w * 0.16f, 5, 0,
			                         c_vein_dark, c_vein_lit, c_vein_spec, c_vein_spec,
			                         ND_MAT3D_CRYSTAL_ORE);
		}
	} else if (is_pebble) {
		int count = is_cluster ? 3 : 1;
		for (int i = 0; i < count; i++) {
			float ox = (i == 0) ? 0.0f : ((i == 1) ? -w * 0.20f : w * 0.20f);
			float oy = (i == 0) ? 0.0f : ((i == 1) ? w * 0.10f : -w * 0.08f);
			float sc = (i == 0) ? 1.0f : 0.72f;
			nd_m3d_add_cleaved_boulder(b, ox, oy, 0.0f,
			                           w * 0.38f * sc, w * 0.32f * sc, h * 0.74f * sc,
			                           7, 3, angularity, is_stratified,
			                           c_deep, c_mid, c_lit, c_high,
			                           has_moss, c_moss_dark, c_moss_lit,
			                           seed + (uint32_t)i * 73u, mat_class);
		}
	} else if (is_boulder) {
		/* Massive multi-lobed monolithic boulder + secondary cleaved shoulder block */
		nd_m3d_add_cleaved_boulder(b, -w * 0.05f, 0.0f, 0.0f,
		                           w * 0.42f, w * 0.35f, h * 0.86f,
		                           8, 4, angularity, is_stratified,
		                           c_deep, c_mid, c_lit, c_high,
		                           has_moss, c_moss_dark, c_moss_lit,
		                           seed, mat_class);
		nd_m3d_add_cleaved_boulder(b, w * 0.22f, w * 0.10f, 0.0f,
		                           w * 0.26f, w * 0.22f, h * 0.54f,
		                           7, 3, angularity, is_stratified,
		                           c_deep, c_mid, c_lit, c_high,
		                           has_moss, c_moss_dark, c_moss_lit,
		                           seed + 313u, mat_class);
	} else {
		/* Medium outcrop rock */
		nd_m3d_add_cleaved_boulder(b, 0.0f, 0.0f, 0.0f,
		                           w * 0.40f, w * 0.34f, h * 0.82f,
		                           8, 4, angularity, is_stratified,
		                           c_deep, c_mid, c_lit, c_high,
		                           has_moss, c_moss_dark, c_moss_lit,
		                           seed, mat_class);
	}

	if (out_slice) {
		out_slice->first_vert = v_start;
		out_slice->vert_count = b->vert_count - v_start;
		out_slice->first_idx  = i_start;
		out_slice->idx_count  = b->idx_count - i_start;
		out_slice->mesh_w_wu  = w;
		out_slice->mesh_h_wu  = h;
		out_slice->sprite_w   = sw;
		out_slice->sprite_h   = sh;
		out_slice->anchor_y   = (uint8_t)(sh > 28 ? 3u : 2u);
		out_slice->shadow_rx  = (uint8_t)(sw * 0.42f);
		out_slice->shadow_ry  = (uint8_t)(sw * 0.28f);
		out_slice->pad0       = 0;
	}
}

/* =========================================================================
 * Option B: Pure-C Software 3D Orthographic Diorama Rasterizer
 * Bakes any 3D mesh slice into lossless 32-bit RGBA Albedo + Tangent-Space Normal Map
 * ========================================================================= */
static inline void
nd_seven_mesh3d_bake_ortho_rgba(const nd_mesh3d_vert_t *verts,
                                const uint32_t *indices,
                                const nd_mesh3d_slice_t *slice,
                                uint32_t *out_albedo_rgba,
                                uint32_t *out_normal_rgba,
                                uint32_t out_stride_px)
{
	if (!verts || !indices || !slice || !out_albedo_rgba)
		return;
	int W = (int)slice->sprite_w;
	int H = (int)slice->sprite_h;
	if (W <= 0 || H <= 0 || W > 256 || H > 256)
		return;

	float zbuf[256 * 256];
	for (int y = 0; y < H; y++) {
		for (int x = 0; x < W; x++) {
			out_albedo_rgba[(uint32_t)y * out_stride_px + (uint32_t)x] = 0u;
			if (out_normal_rgba)
				out_normal_rgba[(uint32_t)y * out_stride_px + (uint32_t)x] = 0xFFFF8080u;
			zbuf[y * W + x] = -1e30f;
		}
	}

	/* 55-deg diorama pitch camera projection:
	 *   Screen Right (+X) = +East (+lx)
	 *   Screen Up    (-Y) = +Up * sin(55°) - South * cos(55°)
	 *   Camera Depth (+Z) = +Up * cos(55°) + South * sin(55°) */
	const float sin_p = 0.81915204f; /* sin(55 deg) */
	const float cos_p = 0.57357644f; /* cos(55 deg) */
	const float cx_scr = (float)W * 0.5f;
	const float by_scr = (float)H - (float)slice->anchor_y - 2.0f;
	const float mw = (slice->mesh_w_wu > 1.0f) ? slice->mesh_w_wu : (float)W;
	const float mh = (slice->mesh_h_wu > 1.0f) ? slice->mesh_h_wu : (float)H;
	const float px_per_wu = fminf((float)W / mw, (float)H / mh);

	for (uint32_t ii = 0; ii + 2u < slice->idx_count; ii += 3u) {
		const nd_mesh3d_vert_t *v0 = &verts[ indices[slice->first_idx + ii + 0u] ];
		const nd_mesh3d_vert_t *v1 = &verts[ indices[slice->first_idx + ii + 1u] ];
		const nd_mesh3d_vert_t *v2 = &verts[ indices[slice->first_idx + ii + 2u] ];

		float sx[3], sy[3], sz[3], card_u[3], card_v[3];
		const nd_mesh3d_vert_t *tri[3] = { v0, v1, v2 };
		int is_uv_card = (v0->pos[3] > 1.0f && v1->pos[3] > 1.0f && v2->pos[3] > 1.0f);
		float mat0 = v0->col[3];
		int fol_kind = 0; /* 1=conifer needle, 2=broadleaf ovate, 3=broadleaf lobed, 4=herb/reed blade */
		float v_base = 0.200f, v_span = 0.038f;
		if (is_uv_card && mat0 >= 0.198f && mat0 <= 0.352f) {
			if (mat0 < 0.239f) { fol_kind = 1; v_base = 0.200f; v_span = 0.038f; }
			else if (mat0 < 0.279f) { fol_kind = 2; v_base = 0.240f; v_span = 0.038f; }
			else if (mat0 < 0.319f) { fol_kind = 3; v_base = 0.280f; v_span = 0.038f; }
			else { fol_kind = 4; v_base = 0.320f; v_span = 0.030f; }
		}
		for (int k = 0; k < 3; k++) {
			float lx = tri[k]->pos[0] * px_per_wu;
			float ly = tri[k]->pos[1] * px_per_wu;
			float lz = tri[k]->pos[2] * px_per_wu;
			sx[k] = cx_scr + lx;
			sy[k] = by_scr - (lz * sin_p - ly * cos_p);
			sz[k] = lz * cos_p + ly * sin_p;
			card_u[k] = (tri[k]->pos[3] > 1.0f) ? ((floorf(tri[k]->pos[3]) - 1.0f) / 249.0f) : 0.0f;
			card_v[k] = (fol_kind > 0) ? fminf(1.0f, fmaxf(0.0f, (tri[k]->col[3] - v_base) / v_span)) : 0.5f;
		}

		float min_x = fminf(sx[0], fminf(sx[1], sx[2]));
		float max_x = fmaxf(sx[0], fmaxf(sx[1], sx[2]));
		float min_y = fminf(sy[0], fminf(sy[1], sy[2]));
		float max_y = fmaxf(sy[0], fmaxf(sy[1], sy[2]));

		int x0 = (int)floorf(min_x);
		int x1 = (int)ceilf(max_x);
		int y0 = (int)floorf(min_y);
		int y1 = (int)ceilf(max_y);
		if (x0 < 0) x0 = 0;
		if (y0 < 0) y0 = 0;
		if (x1 >= W) x1 = W - 1;
		if (y1 >= H) y1 = H - 1;

		float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
		if (fabsf(area) < 1e-5f)
			continue;
		float inv_area = 1.0f / area;

		for (int py = y0; py <= y1; py++) {
			for (int px = x0; px <= x1; px++) {
				float pxf = (float)px + 0.5f;
				float pyf = (float)py + 0.5f;

				float w0 = ((sx[1] - pxf) * (sy[2] - pyf) - (sx[2] - pxf) * (sy[1] - pyf)) * inv_area;
				float w1 = ((sx[2] - pxf) * (sy[0] - pyf) - (sx[0] - pxf) * (sy[2] - pyf)) * inv_area;
				float w2 = 1.0f - w0 - w1;
				if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f)
					continue;

				float z = w0 * sz[0] + w1 * sz[1] + w2 * sz[2];
				if (z <= zbuf[py * W + px])
					continue;

				float nx = w0 * v0->norm[0] + w1 * v1->norm[0] + w2 * v2->norm[0];
				float ny = w0 * v0->norm[1] + w1 * v1->norm[1] + w2 * v2->norm[1];
				float nz = w0 * v0->norm[2] + w1 * v1->norm[2] + w2 * v2->norm[2];
				float n3[3] = { nx, ny, nz };
				nd_m3d_norm3(n3);

				float ao = w0 * v0->norm[3] + w1 * v1->norm[3] + w2 * v2->norm[3];
				float cr = w0 * v0->col[0] + w1 * v1->col[0] + w2 * v2->col[0];
				float cg = w0 * v0->col[1] + w1 * v1->col[1] + w2 * v2->col[1];
				float cb = w0 * v0->col[2] + w1 * v1->col[2] + w2 * v2->col[2];
				float mat = v0->col[3];

				/* UV-space botanical silhouette cutout & normal/albedo synthesis for branchlet cards */
				if (fol_kind > 0) {
					float cu = w0 * card_u[0] + w1 * card_u[1] + w2 * card_u[2];
					float cv = w0 * card_v[0] + w1 * card_v[1] + w2 * card_v[2];
					float dv = cv - 0.5f;
					float adv = fabsf(dv);

					if (fol_kind == 1) {
						/* Pectinate conifer needle-comb bough: woody central rachis + angled needle fascicles */
						float rachis_w = 0.042f * (1.0f - 0.65f * cu);
						if (adv < rachis_w) {
							cr = cr * 0.45f + 0.16f;
							cg = cg * 0.45f + 0.11f;
							cb = cb * 0.45f + 0.07f;
						} else {
							float env_w = 0.48f * sinf(fminf(1.0f, cu * 1.15f + 0.12f) * 2.85f);
							if (adv > env_w || cu > 0.98f)
								continue;
							float u_needle = cu - adv * 0.78f;
							float phase = u_needle * 18.0f + ((dv > 0.0f) ? 0.35f : 0.0f);
							float comb = sinf(phase * 6.2831853f);
							float r_frac = adv / fmaxf(0.05f, env_w);
							if (comb < -0.25f + 0.72f * r_frac)
								continue;
							float n_roll = cosf(phase * 6.2831853f) * 0.32f;
							n3[0] += n_roll;
							n3[1] -= n_roll * 0.6f;
							nd_m3d_norm3(n3);
							float tip_bright = 0.88f + 0.24f * r_frac;
							cr = fminf(1.0f, cr * tip_bright);
							cg = fminf(1.0f, cg * tip_bright);
							cb = fminf(1.0f, cb * tip_bright);
						}
					} else if (fol_kind == 2 || fol_kind == 3) {
						/* Broadleaf twig stem + alternate/sub-opposite serrated or lobed leaves */
						float stem_w = 0.032f * (1.0f - 0.75f * cu);
						if (adv < stem_w && cu < 0.86f) {
							cr = cr * 0.55f + 0.14f;
							cg = cg * 0.55f + 0.11f;
							cb = cb * 0.55f + 0.07f;
						} else {
							float env_w = 0.48f * sinf(fminf(1.0f, cu * 1.05f + 0.08f) * 2.95f);
							if (adv > env_w)
								continue;
							float u_leaf = cu * 5.2f - adv * 1.6f;
							float leaf_idx = floorf(u_leaf);
							float lf = u_leaf - leaf_idx;
							float side_phase = fmodf(fabsf(leaf_idx), 2.0f);
							if (dv < 0.0f)
								lf = fmodf(lf + 0.48f * side_phase, 1.0f);
							float r_frac = adv / fmaxf(0.05f, env_w);
							float serrate = (fol_kind == 3)
								? (0.14f * sinf(r_frac * 15.7f))
								: (0.06f * sinf(r_frac * 34.0f));
							float leaf_half_w = 0.44f * sinf(fminf(1.0f, r_frac) * 3.05f) + serrate;
							if (fabsf(lf - 0.5f) > leaf_half_w)
								continue;
							float vein = (lf - 0.5f) * 0.45f;
							n3[0] += vein;
							n3[2] += (0.22f - 0.44f * fabsf(lf - 0.5f));
							nd_m3d_norm3(n3);
							float v_mod = 0.86f + 0.26f * (1.0f - fabsf(lf - 0.5f) * 1.8f);
							cr = fminf(1.0f, cr * v_mod);
							cg = fminf(1.0f, cg * v_mod);
							cb = fminf(1.0f, cb * v_mod);
						}
					} else if (fol_kind == 4) {
						/* Tapered sword/rosette blade with pointed apex & parallel venation */
						float blade_hw = 0.48f * (1.0f - 0.78f * cu * cu);
						if (adv > blade_hw)
							continue;
						float ridge = sinf(dv * 38.0f) * 0.14f;
						n3[0] += ridge;
						nd_m3d_norm3(n3);
					}
				}

				zbuf[py * W + px] = z;

				/* Continuous PBR directional sun shading + chlorophyll subsurface rim scatter + specular highlight */
				float ndotl = n3[0] * (-0.48f) + n3[1] * (-0.50f) + n3[2] * 0.72f;
				float wrap_diff = fminf(1.0f, fmaxf(0.0f, (ndotl + 0.30f) / 1.30f));
				float hemi_sky = 0.50f + 0.50f * n3[2];
				float amb = (0.34f + 0.24f * hemi_sky) * (0.32f + 0.68f * ao);
				float sun_self = fminf(1.0f, fmaxf(0.25f, (ao - 0.18f) * 1.35f));
				float direct = 0.82f * wrap_diff * sun_self;
				float sss = (mat >= 0.18f && mat <= 0.55f)
					? (fmaxf(0.0f, -ndotl + 0.35f) * 0.22f * ao)
					: 0.0f;
				float light = amb + direct;
				float spec = (mat >= 0.90f && ndotl > 0.58f)
					? (powf((ndotl - 0.58f) / 0.42f, 2.0f) * 0.32f)
					: ((mat >= 0.18f && mat <= 0.35f && ndotl > 0.45f) ? 0.05f * ao : 0.0f);

				int ir = (int)((cr * (light + sss * 0.85f) + spec) * 255.0f + 0.5f);
				int ig = (int)((cg * (light + sss * 1.15f) + spec) * 255.0f + 0.5f);
				int ib = (int)((cb * (light + sss * 0.25f) + spec) * 255.0f + 0.5f);
				if (ir < 0) ir = 0; else if (ir > 255) ir = 255;
				if (ig < 0) ig = 0; else if (ig > 255) ig = 255;
				if (ib < 0) ib = 0; else if (ib > 255) ib = 255;

				out_albedo_rgba[(uint32_t)py * out_stride_px + (uint32_t)px] =
					0xFF000000u | ((uint32_t)ib << 16) | ((uint32_t)ig << 8) | (uint32_t)ir;

				if (out_normal_rgba) {
					/* Project 3D normal into billboard tangent space:
					 *   T_x = +East (+n3[0])
					 *   T_y = +Billboard Up (n3[2]*sin_p - n3[1]*cos_p)
					 *   T_z = +Camera facing (n3[2]*cos_p + n3[1]*sin_p) */
					float tx = n3[0];
					float ty = n3[2] * sin_p - n3[1] * cos_p;
					float tz = fmaxf(0.15f, n3[2] * cos_p + n3[1] * sin_p);
					float tn[3] = { tx, ty, tz };
					nd_m3d_norm3(tn);
					int nr = (int)((tn[0] * 0.5f + 0.5f) * 255.0f + 0.5f);
					int ng = (int)((tn[1] * 0.5f + 0.5f) * 255.0f + 0.5f);
					int nb = (int)((tn[2] * 0.5f + 0.5f) * 255.0f + 0.5f);
					if (nr < 0) nr = 0; else if (nr > 255) nr = 255;
					if (ng < 0) ng = 0; else if (ng > 255) ng = 255;
					if (nb < 128) nb = 128; else if (nb > 255) nb = 255;
					out_normal_rgba[(uint32_t)py * out_stride_px + (uint32_t)px] =
						0xFF000000u | ((uint32_t)nb << 16) | ((uint32_t)ng << 8) | (uint32_t)nr;
				}
			}
		}
	}

	/* Second pass: subtle natural silhouette depth shading (soft edge self-occlusion instead of harsh cartoon outline) */
	for (int y = 1; y < H - 1; y++) {
		for (int x = 1; x < W - 1; x++) {
			uint32_t c = out_albedo_rgba[(uint32_t)y * out_stride_px + (uint32_t)x];
			if ((c >> 24) == 0u)
				continue;
			int edge =
				((out_albedo_rgba[(uint32_t)(y - 1) * out_stride_px + (uint32_t)x] >> 24) == 0u) ||
				((out_albedo_rgba[(uint32_t)(y + 1) * out_stride_px + (uint32_t)x] >> 24) == 0u) ||
				((out_albedo_rgba[(uint32_t)y * out_stride_px + (uint32_t)(x - 1)] >> 24) == 0u) ||
				((out_albedo_rgba[(uint32_t)y * out_stride_px + (uint32_t)(x + 1)] >> 24) == 0u);
			if (edge) {
				int r = (int)((c & 0xFFu) * 86u / 100u);
				int g = (int)(((c >> 8) & 0xFFu) * 86u / 100u);
				int b_ch = (int)(((c >> 16) & 0xFFu) * 86u / 100u);
				out_albedo_rgba[(uint32_t)y * out_stride_px + (uint32_t)x] =
					0xFF000000u | ((uint32_t)b_ch << 16) | ((uint32_t)g << 8) | (uint32_t)r;
			}
		}
	}
}

#endif /* ND_SEVEN_MESH3D_H */
