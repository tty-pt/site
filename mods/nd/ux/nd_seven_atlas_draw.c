#include "nd_seven_atlas_draw.h"
#include "nd_seven_motion.h"
#include "nd_seven_ground.h"
#include <ttypt/lam.h>
#include <nd/plant.h>
#include <stdio.h>
#include <string.h>

static uint8_t s_atlas[ND_SEVEN_ATLAS_W * ND_SEVEN_ATLAS_H_PX];
static uint32_t s_atlas_rgba[ND_SEVEN_ATLAS_W * ND_SEVEN_RUNTIME_ATLAS_H];
static uint32_t s_atlas_norm_rgba[ND_SEVEN_ATLAS_W * ND_SEVEN_RUNTIME_ATLAS_H];
static int s_atlas_ready;
static uint32_t s_atlas_ver = 1;
static uint16_t s_shelf_x = 0;
static uint16_t s_shelf_y = ND_SEVEN_ATLAS_H_PX + 24;
static uint16_t s_shelf_h = 0;

static const nd_seven_sprite_rect_t s_botanical_rects[NDB_COUNT] = {
	[NDB_GRASS_TUFT_1] = { 0,   ND_SEVEN_ATLAS_H_PX, 20, 18 },
	[NDB_GRASS_TUFT_2] = { 20,  ND_SEVEN_ATLAS_H_PX, 20, 18 },
	[NDB_GRASS_TUFT_3] = { 40,  ND_SEVEN_ATLAS_H_PX, 20, 18 },
	[NDB_TALL_GRASS_1] = { 60,  ND_SEVEN_ATLAS_H_PX, 18, 22 },
	[NDB_TALL_GRASS_2] = { 78,  ND_SEVEN_ATLAS_H_PX, 18, 22 },
	[NDB_REED_1]       = { 96,  ND_SEVEN_ATLAS_H_PX, 16, 24 },
	[NDB_REED_2]       = { 112, ND_SEVEN_ATLAS_H_PX, 16, 24 }
};

static inline void atlas_put_px_alpha(uint16_t x, uint16_t y, uint32_t rgb, int alpha)
{
	if (alpha <= 0 || x >= ND_SEVEN_ATLAS_W || y >= ND_SEVEN_RUNTIME_ATLAS_H)
		return;
	if (alpha > 255)
		alpha = 255;
	uint32_t *dst = &s_atlas_rgba[(size_t)y * ND_SEVEN_ATLAS_W + x];
	uint32_t old = *dst;
	int old_a = (int)((old >> 24) & 0xFFu);
	if (old_a == 0 || alpha >= 252) {
		*dst = (rgb & 0x00FFFFFFu) | ((uint32_t)alpha << 24);
		return;
	}
	int out_a = alpha + ((old_a * (255 - alpha)) >> 8);
	if (out_a > 255)
		out_a = 255;
	int nr = (int)(rgb & 0xFFu);
	int ng = (int)((rgb >> 8) & 0xFFu);
	int nb = (int)((rgb >> 16) & 0xFFu);
	int or_c = (int)(old & 0xFFu);
	int og_c = (int)((old >> 8) & 0xFFu);
	int ob_c = (int)((old >> 16) & 0xFFu);
	int r = ((nr * alpha) + (or_c * (255 - alpha))) >> 8;
	int g = ((ng * alpha) + (og_c * (255 - alpha))) >> 8;
	int b = ((nb * alpha) + (ob_c * (255 - alpha))) >> 8;
	*dst = ((uint32_t)out_a << 24) | ((uint32_t)(b & 0xFF) << 16) |
	       ((uint32_t)(g & 0xFF) << 8) | (uint32_t)(r & 0xFF);
}

static inline uint32_t lerp_rgb(uint32_t c0, uint32_t c1, int t_q8)
{
	if (t_q8 <= 0) return c0;
	if (t_q8 >= 256) return c1;
	int r0 = (int)(c0 & 0xFFu), g0 = (int)((c0 >> 8) & 0xFFu), b0 = (int)((c0 >> 16) & 0xFFu);
	int r1 = (int)(c1 & 0xFFu), g1 = (int)((c1 >> 8) & 0xFFu), b1 = (int)((c1 >> 16) & 0xFFu);
	int r = r0 + (((r1 - r0) * t_q8) >> 8);
	int g = g0 + (((g1 - g0) * t_q8) >> 8);
	int b = b0 + (((b1 - b0) * t_q8) >> 8);
	return 0xFF000000u | ((uint32_t)(b & 0xFF) << 16) |
	       ((uint32_t)(g & 0xFF) << 8) | (uint32_t)(r & 0xFF);
}

static void draw_tapered_blade(uint16_t bx, uint16_t by, uint16_t rect_w, uint16_t rect_h,
                               int x0, int dy_start, int bh, int lean,
                               uint32_t c_root, uint32_t c_deep, uint32_t c_mid,
                               uint32_t c_lush, uint32_t c_tip, int is_plume)
{
	int span = (bh - dy_start > 1) ? (bh - dy_start - 1) : 1;
	for (int dy = dy_start; dy < bh; dy++) {
		int py = (int)(rect_h - 1) - dy;
		if (py < 0 || py >= (int)rect_h)
			continue;
		int rel = dy - dy_start;
		int t_q8 = (rel * 256) / span; /* 0..256 from root to tip */

		/* Quadratic curved trajectory in 1/256th pixel fixed point */
		int curve_q8 = (t_q8 * ((t_q8 * 180 + 76 * 256) >> 8)) >> 8;
		int fx_q8 = (x0 << 8) + ((lean * curve_q8));
		int ix = fx_q8 >> 8;
		int frac = fx_q8 & 0xFF;

		/* Smooth color gradient from terrain-matched root to sunlit tip */
		uint32_t col;
		if (t_q8 < 64) {
			col = lerp_rgb(c_root, c_deep, t_q8 * 4);
		} else if (t_q8 < 144) {
			col = lerp_rgb(c_deep, c_mid, ((t_q8 - 64) * 256) / 80);
		} else if (t_q8 < 216) {
			col = lerp_rgb(c_mid, c_lush, ((t_q8 - 144) * 256) / 72);
		} else {
			col = lerp_rgb(c_lush, c_tip, ((t_q8 - 216) * 256) / 40);
		}

		/* Vertical alpha envelope: feathered roots (no dark bottom line) & pointed tip (no square top) */
		int root_alpha = (dy == 0) ? 95 : (dy == 1 ? 165 : (dy == 2 ? 225 : 255));
		if (rel == 0 && dy > 0)
			root_alpha = (root_alpha * 165) >> 8;
		int tip_rem = bh - 1 - dy;
		int tip_alpha = (tip_rem == 0) ? 105 : (tip_rem == 1 ? 185 : (tip_rem == 2 ? 235 : 255));
		int base_alpha = (root_alpha * tip_alpha) >> 8;

		/* Taper width from ~1.55px at base down to 0.75px at tip with Wu sub-pixel edge anti-aliasing */
		int width_q8 = 396 - ((t_q8 * 210) >> 8); /* 396 (~1.55px) -> 186 (~0.73px) */
		if (is_plume && tip_rem >= 1 && tip_rem <= 3)
			width_q8 += 72;

		int a_main = (base_alpha * (256 - ((frac * (256 - (width_q8 >> 1))) >> 8))) >> 8;
		int a_right = (base_alpha * ((frac * width_q8) >> 8)) >> 8;
		int a_left = (width_q8 > 280 && frac < 96) ? ((base_alpha * (96 - frac)) >> 8) : 0;

		if (ix >= 1 && ix < (int)rect_w - 1)
			atlas_put_px_alpha((uint16_t)(bx + ix), (uint16_t)(by + py), col, a_main);
		if (a_right > 16 && ix + 1 >= 1 && ix + 1 < (int)rect_w - 1)
			atlas_put_px_alpha((uint16_t)(bx + ix + 1), (uint16_t)(by + py), col, a_right);
		if (a_left > 16 && ix - 1 >= 1 && ix - 1 < (int)rect_w - 1)
			atlas_put_px_alpha((uint16_t)(bx + ix - 1), (uint16_t)(by + py), col, a_left);
	}
}

