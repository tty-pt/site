/*
 * test_areole_chaff_occlusion.c
 *
 * Integration test verifying libareole + libchaff + libcorolla frustum culling
 * and conservative 2D Hi-Z occlusion culling against a ground-truth software
 * Z-buffer visibility oracle across:
 *   Scenario A: Flat multi-octave libchaff alpine terrain (96 camera poses:
 *               12 yaws x 4 pitches x 2 distances).
 *   Scenario B: libcorolla curved torus manifold + libchaff terrain at
 *               outer equator, polar crown, and concave inner hub equator.
 *   Scenario C: Dual-body close-up lamb (dist=45, near=2, far=25M) with
 *               libchaff foreground mountain ridge + distant libcorolla
 *               companion torus moon (16M wu away), testing linear-W depth
 *               precision and cross-body occlusion.
 *
 * Invariants verified:
 *   1. ZERO false negatives: any terrain patch owning visible ground-truth
 *      pixels in the software Z-buffer MUST be covered by emitted areole nodes
 *      (both frustum-only and with ar_occluder_t enabled).
 *   2. Real occlusion culling: foreground ridges and planetary limbs must cull
 *      hidden background valleys and occluded moon chunks.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include <ttypt/areole.h>
#include <ttypt/chaff.h>
#include <ttypt/corolla.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int g_failures = 0;

#define ASSERT_TRUE(cond, msg, ...) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL [%s:%d]: " msg "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
		g_failures++; \
	} \
} while (0)

/* ============================================================================
 * 1. Exact Double-Precision Column-Major Camera & Projection Helpers
 * ============================================================================ */

static void
mat4_mul_d(const double a[16], const double b[16], double out[16])
{
	double r[16];
	for (int c = 0; c < 4; c++) {
		for (int row = 0; row < 4; row++) {
			r[c * 4 + row] =
				a[0 * 4 + row] * b[c * 4 + 0] +
				a[1 * 4 + row] * b[c * 4 + 1] +
				a[2 * 4 + row] * b[c * 4 + 2] +
				a[3 * 4 + row] * b[c * 4 + 3];
		}
	}
	memcpy(out, r, sizeof(r));
}

static void
build_lookat_persp_mvp(const double eye[3], const double target[3], const double up_in[3],
                       double fovy_rad, double aspect, double znear, double zfar,
                       float out_mvp[16])
{
	double f[3] = { target[0] - eye[0], target[1] - eye[1], target[2] - eye[2] };
	double fl = sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
	if (fl < 1e-12) fl = 1.0;
	f[0] /= fl; f[1] /= fl; f[2] /= fl;

	double s[3] = {
		f[1] * up_in[2] - f[2] * up_in[1],
		f[2] * up_in[0] - f[0] * up_in[2],
		f[0] * up_in[1] - f[1] * up_in[0]
	};
	double sl = sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
	if (sl < 1e-12) {
		double alt[3] = { 0.0, 1.0, 0.0 };
		s[0] = f[1] * alt[2] - f[2] * alt[1];
		s[1] = f[2] * alt[0] - f[0] * alt[2];
		s[2] = f[0] * alt[1] - f[1] * alt[0];
		sl = sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
	}
	s[0] /= sl; s[1] /= sl; s[2] /= sl;

	double u[3] = {
		s[1] * f[2] - s[2] * f[1],
		s[2] * f[0] - s[0] * f[2],
		s[0] * f[1] - s[1] * f[0]
	};

	double view[16] = {
		s[0],  u[0], -f[0], 0.0,
		s[1],  u[1], -f[1], 0.0,
		s[2],  u[2], -f[2], 0.0,
		-(s[0] * eye[0] + s[1] * eye[1] + s[2] * eye[2]),
		-(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]),
		 (f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2]),
		1.0
	};

	double tf = 1.0 / tan(fovy_rad * 0.5);
	double nf = 1.0 / (znear - zfar);
	double proj[16] = {
		tf / aspect, 0.0, 0.0, 0.0,
		0.0, tf, 0.0, 0.0,
		0.0, 0.0, (zfar + znear) * nf, -1.0,
		0.0, 0.0, 2.0 * zfar * znear * nf, 0.0
	};

	double mvp[16];
	mat4_mul_d(proj, view, mvp);
	for (int i = 0; i < 16; i++)
		out_mvp[i] = (float)mvp[i];
}

/* ============================================================================
 * 2. Software Z-Buffer Ground-Truth Visibility Oracle (Linear-W Depth)
 * ============================================================================ */

#define ORACLE_W 160
#define ORACLE_H 90

typedef struct {
	float   wbuf[ORACLE_W * ORACLE_H];   /* Minimum linear clip-space w per pixel */
	int32_t owner[ORACLE_W * ORACLE_H];  /* Patch ID of nearest visible triangle (-1 = sky) */
} oracle_fb_t;

static void
oracle_reset(oracle_fb_t *fb)
{
	for (int i = 0; i < ORACLE_W * ORACLE_H; i++) {
		fb->wbuf[i] = 1e30f;
		fb->owner[i] = -1;
	}
}

typedef struct {
	float x, y, z, w;
} clip_v_t;

static clip_v_t
clip_project(const float m[16], float x, float y, float z)
{
	clip_v_t v;
	v.x = m[0] * x + m[4] * y + m[8]  * z + m[12];
	v.y = m[1] * x + m[5] * y + m[9]  * z + m[13];
	v.z = m[2] * x + m[6] * y + m[10] * z + m[14];
	v.w = m[3] * x + m[7] * y + m[11] * z + m[15];
	return v;
}

static void
oracle_raster_clipped_tri(oracle_fb_t *fb, clip_v_t v0, clip_v_t v1, clip_v_t v2, int32_t owner_id)
{
	float iw0 = 1.0f / v0.w, iw1 = 1.0f / v1.w, iw2 = 1.0f / v2.w;
	float sx0 = (v0.x * iw0 * 0.5f + 0.5f) * (float)ORACLE_W;
	float sy0 = (v0.y * iw0 * 0.5f + 0.5f) * (float)ORACLE_H;
	float sz0 = v0.z * iw0;

	float sx1 = (v1.x * iw1 * 0.5f + 0.5f) * (float)ORACLE_W;
	float sy1 = (v1.y * iw1 * 0.5f + 0.5f) * (float)ORACLE_H;
	float sz1 = v1.z * iw1;

	float sx2 = (v2.x * iw2 * 0.5f + 0.5f) * (float)ORACLE_W;
	float sy2 = (v2.y * iw2 * 0.5f + 0.5f) * (float)ORACLE_H;
	float sz2 = v2.z * iw2;

	float area = (sx1 - sx0) * (sy2 - sy0) - (sy1 - sy0) * (sx2 - sx0);
	if (fabsf(area) < 1e-5f) return;

	float min_x = fminf(sx0, fminf(sx1, sx2));
	float max_x = fmaxf(sx0, fmaxf(sx1, sx2));
	float min_y = fminf(sy0, fminf(sy1, sy2));
	float max_y = fmaxf(sy0, fmaxf(sy1, sy2));

	int ix0 = (int)floorf(min_x);
	int ix1 = (int)ceilf(max_x);
	int iy0 = (int)floorf(min_y);
	int iy1 = (int)ceilf(max_y);
	if (ix0 < 0) ix0 = 0;
	if (iy0 < 0) iy0 = 0;
	if (ix1 >= ORACLE_W) ix1 = ORACLE_W - 1;
	if (iy1 >= ORACLE_H) iy1 = ORACLE_H - 1;
	if (ix0 > ix1 || iy0 > iy1) return;

	float inv_area = 1.0f / area;
	for (int py = iy0; py <= iy1; py++) {
		float cy = (float)py + 0.5f;
		for (int px = ix0; px <= ix1; px++) {
			float cx = (float)px + 0.5f;
			float e0 = ((sx1 - sx0) * (cy - sy0) - (sy1 - sy0) * (cx - sx0)) * inv_area;
			if (e0 < 0.0f) continue;
			float e1 = ((sx2 - sx1) * (cy - sy1) - (sy2 - sy1) * (cx - sx1)) * inv_area;
			if (e1 < 0.0f) continue;
			float e2 = ((sx0 - sx2) * (cy - sy2) - (sy0 - sy2) * (cx - sx2)) * inv_area;
			if (e2 < 0.0f) continue;

			float ndc_z = e1 * sz0 + e2 * sz1 + e0 * sz2;
			if (ndc_z < -1.0f || ndc_z > 1.0f) continue;

			float inv_w = e1 * iw0 + e2 * iw1 + e0 * iw2;
			if (inv_w <= 0.0f) continue;
			float w = 1.0f / inv_w;

			int idx = py * ORACLE_W + px;
			if (w < fb->wbuf[idx]) {
				fb->wbuf[idx] = w;
				fb->owner[idx] = owner_id;
			}
		}
	}
}

