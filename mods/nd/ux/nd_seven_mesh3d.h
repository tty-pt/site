/*
 * nd_seven_mesh3d.h — Pure-C 3D Procedural Geological Mesh Engine
 * + Orthographic Pixel-Art Albedo & Tangent-Space Normal Map Baker.
 *
 * All 3D botanical mesh generation and foliage atlas generation live in nd-plant:
 *   - <nd/plant_mesh3d.h>
 *   - <nd/plant_foliage_atlas.h>
 *
 * This header provides:
 *   1. 3D Faceted Geological & Mineral Generator (All 51 NDS_SP_* Specimens across pebbles,
 *      rocks, boulders, crystals, and ores).
 *   2. Deterministic software Z-buffer orthographic 55-deg diorama rasterizer
 *      (nd_seven_mesh3d_bake_ortho_rgba) that bakes any 3D mesh template slice into
 *      lossless 32-bit RGBA pixel-art albedo sprites + tangent-space normal maps (_n.png).
 */

#ifndef ND_SEVEN_MESH3D_H
#define ND_SEVEN_MESH3D_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include <nd/plant.h>
#include <nd/plant_mesh3d.h>

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
			int term_mode = (sp == 4 || sp == 5 || sp == 8 || sp == 10) ? 1 :
			                ((sp == 2 || sp == 3) ? 2 : 0);
			int sides = (sp == 10) ? 4 : 6;
			for (int i = 0; i < n_prisms; i++) {
				float ang = ((float)i / (float)n_prisms) * 6.2831853f + 0.4f;
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
		int fol_kind = 0; /* 1=conifer needle, 2=broadleaf ovate, 3=broadleaf lobed, 4=bipinnate/reed blade */
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
						/* Quadrant (1,1): Bipinnate feathery umbrella spray (Acacia / delicate compound leaves):
						 * Main central rachis + 7 pairs of opposite pinnae bearing feathery micro-leaflets */
						float stem_w = 0.024f * (1.0f - 0.65f * cu);
						if (adv < stem_w && cu < 0.88f) {
							cr = cr * 0.48f + 0.14f;
							cg = cg * 0.48f + 0.11f;
							cb = cb * 0.48f + 0.07f;
						} else {
							float env_w = 0.48f * sinf(fminf(1.0f, cu * 1.08f + 0.06f) * 2.92f);
							if (adv > env_w || cu < 0.04f || cu > 0.96f)
								continue;
							float u_pinna = (cu - adv * 0.42f) * 7.5f;
							float pf = u_pinna - floorf(u_pinna);
							float r_frac = adv / fmaxf(0.05f, env_w);
							float pinna_hw = 0.34f * sinf(fminf(1.0f, r_frac) * 3.0f);
							if (fabsf(pf - 0.5f) > pinna_hw)
								continue;
							/* Micro-leaflet feathering along each pinna */
							float l_phase = r_frac * 11.0f;
							float lf = l_phase - floorf(l_phase);
							if (lf < 0.16f || lf > 0.88f)
								continue;
							float ridge = (pf - 0.5f) * 0.45f;
							n3[0] += ridge;
							n3[2] += 0.18f * (1.0f - r_frac);
							nd_m3d_norm3(n3);
							float tip_mod = 0.88f + 0.24f * r_frac;
							cr = fminf(1.0f, cr * tip_mod);
							cg = fminf(1.0f, cg * tip_mod);
							cb = fminf(1.0f, cb * tip_mod);
						}
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

	/* Second pass: subtle natural silhouette depth shading */
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