static void synthesize_botanical_shelf(void)
{
	/* Terrain-harmonized palette matching s_fs_terrain deepMoss / lushTurf / sunlit tips */
	const uint32_t c_root  = RGB(68, 108, 52);
	const uint32_t c_deep  = RGB(78, 120, 56);
	const uint32_t c_mid   = RGB(90, 136, 62);
	const uint32_t c_lush  = RGB(106, 154, 70);
	const uint32_t c_tip   = RGB(136, 180, 82);
	const uint32_t c_plume = RGB(168, 194, 96);
	const uint32_t c_stalk = RGB(80, 124, 58);
	const uint32_t c_head1 = RGB(122, 80, 44);
	const uint32_t c_head2 = RGB(92, 58, 30);

	/* Clear reserved botanical shelf row (y: ND_SEVEN_ATLAS_H_PX .. +23) */
	memset(&s_atlas_rgba[(size_t)ND_SEVEN_ATLAS_H_PX * ND_SEVEN_ATLAS_W], 0,
	       24u * ND_SEVEN_ATLAS_W * sizeof(uint32_t));

	/* Synthesize 3 fine, tapered multi-blade meadow turf swatches (20x18 each, 11 overlapping curved blades) */
	for (int v = 0; v < 3; v++) {
		uint16_t bx = s_botanical_rects[NDB_GRASS_TUFT_1 + v].x;
		uint16_t by = s_botanical_rects[NDB_GRASS_TUFT_1 + v].y;
		static const int8_t blade_x[3][11] = {
			{ 3, 4, 6, 7, 8, 10, 11, 12, 14, 15, 16 },
			{ 3, 5, 6, 8, 9, 10, 12, 13, 14, 15, 16 },
			{ 2, 4, 6, 7, 9, 10, 11, 13, 14, 15, 17 }
		};
		static const int8_t blade_y0[3][11] = {
			{ 2, 1, 0, 1, 0, 0, 1, 0, 1, 2, 2 },
			{ 2, 0, 1, 0, 1, 0, 0, 1, 0, 1, 2 },
			{ 2, 1, 0, 0, 1, 0, 1, 0, 1, 2, 2 }
		};
		static const int8_t blade_h[3][11] = {
			{ 9, 12, 14, 11, 16, 13, 17, 12, 15, 11, 9 },
			{ 10, 13, 11, 15, 13, 17, 14, 16, 12, 13, 8 },
			{ 8, 11, 15, 13, 16, 12, 17, 14, 13, 10, 9 }
		};
		static const int8_t blade_lean[3][11] = {
			{ -3, -2, -2, -1, -1, 0, 1, 1, 2, 2, 3 },
			{ -2, -2, -1,  1, -1, 0, 1, 2, 1, 2, 3 },
			{ -3, -1, -2,  0,  1, -1, 1, 2, 1, 3, 2 }
		};
		for (int b = 0; b < 11; b++) {
			draw_tapered_blade(bx, by, 20, 18,
			                   blade_x[v][b], blade_y0[v][b], blade_h[v][b], blade_lean[v][b],
			                   c_root, c_deep, c_mid, c_lush, c_tip, 0);
		}
	}

	/* Synthesize 2 tall meadow grass clusters with arching tapered seed plumes (18x22 each, 9 blades) */
	for (int v = 0; v < 2; v++) {
		uint16_t bx = s_botanical_rects[NDB_TALL_GRASS_1 + v].x;
		uint16_t by = s_botanical_rects[NDB_TALL_GRASS_1 + v].y;
		static const int8_t tall_x[2][9] = {
			{ 3, 5, 6, 8, 9, 10, 12, 13, 14 },
			{ 3, 4, 6, 7, 9, 11, 12, 13, 15 }
		};
		static const int8_t tall_y0[2][9] = {
			{ 2, 1, 0, 1, 0, 0, 1, 1, 2 },
			{ 2, 1, 1, 0, 0, 1, 0, 1, 2 }
		};
		static const int8_t tall_h[2][9] = {
			{ 13, 16, 19, 15, 21, 17, 20, 16, 12 },
			{ 12, 17, 15, 20, 18, 21, 16, 18, 13 }
		};
		static const int8_t tall_lean[2][9] = {
			{ -3, -2, -2, -1, 0, 1, 2, 2, 3 },
			{ -2, -3, -1, -1, 1, 1, 2, 3, 2 }
		};
		for (int b = 0; b < 9; b++) {
			int bh = tall_h[v][b];
			int has_plume = (bh >= 19);
			draw_tapered_blade(bx, by, 18, 22,
			                   tall_x[v][b], tall_y0[v][b], bh, tall_lean[v][b],
			                   c_root, c_deep, c_mid, c_lush,
			                   has_plume ? c_plume : c_tip, has_plume);
		}
	}

	/* Synthesize 2 dense littoral shoreline reed / cattail clusters (16x24 each, 6 tapered stalks) */
	for (int r = 0; r < 2; r++) {
		uint16_t bx = s_botanical_rects[NDB_REED_1 + r].x;
		uint16_t by = s_botanical_rects[NDB_REED_1 + r].y;
		static const int8_t stalk_x[2][6] = {
			{ 3, 5, 7, 9, 11, 13 },
			{ 3, 5, 7, 10, 11, 13 }
		};
		static const int8_t stalk_y0[2][6] = {
			{ 2, 0, 0, 1, 0, 2 },
			{ 2, 0, 1, 0, 1, 2 }
		};
		static const int8_t stalk_h[2][6] = {
			{ 16, 20, 23, 18, 22, 15 },
			{ 15, 22, 19, 23, 17, 20 }
		};
		static const int8_t has_cattail[2][6] = {
			{ 0, 1, 1, 0, 1, 0 },
			{ 0, 1, 0, 1, 0, 1 }
		};
		for (int s = 0; s < 6; s++) {
			int sx = stalk_x[r][s];
			int sy0 = stalk_y0[r][s];
			int sh = stalk_h[r][s];
			int lean = (s < 2) ? -1 : (s > 3 ? 1 : 0);
			draw_tapered_blade(bx, by, 16, 24,
			                   sx, sy0, sh, lean,
			                   c_root, c_deep, c_stalk, c_lush, c_tip, 0);
			if (has_cattail[r][s]) {
				for (int dy = sh - 8; dy <= sh - 4; dy++) {
					int py = 23 - dy;
					int px = sx + (lean * dy) / 14;
					if (px >= 1 && px < 15) {
						atlas_put_px_alpha((uint16_t)(bx + px), (uint16_t)(by + py), c_head1, 245);
						if (px > 1)
							atlas_put_px_alpha((uint16_t)(bx + px - 1), (uint16_t)(by + py), c_head2, 185);
					}
				}
			}
		}
	}
}

typedef struct {
	uint32_t img_id;
	uint32_t norm_img_id;
	uint8_t  loaded;
	uint8_t  norm_loaded;
	uint16_t raw_w;
	uint16_t raw_h;
	nd_seven_sprite_rect_t rect;
} flora_dyn_slot_t;

static flora_dyn_slot_t s_flora_slots[ND_PLANT_MAX][2][4];
static uint32_t s_decode_scratch[384 * 384];
static uint32_t s_norm_scratch[384 * 384];