static void
oracle_raster_tri_3d(oracle_fb_t *fb, const float mvp[16],
                     const float p0[3], const float p1[3], const float p2[3],
                     float near_w, int32_t owner_id)
{
	clip_v_t in[3] = {
		clip_project(mvp, p0[0], p0[1], p0[2]),
		clip_project(mvp, p1[0], p1[1], p1[2]),
		clip_project(mvp, p2[0], p2[1], p2[2])
	};
	clip_v_t out[4];
	int out_n = 0;

	for (int i = 0; i < 3; i++) {
		clip_v_t a = in[i];
		clip_v_t b = in[(i + 1) % 3];
		float da = a.w - near_w;
		float db = b.w - near_w;
		if (da >= 0.0f) {
			out[out_n++] = a;
			if (db < 0.0f) {
				float t = da / (da - db);
				clip_v_t c = {
					a.x + t * (b.x - a.x),
					a.y + t * (b.y - a.y),
					a.z + t * (b.z - a.z),
					a.w + t * (b.w - a.w)
				};
				out[out_n++] = c;
			}
		} else if (db >= 0.0f) {
			float t = da / (da - db);
			clip_v_t c = {
				a.x + t * (b.x - a.x),
				a.y + t * (b.y - a.y),
				a.z + t * (b.z - a.z),
				a.w + t * (b.w - a.w)
			};
			out[out_n++] = c;
		}
	}

	if (out_n >= 3) {
		oracle_raster_clipped_tri(fb, out[0], out[1], out[2], owner_id);
		if (out_n == 4)
			oracle_raster_clipped_tri(fb, out[0], out[2], out[3], owner_id);
	}
}

/* ============================================================================
 * 3. Multi-Octave libchaff Alpine Heightfield
 * ============================================================================ */

static float
chaff_alpine_height(int32_t wx, int32_t wy, uint32_t seed)
{
	/* Multi-octave ridged mountain range + broad continental basins.
	 * Uses libchaff's rotated FBM (shifts 5..10, ridged >= 8).
	 * Output is in Q12.4; scale into alpine range ~ [-350, +1350] wu. */
	int32_t q12_4 = chaff_fbm_rotated_2d_q12_4(
		wx, wy,
		5, 10,   /* min_shift=5 (32 wu), max_shift=10 (1024 wu) */
		8,       /* ridged folding for mountain ridges */
		5, 18,   /* amp_min_shift=5, base_amp=18 */
		seed, 0x9E37u,
		CHAFF_ROT_GOLD_COS_Q15, CHAFF_ROT_GOLD_SIN_Q15
	);
	float h = (float)q12_4 * (1.0f / 16.0f) * 3.8f + 180.0f;
	if (h < -500.0f) h = -500.0f;
	if (h > 1800.0f) h = 1800.0f;
	return h;
}

typedef struct {
	uint32_t seed;
	float    ridge_boost_x; /* Optional deterministic foreground ridge Y for dual-body test */
	float    ridge_boost_h;
	float    ridge_boost_w;
	float    valley_y;      /* Optional valley center Y (if valley_r > 0, smoothly flattens near lamb) */
	float    valley_r;
} chaff_terrain_ctx_t;

static float
eval_terrain_h(const chaff_terrain_ctx_t *ctx, int32_t wx, int32_t wy)
{
	float h = chaff_alpine_height(wx, wy, ctx->seed);
	if (ctx->valley_r > 0.0f) {
		float dy = fabsf((float)wy - ctx->valley_y);
		if (dy < ctx->valley_r) {
			float t = dy / ctx->valley_r;
			float s = t * t * (3.0f - 2.0f * t);
			h = 40.0f * (1.0f - s) + h * 0.25f * s;
		} else {
			h *= 0.25f;
		}
	}
	if (ctx->ridge_boost_h > 0.0f) {
		float dy = (float)wy - ctx->ridge_boost_x;
		float w = ctx->ridge_boost_w;
		if (fabsf(dy) < w) {
			float t = cosf((dy / w) * (float)(M_PI * 0.5));
			h += ctx->ridge_boost_h * t * t;
		}
	}
	return h;
}

static void
chaff_sampler(void *user_ctx, uint16_t world, uint8_t level,
              int32_t wx, int32_t wy, ar_sample_t *out)
{
	(void)world;
	(void)level;
	const chaff_terrain_ctx_t *ctx = (const chaff_terrain_ctx_t *)user_ctx;
	float h = eval_terrain_h(ctx, wx, wy);
	out->val = (int16_t)lrintf(h);
	out->rgba = 0xFF668844u;
	out->flags = 0u;
}

/* Returns true if the world patch [px0, px0+pw) x [py0, py0+ph) is covered by
 * emitted areole nodes (checking all 4 quadrant centers inside the patch, or
 * checking whether any emitted node overlaps the patch). */
static bool
patch_covered_by_nodes(const ar_node_t *const *nodes, uint32_t count,
                       int32_t px, int32_t py, int32_t pw, int32_t ph,
                       int32_t period_x, int32_t period_y)
{
	int32_t cx = px + pw / 2;
	int32_t cy = py + ph / 2;
	for (uint32_t i = 0; i < count; i++) {
		const ar_node_t *n = nodes[i];
		int32_t dx = cx - n->ox;
		int32_t dy = cy - n->oy;
		if (period_x > 0) {
			dx %= period_x;
			if (dx < 0) dx += period_x;
		}
		if (period_y > 0) {
			dy %= period_y;
			if (dy < 0) dy += period_y;
		}
		if (dx >= 0 && dx < n->w && dy >= 0 && dy < n->h)
			return true;
	}
	return false;
}

/* ============================================================================
 * Scenario A: Flat Multi-Octave libchaff Alpine Terrain (96 poses)
 * ============================================================================ */

