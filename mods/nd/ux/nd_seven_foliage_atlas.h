/*
 * nd_seven_foliage_atlas.h — Pure-C Startup-Baked Skyrim / SpeedTree Branchlet Texture Atlas.
 *
 * Generates a deterministic 512x512 RGBA8 botanical branchlet atlas (2x2 tiles of 256x256)
 * with an 8-level CPU mipmap chain (levels 0..7, down to 4x4 so tiles stay 100% isolated)
 * using SpeedTree alpha-coverage preservation across mip levels so distant crowns remain
 * lush and never thin out or shimmer.
 *
 * Tile layout (u = longitudinal stem [0..1] along +X, v = lateral width [0..1] along +Y):
 *   (0, 0) Tile 0: CONIFER — Pectinate pine/fir shoot with central + lateral woody twigs & ~240 needles
 *   (1, 0) Tile 1: OVATE   — Forked broadleaf shoot with 22 overlapping serrated ovate leaves & venation
 *   (0, 1) Tile 2: LOBED   — Forked oak/maple shoot with 16 overlapping deeply lobed leaves & venation
 *   (1, 1) Tile 3: BLADE   — Slender weeping/frond shoot with 26 sweeping lanceolate blades
 *
 * RGBA8 Channel Packing:
 *   R: Botanical shade / depth / vein highlight [0..255]
 *   G: Tangent micro-normal roll across leaf fold / needle ridge (128 = neutral, 0..255 -> -1..+1)
 *   B: Woody twig / rachis / petiole mask (0 = chlorophyll foliage, 255 = woody bark twig)
 *   A: Anti-aliased botanical silhouette alpha coverage [0..255]
 */

#ifndef ND_SEVEN_FOLIAGE_ATLAS_H
#define ND_SEVEN_FOLIAGE_ATLAS_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

#define ND_FOL_ATLAS_SIZE   512
#define ND_FOL_TILE_SIZE    256
#define ND_FOL_MIP_LEVELS   8   /* 512 -> 256 -> 128 -> 64 -> 32 -> 16 -> 8 -> 4 (each 2x2 tile >= 2x2) */

static inline uint32_t nd_fa_hash(uint32_t x)
{
	x ^= x >> 16;
	x *= 0x7FEB352Du;
	x ^= x >> 15;
	x *= 0x846CA68Bu;
	x ^= x >> 16;
	return x;
}

static inline float nd_fa_rand01(uint32_t seed)
{
	return (float)(nd_fa_hash(seed) & 0xFFFFu) * (1.0f / 65535.0f);
}