static nd_mesh3d_vert_t  s_mesh3d_verts[ND_MESH3D_MAX_VERTS];
static uint32_t          s_mesh3d_indices[ND_MESH3D_MAX_INDICES];
static nd_mesh3d_slice_t s_mesh3d_slices[ND_MESH3D_TOTAL];
static uint32_t          s_mesh3d_vert_count = 0;
static uint32_t          s_mesh3d_idx_count  = 0;
static uint8_t           s_mesh3d_ready      = 0;

/* Map each of the 51 s_nd_stone_specs indices (0..50) to its canonical (width, height, generator_id) */
static void
stone_spec_canonical_params(unsigned idx, uint16_t *out_w, uint16_t *out_h, unsigned *out_gen_sp)
{
	uint16_t w = 28, h = 24;
	unsigned gen = 34;
	switch (idx) {
	case 0:  w = 20; h = 16; gen = 28; break; /* FragmentoGneiss */
	case 1:  w = 48; h = 38; gen = 45; break; /* PedraGlacialFendida */
	case 2:  w = 30; h = 24; gen = 36; break; /* PedraGranitoAlpino */
	case 3:  w = 32; h = 26; gen = 37; break; /* RochaLiquenAlpino */
	case 4:  w = 18; h = 14; gen = 22; break; /* SeixoMorena */
	case 5:  w = 28; h = 24; gen = 14; break; /* ClusterCobre */
	case 6:  w = 28; h = 24; gen = 16; break; /* ClusterFerro */
	case 7:  w = 26; h = 26; gen = 3;  break; /* ClusterTurmalina */
	case 8:  w = 28; h = 28; gen = 1;  break; /* CristaisQuartzo */
	case 9:  w = 18; h = 22; gen = 0;  break; /* CristalQuartzo_solo */
	case 10: w = 18; h = 22; gen = 2;  break; /* CristalTurmalina_solo */
	case 11: w = 18; h = 16; gen = 15; break; /* PedacoFerro_solo */
	case 12: w = 18; h = 16; gen = 13; break; /* PepitaCobre_solo */
	case 13: w = 18; h = 16; gen = 19; break; /* PepitaOuro_solo */
	case 14: w = 28; h = 24; gen = 20; break; /* PepitasOuro */
	case 15: w = 48; h = 38; gen = 46; break; /* BlocoCalcarioNatural */
	case 16: w = 30; h = 24; gen = 38; break; /* PedraLateritica */
	case 17: w = 44; h = 34; gen = 47; break; /* PedraMusgoSeco */
	case 18: w = 18; h = 14; gen = 26; break; /* SeixoRioCalcario */
	case 19: w = 28; h = 24; gen = 12; break; /* ClusterCarvao */
	case 20: w = 26; h = 26; gen = 5;  break; /* ClusterEsmeralda */
	case 21: w = 28; h = 28; gen = 7;  break; /* ClusterObsidiana */
	case 22: w = 28; h = 24; gen = 18; break; /* ClusterPrata */
	case 23: w = 18; h = 18; gen = 9;  break; /* SoloAmbar */
	case 24: w = 18; h = 16; gen = 11; break; /* SoloCarvao */
	case 25: w = 18; h = 22; gen = 4;  break; /* SoloEsmeralda */
	case 26: w = 18; h = 18; gen = 8;  break; /* SoloJade */
	case 27: w = 18; h = 22; gen = 6;  break; /* SoloObsidiana */
	case 28: w = 18; h = 16; gen = 17; break; /* SoloPrata */
	case 29: w = 20; h = 20; gen = 10; break; /* SoloSal */
	case 30: w = 32; h = 26; gen = 39; break; /* AfloramentoQuartzito */
	case 31: w = 24; h = 18; gen = 27; break; /* ClusterSeixosSoltos */
	case 32: w = 30; h = 24; gen = 35; break; /* PedraLiquenSeco */
	case 33: w = 18; h = 14; gen = 22; break; /* SeixoGranito */
	case 34: w = 24; h = 18; gen = 25; break; /* ClusterSeixosMolhados */
	case 35: w = 22; h = 18; gen = 29; break; /* FragmentoGranito */
	case 36: w = 46; h = 36; gen = 43; break; /* PedraMusgoHumido */
	case 37: w = 18; h = 14; gen = 21; break; /* SeixoRio */
	case 38: w = 34; h = 28; gen = 42; break; /* AfloramentoRaizes */
	case 39: w = 48; h = 40; gen = 50; break; /* BoulderCascataMusgo */
	case 40: w = 46; h = 38; gen = 49; break; /* BoulderMusgo */
	case 41: w = 30; h = 24; gen = 41; break; /* PedraRolada */
	case 42: w = 22; h = 16; gen = 23; break; /* SeixosMolhados */
	case 43: w = 30; h = 24; gen = 32; break; /* AfloramentoEscoria */
	case 44: w = 48; h = 38; gen = 48; break; /* BoulderLavaSolidificada */
	case 45: w = 26; h = 20; gen = 31; break; /* ClusterPedraPomes */
	case 46: w = 22; h = 18; gen = 30; break; /* FragmentoBasalto */
	case 47: w = 32; h = 24; gen = 39; break; /* AfloramentoLama */
	case 48: w = 30; h = 24; gen = 34; break; /* PedraAlgas */
	case 49: w = 44; h = 34; gen = 43; break; /* PedraMusgoHumido (wetland) */
	case 50: w = 18; h = 14; gen = 24; break; /* SeixoMolhado */
	default: break;
	}
	*out_w = w;
	*out_h = h;
	*out_gen_sp = gen;
}

void nd_seven_mesh3d_init(void)
{
	if (s_mesh3d_ready)
		return;
	memset(s_mesh3d_slices, 0, sizeof(s_mesh3d_slices));
	nd_mesh3d_builder_t b;
	b.verts      = s_mesh3d_verts;
	b.vert_count = 0;
	b.vert_cap   = ND_MESH3D_MAX_VERTS;
	b.indices    = s_mesh3d_indices;
	b.idx_count  = 0;
	b.idx_cap    = ND_MESH3D_MAX_INDICES;

	/* Build all 128 canonical plant templates (1..128: 16 species x 2 stages x 4 skeletal alleles) */
	for (unsigned sp = 0; sp < ND_PLANT_MAX; sp++) {
		const nd_plant_spec_t *spec = &s_nd_plant_specs[sp];
		for (unsigned big = 0; big < 2u; big++) {
			for (unsigned var = 0; var < 4u; var++) {
				uint8_t mid = (uint8_t)(ND_MESH3D_PLANT_BASE + sp * 8u + big * 4u + var);
				nd_m3d_build_plant_from_genes(&b, spec, (int)big, (int)var,
				                              0xB07A0u + sp * 97u,
				                              &s_mesh3d_slices[mid]);
			}
		}
	}

	/* Build all 51 canonical stone/mineral templates (129..179) */
	for (unsigned idx = 0; idx < (unsigned)ND_MESH3D_STONE_COUNT; idx++) {
		uint16_t sw = 28, sh = 24;
		unsigned gen_sp = 0;
		stone_spec_canonical_params(idx, &sw, &sh, &gen_sp);
		uint8_t mid = (uint8_t)(ND_MESH3D_STONE_BASE + idx);
		nd_m3d_build_stone_spec(&b, gen_sp, sw, sh, &s_mesh3d_slices[mid]);
	}

	s_mesh3d_vert_count = b.vert_count;
	s_mesh3d_idx_count  = b.idx_count;
	s_mesh3d_ready      = 1;
}

const nd_mesh3d_vert_t *nd_seven_mesh3d_verts(uint32_t *out_count)
{
	nd_seven_mesh3d_init();
	if (out_count) *out_count = s_mesh3d_vert_count;
	return s_mesh3d_verts;
}

const uint32_t *nd_seven_mesh3d_indices(uint32_t *out_count)
{
	nd_seven_mesh3d_init();
	if (out_count) *out_count = s_mesh3d_idx_count;
	return s_mesh3d_indices;
}