static void
test_scenario_a_flat_alpine(void)
{
	printf("Scenario A: Flat Multi-Octave libchaff Alpine Terrain (96 poses)...\n");

	chaff_terrain_ctx_t tctx = { .seed = 0x51A9F01u, .ridge_boost_h = 0.0f };
	ar_config_t cfg = { 0 };
	cfg.base_w = 128;
	cfg.base_h = 128;
	cfg.grid_dim = 16;
	cfg.max_levels = 5;
	cfg.pool_capacity = 2048;
	cfg.max_active = 1024;
	cfg.value_scale = 1.0f;
	cfg.sampler = chaff_sampler;
	cfg.user_ctx = &tctx;

	ar_system_t *sys = ar_system_create_ex(&cfg);
	ASSERT_TRUE(sys != NULL, "ar_system_create_ex failed");
	if (!sys) return;

	oracle_fb_t *fb = (oracle_fb_t *)malloc(sizeof(oracle_fb_t));
	ar_occluder_t *occ = (ar_occluder_t *)malloc(sizeof(ar_occluder_t));
	const ar_node_t **nodes_frustum = (const ar_node_t **)malloc(1024 * sizeof(ar_node_t *));
	const ar_node_t **nodes_occ = (const ar_node_t **)malloc(1024 * sizeof(ar_node_t *));
	ASSERT_TRUE(fb && occ && nodes_frustum && nodes_occ, "malloc failed");

	/* Ground-truth patch grid: 25 x 25 patches of size 128 wu centered near (0,0)
	 * covering [-1600, +1600]^2 around the camera focal point. */
	const int32_t patch_sz = 128;
	const int grid_r = 14; /* 29 x 29 = 841 patches -> [-1856, +1856] */
	const int grid_n = grid_r * 2 + 1;
	const int total_patches = grid_n * grid_n;
	int *patch_pix = (int *)calloc((size_t)total_patches, sizeof(int));

	/* Precompute ground-truth elevation at 32 wu spacing (4x4 quads per 128 wu patch) */
	const int sub = 4;
	const int verts_side = grid_n * sub + 1;
	const int32_t ox_min = -grid_r * patch_sz;
	const int32_t oy_min = -grid_r * patch_sz;
	float *gt_h = (float *)malloc((size_t)verts_side * (size_t)verts_side * sizeof(float));
	for (int vy = 0; vy < verts_side; vy++) {
		for (int vx = 0; vx < verts_side; vx++) {
			int32_t wx = ox_min + vx * (patch_sz / sub);
			int32_t wy = oy_min + vy * (patch_sz / sub);
			gt_h[vy * verts_side + vx] = eval_terrain_h(&tctx, wx, wy);
		}
	}

	const double pitches_deg[4] = { 12.0, 22.0, 38.0, 60.0 };
	const double dists[2] = { 220.0, 820.0 };
	const double fovy = 52.0 * M_PI / 180.0;
	const double aspect = (double)ORACLE_W / (double)ORACLE_H;
	const double znear = 4.0, zfar = 8000.0;

	/* Place camera focal point in a valley between alpine peaks so low pitches look into ridges */
	const double fx = 64.0, fy = 64.0;
	const double fz = (double)eval_terrain_h(&tctx, (int32_t)fx, (int32_t)fy) + 12.0;

	int total_poses = 0;
	int total_vis_patches = 0;
	int fn_frustum = 0;
	int fn_occ = 0;
	uint64_t sum_frustum_nodes = 0;
	uint64_t sum_occ_nodes = 0;
	uint64_t sum_low_pitch_frustum = 0;
	uint64_t sum_low_pitch_occ = 0;

	for (int di = 0; di < 2; di++) {
		double dist = dists[di];
		for (int pi = 0; pi < 4; pi++) {
			double pitch = pitches_deg[pi] * M_PI / 180.0;
			for (int yi = 0; yi < 12; yi++) {
				double yaw = (double)yi * (30.0 * M_PI / 180.0);
				double horiz = dist * cos(pitch);
				double eye[3] = {
					fx + horiz * sin(yaw),
					fy - horiz * cos(yaw),
					fz + dist * sin(pitch)
				};
				/* Ensure eye is strictly above the local ground-truth terrain neighborhood (never underground) */
				double eye_ground = (double)eval_terrain_h(&tctx, (int32_t)llround(eye[0]), (int32_t)llround(eye[1]));
				int evx = (int)floor((eye[0] - (double)ox_min) / (double)(patch_sz / sub));
				int evy = (int)floor((eye[1] - (double)oy_min) / (double)(patch_sz / sub));
				for (int dy = -2; dy <= 2; dy++) {
					int ny = evy + dy;
					if (ny < 0 || ny >= verts_side) continue;
					for (int dx = -2; dx <= 2; dx++) {
						int nx = evx + dx;
						if (nx < 0 || nx >= verts_side) continue;
						double gh = (double)gt_h[ny * verts_side + nx];
						if (gh > eye_ground) eye_ground = gh;
					}
				}
				if (eye[2] < eye_ground + 24.0)
					eye[2] = eye_ground + 24.0;

				double target[3] = { fx, fy, fz };
				double up[3] = { 0.0, 0.0, 1.0 };
				float mvp[16];
				build_lookat_persp_mvp(eye, target, up, fovy, aspect, znear, zfar, mvp);

				/* 1. Rasterize ground-truth libchaff heightfield into software Z-buffer */
				oracle_reset(fb);
				for (int py = 0; py < grid_n; py++) {
					for (int px = 0; px < grid_n; px++) {
						int pid = py * grid_n + px;
						for (int sy = 0; sy < sub; sy++) {
							int vy = py * sub + sy;
							float wy0 = (float)(oy_min + vy * (patch_sz / sub));
							float wy1 = (float)(oy_min + (vy + 1) * (patch_sz / sub));
							for (int sx = 0; sx < sub; sx++) {
								int vx = px * sub + sx;
								float wx0 = (float)(ox_min + vx * (patch_sz / sub));
								float wx1 = (float)(ox_min + (vx + 1) * (patch_sz / sub));
								float p00[3] = { wx0, wy0, gt_h[vy * verts_side + vx] };
								float p10[3] = { wx1, wy0, gt_h[vy * verts_side + vx + 1] };
								float p01[3] = { wx0, wy1, gt_h[(vy + 1) * verts_side + vx] };
								float p11[3] = { wx1, wy1, gt_h[(vy + 1) * verts_side + vx + 1] };
								oracle_raster_tri_3d(fb, mvp, p00, p10, p11, (float)znear, pid);
								oracle_raster_tri_3d(fb, mvp, p00, p11, p01, (float)znear, pid);
							}
						}
					}
				}

				/* Count visible pixels per ground-truth patch (ignore 2-px outer border to avoid subpixel tie noise) */
				memset(patch_pix, 0, (size_t)total_patches * sizeof(int));
				for (int y = 2; y < ORACLE_H - 2; y++) {
					for (int x = 2; x < ORACLE_W - 2; x++) {
						int32_t id = fb->owner[y * ORACLE_W + x];
						if (id >= 0 && id < total_patches)
							patch_pix[id]++;
					}
				}

				/* 2. Query libareole WITHOUT occluder */
				ar_cull_opts_t opts = { 0 };
				opts.z_min = -520.0f;
				opts.z_max = 1820.0f;
				opts.focal_x = (float)fx;
				opts.focal_y = (float)fy;
				opts.focal_z = (float)fz;
				opts.viewport_w = 1280.0f;
				opts.viewport_h = 720.0f;
				opts.target_quad_px = 18.0f;
				opts.margin = 4.0f;
				opts.max_level = 4;
				opts.use_cached_bounds = true;
				opts.cached_bounds_max_level = 2;
				opts.cached_bounds_pad = 32.0f;

				uint32_t cnt_frustum = ar_system_query_matrix(sys, 0, mvp, &opts, nodes_frustum, 1024);

				/* 3. Query libareole WITH ar_occluder_t */
				ar_occluder_reset(occ, mvp, (float)znear, 0.002f);
				opts.occluder = occ;
				opts.occluder_z_bias = 12.0f;
				opts.occluder_texel_bias = 1.6f; /* bounds max libchaff alpine slope between coarse verts */
				opts.occluder_box_z_pad = 16.0f;
				uint32_t cnt_occ = ar_system_query_matrix(sys, 0, mvp, &opts, nodes_occ, 1024);

				sum_frustum_nodes += cnt_frustum;
				sum_occ_nodes += cnt_occ;
				if (pi <= 1) {
					sum_low_pitch_frustum += cnt_frustum;
					sum_low_pitch_occ += cnt_occ;
				}

				/* 4. Check ZERO false negatives against software Z-buffer ground truth */
				for (int pid = 0; pid < total_patches; pid++) {
					if (patch_pix[pid] < 2) continue;
					total_vis_patches++;
					int px = pid % grid_n;
					int py = pid / grid_n;
					int32_t wx = ox_min + px * patch_sz;
					int32_t wy = oy_min + py * patch_sz;

					if (!patch_covered_by_nodes(nodes_frustum, cnt_frustum, wx, wy, patch_sz, patch_sz, 0, 0)) {
						fn_frustum++;
						if (fn_frustum <= 5) {
							fprintf(stderr, "  [FRUSTUM FN] pose(dist=%.0f pitch=%.0f yaw=%.0f) patch(%d,%d) pix=%d\n",
							        dist, pitches_deg[pi], yi * 30.0, wx, wy, patch_pix[pid]);
						}
					}
					if (!patch_covered_by_nodes(nodes_occ, cnt_occ, wx, wy, patch_sz, patch_sz, 0, 0)) {
						fn_occ++;
						if (fn_occ <= 5) {
							fprintf(stderr, "  [OCCLUDER FN] pose(dist=%.0f pitch=%.0f yaw=%.0f) patch(%d,%d) pix=%d\n",
							        dist, pitches_deg[pi], yi * 30.0, wx, wy, patch_pix[pid]);
						}
					}
				}
				total_poses++;
			}
		}
	}

	double cull_pct = sum_frustum_nodes > 0
		? 100.0 * (double)(sum_frustum_nodes - sum_occ_nodes) / (double)sum_frustum_nodes
		: 0.0;
	double low_pitch_cull_pct = sum_low_pitch_frustum > 0
		? 100.0 * (double)(sum_low_pitch_frustum - sum_low_pitch_occ) / (double)sum_low_pitch_frustum
		: 0.0;

	printf("  -> %d poses, %d GT visible patch instances: fn_frustum=%d fn_occ=%d\n",
	       total_poses, total_vis_patches, fn_frustum, fn_occ);
	printf("  -> Nodes: frustum=%llu occ=%llu (overall cull=%.1f%%, low-pitch cull=%.1f%%)\n",
	       (unsigned long long)sum_frustum_nodes, (unsigned long long)sum_occ_nodes,
	       cull_pct, low_pitch_cull_pct);

	ASSERT_TRUE(total_vis_patches > 2000, "Expected substantial ground-truth visible patches");
	ASSERT_TRUE(fn_frustum == 0, "Zero false negatives required for frustum culling (got %d)", fn_frustum);
	ASSERT_TRUE(fn_occ == 0, "Zero false negatives required for Hi-Z occlusion culling (got %d)", fn_occ);
	ASSERT_TRUE(sum_occ_nodes < sum_frustum_nodes, "Hi-Z occluder must cull genuinely hidden alpine terrain");
	ASSERT_TRUE(low_pitch_cull_pct >= 10.0, "Expected >=10%% low-pitch occlusion cull ratio (got %.1f%%)", low_pitch_cull_pct);

	free(gt_h);
	free(patch_pix);
	free(nodes_occ);
	free(nodes_frustum);
	free(occ);
	free(fb);
	ar_system_destroy(sys);
}