static inline float nd_fa_clampf(float v, float lo, float hi)
{
	return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static inline float nd_fa_smoothstep(float e0, float e1, float x)
{
	float t = nd_fa_clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

static inline uint8_t nd_fa_u8(float v)
{
	int i = (int)(nd_fa_clampf(v, 0.0f, 1.0f) * 255.0f + 0.5f);
	return (uint8_t)i;
}

/* Over-composite a botanical feature pixel into a 256x256 tile region of the 512x512 RGBA8 atlas */
static inline void nd_fa_blend_px(uint8_t *atlas, int ox, int oy, int px, int py,
                                  float shade, float bump, float wood, float alpha)
{
	if (px < 0 || px >= ND_FOL_TILE_SIZE || py < 0 || py >= ND_FOL_TILE_SIZE) return;
	if (alpha <= 0.004f) return;
	uint8_t *dst = atlas + (((oy + py) * ND_FOL_ATLAS_SIZE) + (ox + px)) * 4;
	float da = (float)dst[3] * (1.0f / 255.0f);
	float out_a = alpha + da * (1.0f - alpha);
	if (out_a <= 1e-4f) return;
	/* Straight-alpha compositing weighted by incoming alpha so interior detail stays crisp */
	float w = alpha / out_a;
	float dr = (float)dst[0] * (1.0f / 255.0f);
	float dg = (float)dst[1] * (1.0f / 255.0f);
	float db = (float)dst[2] * (1.0f / 255.0f);
	if (da <= 0.004f) {
		dr = shade; dg = bump; db = wood;
	} else {
		dr = dr + (shade - dr) * w;
		dg = dg + (bump  - dg) * w;
		db = db + (wood  - db) * w;
	}
	dst[0] = nd_fa_u8(dr);
	dst[1] = nd_fa_u8(dg);
	dst[2] = nd_fa_u8(db);
	dst[3] = nd_fa_u8(out_a);
}

/* Rasterize a tapered curved woody twig segment from (x0,y0) to (x1,y1) in normalized tile coords [0..1] */
static inline void nd_fa_draw_twig(uint8_t *atlas, int ox, int oy,
                                   float x0, float y0, float x1, float y1,
                                   float r0, float r1, float curve, float shade_mul)
{
	const float S = (float)ND_FOL_TILE_SIZE;
	float dx = x1 - x0, dy = y1 - y0;
	float len = sqrtf(dx * dx + dy * dy);
	if (len < 1e-4f) return;
	float nx = -dy / len, ny = dx / len;
	float pad = fmaxf(r0, r1) + fabsf(curve) + 2.5f / S;
	int min_x = (int)floorf((fminf(x0, x1) - pad) * S);
	int max_x = (int)ceilf((fmaxf(x0, x1) + pad) * S);
	int min_y = (int)floorf((fminf(y0, y1) - pad) * S);
	int max_y = (int)ceilf((fmaxf(y0, y1) + pad) * S);
	if (min_x < 0) min_x = 0;
	if (max_x >= ND_FOL_TILE_SIZE) max_x = ND_FOL_TILE_SIZE - 1;
	if (min_y < 0) min_y = 0;
	if (max_y >= ND_FOL_TILE_SIZE) max_y = ND_FOL_TILE_SIZE - 1;

	float aa = 1.35f / S;
	for (int py = min_y; py <= max_y; py++) {
		float fy = ((float)py + 0.5f) / S;
		for (int px = min_x; px <= max_x; px++) {
			float fx = ((float)px + 0.5f) / S;
			float rx = fx - x0, ry = fy - y0;
			float t = (rx * dx + ry * dy) / (len * len);
			if (t < -0.04f || t > 1.04f) continue;
			float tc = nd_fa_clampf(t, 0.0f, 1.0f);
			float bend = curve * 4.0f * tc * (1.0f - tc);
			float cx = x0 + dx * tc + nx * bend;
			float cy = y0 + dy * tc + ny * bend;
			float sd = (fx - cx) * nx + (fy - cy) * ny;
			float along_err = (t < 0.0f) ? (-t * len) : ((t > 1.0f) ? ((t - 1.0f) * len) : 0.0f);
			float d = sqrtf(sd * sd + along_err * along_err);
			float rad = r0 + (r1 - r0) * tc;
			float a = 1.0f - nd_fa_smoothstep(rad - aa, rad + aa, d);
			if (a <= 0.005f) continue;
			float cyl = nd_fa_clampf(sd / fmaxf(rad, 1e-4f), -1.0f, 1.0f);
			float shade = shade_mul * (0.62f + 0.24f * sqrtf(fmaxf(0.0f, 1.0f - cyl * cyl)) + 0.10f * tc);
			float bump = 0.5f + 0.42f * cyl;
			nd_fa_blend_px(atlas, ox, oy, px, py, shade, bump, 0.96f, a);
		}
	}
}

/* Rasterize a single curved conifer needle with tapered tip and 3D ridge sheen */
static inline void nd_fa_draw_needle(uint8_t *atlas, int ox, int oy,
                                     float x0, float y0, float ang, float len,
                                     float half_w, float curve, float shade_base)
{
	const float S = (float)ND_FOL_TILE_SIZE;
	float ca = cosf(ang), sa = sinf(ang);
	float nx = -sa, ny = ca;
	float x1 = x0 + ca * len, y1 = y0 + sa * len;
	float pad = half_w + fabsf(curve) + 2.0f / S;
	int min_x = (int)floorf((fminf(x0, x1) - pad) * S);
	int max_x = (int)ceilf((fmaxf(x0, x1) + pad) * S);
	int min_y = (int)floorf((fminf(y0, y1) - pad) * S);
	int max_y = (int)ceilf((fmaxf(y0, y1) + pad) * S);
	if (min_x < 0) min_x = 0;
	if (max_x >= ND_FOL_TILE_SIZE) max_x = ND_FOL_TILE_SIZE - 1;
	if (min_y < 0) min_y = 0;
	if (max_y >= ND_FOL_TILE_SIZE) max_y = ND_FOL_TILE_SIZE - 1;

	float aa = 1.25f / S;
	for (int py = min_y; py <= max_y; py++) {
		float fy = ((float)py + 0.5f) / S;
		for (int px = min_x; px <= max_x; px++) {
			float fx = ((float)px + 0.5f) / S;
			float rx = fx - x0, ry = fy - y0;
			float u = (rx * ca + ry * sa) / fmaxf(len, 1e-4f);
			if (u < -0.03f || u > 1.03f) continue;
			float uc = nd_fa_clampf(u, 0.0f, 1.0f);
			float v = (rx * nx + ry * ny) - curve * uc * uc;
			float w = half_w * (0.85f + 0.25f * sinf(uc * 3.14159265f)) * (1.0f - 0.82f * uc * uc);
			float end_pen = (u < 0.0f) ? (-u * len) : ((u > 1.0f) ? ((u - 1.0f) * len) : 0.0f);
			float dist = sqrtf(fmaxf(0.0f, fabsf(v) - w) * fmaxf(0.0f, fabsf(v) - w) + end_pen * end_pen);
			float a = (fabsf(v) <= w && end_pen <= 0.0f)
				? 1.0f
				: (1.0f - nd_fa_smoothstep(0.0f, aa, dist));
			if (a <= 0.005f) continue;
			float vn = nd_fa_clampf(v / fmaxf(w, 1e-4f), -1.0f, 1.0f);
			float ridge = 1.0f - fabsf(vn);
			float shade = shade_base * (0.76f + 0.26f * uc + 0.14f * ridge);
			float bump = 0.5f + 0.38f * vn;
			nd_fa_blend_px(atlas, ox, oy, px, py, shade, bump, 0.02f, a);
		}
	}
}

/* Rasterize a botanical broadleaf blade (mode 0 = serrated ovate, mode 1 = deeply lobed oak/maple, mode 2 = narrow willow/lanceolate) */
static inline void nd_fa_draw_leaf(uint8_t *atlas, int ox, int oy,
                                   float x0, float y0, float ang,
                                   float len, float max_w, int mode,
                                   float shade_base, uint32_t seed)
{
	const float S = (float)ND_FOL_TILE_SIZE;
	float ca = cosf(ang), sa = sinf(ang);
	float nx = -sa, ny = ca;
	float x1 = x0 + ca * len, y1 = y0 + sa * len;
	float pad = max_w * 1.15f + 3.0f / S;
	int min_x = (int)floorf((fminf(x0, x1) - pad) * S);
	int max_x = (int)ceilf((fmaxf(x0, x1) + pad) * S);
	int min_y = (int)floorf((fminf(y0, y1) - pad) * S);
	int max_y = (int)ceilf((fmaxf(y0, y1) + pad) * S);
	if (min_x < 0) min_x = 0;
	if (max_x >= ND_FOL_TILE_SIZE) max_x = ND_FOL_TILE_SIZE - 1;
	if (min_y < 0) min_y = 0;
	if (max_y >= ND_FOL_TILE_SIZE) max_y = ND_FOL_TILE_SIZE - 1;

	float bend_dir = (nd_fa_rand01(seed + 11u) - 0.5f) * 0.18f * len;
	float aa = 1.35f / S;
	for (int py = min_y; py <= max_y; py++) {
		float fy = ((float)py + 0.5f) / S;
		for (int px = min_x; px <= max_x; px++) {
			float fx = ((float)px + 0.5f) / S;
			float rx = fx - x0, ry = fy - y0;
			float u = (rx * ca + ry * sa) / fmaxf(len, 1e-4f);
			if (u < -0.02f || u > 1.03f) continue;
			float uc = nd_fa_clampf(u, 0.0f, 1.0f);
			float v = (rx * nx + ry * ny) - bend_dir * uc * (1.0f - uc);
			float av = fabsf(v);

			/* Silhouette half-width profile w(u) */
			float w;
			if (mode == 0) {
				/* Serrated ovate/elliptic leaf (birch, beech, cherry, olive, acacia) */
				float base_prof = powf(fmaxf(0.0f, sinf(uc * 3.14159265f)), 0.72f) * (1.08f - 0.34f * uc);
				float tooth = 1.0f + 0.075f * sinf(uc * 36.0f) * sinf(uc * 3.14159265f);
				w = max_w * base_prof * tooth;
			} else if (mode == 1) {
				/* Deeply pinnately-lobed oak / palmate maple leaf */
				float env = powf(fmaxf(0.0f, sinf(uc * 3.14159265f)), 0.62f) * (0.95f + 0.25f * sinf(uc * 2.4f));
				float lobe_wave = 0.68f + 0.36f * cosf((uc * 3.6f - 0.45f) * 6.2831853f);
				float tip_lobe = (uc > 0.78f) ? (1.0f - 0.65f * ((uc - 0.78f) / 0.22f)) : 1.0f;
				w = max_w * env * nd_fa_clampf(lobe_wave, 0.28f, 1.12f) * tip_lobe;
			} else {
				/* Slender curving lanceolate willow / reed blade */
				float env = powf(fmaxf(0.0f, sinf(uc * 3.14159265f)), 0.58f) * (1.10f - 0.55f * uc);
				w = max_w * env;
			}
			float end_d = (u < 0.0f) ? (-u * len) : ((u > 1.0f) ? ((u - 1.0f) * len) : 0.0f);
			float edge_d = av - w;
			float sdf = (edge_d > 0.0f || end_d > 0.0f)
				? sqrtf(fmaxf(0.0f, edge_d) * fmaxf(0.0f, edge_d) + end_d * end_d)
				: edge_d;
			float a = 1.0f - nd_fa_smoothstep(-aa * 0.35f, aa, sdf);
			if (a <= 0.005f) continue;

			float vn = (w > 1e-4f) ? nd_fa_clampf(v / w, -1.0f, 1.0f) : 0.0f;
			float avn = fabsf(vn);
			/* Raised central midrib + chevron lateral veins */
			float midrib = expf(-avn * 18.0f) * (1.0f - 0.55f * uc);
			float vein_phase = (mode == 2)
				? (avn * 16.0f)
				: (uc * ((mode == 1) ? 7.5f : 11.0f) - avn * 2.4f);
			float lat_vein = powf(0.5f + 0.5f * cosf(vein_phase * 6.2831853f), 5.0f) * (1.0f - avn * 0.7f) * 0.55f;
			/* Subtle cup-fold across leaf blade & sunlit tip gradient */
			float fold = (1.0f - avn * 0.28f) * (0.86f + 0.20f * uc);
			float shade = shade_base * fold + midrib * 0.11f + lat_vein * 0.07f;
			float bump = 0.5f + 0.34f * vn + ((vn >= 0.0f) ? -0.10f : 0.10f) * midrib + 0.08f * lat_vein;
			float wood = (uc < 0.08f && avn < 0.22f) ? (0.45f * (1.0f - uc / 0.08f)) : 0.0f;
			nd_fa_blend_px(atlas, ox, oy, px, py, shade, bump, wood, a);
		}
	}
}

/* Tile (0, 0): Conifer Pine / Fir Pectinate Needle Spray */
static inline void nd_fa_bake_conifer_tile(uint8_t *atlas, int ox, int oy)
{
	/* Main woody shoot rachis */
	nd_fa_draw_twig(atlas, ox, oy, 0.02f, 0.50f, 0.88f, 0.50f, 0.016f, 0.0045f, 0.010f, 0.76f);

	/* 10 alternating lateral sub-shoots + secondary forks with distinct V-notches between shoots
	 * so conifer boughs have feathery, serrated Skyrim evergreen silhouettes instead of oval paddles */
	static const float sub_u[10]   = { 0.08f, 0.11f, 0.24f, 0.27f, 0.40f, 0.43f, 0.56f, 0.59f, 0.72f, 0.74f };
	static const float sub_ang[10] = { 0.94f, -0.92f, 0.84f, -0.82f, 0.74f, -0.72f, 0.64f, -0.62f, 0.50f, -0.48f };
	static const float sub_len[10] = { 0.42f, 0.41f, 0.43f, 0.42f, 0.38f, 0.37f, 0.31f, 0.30f, 0.22f, 0.21f };
	for (int s = 0; s < 10; s++) {
		float sx0 = sub_u[s];
		float sy0 = 0.50f + 0.010f * 4.0f * sx0 * (1.0f - sx0);
		float sx1 = sx0 + cosf(sub_ang[s]) * sub_len[s];
		float sy1 = sy0 + sinf(sub_ang[s]) * sub_len[s];
		nd_fa_draw_twig(atlas, ox, oy, sx0, sy0, sx1, sy1, 0.0085f, 0.0028f, (s & 1) ? -0.008f : 0.008f, 0.70f);

		/* Secondary fork off the middle of the lateral shoot */
		if (s < 8) {
			float fx0 = sx0 + (sx1 - sx0) * 0.44f;
			float fy0 = sy0 + (sy1 - sy0) * 0.44f;
			float fang = sub_ang[s] + ((s & 1) ? -0.46f : 0.46f);
			float flen = sub_len[s] * 0.52f;
			float fx1 = fx0 + cosf(fang) * flen;
			float fy1 = fy0 + sinf(fang) * flen;
			nd_fa_draw_twig(atlas, ox, oy, fx0, fy0, fx1, fy1, 0.0055f, 0.0020f, 0.0f, 0.68f);
			int n_fork = 14;
			for (int k = 0; k < n_fork; k++) {
				uint32_t h = nd_fa_hash(1900u + (uint32_t)s * 131u + (uint32_t)k * 23u);
				float t = 0.08f + 0.88f * ((float)k / (float)(n_fork - 1));
				float bx = fx0 + (fx1 - fx0) * t;
				float by = fy0 + (fy1 - fy0) * t;
				for (int side = -1; side <= 1; side += 2) {
					float jit = (nd_fa_rand01(h + (uint32_t)(side + 2)) - 0.5f) * 0.22f;
					float nang = fang + (float)side * (0.58f + 0.20f * (1.0f - t)) + jit;
					float nlen = (0.072f + 0.026f * nd_fa_rand01(h + 9u)) * (1.0f - 0.28f * t);
					float sh = 0.48f + 0.40f * t + 0.10f * nd_fa_rand01(h + 19u);
					nd_fa_draw_needle(atlas, ox, oy, bx, by, nang, nlen, 0.0044f, (float)side * 0.006f, sh);
				}
			}
		}

		/* Crisp individual needle fascicles along each lateral sub-shoot */
		int n_sub = 22;
		for (int k = 0; k < n_sub; k++) {
			uint32_t h = nd_fa_hash(1000u + (uint32_t)s * 97u + (uint32_t)k * 17u);
			float t = 0.06f + 0.92f * ((float)k / (float)(n_sub - 1));
			float bx = sx0 + (sx1 - sx0) * t;
			float by = sy0 + (sy1 - sy0) * t;
			for (int side = -1; side <= 1; side += 2) {
				float jitter = (nd_fa_rand01(h + (uint32_t)(side + 2)) - 0.5f) * 0.24f;
				float nang = sub_ang[s] + (float)side * (0.56f + 0.24f * (1.0f - t)) + jitter;
				float nlen = (0.088f + 0.034f * nd_fa_rand01(h + 9u)) * (1.0f - 0.30f * t);
				float sh = 0.46f + 0.44f * t + 0.10f * nd_fa_rand01(h + 23u);
				nd_fa_draw_needle(atlas, ox, oy, bx, by, nang, nlen, 0.0046f, (float)side * 0.007f, sh);
			}
		}
		/* Sub-shoot apical needle rosette */
		for (int k = -2; k <= 2; k++) {
			float nang = sub_ang[s] + (float)k * 0.20f;
			nd_fa_draw_needle(atlas, ox, oy, sx1 * 0.96f + sx0 * 0.04f, sy1 * 0.96f + sy0 * 0.04f,
			                  nang, 0.072f, 0.0044f, 0.0f, 0.94f);
		}
	}

	/* Central leader shoot needle whorls (foreground sunlit layer) */
	int n_main = 34;
	for (int k = 0; k < n_main; k++) {
		uint32_t h = nd_fa_hash(5000u + (uint32_t)k * 31u);
		float t = 0.04f + 0.90f * ((float)k / (float)(n_main - 1));
		float bx = 0.02f + 0.86f * t;
		float by = 0.50f + 0.010f * 4.0f * t * (1.0f - t);
		for (int side = -1; side <= 1; side += 2) {
			float jitter = (nd_fa_rand01(h + (uint32_t)(side + 3)) - 0.5f) * 0.26f;
			float nang = (float)side * (0.62f + 0.26f * (1.0f - t * 0.7f)) + jitter;
			float nlen = (0.096f + 0.032f * nd_fa_rand01(h + 13u)) * (1.0f - 0.32f * t * t);
			float sh = 0.58f + 0.34f * t + 0.08f * nd_fa_rand01(h + 37u);
			nd_fa_draw_needle(atlas, ox, oy, bx, by, nang, nlen, 0.0048f, (float)side * 0.008f, sh);
		}
	}
	/* Terminal leader tuft */
	for (int k = -3; k <= 3; k++) {
		float ang = (float)k * 0.18f;
		nd_fa_draw_needle(atlas, ox, oy, 0.87f, 0.50f, ang, 0.085f, 0.0045f, 0.0f, 0.96f);
	}
}

/* Tile (1, 0): Multi-Forked Serrated Broadleaf Ovate Branchlet (Birch, Beech, Olive, Alder)
 * Emits ~84 scale-accurate small leaves across 8 forked sub-twigs with natural sky-holes */
static inline void nd_fa_bake_ovate_tile(uint8_t *atlas, int ox, int oy)
{
	nd_fa_draw_twig(atlas, ox, oy, 0.02f, 0.50f, 0.84f, 0.50f, 0.013f, 0.0038f, 0.014f, 0.78f);
	static const float tw_u[8]   = { 0.10f, 0.14f, 0.26f, 0.30f, 0.44f, 0.48f, 0.62f, 0.66f };
	static const float tw_ang[8] = { 0.88f, -0.86f, 0.78f, -0.76f, 0.66f, -0.64f, 0.52f, -0.50f };
	static const float tw_len[8] = { 0.38f, 0.37f, 0.40f, 0.39f, 0.35f, 0.34f, 0.26f, 0.25f };

	/* Pass 1: Woody sub-twig network + deep shaded under-leaves */
	for (int s = 0; s < 8; s++) {
		float sx0 = tw_u[s], sy0 = 0.50f + 0.014f * 4.0f * sx0 * (1.0f - sx0);
		float sx1 = sx0 + cosf(tw_ang[s]) * tw_len[s];
		float sy1 = sy0 + sinf(tw_ang[s]) * tw_len[s];
		nd_fa_draw_twig(atlas, ox, oy, sx0, sy0, sx1, sy1, 0.0070f, 0.0024f, (s & 1) ? -0.008f : 0.008f, 0.72f);

		/* Secondary twiglet fork */
		float fx0 = sx0 + (sx1 - sx0) * 0.45f;
		float fy0 = sy0 + (sy1 - sy0) * 0.45f;
		float fang = tw_ang[s] + ((s & 1) ? -0.52f : 0.52f);
		float flen = tw_len[s] * 0.52f;
		float fx1 = fx0 + cosf(fang) * flen;
		float fy1 = fy0 + sinf(fang) * flen;
		nd_fa_draw_twig(atlas, ox, oy, fx0, fy0, fx1, fy1, 0.0048f, 0.0018f, 0.0f, 0.68f);

		/* Shaded background leaves along sub-twig and fork */
		for (int k = 0; k < 5; k++) {
			uint32_t h = nd_fa_hash(2100u + (uint32_t)s * 83u + (uint32_t)k * 29u);
			float t = 0.20f + 0.72f * ((float)k / 4.0f);
			float bx = sx0 + (sx1 - sx0) * t;
			float by = sy0 + (sy1 - sy0) * t;
			float side = (k & 1) ? 1.0f : -1.0f;
			float lang = tw_ang[s] + side * (0.55f + 0.25f * nd_fa_rand01(h)) + (nd_fa_rand01(h + 3u) - 0.5f) * 0.20f;
			float llen = (0.095f + 0.024f * nd_fa_rand01(h + 7u)) * (1.05f - 0.20f * t);
			float lw   = llen * 0.36f;
			float sh   = 0.48f + 0.24f * t + 0.08f * nd_fa_rand01(h + 11u);
			nd_fa_draw_leaf(atlas, ox, oy, bx, by, lang, llen, lw, 0, sh, h);
		}
	}

	/* Pass 2: Sunlit foreground leaves on lateral sub-twigs, secondary forks, and central shoot */
	for (int s = 0; s < 8; s++) {
		float sx0 = tw_u[s], sy0 = 0.50f + 0.014f * 4.0f * sx0 * (1.0f - sx0);
		float sx1 = sx0 + cosf(tw_ang[s]) * tw_len[s];
		float sy1 = sy0 + sinf(tw_ang[s]) * tw_len[s];
		float fx0 = sx0 + (sx1 - sx0) * 0.45f;
		float fy0 = sy0 + (sy1 - sy0) * 0.45f;
		float fang = tw_ang[s] + ((s & 1) ? -0.52f : 0.52f);
		float flen = tw_len[s] * 0.52f;

		for (int k = 0; k < 4; k++) {
			uint32_t h = nd_fa_hash(2600u + (uint32_t)s * 97u + (uint32_t)k * 31u);
			float t = 0.28f + 0.70f * ((float)k / 3.0f);
			float bx = fx0 + cosf(fang) * (flen * t);
			float by = fy0 + sinf(fang) * (flen * t);
			float side = (k & 1) ? -1.0f : 1.0f;
			float lang = fang + side * 0.48f + (nd_fa_rand01(h) - 0.5f) * 0.22f;
			float llen = 0.088f + 0.022f * nd_fa_rand01(h + 5u);
			float sh   = 0.74f + 0.22f * t;
			nd_fa_draw_leaf(atlas, ox, oy, bx, by, lang, llen, llen * 0.37f, 0, sh, h + 17u);
		}
		/* Terminal pair at sub-twig tip */
		for (int side = -1; side <= 1; side += 2) {
			uint32_t h = nd_fa_hash(2900u + (uint32_t)s * 41u + (uint32_t)(side + 2));
			float lang = tw_ang[s] + (float)side * 0.32f;
			nd_fa_draw_leaf(atlas, ox, oy, sx1 * 0.94f + sx0 * 0.06f, sy1 * 0.94f + sy0 * 0.06f,
			                lang, 0.092f, 0.034f, 0, 0.94f, h);
		}
	}
	/* Central shoot leaves & terminal crown tuft */
	for (int k = 0; k < 12; k++) {
		uint32_t h = nd_fa_hash(2990u + (uint32_t)k * 53u);
		float t = 0.12f + 0.78f * ((float)k / 11.0f);
		float bx = 0.04f + 0.80f * t;
		float by = 0.50f + 0.014f * 4.0f * t * (1.0f - t);
		float side = (k & 1) ? 1.0f : -1.0f;
		float lang = side * (0.45f + 0.25f * (1.0f - t)) + (nd_fa_rand01(h) - 0.5f) * 0.18f;
		float llen = 0.098f + 0.020f * nd_fa_rand01(h + 9u);
		float sh   = 0.72f + 0.26f * t;
		nd_fa_draw_leaf(atlas, ox, oy, bx, by, lang, llen, llen * 0.37f, 0, sh, h);
	}
}

/* Tile (0, 1): Multi-Forked Deeply Lobed Oak / Maple Broadleaf Branchlet
 * Emits ~68 scale-accurate lobed oak/maple leaves clustered in natural sub-rosettes */
static inline void nd_fa_bake_lobed_tile(uint8_t *atlas, int ox, int oy)
{
	nd_fa_draw_twig(atlas, ox, oy, 0.02f, 0.50f, 0.82f, 0.50f, 0.015f, 0.0045f, -0.012f, 0.76f);
	static const float tw_u[7]   = { 0.12f, 0.16f, 0.30f, 0.35f, 0.50f, 0.54f, 0.68f };
	static const float tw_ang[7] = { 0.86f, -0.84f, 0.74f, -0.72f, 0.62f, -0.60f, 0.44f };
	static const float tw_len[7] = { 0.37f, 0.36f, 0.38f, 0.37f, 0.32f, 0.31f, 0.22f };

	/* Pass 1: Crooked oak/maple sub-twigs + deep interior leaves */
	for (int s = 0; s < 7; s++) {
		float sx0 = tw_u[s], sy0 = 0.50f - 0.012f * 4.0f * sx0 * (1.0f - sx0);
		float sx1 = sx0 + cosf(tw_ang[s]) * tw_len[s];
		float sy1 = sy0 + sinf(tw_ang[s]) * tw_len[s];
		nd_fa_draw_twig(atlas, ox, oy, sx0, sy0, sx1, sy1, 0.0080f, 0.0028f, (s & 1) ? 0.009f : -0.009f, 0.72f);

		for (int k = 0; k < 4; k++) {
			uint32_t h = nd_fa_hash(3100u + (uint32_t)s * 79u + (uint32_t)k * 37u);
			float t = 0.22f + 0.68f * ((float)k / 3.0f);
			float bx = sx0 + (sx1 - sx0) * t;
			float by = sy0 + (sy1 - sy0) * t;
			float side = (k & 1) ? 1.0f : -1.0f;
			float lang = tw_ang[s] + side * (0.62f + 0.22f * nd_fa_rand01(h));
			float llen = 0.115f + 0.025f * nd_fa_rand01(h + 5u);
			float sh   = 0.46f + 0.26f * t;
			nd_fa_draw_leaf(atlas, ox, oy, bx, by, lang, llen, llen * 0.44f, 1, sh, h);
		}
	}

	/* Pass 2: Bright terminal oak/maple leaf rosettes around each sub-twig tip & central axis */
	for (int s = 0; s < 7; s++) {
		float sx0 = tw_u[s], sy0 = 0.50f - 0.012f * 4.0f * sx0 * (1.0f - sx0);
		float sx1 = sx0 + cosf(tw_ang[s]) * tw_len[s];
		float sy1 = sy0 + sinf(tw_ang[s]) * tw_len[s];
		/* 5-leaf terminal oak rosette at (sx1, sy1) */
		for (int r = -2; r <= 2; r++) {
			uint32_t h = nd_fa_hash(3500u + (uint32_t)s * 103u + (uint32_t)(r + 3) * 19u);
			float lang = tw_ang[s] + (float)r * 0.42f + (nd_fa_rand01(h) - 0.5f) * 0.14f;
			float llen = (0.110f + 0.024f * nd_fa_rand01(h + 7u)) * (1.0f - 0.06f * fabsf((float)r));
			float sh   = 0.76f + 0.05f * (float)(r + 2) + 0.06f * nd_fa_rand01(h + 13u);
			nd_fa_draw_leaf(atlas, ox, oy, sx0 * 0.12f + sx1 * 0.88f, sy0 * 0.12f + sy1 * 0.88f,
			                lang, llen, llen * 0.44f, 1, sh, h);
		}
	}
	/* Central axis foreground oak/maple leaves */
	for (int k = 0; k < 10; k++) {
		uint32_t h = nd_fa_hash(3900u + (uint32_t)k * 61u);
		float t = 0.15f + 0.78f * ((float)k / 9.0f);
		float bx = 0.04f + 0.78f * t;
		float by = 0.50f - 0.012f * 4.0f * t * (1.0f - t);
		float side = (k & 1) ? 1.0f : -1.0f;
		float lang = side * (0.48f + 0.25f * (1.0f - t)) + (nd_fa_rand01(h) - 0.5f) * 0.16f;
		float llen = 0.118f + 0.022f * nd_fa_rand01(h + 11u);
		float sh   = 0.72f + 0.26f * t;
		nd_fa_draw_leaf(atlas, ox, oy, bx, by, lang, llen, llen * 0.44f, 1, sh, h);
	}
}

/* Tile (1, 1): Feathery Bipinnate Pinnae (Acacia senegal) & Slender Lanceolate Willow Spray */
static inline void nd_fa_bake_blade_tile(uint8_t *atlas, int ox, int oy)
{
	nd_fa_draw_twig(atlas, ox, oy, 0.02f, 0.50f, 0.88f, 0.50f, 0.011f, 0.0028f, 0.008f, 0.76f);

	/* 10 lateral pinnae rachillae bearing fine compound micro-leaflets for feathery Acacia & Willow crowns */
	static const float pin_u[10]   = { 0.08f, 0.12f, 0.22f, 0.26f, 0.38f, 0.42f, 0.54f, 0.58f, 0.70f, 0.74f };
	static const float pin_ang[10] = { 0.88f, -0.86f, 0.80f, -0.78f, 0.72f, -0.70f, 0.62f, -0.60f, 0.48f, -0.46f };
	static const float pin_len[10] = { 0.38f, 0.37f, 0.40f, 0.39f, 0.36f, 0.35f, 0.30f, 0.29f, 0.22f, 0.21f };
	for (int p = 0; p < 10; p++) {
		float px0 = pin_u[p];
		float py0 = 0.50f + 0.008f * 4.0f * px0 * (1.0f - px0);
		float px1 = px0 + cosf(pin_ang[p]) * pin_len[p];
		float py1 = py0 + sinf(pin_ang[p]) * pin_len[p];
		nd_fa_draw_twig(atlas, ox, oy, px0, py0, px1, py1, 0.0055f, 0.0018f, (p & 1) ? -0.005f : 0.005f, 0.70f);

		int n_pinnules = 12;
		for (int k = 0; k < n_pinnules; k++) {
			uint32_t h = nd_fa_hash(6000u + (uint32_t)p * 71u + (uint32_t)k * 19u);
			float t = 0.08f + 0.88f * ((float)k / (float)(n_pinnules - 1));
			float bx = px0 + (px1 - px0) * t;
			float by = py0 + (py1 - py0) * t;
			for (int side = -1; side <= 1; side += 2) {
				float ang = pin_ang[p] + (float)side * 0.76f + (nd_fa_rand01(h) - 0.5f) * 0.14f;
				float len = (0.082f - 0.020f * t) * (0.90f + 0.20f * nd_fa_rand01(h + 7u));
				float sh = 0.54f + 0.42f * t + 0.06f * nd_fa_rand01(h + 13u);
				nd_fa_draw_leaf(atlas, ox, oy, bx, by, ang, len, 0.017f, 2, sh, h + (uint32_t)(side * 11));
			}
		}
	}

	/* Fine leaflets along main rachis & terminal pinna */
	int n_pairs = 16;
	for (int i = 0; i < n_pairs; i++) {
		uint32_t h = nd_fa_hash(4000u + (uint32_t)i * 37u);
		float t = 0.06f + 0.88f * ((float)i / (float)(n_pairs - 1));
		float bx = 0.03f + 0.84f * t;
		float by = 0.50f + 0.008f * 4.0f * t * (1.0f - t);
		for (int side = -1; side <= 1; side += 2) {
			float jit = (nd_fa_rand01(h + (uint32_t)(side + 5)) - 0.5f) * 0.16f;
			float ang = (float)side * (0.58f + 0.26f * (1.0f - t)) + jit;
			float len = (0.105f - 0.025f * t) * (0.90f + 0.20f * nd_fa_rand01(h + 19u));
			float sh = 0.68f + 0.28f * t;
			nd_fa_draw_leaf(atlas, ox, oy, bx, by, ang, len, 0.019f, 2, sh, h + (uint32_t)(side * 13));
		}
	}
}

/* Bake full 512x512 level-0 botanical branchlet atlas */
static inline void nd_foliage_atlas_bake_level0(uint8_t *rgba512)
{
	memset(rgba512, 0, (size_t)ND_FOL_ATLAS_SIZE * (size_t)ND_FOL_ATLAS_SIZE * 4u);
	nd_fa_bake_conifer_tile(rgba512, 0,                0);
	nd_fa_bake_ovate_tile  (rgba512, ND_FOL_TILE_SIZE, 0);
	nd_fa_bake_lobed_tile  (rgba512, 0,                ND_FOL_TILE_SIZE);
	nd_fa_bake_blade_tile  (rgba512, ND_FOL_TILE_SIZE, ND_FOL_TILE_SIZE);
}

/* SpeedTree alpha-coverage-preserving 2x2 box downsample:
 * Weights RGB by alpha so transparent pixels never bleed black halos into leaf rims,
 * and boosts mip alpha slightly so alpha-tested foliage preserves canopy fullness at distance. */
static inline void nd_foliage_atlas_downsample(const uint8_t *src, int sw, int sh, uint8_t *dst)
{
	int dw = (sw > 1) ? (sw >> 1) : 1;
	int dh = (sh > 1) ? (sh >> 1) : 1;
	for (int y = 0; y < dh; y++) {
		int sy0 = y * 2;
		int sy1 = (sy0 + 1 < sh) ? (sy0 + 1) : sy0;
		for (int x = 0; x < dw; x++) {
			int sx0 = x * 2;
			int sx1 = (sx0 + 1 < sw) ? (sx0 + 1) : sx0;
			const uint8_t *p[4] = {
				src + (sy0 * sw + sx0) * 4,
				src + (sy0 * sw + sx1) * 4,
				src + (sy1 * sw + sx0) * 4,
				src + (sy1 * sw + sx1) * 4
			};
			float sum_a = 0.0f, max_a = 0.0f;
			float sum_r = 0.0f, sum_g = 0.0f, sum_b = 0.0f;
			for (int k = 0; k < 4; k++) {
				float a = (float)p[k][3] * (1.0f / 255.0f);
				sum_a += a;
				if (a > max_a) max_a = a;
				sum_r += (float)p[k][0] * a;
				sum_g += (float)p[k][1] * a;
				sum_b += (float)p[k][2] * a;
			}
			uint8_t *o = dst + (y * dw + x) * 4;
			if (sum_a > 1e-4f) {
				float inv_a = 1.0f / sum_a;
				o[0] = (uint8_t)nd_fa_clampf(sum_r * inv_a, 0.0f, 255.0f);
				o[1] = (uint8_t)nd_fa_clampf(sum_g * inv_a, 0.0f, 255.0f);
				o[2] = (uint8_t)nd_fa_clampf(sum_b * inv_a, 0.0f, 255.0f);
			} else {
				o[0] = 180; o[1] = 128; o[2] = 0;
			}
			/* Preserve visual foliage coverage across mip levels (blend avg with max) */
			float avg_a = sum_a * 0.25f;
			float cov_a = nd_fa_clampf(avg_a * 0.72f + max_a * 0.34f, 0.0f, 1.0f);
			o[3] = nd_fa_u8(cov_a);
		}
	}
}

#endif /* ND_SEVEN_FOLIAGE_ATLAS_H */