const nd_mesh3d_slice_t *nd_seven_mesh3d_slice(uint8_t mesh_id)
{
	nd_seven_mesh3d_init();
	if (mesh_id == 0 || mesh_id >= ND_MESH3D_TOTAL)
		return NULL;
	return &s_mesh3d_slices[mesh_id];
}

uint8_t nd_seven_plant_mesh3d_id(uint8_t plant_id)
{
	if (!ND_PLANT_UNPACK_PRESENT(plant_id))
		return ND_MESH3D_NONE;
	unsigned sp_idx  = ND_PLANT_UNPACK_IDX(plant_id);
	unsigned is_big  = ND_PLANT_UNPACK_BIG(plant_id);
	unsigned var_idx = (ND_PLANT_UNPACK_VAR(plant_id) - 1u) & 3u;
	return (uint8_t)(ND_MESH3D_PLANT_BASE + sp_idx * 8u + is_big * 4u + var_idx);
}

/* Bake a 3D mesh template synchronously into a dynamic atlas slot (albedo + normal map) */
static int
bake_mesh3d_into_slot(flora_dyn_slot_t *slot, uint8_t mesh_id)
{
	if (!slot || mesh_id == 0 || mesh_id >= ND_MESH3D_TOTAL)
		return 0;
	if (slot->loaded)
		return 1;
	nd_seven_mesh3d_init();
	const nd_mesh3d_slice_t *sl = &s_mesh3d_slices[mesh_id];
	uint32_t iw = sl->sprite_w;
	uint32_t ih = sl->sprite_h;
	if (iw == 0 || ih == 0 || iw > 256 || ih > 256)
		return 0;

	nd_seven_mesh3d_bake_ortho_rgba(s_mesh3d_verts, s_mesh3d_indices, sl,
	                                s_decode_scratch, s_norm_scratch, iw);

	const uint32_t pad_x = 4u;
	const uint32_t pad_top = 2u;
	uint32_t slot_w = iw + pad_x * 2u;
	uint32_t slot_h = ih + pad_top;
	if ((uint32_t)s_shelf_x + slot_w > ND_SEVEN_ATLAS_W) {
		s_shelf_x = 0;
		s_shelf_y = (uint16_t)(s_shelf_y + s_shelf_h);
		s_shelf_h = 0;
	}
	if ((uint32_t)s_shelf_y + slot_h > ND_SEVEN_RUNTIME_ATLAS_H)
		return 0;

	for (uint32_t ry = 0; ry < slot_h; ry++) {
		memset(&s_atlas_rgba[(s_shelf_y + ry) * ND_SEVEN_ATLAS_W + s_shelf_x],
		       0, slot_w * sizeof(uint32_t));
		for (uint32_t rx = 0; rx < slot_w; rx++) {
			s_atlas_norm_rgba[(s_shelf_y + ry) * ND_SEVEN_ATLAS_W + s_shelf_x + rx] = 0xFFFF8080u;
		}
	}
	for (uint32_t ry = 0; ry < ih; ry++) {
		memcpy(&s_atlas_rgba[(s_shelf_y + pad_top + ry) * ND_SEVEN_ATLAS_W + s_shelf_x + pad_x],
		       &s_decode_scratch[ry * iw],
		       iw * sizeof(uint32_t));
		memcpy(&s_atlas_norm_rgba[(s_shelf_y + pad_top + ry) * ND_SEVEN_ATLAS_W + s_shelf_x + pad_x],
		       &s_norm_scratch[ry * iw],
		       iw * sizeof(uint32_t));
	}
	slot->raw_w       = (uint16_t)iw;
	slot->raw_h       = (uint16_t)ih;
	slot->rect.x      = s_shelf_x;
	slot->rect.y      = s_shelf_y;
	slot->rect.w      = (uint16_t)slot_w;
	slot->rect.h      = (uint16_t)slot_h;
	s_shelf_x         = (uint16_t)(s_shelf_x + slot_w);
	if (slot_h > s_shelf_h)
		s_shelf_h = (uint16_t)slot_h;
	slot->loaded      = 1;
	slot->norm_loaded = 1;
	s_atlas_ver++;
	return 1;
}

void atlas_load(void)
{
	size_t o = 0;
	if (s_atlas_ready)
		return;
	for (size_t i = 0; i + 1 < ND_SEVEN_RLE_LEN && o < sizeof(s_atlas); i += 2) {
		size_t run = (size_t)nd_seven_rle[i] + 1;
		uint8_t v = nd_seven_rle[i + 1];
		while (run-- && o < sizeof(s_atlas))
			s_atlas[o++] = v;
	}
	for (size_t p = 0; p < sizeof(s_atlas); p++) {
		uint8_t idx = s_atlas[p];
		s_atlas_rgba[p] = (idx < ND_SEVEN_PALETTE_N) ? nd_seven_palette[idx] : 0;
	}
	for (size_t p = 0; p < sizeof(s_atlas_norm_rgba) / sizeof(s_atlas_norm_rgba[0]); p++) {
		s_atlas_norm_rgba[p] = 0x00FF8080u;
	}
	synthesize_botanical_shelf();
	nd_seven_mesh3d_init();
	s_atlas_ready = 1;
}

const uint8_t *atlas_px(void)
{
	atlas_load();
	return s_atlas;
}

const uint32_t *atlas_rgba(void)
{
	atlas_load();
	return s_atlas_rgba;
}

const uint32_t *atlas_norm_rgba(void)
{
	atlas_load();
	return s_atlas_norm_rgba;
}

uint32_t atlas_rgba_version(void)
{
	atlas_load();
	return s_atlas_ver;
}

int nd_seven_plant_has_normal(uint8_t plant_id)
{
	if (!ND_PLANT_UNPACK_PRESENT(plant_id))
		return 0;
	unsigned sp_idx = ND_PLANT_UNPACK_IDX(plant_id);
	unsigned is_big = ND_PLANT_UNPACK_BIG(plant_id);
	unsigned var_idx = (ND_PLANT_UNPACK_VAR(plant_id) - 1u) & 3u;
	atlas_load();
	flora_dyn_slot_t *slot = &s_flora_slots[sp_idx][is_big][var_idx];
	if (!slot->loaded)
		bake_mesh3d_into_slot(slot, nd_seven_plant_mesh3d_id(plant_id));
	return (slot->loaded && slot->norm_loaded) ? 1 : 0;
}

int nd_seven_plant_rect(uint8_t plant_id, nd_seven_sprite_rect_t *out_rect)
{
	if (!ND_PLANT_UNPACK_PRESENT(plant_id))
		return 0;
	unsigned sp_idx = ND_PLANT_UNPACK_IDX(plant_id);
	unsigned is_big = ND_PLANT_UNPACK_BIG(plant_id);
	unsigned var_idx = (ND_PLANT_UNPACK_VAR(plant_id) - 1u) & 3u;

	atlas_load();
	flora_dyn_slot_t *slot = &s_flora_slots[sp_idx][is_big][var_idx];
	if (!slot->loaded)
		bake_mesh3d_into_slot(slot, nd_seven_plant_mesh3d_id(plant_id));
	if (slot->loaded) {
		if (out_rect) *out_rect = slot->rect;
		return 1;
	}
	return 0;
}

typedef struct {
	const char *slug;
	const char *name;
} nd_stone_spec_t;