/* ============================================================================
 * Scenario B: libcorolla Curved Torus Manifold + libchaff Terrain
 *             (Outer Equator, Polar Crown, Concave Inner Hub Equator)
 * ============================================================================ */

typedef struct {
	corolla_surface_t surf;
	corolla_pose_t    pose;
	bool              has_pose;
	double            ref_origin[3];
	int32_t           world_w;
	int32_t           world_h;
	double            z_scale;
	double            cam_body[3]; /* Observer eye in body space for horizon cull */
} torus_cull_ctx_t;

static void
torus_point_3d_cb(void *ctx_ptr, float wx, float wy, float z, float out_xyz[3])
{
	const torus_cull_ctx_t *c = (const torus_cull_ctx_t *)ctx_ptr;
	double u = (double)wx / (double)c->world_w;
	double v = (double)wy / (double)c->world_h;
	u -= floor(u);
	v -= floor(v);
	corolla_frame_t f;
	corolla_eval_frame(&c->surf, u, v, (double)z * c->z_scale, &f);
	double p[3] = { f.pos[0], f.pos[1], f.pos[2] };
	if (c->has_pose) {
		double q[3];
		corolla_pose_point_to_inertial(&c->pose, p, q);
		p[0] = q[0]; p[1] = q[1]; p[2] = q[2];
	}
	out_xyz[0] = (float)(p[0] - c->ref_origin[0]);
	out_xyz[1] = (float)(p[1] - c->ref_origin[1]);
	out_xyz[2] = (float)(p[2] - c->ref_origin[2]);
}

static void
torus_bounds_3d_cb(void *ctx_ptr, float ox, float oy, float w, float h,
                   float z_min, float z_max,
                   float out_min[3], float out_max[3],
                   float out_center[3], float *out_span_w, float *out_span_h)
{
	const torus_cull_ctx_t *c = (const torus_cull_ctx_t *)ctx_ptr;
	double u0 = (double)ox / (double)c->world_w;
	double v0 = (double)oy / (double)c->world_h;
	double du = (double)w / (double)c->world_w;
	double dv = (double)h / (double)c->world_h;

	double bmin[3], bmax[3];
	corolla_patch_aabb(&c->surf, c->has_pose ? &c->pose : NULL, c->ref_origin,
	                   u0, v0, du, dv,
	                   (double)z_min * c->z_scale, (double)z_max * c->z_scale,
	                   bmin, bmax);
	for (int i = 0; i < 3; i++) {
		out_min[i] = (float)bmin[i];
		out_max[i] = (float)bmax[i];
		out_center[i] = 0.5f * (out_min[i] + out_max[i]);
	}

	double theta_mid = COROLLA_TWO_PI * ((v0 + 0.5 * dv) - 0.5);
	double ring_r = c->surf.r_major + c->surf.r_minor * cos(theta_mid);
	*out_span_w = (float)(du * COROLLA_TWO_PI * ring_r);
	*out_span_h = (float)(dv * COROLLA_TWO_PI * c->surf.r_minor);
}