enum {
	/* alpine_tundra (0..4) */
	NDS_SP_ALPINE_GNEISS = 0,
	NDS_SP_ALPINE_GLACIAL,
	NDS_SP_ALPINE_GRANITE,
	NDS_SP_ALPINE_LICHEN,
	NDS_SP_ALPINE_MORAINE,
	/* hot_desert (5..14) */
	NDS_SP_DESERT_COPPER_CL,
	NDS_SP_DESERT_IRON_CL,
	NDS_SP_DESERT_TOURM_CL,
	NDS_SP_DESERT_QUARTZ_CL,
	NDS_SP_DESERT_QUARTZ_SO,
	NDS_SP_DESERT_TOURM_SO,
	NDS_SP_DESERT_IRON_SO,
	NDS_SP_DESERT_COPPER_SO,
	NDS_SP_DESERT_GOLD_SO,
	NDS_SP_DESERT_GOLD_CL,
	/* mediterranean (15..18) */
	NDS_SP_MED_LIMESTONE_BLK,
	NDS_SP_MED_LATERITE,
	NDS_SP_MED_DRY_MOSS,
	NDS_SP_MED_LIMESTONE_PEB,
	/* minerals (19..29) */
	NDS_SP_MIN_COAL_CL,
	NDS_SP_MIN_EMERALD_CL,
	NDS_SP_MIN_OBSIDIAN_CL,
	NDS_SP_MIN_SILVER_CL,
	NDS_SP_MIN_AMBER_SO,
	NDS_SP_MIN_COAL_SO,
	NDS_SP_MIN_EMERALD_SO,
	NDS_SP_MIN_JADE_SO,
	NDS_SP_MIN_OBSIDIAN_SO,
	NDS_SP_MIN_SILVER_SO,
	NDS_SP_MIN_SALT_SO,
	/* shrubland (30..33) */
	NDS_SP_SHRUB_QUARTZITE,
	NDS_SP_SHRUB_PEBBLES,
	NDS_SP_SHRUB_LICHEN,
	NDS_SP_SHRUB_GRANITE,
	/* temperate_rainforest (34..37) */
	NDS_SP_TEMP_WET_PEBBLES,
	NDS_SP_TEMP_GRANITE_FRAG,
	NDS_SP_TEMP_MOSS_ROCK,
	NDS_SP_TEMP_RIVER_PEBBLE,
	/* tropical_rainforest (38..42) */
	NDS_SP_TROP_ROOT_OUTCROP,
	NDS_SP_TROP_MOSS_CASCADE,
	NDS_SP_TROP_MOSS_BOULDER,
	NDS_SP_TROP_ROLLED_ROCK,
	NDS_SP_TROP_WET_PEBBLES,
	/* volcanic (43..46) */
	NDS_SP_VOLC_SCORIA,
	NDS_SP_VOLC_LAVA_BOULDER,
	NDS_SP_VOLC_PUMICE_CL,
	NDS_SP_VOLC_BASALT_FRAG,
	/* wetland (47..50) */
	NDS_SP_WET_MUD_OUTCROP,
	NDS_SP_WET_ALGAE_ROCK,
	NDS_SP_WET_MOSS_ROCK,
	NDS_SP_WET_PEBBLE,
	NDS_SP_COUNT
};

static const nd_stone_spec_t s_nd_stone_specs[NDS_SP_COUNT] = {
	[NDS_SP_ALPINE_GNEISS]     = { "alpine_tundra/FragmentoGneiss",             "Gneiss Fragment" },
	[NDS_SP_ALPINE_GLACIAL]    = { "alpine_tundra/PedraGlacialFendida",         "Split Glacial Boulder" },
	[NDS_SP_ALPINE_GRANITE]    = { "alpine_tundra/PedraGranitoAlpino",          "Alpine Granite Rock" },
	[NDS_SP_ALPINE_LICHEN]     = { "alpine_tundra/RochaLiquenAlpino",           "Alpine Lichen Rock" },
	[NDS_SP_ALPINE_MORAINE]    = { "alpine_tundra/SeixoMorena",                 "Moraine Pebble" },
	[NDS_SP_DESERT_COPPER_CL]  = { "hot_desert/ClusterCobre",                   "Copper Ore Cluster" },
	[NDS_SP_DESERT_IRON_CL]    = { "hot_desert/ClusterFerro",                   "Iron Ore Cluster" },
	[NDS_SP_DESERT_TOURM_CL]   = { "hot_desert/ClusterTurmalina",               "Tourmaline Cluster" },
	[NDS_SP_DESERT_QUARTZ_CL]  = { "hot_desert/CristaisQuartzo",                "Quartz Crystals" },
	[NDS_SP_DESERT_QUARTZ_SO]  = { "hot_desert/CristalQuartzo_solo",            "Quartz Crystal" },
	[NDS_SP_DESERT_TOURM_SO]   = { "hot_desert/CristalTurmalina_solo",          "Tourmaline Crystal" },
	[NDS_SP_DESERT_IRON_SO]    = { "hot_desert/PedacoFerro_solo",               "Iron Shard" },
	[NDS_SP_DESERT_COPPER_SO]  = { "hot_desert/PepitaCobre_solo",               "Copper Nugget" },
	[NDS_SP_DESERT_GOLD_SO]    = { "hot_desert/PepitaOuro_solo",                "Gold Nugget" },
	[NDS_SP_DESERT_GOLD_CL]    = { "hot_desert/PepitasOuro",                    "Gold Vein Rock" },
	[NDS_SP_MED_LIMESTONE_BLK] = { "mediterranean/BlocoCalcarioNatural",        "Limestone Outcrop" },
	[NDS_SP_MED_LATERITE]      = { "mediterranean/PedraLateritica",             "Laterite Rock" },
	[NDS_SP_MED_DRY_MOSS]      = { "mediterranean/PedraMusgoSeco",              "Dry Moss Boulder" },
	[NDS_SP_MED_LIMESTONE_PEB] = { "mediterranean/SeixoRioCalcario",            "Limestone Pebble" },
	[NDS_SP_MIN_COAL_CL]       = { "minerals/ClusterCarvao",                    "Coal Seam Rock" },
	[NDS_SP_MIN_EMERALD_CL]    = { "minerals/ClusterEsmeralda",                 "Emerald Cluster" },
	[NDS_SP_MIN_OBSIDIAN_CL]   = { "minerals/ClusterObsidiana",                 "Obsidian Cluster" },
	[NDS_SP_MIN_SILVER_CL]     = { "minerals/ClusterPrata",                     "Silver Vein Rock" },
	[NDS_SP_MIN_AMBER_SO]      = { "minerals/SoloAmbar",                        "Amber Piece" },
	[NDS_SP_MIN_COAL_SO]       = { "minerals/SoloCarvao",                       "Coal Shard" },
	[NDS_SP_MIN_EMERALD_SO]    = { "minerals/SoloEsmeralda",                    "Emerald Crystal" },
	[NDS_SP_MIN_JADE_SO]       = { "minerals/SoloJade",                         "Jade Stone" },
	[NDS_SP_MIN_OBSIDIAN_SO]   = { "minerals/SoloObsidiana",                    "Obsidian Shard" },
	[NDS_SP_MIN_SILVER_SO]     = { "minerals/SoloPrata",                        "Silver Nugget" },
	[NDS_SP_MIN_SALT_SO]       = { "minerals/SoloSal",                          "Rock Salt Crystal" },
	[NDS_SP_SHRUB_QUARTZITE]   = { "shrubland/AfloramentoQuartzito",            "Quartzite Outcrop" },
	[NDS_SP_SHRUB_PEBBLES]     = { "shrubland/ClusterSeixosSoltos",             "Loose Pebble Cluster" },
	[NDS_SP_SHRUB_LICHEN]      = { "shrubland/PedraLiquenSeco",                 "Dry Lichen Rock" },
	[NDS_SP_SHRUB_GRANITE]     = { "shrubland/SeixoGranito",                    "Granite Pebble" },
	[NDS_SP_TEMP_WET_PEBBLES]  = { "temperate_rainforest/ClusterSeixosMolhados","Wet Pebble Cluster" },
	[NDS_SP_TEMP_GRANITE_FRAG] = { "temperate_rainforest/FragmentoGranito",     "Granite Fragment" },
	[NDS_SP_TEMP_MOSS_ROCK]    = { "temperate_rainforest/PedraMusgoHumido",     "Mossy Forest Boulder" },
	[NDS_SP_TEMP_RIVER_PEBBLE] = { "temperate_rainforest/SeixoRio",             "River Pebble" },
	[NDS_SP_TROP_ROOT_OUTCROP] = { "tropical_rainforest/AfloramentoRaizes",     "Root-Bound Outcrop" },
	[NDS_SP_TROP_MOSS_CASCADE] = { "tropical_rainforest/BoulderCascataMusgo",   "Moss Cascade Boulder" },
	[NDS_SP_TROP_MOSS_BOULDER] = { "tropical_rainforest/BoulderMusgo",          "Jungle Moss Boulder" },
	[NDS_SP_TROP_ROLLED_ROCK]  = { "tropical_rainforest/PedraRolada",           "Tropical River Rock" },
	[NDS_SP_TROP_WET_PEBBLES]  = { "tropical_rainforest/SeixosMolhados",        "Wet Jungle Pebbles" },
	[NDS_SP_VOLC_SCORIA]       = { "volcanic/AfloramentoEscoria",               "Scoria Outcrop" },
	[NDS_SP_VOLC_LAVA_BOULDER] = { "volcanic/BoulderLavaSolidificada",          "Solidified Lava Boulder" },
	[NDS_SP_VOLC_PUMICE_CL]    = { "volcanic/ClusterPedraPomes",                "Pumice Cluster" },
	[NDS_SP_VOLC_BASALT_FRAG]  = { "volcanic/FragmentoBasalto",                 "Basalt Fragment" },
	[NDS_SP_WET_MUD_OUTCROP]   = { "wetland/AfloramentoLama",                   "Mudstone Outcrop" },
	[NDS_SP_WET_ALGAE_ROCK]    = { "wetland/PedraAlgas",                        "Algae-Covered Rock" },
	[NDS_SP_WET_MOSS_ROCK]     = { "wetland/PedraMusgoHumido",                  "Wetland Moss Boulder" },
	[NDS_SP_WET_PEBBLE]        = { "wetland/SeixoMolhado",                      "Wet Marsh Pebble" },
};

static flora_dyn_slot_t s_stone_slots[NDS_SP_COUNT];

static int resolve_stone_spec_idx(const ndw_obj_t *o)
{
	if (!o || (o->kind != NDW_K_STONE && o->kind != NDW_K_ROCK && o->kind != NDW_K_BOULDER))
		return -1;

	unsigned biome = o->col_mask;
	unsigned h = (unsigned)o->variant ^ ((unsigned)o->slot_id * 73u);

	/* Map 19 world biomes (0..18) to 8 ecological stone families:
	 * 0: alpine_tundra        (1: Ice, 2: Tundra, 3: Boreal, 6: Cold Desert)
	 * 1: mediterranean        (4: Prairie, 10: Savanna)
	 * 2: wetland              (0: Water/Coast, 5: Swamp)
	 * 3: shrubland            (7: Scrub)
	 * 4: temperate_rainforest (8: Woodland, 9: Forest, 11: Conifer, 12: Montane, 13: Rainforest)
	 * 5: hot_desert           (14: Steppe, 15: Desert)
	 * 6: tropical_rainforest  (16: Monsoon, 17: Jungle)
	 * 7: volcanic             (18: Lava)
	 */
	static const uint8_t s_small_pools[8][8] = {
		/* 0: alpine */
		{ NDS_SP_ALPINE_GNEISS, NDS_SP_ALPINE_MORAINE, NDS_SP_MIN_SILVER_SO, NDS_SP_MIN_COAL_SO,
		  NDS_SP_ALPINE_GNEISS, NDS_SP_ALPINE_MORAINE, NDS_SP_MIN_SILVER_CL, NDS_SP_MIN_OBSIDIAN_SO },
		/* 1: mediterranean */
		{ NDS_SP_MED_LIMESTONE_PEB, NDS_SP_MED_LATERITE, NDS_SP_MIN_AMBER_SO, NDS_SP_MIN_SALT_SO,
		  NDS_SP_MED_LIMESTONE_PEB, NDS_SP_SHRUB_GRANITE, NDS_SP_DESERT_COPPER_SO, NDS_SP_DESERT_GOLD_SO },
		/* 2: wetland */
		{ NDS_SP_WET_PEBBLE, NDS_SP_WET_MUD_OUTCROP, NDS_SP_MIN_AMBER_SO, NDS_SP_MIN_JADE_SO,
		  NDS_SP_WET_PEBBLE, NDS_SP_TEMP_RIVER_PEBBLE, NDS_SP_WET_ALGAE_ROCK, NDS_SP_MIN_COAL_SO },
		/* 3: shrubland */
		{ NDS_SP_SHRUB_GRANITE, NDS_SP_SHRUB_QUARTZITE, NDS_SP_MIN_COAL_SO, NDS_SP_DESERT_IRON_SO,
		  NDS_SP_SHRUB_GRANITE, NDS_SP_SHRUB_QUARTZITE, NDS_SP_DESERT_QUARTZ_SO, NDS_SP_MIN_SILVER_SO },
		/* 4: temperate_rainforest */
		{ NDS_SP_TEMP_RIVER_PEBBLE, NDS_SP_TEMP_GRANITE_FRAG, NDS_SP_MIN_AMBER_SO, NDS_SP_MIN_EMERALD_SO,
		  NDS_SP_TEMP_RIVER_PEBBLE, NDS_SP_TEMP_WET_PEBBLES, NDS_SP_MIN_JADE_SO, NDS_SP_MIN_SILVER_SO },
		/* 5: hot_desert */
		{ NDS_SP_DESERT_QUARTZ_SO, NDS_SP_DESERT_TOURM_SO, NDS_SP_DESERT_IRON_SO, NDS_SP_DESERT_COPPER_SO,
		  NDS_SP_DESERT_GOLD_SO, NDS_SP_DESERT_TOURM_CL, NDS_SP_DESERT_QUARTZ_CL, NDS_SP_MIN_SALT_SO },
		/* 6: tropical_rainforest */
		{ NDS_SP_TROP_WET_PEBBLES, NDS_SP_MIN_EMERALD_SO, NDS_SP_MIN_JADE_SO, NDS_SP_DESERT_GOLD_SO,
		  NDS_SP_TROP_WET_PEBBLES, NDS_SP_TEMP_RIVER_PEBBLE, NDS_SP_MIN_AMBER_SO, NDS_SP_DESERT_TOURM_SO },
		/* 7: volcanic */
		{ NDS_SP_VOLC_SCORIA, NDS_SP_VOLC_PUMICE_CL, NDS_SP_MIN_OBSIDIAN_SO, NDS_SP_MIN_COAL_SO,
		  NDS_SP_VOLC_BASALT_FRAG, NDS_SP_DESERT_IRON_SO, NDS_SP_MIN_OBSIDIAN_CL, NDS_SP_VOLC_SCORIA },
	};

	static const uint8_t s_rock_pools[8][6] = {
		/* 0: alpine */
		{ NDS_SP_ALPINE_GRANITE, NDS_SP_ALPINE_LICHEN, NDS_SP_ALPINE_MORAINE,
		  NDS_SP_MIN_SILVER_CL, NDS_SP_MIN_COAL_CL, NDS_SP_ALPINE_GRANITE },
		/* 1: mediterranean */
		{ NDS_SP_MED_LATERITE, NDS_SP_MED_DRY_MOSS, NDS_SP_MED_LIMESTONE_PEB,
		  NDS_SP_DESERT_COPPER_CL, NDS_SP_MIN_SILVER_CL, NDS_SP_MED_LATERITE },
		/* 2: wetland */
		{ NDS_SP_WET_ALGAE_ROCK, NDS_SP_WET_MUD_OUTCROP, NDS_SP_WET_PEBBLE,
		  NDS_SP_TEMP_WET_PEBBLES, NDS_SP_MIN_COAL_CL, NDS_SP_WET_ALGAE_ROCK },
		/* 3: shrubland */
		{ NDS_SP_SHRUB_PEBBLES, NDS_SP_SHRUB_LICHEN, NDS_SP_SHRUB_QUARTZITE,
		  NDS_SP_DESERT_COPPER_CL, NDS_SP_MIN_COAL_CL, NDS_SP_SHRUB_LICHEN },
		/* 4: temperate_rainforest */
		{ NDS_SP_TEMP_WET_PEBBLES, NDS_SP_TEMP_GRANITE_FRAG, NDS_SP_TEMP_MOSS_ROCK,
		  NDS_SP_MIN_EMERALD_CL, NDS_SP_MIN_SILVER_CL, NDS_SP_TEMP_GRANITE_FRAG },
		/* 5: hot_desert */
		{ NDS_SP_DESERT_COPPER_CL, NDS_SP_DESERT_IRON_CL, NDS_SP_DESERT_GOLD_CL,
		  NDS_SP_DESERT_QUARTZ_CL, NDS_SP_DESERT_TOURM_CL, NDS_SP_MED_LATERITE },
		/* 6: tropical_rainforest */
		{ NDS_SP_TROP_ROLLED_ROCK, NDS_SP_TROP_ROOT_OUTCROP, NDS_SP_TROP_WET_PEBBLES,
		  NDS_SP_MIN_EMERALD_CL, NDS_SP_TEMP_MOSS_ROCK, NDS_SP_TROP_ROLLED_ROCK },
		/* 7: volcanic */
		{ NDS_SP_VOLC_BASALT_FRAG, NDS_SP_VOLC_PUMICE_CL, NDS_SP_VOLC_SCORIA,
		  NDS_SP_MIN_OBSIDIAN_CL, NDS_SP_MIN_COAL_CL, NDS_SP_DESERT_IRON_CL },
	};

	static const uint8_t s_boulder_pools[8][4] = {
		/* 0: alpine */
		{ NDS_SP_ALPINE_GLACIAL, NDS_SP_ALPINE_LICHEN, NDS_SP_ALPINE_GRANITE, NDS_SP_ALPINE_GLACIAL },
		/* 1: mediterranean */
		{ NDS_SP_MED_LIMESTONE_BLK, NDS_SP_MED_DRY_MOSS, NDS_SP_MED_LATERITE, NDS_SP_MED_LIMESTONE_BLK },
		/* 2: wetland */
		{ NDS_SP_WET_MOSS_ROCK, NDS_SP_WET_ALGAE_ROCK, NDS_SP_WET_MUD_OUTCROP, NDS_SP_WET_MOSS_ROCK },
		/* 3: shrubland */
		{ NDS_SP_SHRUB_LICHEN, NDS_SP_SHRUB_PEBBLES, NDS_SP_MED_LIMESTONE_BLK, NDS_SP_SHRUB_LICHEN },
		/* 4: temperate_rainforest */
		{ NDS_SP_TEMP_MOSS_ROCK, NDS_SP_TROP_MOSS_BOULDER, NDS_SP_TEMP_WET_PEBBLES, NDS_SP_TEMP_MOSS_ROCK },
		/* 5: hot_desert */
		{ NDS_SP_DESERT_IRON_CL, NDS_SP_DESERT_GOLD_CL, NDS_SP_MED_LIMESTONE_BLK, NDS_SP_DESERT_COPPER_CL },
		/* 6: tropical_rainforest */
		{ NDS_SP_TROP_MOSS_CASCADE, NDS_SP_TROP_MOSS_BOULDER, NDS_SP_TROP_ROOT_OUTCROP, NDS_SP_TROP_ROLLED_ROCK },
		/* 7: volcanic */
		{ NDS_SP_VOLC_LAVA_BOULDER, NDS_SP_MIN_OBSIDIAN_CL, NDS_SP_VOLC_BASALT_FRAG, NDS_SP_VOLC_LAVA_BOULDER },
	};

	unsigned fam = 4u;
	switch (biome) {
	case 1: case 2: case 3: case 6:
		fam = 0u; break;
	case 4: case 10:
		fam = 1u; break;
	case 0: case 5:
		fam = 2u; break;
	case 7:
		fam = 3u; break;
	case 8: case 9: case 11: case 12: case 13:
		fam = 4u; break;
	case 14: case 15:
		fam = 5u; break;
	case 16: case 17:
		fam = 6u; break;
	case 18:
		fam = 7u; break;
	default:
		fam = 4u; break;
	}

	if (o->kind == NDW_K_STONE)
		return (int)s_small_pools[fam][h % 8u];
	if (o->kind == NDW_K_ROCK)
		return (int)s_rock_pools[fam][h % 6u];
	return (int)s_boulder_pools[fam][h % 4u];
}