static void
test_scenario_b_torus_manifold(void)
{
	printf("Scenario B: libcorolla Curved Torus Manifold + libchaff Terrain (outer/polar/concave)...\n");

	/* Compact planetary torus where curvature & concave inner-hub arch are pronounced */
	const double R_maj = 20000.0;
	const double r_min = 8000.0;
	const int32_t WORLD_W = 65536;
	const int32_t WORLD_H = 65536;

	chaff_terrain_ctx_t tctx = { .seed = 0x7050C01u, .ridge_boost_h = 0.0f };
	ar_config_t cfg = { 0 };
	cfg.base_w = 512;
	cfg.base_h = 512;
	cfg.grid_dim = 8;
	cfg.max_levels = 7; /* 512 << 6 = 32768, 2x2 root covers 65536 */
	cfg.period_x = WORLD_W;
	cfg.period_y = WORLD_H;
	cfg.pool_capacity = 2048;
	cfg.max_active = 1024;
	cfg.value_scale = 1.0f;
	cfg.sampler = chaff_sampler;
	cfg.user_ctx = &tctx;

	ar_system_t *sys = ar_system_create_ex(&cfg);
	ASSERT_TRUE(sys != NULL, "torus ar_system_create_ex failed");
	if (!sys) return;

	oracle_fb_t *fb = (oracle_fb_t *)malloc(sizeof(oracle_fb_t));
	ar_occluder_t *occ = (ar_occluder_t *)malloc(sizeof(ar_occluder_t));
	const ar_node_t **nodes_frustum = (const ar_node_t **)malloc(1024 * sizeof(ar_node_t *));
	const ar_node_t **nodes_occ = (const ar_node_t **)malloc(1024 * sizeof(ar_node_t *));

	/* Ground-truth torus tessellation: 128 x 128 patches (each 512x512 world units == cfg.base_w) */
	const int gt_n = 128;
	const int32_t gt_patch_sz = WORLD_W / gt_n; /* 512 */
	const int gt_sub = 1;
	const int gt_verts = gt_n * gt_sub; /* periodic 128 x 128 */
	float *gt_h = (float *)malloc((size_t)gt_verts * (size_t)gt_verts * sizeof(float));
	for (int vy = 0; vy < gt_verts; vy++) {
		int32_t wy = vy * (gt_patch_sz / gt_sub);
		for (int vx = 0; vx < gt_verts; vx++) {
			int32_t wx = vx * (gt_patch_sz / gt_sub);
			/* Scale height slightly down so peaks are ~6% of tube radius (realistic planetary scale) */
			gt_h[vy * gt_verts + vx] = eval_terrain_h(&tctx, wx, wy) * 0.4f;
		}
	}
	int *patch_pix = (int *)calloc((size_t)(gt_n * gt_n), sizeof(int));

	/* 3 latitudes: v=0.5 (outer convex), v=0.75 (north crown pole), v=0.0 (inner concave hub!) */
	const double lat_v[3] = { 0.5, 0.75, 0.0 };
	const char *lat_names[3] = { "outer_equator", "north_crown", "inner_concave" };
	const double pitches_deg[2] = { 16.0, 38.0 };
	const double cam_dists[2] = { 900.0, 4500.0 };

	int total_vis = 0;
	int fn_frustum = 0;
	int fn_occ = 0;
	uint64_t sum_frustum = 0;
	uint64_t sum_occ = 0;

	corolla_surface_t surf = corolla_torus(R_maj, r_min);

	for (int li = 0; li < 3; li++) {
		double u_cam = 0.25;
		double v_cam = lat_v[li];
		int32_t cam_wx = (int32_t)(u_cam * WORLD_W);
		int32_t cam_wy = (int32_t)(v_cam * WORLD_H);
		double cam_h = (double)eval_terrain_h(&tctx, cam_wx, cam_wy) * 0.4 + 40.0;

		corolla_frame_t frame;
		corolla_eval_frame(&surf, u_cam, v_cam, 0.0, &frame);

		torus_cull_ctx_t tcc = { 0 };
		tcc.surf = surf;
		tcc.has_pose = false;
		tcc.ref_origin[0] = frame.pos[0];
		tcc.ref_origin[1] = frame.pos[1];
		tcc.ref_origin[2] = frame.pos[2];
		tcc.world_w = WORLD_W;
		tcc.world_h = WORLD_H;
		tcc.z_scale = 0.4; /* matches gt_h scale */

		/* Precompute relative 3D vertex positions for this latitude's ref_origin */
		float *gt_xyz = (float *)malloc((size_t)gt_verts * (size_t)gt_verts * 3 * sizeof(float));
		for (int vy = 0; vy < gt_verts; vy++) {
			double v = (double)vy / (double)gt_verts;
			for (int vx = 0; vx < gt_verts; vx++) {
				double u = (double)vx / (double)gt_verts;
				corolla_frame_t pf;
				corolla_eval_frame(&surf, u, v, (double)gt_h[vy * gt_verts + vx], &pf);
				size_t idx = ((size_t)vy * gt_verts + vx) * 3;
				gt_xyz[idx + 0] = (float)(pf.pos[0] - tcc.ref_origin[0]);
				gt_xyz[idx + 1] = (float)(pf.pos[1] - tcc.ref_origin[1]);
				gt_xyz[idx + 2] = (float)(pf.pos[2] - tcc.ref_origin[2]);
			}
		}

		for (int di = 0; di < 2; di++) {
			double dist = cam_dists[di];
			for (int pi = 0; pi < 2; pi++) {
				double pitch = pitches_deg[pi] * M_PI / 180.0;
				for (int yi = 0; yi < 6; yi++) {
					double yaw = (double)yi * (60.0 * M_PI / 180.0);
					/* Local tangent frame at (u_cam, v_cam): tan_u (East), tan_v (North), norm (Up) */
					double target[3] = {
						frame.norm[0] * cam_h,
						frame.norm[1] * cam_h,
						frame.norm[2] * cam_h
					};
					double horiz = dist * cos(pitch);
					double vert = dist * sin(pitch);
					double dir_e = sin(yaw);
					double dir_n = -cos(yaw);
					double eye[3] = {
						target[0] + horiz * (dir_e * frame.tan_u[0] + dir_n * frame.tan_v[0]) + vert * frame.norm[0],
						target[1] + horiz * (dir_e * frame.tan_u[1] + dir_n * frame.tan_v[1]) + vert * frame.norm[1],
						target[2] + horiz * (dir_e * frame.tan_u[2] + dir_n * frame.tan_v[2]) + vert * frame.norm[2]
					};

					float mvp[16];
					const double znear = 8.0, zfar = 120000.0;
					build_lookat_persp_mvp(eye, target, frame.norm,
					                       55.0 * M_PI / 180.0,
					                       (double)ORACLE_W / (double)ORACLE_H,
					                       znear, zfar, mvp);

					/* 1. Rasterize entire curved 3D torus into software Z-buffer */
					oracle_reset(fb);
					for (int py = 0; py < gt_n; py++) {
						for (int px = 0; px < gt_n; px++) {
							int pid = py * gt_n + px;
							for (int sy = 0; sy < gt_sub; sy++) {
								int vy0 = (py * gt_sub + sy) % gt_verts;
								int vy1 = (vy0 + 1) % gt_verts;
								for (int sx = 0; sx < gt_sub; sx++) {
									int vx0 = (px * gt_sub + sx) % gt_verts;
									int vx1 = (vx0 + 1) % gt_verts;
									const float *p00 = &gt_xyz[((size_t)vy0 * gt_verts + vx0) * 3];
									const float *p10 = &gt_xyz[((size_t)vy0 * gt_verts + vx1) * 3];
									const float *p01 = &gt_xyz[((size_t)vy1 * gt_verts + vx0) * 3];
									const float *p11 = &gt_xyz[((size_t)vy1 * gt_verts + vx1) * 3];
									oracle_raster_tri_3d(fb, mvp, p00, p10, p11, (float)znear, pid);
									oracle_raster_tri_3d(fb, mvp, p00, p11, p01, (float)znear, pid);
								}
							}
						}
					}

					memset(patch_pix, 0, (size_t)(gt_n * gt_n) * sizeof(int));
					for (int y = 2; y < ORACLE_H - 2; y++) {
						for (int x = 2; x < ORACLE_W - 2; x++) {
							int32_t id = fb->owner[y * ORACLE_W + x];
							if (id >= 0 && id < gt_n * gt_n)
								patch_pix[id]++;
						}
					}

					ar_cull_opts_t opts = { 0 };
					opts.z_min = -520.0f;
					opts.z_max = 1820.0f;
					opts.focal_x = (float)cam_wx;
					opts.focal_y = (float)cam_wy;
					opts.focal_z = (float)(cam_h / tcc.z_scale);
					opts.viewport_w = 1280.0f;
					opts.viewport_h = 720.0f;
					opts.target_quad_px = 28.0f;
					opts.max_level = 6;
					opts.bounds_3d_fn = torus_bounds_3d_cb;
					opts.bounds_ctx = &tcc;

					uint32_t cnt_frustum = ar_system_query_matrix(sys, 0, mvp, &opts, nodes_frustum, 1024);

					/* Now with conservative 2D Hi-Z occluder + concave inner-equator chord sagitta protection */
					ar_occluder_reset(occ, mvp, (float)znear, 0.002f);
					opts.occluder = occ;
					opts.point_3d_fn = torus_point_3d_cb;
					opts.occluder_z_bias = 18.0f;
					opts.occluder_texel_bias = 1.6f;
					opts.occluder_concave_inv_r = (float)(1.0 / (R_maj - r_min)) / (float)tcc.z_scale;
					opts.occluder_box_z_pad = 16.0f;

					uint32_t cnt_occ = ar_system_query_matrix(sys, 0, mvp, &opts, nodes_occ, 1024);

					sum_frustum += cnt_frustum;
					sum_occ += cnt_occ;

					for (int pid = 0; pid < gt_n * gt_n; pid++) {
						if (patch_pix[pid] < 2) continue;
						total_vis++;
						int px = pid % gt_n;
						int py = pid / gt_n;
						int32_t wx = px * gt_patch_sz;
						int32_t wy = py * gt_patch_sz;
						if (!patch_covered_by_nodes(nodes_frustum, cnt_frustum, wx, wy,
						                            gt_patch_sz, gt_patch_sz, WORLD_W, WORLD_H)) {
							fn_frustum++;
							if (fn_frustum <= 5) {
								fprintf(stderr, "  [TORUS FRUSTUM FN] %s dist=%.0f pitch=%.0f yaw=%.0f patch(%d,%d) pix=%d\n",
								        lat_names[li], dist, pitches_deg[pi], yi * 60.0, wx, wy, patch_pix[pid]);
							}
						}
						if (!patch_covered_by_nodes(nodes_occ, cnt_occ, wx, wy,
						                            gt_patch_sz, gt_patch_sz, WORLD_W, WORLD_H)) {
							fn_occ++;
							if (fn_occ <= 5) {
								fprintf(stderr, "  [TORUS OCC FN] %s dist=%.0f pitch=%.0f yaw=%.0f patch(%d,%d) pix=%d\n",
								        lat_names[li], dist, pitches_deg[pi], yi * 60.0, wx, wy, patch_pix[pid]);
							}
						}
					}
				}
			}
		}
		free(gt_xyz);
	}

	double cull_pct = sum_frustum > 0
		? 100.0 * (double)(sum_frustum - sum_occ) / (double)sum_frustum
		: 0.0;
	printf("  -> 72 torus poses (outer/polar/concave), %d GT visible patches: fn_frustum=%d fn_occ=%d\n",
	       total_vis, fn_frustum, fn_occ);
	printf("  -> Torus nodes: frustum=%llu occ=%llu (limb+ridge cull=%.1f%%)\n",
	       (unsigned long long)sum_frustum, (unsigned long long)sum_occ, cull_pct);

	ASSERT_TRUE(total_vis > 1500, "Expected substantial visible torus patches");
	ASSERT_TRUE(fn_frustum == 0, "Zero false negatives required for torus frustum culling (got %d)", fn_frustum);
	ASSERT_TRUE(fn_occ == 0, "Zero false negatives required for torus Hi-Z occlusion (got %d)", fn_occ);
	ASSERT_TRUE(cull_pct >= 10.0, "Expected >=10%% planetary limb+ridge occlusion on torus (got %.1f%%)", cull_pct);

	free(patch_pix);
	free(gt_h);
	free(nodes_occ);
	free(nodes_frustum);
	free(occ);
	free(fb);
	ar_system_destroy(sys);
}