uint8_t nd_seven_stone_mesh3d_id(const ndw_obj_t *o)
{
	int idx = resolve_stone_spec_idx(o);
	if (idx < 0 || idx >= NDS_SP_COUNT)
		return ND_MESH3D_NONE;
	return (uint8_t)(ND_MESH3D_STONE_BASE + (unsigned)idx);
}

uint8_t nd_seven_obj_mesh3d_id(const ndw_obj_t *o)
{
	if (!o)
		return ND_MESH3D_NONE;
	if (ND_PLANT_UNPACK_PRESENT(o->plant_id))
		return nd_seven_plant_mesh3d_id(o->plant_id);
	if (o->kind == NDW_K_STONE || o->kind == NDW_K_ROCK || o->kind == NDW_K_BOULDER)
		return nd_seven_stone_mesh3d_id(o);
	/* Fallback for unbound procedural plants by kind */
	unsigned v = (unsigned)(o->variant & 3u);
	unsigned b = o->big ? 1u : 0u;
	switch (o->kind) {
	case NDW_K_DECID:  return (uint8_t)(ND_MESH3D_PLANT_BASE + 10u * 8u + b * 4u + v); /* Quercus robur */
	case NDW_K_TALL:   return (uint8_t)(ND_MESH3D_PLANT_BASE +  2u * 8u + b * 4u + v); /* Betula pendula */
	case NDW_K_FIR:    return (uint8_t)(ND_MESH3D_PLANT_BASE +  1u * 8u + b * 4u + v); /* Pseudotsuga menziesii */
	case NDW_K_CACTUS: return (uint8_t)(ND_MESH3D_PLANT_BASE +  6u * 8u + b * 4u + v); /* Arthrocereus rondonianus */
	default:           return ND_MESH3D_NONE;
	}
}