/* ============================================================================
 * Scenario C: Dual-Body Close-Up Lamb (dist=45, near=2, far=25M)
 *             + Foreground libchaff Mountain Ridge
 *             + Distant Companion Torus Moon (16M wu away)
 * ============================================================================ */

static void
test_scenario_c_closeup_with_moon(void)
{
	printf("Scenario C: Dual-Body Close-Up Lamb + Foreground Ridge + Companion Moon (2..25M wu)...\n");

	/* Full NeverDark planetary radii */
	const double PRIM_R = 2000000.0, PRIM_r = 1000000.0;
	const double MOON_R = 1000000.0, MOON_r = 500000.0;
	const int32_t WORLD_W = 10485760;
	const int32_t WORLD_H = 8388608;

	/* Primary terrain: flat valley around the lamb + prominent mountain ridge ~2400 wu North (+v) */
	const double u_cam = 0.25, v_cam = 0.5; /* outer equator */
	const int32_t cam_wx = (int32_t)(u_cam * WORLD_W);
	const int32_t cam_wy = (int32_t)(v_cam * WORLD_H);

	chaff_terrain_ctx_t prim_tctx = {
		.seed = 0xCAFE001u,
		.ridge_boost_x = (float)(cam_wy + 3200),
		.ridge_boost_h = 850.0f,
		.ridge_boost_w = 1400.0f,
		.valley_y = (float)cam_wy,
		.valley_r = 1800.0f
	};
	chaff_terrain_ctx_t moon_tctx = {
		.seed = 0xBEEF002u,
		.ridge_boost_h = 0.0f
	};

	ar_config_t prim_cfg = { 0 };
	prim_cfg.base_w = 256;
	prim_cfg.base_h = 256;
	prim_cfg.grid_dim = 8;
	prim_cfg.max_levels = 15;
	prim_cfg.period_x = WORLD_W;
	prim_cfg.period_y = WORLD_H;
	prim_cfg.pool_capacity = 2048;
	prim_cfg.max_active = 1024;
	prim_cfg.value_scale = 1.0f;
	prim_cfg.sampler = chaff_sampler;
	prim_cfg.user_ctx = &prim_tctx;

	ar_config_t moon_cfg = prim_cfg;
	moon_cfg.user_ctx = &moon_tctx;

	ar_system_t *prim_sys = ar_system_create_ex(&prim_cfg);
	ar_system_t *moon_sys = ar_system_create_ex(&moon_cfg);
	ASSERT_TRUE(prim_sys && moon_sys, "dual-body system creation failed");
	if (!prim_sys || !moon_sys) return;

	corolla_surface_t prim_surf = corolla_torus(PRIM_R, PRIM_r);
	corolla_surface_t moon_surf = corolla_torus(MOON_R, MOON_r);

	corolla_frame_t cam_frame;
	corolla_eval_frame(&prim_surf, u_cam, v_cam, 0.0, &cam_frame);

	double lamb_h = (double)eval_terrain_h(&prim_tctx, cam_wx, cam_wy) + 2.0;

	/* Eye 45 wu South (-tan_v) and slightly above the lamb, looking North and +16 deg up
	 * across the foreground mountain ridge into the sky where the companion moon hangs. */
	double eye[3] = {
		cam_frame.norm[0] * (lamb_h + 12.0) - cam_frame.tan_v[0] * 45.0,
		cam_frame.norm[1] * (lamb_h + 12.0) - cam_frame.tan_v[1] * 45.0,
		cam_frame.norm[2] * (lamb_h + 12.0) - cam_frame.tan_v[2] * 45.0
	};
	double look[3] = {
		eye[0] + cam_frame.tan_v[0] * 1000.0 + cam_frame.norm[0] * 287.0,
		eye[1] + cam_frame.tan_v[1] * 1000.0 + cam_frame.norm[1] * 287.0,
		eye[2] + cam_frame.tan_v[2] * 1000.0 + cam_frame.norm[2] * 287.0
	};

	const double znear = 2.0, zfar = 25000000.0;
	float mvp[16];
	build_lookat_persp_mvp(eye, look, cam_frame.norm,
	                       55.0 * M_PI / 180.0,
	                       (double)ORACLE_W / (double)ORACLE_H,
	                       znear, zfar, mvp);

	torus_cull_ctx_t prim_tcc = { 0 };
	prim_tcc.surf = prim_surf;
	prim_tcc.has_pose = false;
	prim_tcc.ref_origin[0] = cam_frame.pos[0];
	prim_tcc.ref_origin[1] = cam_frame.pos[1];
	prim_tcc.ref_origin[2] = cam_frame.pos[2];
	prim_tcc.world_w = WORLD_W;
	prim_tcc.world_h = WORLD_H;
	prim_tcc.z_scale = 1.0;

	/* Test Case C1: Companion moon partially peeking over the foreground mountain ridge!
	 * Moon center 16M wu North and +5.6M wu Up (~19.3 deg); moon spans +-1.5M (+-5.3 deg),
	 * so its lower half dips behind the foreground mountain ridge while its upper half rises clearly into view. */
	corolla_pose_t moon_pose_partial = { 0 };
	moon_pose_partial.rot[0] = 1.0; moon_pose_partial.rot[4] = 1.0; moon_pose_partial.rot[8] = 1.0;
	moon_pose_partial.inv_rot[0] = 1.0; moon_pose_partial.inv_rot[4] = 1.0; moon_pose_partial.inv_rot[8] = 1.0;
	for (int i = 0; i < 3; i++) {
		moon_pose_partial.center[i] = cam_frame.pos[i]
			+ cam_frame.tan_v[i] * 16000000.0
			+ cam_frame.norm[i]  *  6100000.0;
	}

	torus_cull_ctx_t moon_tcc = prim_tcc;
	moon_tcc.surf = moon_surf;
	moon_tcc.has_pose = true;
	moon_tcc.pose = moon_pose_partial;

	oracle_fb_t *fb = (oracle_fb_t *)malloc(sizeof(oracle_fb_t));
	ar_occluder_t *occ = (ar_occluder_t *)malloc(sizeof(ar_occluder_t));
	const ar_node_t **prim_nodes = (const ar_node_t **)malloc(1024 * sizeof(ar_node_t *));
	const ar_node_t **moon_nodes_no_occ = (const ar_node_t **)malloc(1024 * sizeof(ar_node_t *));
	const ar_node_t **moon_nodes_occ = (const ar_node_t **)malloc(1024 * sizeof(ar_node_t *));

	/* 1. Query primary planet into shared ar_occluder_t */
	ar_occluder_reset(occ, mvp, (float)znear, 0.002f);
	ar_cull_opts_t prim_opts = { 0 };
	prim_opts.z_min = -520.0f;
	prim_opts.z_max = 3000.0f;
	prim_opts.focal_x = (float)cam_wx;
	prim_opts.focal_y = (float)cam_wy;
	prim_opts.focal_z = (float)lamb_h;
	prim_opts.viewport_w = 1280.0f;
	prim_opts.viewport_h = 720.0f;
	prim_opts.target_quad_px = 20.0f;
	prim_opts.max_level = 14;
	prim_opts.bounds_3d_fn = torus_bounds_3d_cb;
	prim_opts.bounds_ctx = &prim_tcc;
	prim_opts.occluder = occ;
	prim_opts.point_3d_fn = torus_point_3d_cb;
	prim_opts.occluder_z_bias = 16.0f;
	prim_opts.occluder_texel_bias = 0.8f;
	prim_opts.occluder_concave_inv_r = (float)(1.0 / (PRIM_R - PRIM_r));
	prim_opts.occluder_box_z_pad = 16.0f;

	uint32_t prim_cnt = ar_system_query_matrix(prim_sys, 0, mvp, &prim_opts, prim_nodes, 1024);

	/* 2. Query companion moon WITHOUT vs WITH the primary-populated occluder */
	ar_cull_opts_t moon_opts = { 0 };
	moon_opts.z_min = -520.0f;
	moon_opts.z_max = 2000.0f;
	moon_opts.focal_x = 0.0f;
	moon_opts.focal_y = 0.0f;
	moon_opts.focal_z = 0.0f;
	moon_opts.viewport_w = 1280.0f;
	moon_opts.viewport_h = 720.0f;
	moon_opts.target_quad_px = 6.0f;
	moon_opts.max_level = 14;
	moon_opts.bounds_3d_fn = torus_bounds_3d_cb;
	moon_opts.bounds_ctx = &moon_tcc;

	uint32_t moon_cnt_no_occ = ar_system_query_matrix(moon_sys, 0, mvp, &moon_opts, moon_nodes_no_occ, 1024);

	moon_opts.occluder = occ;
	moon_opts.point_3d_fn = torus_point_3d_cb;
	moon_opts.occluder_z_bias = 200.0f;
	moon_opts.occluder_concave_inv_r = (float)(1.0 / (MOON_R - MOON_r));
	moon_opts.occluder_box_z_pad = 200.0f;
	uint32_t moon_cnt_occ = ar_system_query_matrix(moon_sys, 0, mvp, &moon_opts, moon_nodes_occ, 1024);

	/* 3. Software Z-buffer ground truth: render foreground primary terrain + entire companion moon */
	oracle_reset(fb);

	/* Rasterize primary terrain in a 6000x6000 window ahead of the lamb */
	const int pgrid = 48;
	const int32_t pstep = 160;
	for (int gy = -4; gy < pgrid; gy++) {
		int32_t wy0 = cam_wy + gy * pstep;
		int32_t wy1 = wy0 + pstep;
		for (int gx = -pgrid / 2; gx < pgrid / 2; gx++) {
			int32_t wx0 = cam_wx + gx * pstep;
			int32_t wx1 = wx0 + pstep;
			float p00[3], p10[3], p01[3], p11[3];
			torus_point_3d_cb(&prim_tcc, (float)wx0, (float)wy0, eval_terrain_h(&prim_tctx, wx0, wy0), p00);
			torus_point_3d_cb(&prim_tcc, (float)wx1, (float)wy0, eval_terrain_h(&prim_tctx, wx1, wy0), p10);
			torus_point_3d_cb(&prim_tcc, (float)wx0, (float)wy1, eval_terrain_h(&prim_tctx, wx0, wy1), p01);
			torus_point_3d_cb(&prim_tcc, (float)wx1, (float)wy1, eval_terrain_h(&prim_tctx, wx1, wy1), p11);
			/* Owner ID 0 = primary planet */
			oracle_raster_tri_3d(fb, mvp, p00, p10, p11, (float)znear, 0);
			oracle_raster_tri_3d(fb, mvp, p00, p11, p01, (float)znear, 0);
		}
	}

	/* Rasterize companion moon ground truth (16 x 16 patches) with owner IDs 1000 + pid */
	const int mn = 16;
	const int32_t m_pw = WORLD_W / mn;
	const int32_t m_ph = WORLD_H / mn;
	int *moon_pix = (int *)calloc((size_t)(mn * mn), sizeof(int));
	for (int py = 0; py < mn; py++) {
		int32_t wy0 = py * m_ph;
		int32_t wy1 = (py + 1) * m_ph;
		for (int px = 0; px < mn; px++) {
			int32_t wx0 = px * m_pw;
			int32_t wx1 = (px + 1) * m_pw;
			int pid = py * mn + px;
			float p00[3], p10[3], p01[3], p11[3];
			torus_point_3d_cb(&moon_tcc, (float)wx0, (float)wy0, eval_terrain_h(&moon_tctx, wx0, wy0), p00);
			torus_point_3d_cb(&moon_tcc, (float)wx1, (float)wy0, eval_terrain_h(&moon_tctx, wx1, wy0), p10);
			torus_point_3d_cb(&moon_tcc, (float)wx0, (float)wy1, eval_terrain_h(&moon_tctx, wx0, wy1), p01);
			torus_point_3d_cb(&moon_tcc, (float)wx1, (float)wy1, eval_terrain_h(&moon_tctx, wx1, wy1), p11);
			oracle_raster_tri_3d(fb, mvp, p00, p10, p11, (float)znear, 1000 + pid);
			oracle_raster_tri_3d(fb, mvp, p00, p11, p01, (float)znear, 1000 + pid);
		}
	}

	int prim_pixels = 0, moon_pixels = 0;
	for (int y = 2; y < ORACLE_H - 2; y++) {
		for (int x = 2; x < ORACLE_W - 2; x++) {
			int32_t id = fb->owner[y * ORACLE_W + x];
			if (id == 0) prim_pixels++;
			else if (id >= 1000 && id < 1000 + mn * mn) {
				moon_pixels++;
				moon_pix[id - 1000]++;
			}
		}
	}

	int moon_vis_patches = 0, moon_fn_no_occ = 0, moon_fn_occ = 0;
	for (int pid = 0; pid < mn * mn; pid++) {
		if (moon_pix[pid] < 2) continue;
		moon_vis_patches++;
		int px = pid % mn;
		int py = pid / mn;
		int32_t wx = px * m_pw;
		int32_t wy = py * m_ph;
		if (!patch_covered_by_nodes(moon_nodes_no_occ, moon_cnt_no_occ, wx, wy, m_pw, m_ph, WORLD_W, WORLD_H))
			moon_fn_no_occ++;
		if (!patch_covered_by_nodes(moon_nodes_occ, moon_cnt_occ, wx, wy, m_pw, m_ph, WORLD_W, WORLD_H))
			moon_fn_occ++;
	}

	printf("  -> C1 (partial moon above ridge): prim_nodes=%u, moon_nodes=%u->%u, prim_px=%d moon_px=%d, vis_moon_patches=%d fn_no_occ=%d fn_occ=%d\n",
	       prim_cnt, moon_cnt_no_occ, moon_cnt_occ, prim_pixels, moon_pixels,
	       moon_vis_patches, moon_fn_no_occ, moon_fn_occ);

	ASSERT_TRUE(prim_cnt > 0, "Primary planet close-up must emit nodes");
	ASSERT_TRUE(moon_pixels > 20 && moon_vis_patches > 0, "Moon top must be visible above foreground ridge");
	ASSERT_TRUE(moon_fn_no_occ == 0, "Zero false negatives on moon frustum culling (got %d)", moon_fn_no_occ);
	ASSERT_TRUE(moon_fn_occ == 0, "Zero false negatives on moon cross-body Hi-Z occlusion (got %d)", moon_fn_occ);
	ASSERT_TRUE(moon_cnt_occ < moon_cnt_no_occ,
	            "Primary mountain ridge + moon self-occlusion must cull hidden moon chunks (%u -> %u)",
	            moon_cnt_no_occ, moon_cnt_occ);

	/* Test Case C2: Moon moved completely BEHIND the primary mountain ridge (elev +1,800,000 -> ~6.4 deg,
	 * well below the ~24 deg foreground ridge). Must cull ALL moon chunks! */
	corolla_pose_t moon_pose_hidden = moon_pose_partial;
	for (int i = 0; i < 3; i++) {
		moon_pose_hidden.center[i] = cam_frame.pos[i]
			+ cam_frame.tan_v[i] * 16000000.0
			+ cam_frame.norm[i]  *  1800000.0;
	}
	moon_tcc.pose = moon_pose_hidden;

	/* Re-populate occluder with primary planet only, then test hidden moon */
	ar_occluder_reset(occ, mvp, (float)znear, 0.002f);
	(void)ar_system_query_matrix(prim_sys, 0, mvp, &prim_opts, prim_nodes, 1024);

	moon_opts.occluder = NULL;
	uint32_t hidden_no_occ = ar_system_query_matrix(moon_sys, 0, mvp, &moon_opts, moon_nodes_no_occ, 1024);

	moon_opts.occluder = occ;
	moon_opts.occluder_no_write = true;
	uint32_t hidden_occ = ar_system_query_matrix(moon_sys, 0, mvp, &moon_opts, moon_nodes_occ, 1024);

	printf("  -> C2 (moon behind foreground ridge): moon_nodes frustum=%u -> occ=%u\n",
	       hidden_no_occ, hidden_occ);
	ASSERT_TRUE(hidden_no_occ > 0, "Hidden moon should still be inside view frustum before occlusion");
	ASSERT_TRUE(hidden_occ == 0, "Moon completely behind foreground mountain ridge must be 100%% culled (got %u)", hidden_occ);

	free(moon_pix);
	free(moon_nodes_occ);
	free(moon_nodes_no_occ);
	free(prim_nodes);
	free(occ);
	free(fb);
	ar_system_destroy(moon_sys);
	ar_system_destroy(prim_sys);
}

int
main(void)
{
	printf("=== libareole + libchaff + libcorolla Occlusion Integration Suite ===\n");
	test_scenario_a_flat_alpine();
	test_scenario_b_torus_manifold();
	test_scenario_c_closeup_with_moon();

	if (g_failures > 0) {
		fprintf(stderr, "\nFAILED: %d assertion(s) failed.\n", g_failures);
		return 1;
	}
	printf("\nALL libareole + libchaff + libcorolla occlusion tests PASSED!\n");
	return 0;
}