uint8_t nd_seven_entity_mesh3d_id(int spr_id)
{
	switch (spr_id) {
	case NDS_TREE_OAK:       return (uint8_t)(ND_MESH3D_PLANT_BASE + 10u * 8u + 4u + 0u);
	case NDS_TREE_APPLE:     return (uint8_t)(ND_MESH3D_PLANT_BASE + 12u * 8u + 4u + 0u);
	case NDS_TREE_APPLE2:    return (uint8_t)(ND_MESH3D_PLANT_BASE +  2u * 8u + 4u + 1u);
	case NDS_TREE_BIG:       return (uint8_t)(ND_MESH3D_PLANT_BASE +  1u * 8u + 4u + 0u);
	case NDS_TREE_ROUND:     return (uint8_t)(ND_MESH3D_PLANT_BASE + 10u * 8u + 0u + 0u);
	case NDS_BUSH_DARK:      return (uint8_t)(ND_MESH3D_PLANT_BASE +  5u * 8u + 0u + 0u);
	case NDS_BUSH_TEAL:      return (uint8_t)(ND_MESH3D_PLANT_BASE +  0u * 8u + 0u + 1u);
	case NDS_ROCK_GREY:      return (uint8_t)(ND_MESH3D_STONE_BASE + NDS_SP_ALPINE_GRANITE);
	case NDS_ROCK_TAN:       return (uint8_t)(ND_MESH3D_STONE_BASE + NDS_SP_MED_LATERITE);
	case NDS_ROCK_WHITE:     return (uint8_t)(ND_MESH3D_STONE_BASE + NDS_SP_SHRUB_QUARTZITE);
	case NDS_ROCK_TAN_SM:    return (uint8_t)(ND_MESH3D_STONE_BASE + NDS_SP_SHRUB_LICHEN);
	case NDS_ROCK_TAN_TINY:  return (uint8_t)(ND_MESH3D_STONE_BASE + NDS_SP_MED_LIMESTONE_PEB);
	case NDS_STONE_BLUE:     return (uint8_t)(ND_MESH3D_STONE_BASE + NDS_SP_WET_ALGAE_ROCK);
	case NDS_STONE_FLAT:     return (uint8_t)(ND_MESH3D_STONE_BASE + NDS_SP_TEMP_RIVER_PEBBLE);
	case NDS_STONE_TINY:     return (uint8_t)(ND_MESH3D_STONE_BASE + NDS_SP_ALPINE_MORAINE);
	case NDS_STONE_GREY:     return (uint8_t)(ND_MESH3D_STONE_BASE + NDS_SP_SHRUB_GRANITE);
	case NDS_STONE_BLUE_SM:  return (uint8_t)(ND_MESH3D_STONE_BASE + NDS_SP_WET_PEBBLE);
	case NDS_ROCK_WHITE_BIG: return (uint8_t)(ND_MESH3D_STONE_BASE + NDS_SP_MED_LIMESTONE_BLK);
	case NDS_ROCK_TAN_BIG:   return (uint8_t)(ND_MESH3D_STONE_BASE + NDS_SP_ALPINE_GLACIAL);
	default:                 return ND_MESH3D_NONE;
	}
}

const char *nd_seven_stone_name(const ndw_obj_t *o)
{
	int idx = resolve_stone_spec_idx(o);
	if (idx >= 0 && idx < NDS_SP_COUNT)
		return s_nd_stone_specs[idx].name;
	return NULL;
}

int nd_seven_stone_has_normal(const ndw_obj_t *o)
{
	int idx = resolve_stone_spec_idx(o);
	if (idx < 0 || idx >= NDS_SP_COUNT)
		return 0;
	atlas_load();
	flora_dyn_slot_t *slot = &s_stone_slots[idx];
	if (!slot->loaded)
		bake_mesh3d_into_slot(slot, (uint8_t)(ND_MESH3D_STONE_BASE + (unsigned)idx));
	return (slot->loaded && slot->norm_loaded) ? 1 : 0;
}

int nd_seven_stone_rect(const ndw_obj_t *o, nd_seven_sprite_rect_t *out_rect)
{
	int idx = resolve_stone_spec_idx(o);
	if (idx < 0 || idx >= NDS_SP_COUNT)
		return 0;

	atlas_load();
	flora_dyn_slot_t *slot = &s_stone_slots[idx];
	if (!slot->loaded)
		bake_mesh3d_into_slot(slot, (uint8_t)(ND_MESH3D_STONE_BASE + (unsigned)idx));
	if (slot->loaded) {
		if (out_rect) *out_rect = slot->rect;
		return 1;
	}
	return 0;
}

int nd_seven_obj_rect(const ndw_obj_t *o, nd_seven_sprite_rect_t *out_rect)
{
	if (!o || !out_rect)
		return 0;
	if (o->plant_id && nd_seven_plant_rect(o->plant_id, out_rect))
		return 1;
	if ((o->kind == NDW_K_STONE || o->kind == NDW_K_ROCK || o->kind == NDW_K_BOULDER) &&
	    nd_seven_stone_rect(o, out_rect))
		return 1;
	int id = nd_seven_obj_sprite(o);
	if (id > 0 && id < NDS_COUNT) {
		*out_rect = nd_seven_sprite_rects[id];
		return 1;
	}
	return 0;
}

int nd_seven_botanical_rect(int kind, nd_seven_sprite_rect_t *out_rect)
{
	if (kind < 0 || kind >= NDB_COUNT || !out_rect)
		return 0;
	atlas_load();
	*out_rect = s_botanical_rects[kind];
	return 1;
}

uint32_t tint_col(uint32_t c, int r, int g, int b)
{
	uint32_t cr = (uint32_t)((int)(c & 0xFF) * r / 128);
	uint32_t cg = (uint32_t)((int)((c >> 8) & 0xFF) * g / 128);
	uint32_t cb = (uint32_t)((int)((c >> 16) & 0xFF) * b / 128);
	if (cr > 255) cr = 255;
	if (cg > 255) cg = 255;
	if (cb > 255) cb = 255;
	return (c & 0xFF000000u) | (cb << 16) | (cg << 8) | cr;
}

uint32_t shade(uint32_t c, int num)
{
	return tint_col(c, num * 128 / 100, num * 128 / 100, num * 128 / 100);
}

void plant_tint(int fg, tint_t *t)
{
	t->leaf = 1;
	switch (fg & 15) {
	case 3: case 11: t->r = 230; t->g = 118; t->b = 60; break; /* autumn gold / yellow */
	case 1: case 9:  t->r = 255; t->g = 70;  t->b = 64; break; /* flowering red / apple */
	case 7: case 15: t->r = 200; t->g = 150; t->b = 190; break; /* birch / blossom */
	case 2: case 10: t->r = 115; t->g = 195; t->b = 95; break; /* lush emerald / spring conifer */
	case 6: case 14: t->r = 85;  t->g = 180; t->b = 160; break; /* alpine spruce / boreal teal */
	case 4: case 12: t->r = 160; t->g = 145; t->b = 90; break; /* savanna acacia / olive */
	default:         t->r = t->g = t->b = 128; break;
	}
}

int nd_seven_obj_sprite(const ndw_obj_t *o)
{
	if (!o) return 0;
	switch (o->kind) {
	case NDW_K_DECID:
		if (o->big) {
			int pick = o->variant & 3;
			if (pick == 0) return NDS_TREE_OAK;
			if (pick == 1) return NDS_TREE_APPLE;
			if (pick == 2) return NDS_TREE_BIG;
			return NDS_TREE_APPLE2;
		}
		return (o->variant & 1) ? NDS_TREE_ROUND : NDS_BUSH_DARK;

	case NDW_K_TALL:
		if (o->big) {
			return (o->variant & 1) ? NDS_TREE_BIG : NDS_TREE_OAK;
		}
		return (o->variant & 1) ? NDS_TREE_APPLE2 : NDS_TREE_ROUND;

	case NDW_K_FIR:
		return o->big ? NDS_TREE_BIG : ((o->variant & 1) ? NDS_BUSH_TEAL : NDS_BUSH_DARK);

	case NDW_K_CACTUS:
		return (o->variant & 1) ? NDS_BUSH_DARK : NDS_BUSH_TEAL;

	case NDW_K_STONE: {
		static const int stones[4] = { NDS_STONE_FLAT, NDS_STONE_TINY, NDS_STONE_GREY, NDS_STONE_BLUE_SM };
		return stones[o->variant & 3];
	}

	case NDW_K_ROCK: {
		static const int rocks[6] = {
			NDS_ROCK_GREY, NDS_ROCK_TAN, NDS_ROCK_WHITE,
			NDS_ROCK_TAN_SM, NDS_ROCK_TAN_TINY, NDS_STONE_BLUE
		};
		return rocks[(o->variant >> 1) % 6];
	}

	case NDW_K_BOULDER:
		return (o->variant & 1) ? NDS_ROCK_WHITE_BIG : NDS_ROCK_TAN_BIG;

	default:
		return 0;
	}
}
