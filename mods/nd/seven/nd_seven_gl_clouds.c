/*
 * nd_seven_gl_clouds.c — Cloud Shell Frustum Culling, 16x16x24 Clustered Cloud Lighting, and Volumetric Cloud Pass.
 */

#include "nd_seven_gl_internal.h"
#include "nd_seven_ground.h"

#define CLOUD_PRECULL_SLOTS 256u
typedef struct {
	int32_t  ox, oy;
	int32_t  span_w, span_h;
	float    anim_time;
	float    cur_dist;
	uint32_t forget_seq;
	uint8_t  sea_wet;
	uint8_t  all_clear;
	uint8_t  valid;
} cloud_precull_slot_t;

static cloud_precull_slot_t s_cloud_precull[CLOUD_PRECULL_SLOTS];

bool nd_seven_gl_should_queue_cloud_chunk(int32_t ox, int32_t oy,
                                          int32_t span_w, int32_t span_h,
                                          const nd_planet_def_t *pdef)
{
	if (nd_seven_get_cloud_quality() == ND_CLOUD_QUALITY_OFF)
		return false;

	const nd_planet_def_t *eff_pdef = pdef ? pdef : &s_gl.cur_pdef;

	/* Gate 1: Cloud zoom ramp is 0.0 at camera distances <= 480.0f */
	if (s_gl.cur_dist <= 480.0f)
		return false;

	/* Gate 2: Cloud-shell 3D frustum & planetary horizon limb test at eff_shell_z (exact midpoint of [zBase, zTopProj]) */
	float eff_orbit_lift = fminf(16000.0f, fmaxf(0.0f, (s_gl.cur_dist - 1800.0f) * 0.065f));
	float target_shell_z = 1620.0f + eff_orbit_lift * 0.50f;
	float max_safe_z = s_gl.cur_lamb_light[2] + s_gl.cur_dist * 0.58f;
	float eff_shell_z = fminf(target_shell_z, fmaxf(24.0f, max_safe_z));

	static const float uv5[5][2] = {
		{ 0.0f, 0.0f },
		{ 1.0f, 0.0f },
		{ 0.0f, 1.0f },
		{ 1.0f, 1.0f },
		{ 0.5f, 0.5f }
	};
	static const float uv9[9][2] = {
		{ 0.0f, 0.0f }, { 0.5f, 0.0f }, { 1.0f, 0.0f },
		{ 0.0f, 0.5f }, { 0.5f, 0.5f }, { 1.0f, 0.5f },
		{ 0.0f, 1.0f }, { 0.5f, 1.0f }, { 1.0f, 1.0f }
	};
	bool is_macro_span = (span_w >= 1048576 || span_h >= 1048576);
	const float (*uv_pts)[2] = is_macro_span ? uv9 : uv5;
	int uv_cnt = is_macro_span ? 9 : 5;

	float bmin[3] = {  1e30f,  1e30f,  1e30f };
	float bmax[3] = { -1e30f, -1e30f, -1e30f };
	float max_vn = -1.0f;

	for (int i = 0; i < uv_cnt; i++) {
		double wx = (double)ox + (double)uv_pts[i][0] * (double)span_w;
		double wy = (double)oy + (double)uv_pts[i][1] * (double)span_h;
		double pos_d[3];
		float N[3];
		torus_frame_double(wx, wy, (double)eff_shell_z, eff_pdef, pos_d, NULL, NULL, N);
		float rel_p[3] = {
			(float)(pos_d[0] - s_gl.cur_origin[0]),
			(float)(pos_d[1] - s_gl.cur_origin[1]),
			(float)(pos_d[2] - s_gl.cur_origin[2])
		};
		for (int k = 0; k < 3; k++) {
			if (rel_p[k] < bmin[k]) bmin[k] = rel_p[k];
			if (rel_p[k] > bmax[k]) bmax[k] = rel_p[k];
		}
		float vx = s_gl.cur_eye[0] - rel_p[0];
		float vy = s_gl.cur_eye[1] - rel_p[1];
		float vz = s_gl.cur_eye[2] - rel_p[2];
		float vlen = sqrtf(vx * vx + vy * vy + vz * vz);
		if (vlen > 1e-4f) {
			float vn = (vx * N[0] + vy * N[1] + vz * N[2]) / vlen;
			if (vn > max_vn) max_vn = vn;
		} else {
			max_vn = 1.0f;
		}
	}

	float du = (is_macro_span ? 0.25f : 0.5f) * TWO_PI_F * ((float)span_w / (float)ND_SEVEN_WORLD_DIM_W);
	float dv = (is_macro_span ? 0.25f : 0.5f) * TWO_PI_F * ((float)span_h / (float)ND_SEVEN_WORLD_DIM_H);
	float alpha = sqrtf(du * du + dv * dv);
	float sin_alpha = (alpha < 1.57f) ? sinf(alpha) : 1.0f;
	/* s_fs_cloud discards when dot(torusN, viewDir) <= 0.02 */
	if (max_vn + sin_alpha <= 0.015f)
		return false;

	float R = (eff_pdef && eff_pdef->major_r > 0.0f) ? eff_pdef->major_r : 2000000.0f;
	float r0 = (eff_pdef && eff_pdef->minor_r > 0.0f) ? eff_pdef->minor_r : 1335000.0f;
	float bulge = (R + r0) * 0.125f * (du * du + dv * dv) + 450.0f + eff_orbit_lift;
	for (int k = 0; k < 3; k++) {
		bmin[k] -= bulge;
		bmax[k] += bulge;
	}
	if (!ar_frustum_test_box(&s_gl.cur_frustum, bmin[0], bmin[1], bmin[2], bmax[0], bmax[1], bmax[2]))
		return false;

	/* Gate 3: CPU Synoptic Clear-Sky Pre-Cull for regional/local chunks up to 524,288 wu
	 * (cached 256-slot direct-mapped with 0.25s anim_time TTL) */
	if (span_w <= 524288 && span_h <= 524288) {
		uint8_t sea_wet_u8 = (s_gl.cur_sea_level > -900.0f) ? 1u : 0u;
		uint32_t cur_fseq = nd_seven_ground_forget_seq();
		uint32_t h = (uint32_t)ox * 0x9E3779B1u ^ (uint32_t)oy * 0x85EBCA77u
		           ^ (uint32_t)span_w * 0xC2B2AE3Du ^ (uint32_t)span_h * 0x27D4EB2Fu;
		cloud_precull_slot_t *cs = &s_cloud_precull[(h ^ (h >> 16u)) & (CLOUD_PRECULL_SLOTS - 1u)];
		if (cs->valid &&
		    cs->ox == ox && cs->oy == oy &&
		    cs->span_w == span_w && cs->span_h == span_h &&
		    cs->sea_wet == sea_wet_u8 &&
		    cs->forget_seq == cur_fseq &&
		    fabsf(s_gl.cur_anim_time - cs->anim_time) <= 0.25f &&
		    fabsf(s_gl.cur_dist - cs->cur_dist) <= fmaxf(120.0f, cs->cur_dist * 0.15f)) {
			if (cs->all_clear)
				return false;
			return true;
		}
		float sea_wet = sea_wet_u8 ? 1.0f : 0.0f;
		float fw_est = fmaxf(0.18f, s_gl.cur_dist * 0.0014f);
		float pad = fminf(4800.0f, fmaxf(1200.0f, s_gl.cur_dist * 0.25f));
		float x0 = (float)ox - pad;
		float y0 = (float)oy - pad;
		float w_pad = (float)span_w + 2.0f * pad;
		float h_pad = (float)span_h + 2.0f * pad;
		int steps = (span_w > 65536 || span_h > 65536) ? 5 : ((span_w > 8192 || span_h > 8192) ? 3 : 2);
		float inv_steps = 1.0f / (float)steps;
		float margin = (span_w > 65536 || span_h > 65536) ? 0.055f : 0.032f;
		bool all_clear = true;
		for (int gy = 0; gy <= steps && all_clear; gy++) {
			float wy = y0 + ((float)gy * inv_steps) * h_pad;
			for (int gx = 0; gx <= steps; gx++) {
				float wx = x0 + ((float)gx * inv_steps) * w_pad;
				if (!gl_eval_synoptic_is_clear_sky_cpu(wx, wy, s_gl.cur_anim_time, sea_wet, fw_est, true, margin)) {
					all_clear = false;
					break;
				}
			}
		}
		cs->ox = ox;
		cs->oy = oy;
		cs->span_w = span_w;
		cs->span_h = span_h;
		cs->anim_time = s_gl.cur_anim_time;
		cs->cur_dist = s_gl.cur_dist;
		cs->forget_seq = cur_fseq;
		cs->sea_wet = sea_wet_u8;
		cs->all_clear = all_clear ? 1u : 0u;
		cs->valid = 1u;
		if (all_clear)
			return false;
	}

	return true;
}

void nd_seven_gl_clear_local_lights(void)
{
	s_gl.local_light_count = 0;
}

int nd_seven_gl_add_point_light(float wx, float wy, float wz,
                                float r, float g, float b,
                                float intensity, float range)
{
	if (s_gl.local_light_count >= ND_MAX_TOTAL_LIGHTS || intensity <= 0.001f || range <= 1.0f)
		return -1;
	uint32_t idx = s_gl.local_light_count++;
	nd_cluster_light_t *L = &s_gl.local_lights[idx];
	memset(L, 0, sizeof(*L));
	L->pos_x = wx;
	L->pos_y = wy;
	L->pos_z = wz;
	L->pos_w = 1.0f;
	L->color_r = r;
	L->color_g = g;
	L->color_b = b;
	L->intensity = intensity;
	L->range = range;
	L->type = (float)ND_LIGHT_TYPE_POINT;
	return (int)idx;
}

int nd_seven_gl_add_spot_light(float wx, float wy, float wz,
                               float dx, float dy, float dz,
                               float r, float g, float b,
                               float intensity, float range,
                               float cutoff_deg, float exponent)
{
	if (s_gl.local_light_count >= ND_MAX_TOTAL_LIGHTS || intensity <= 0.001f || range <= 1.0f)
		return -1;
	uint32_t idx = s_gl.local_light_count++;
	nd_cluster_light_t *L = &s_gl.local_lights[idx];
	memset(L, 0, sizeof(*L));
	L->pos_x = wx;
	L->pos_y = wy;
	L->pos_z = wz;
	L->pos_w = 1.0f;
	float dlen = sqrtf(dx * dx + dy * dy + dz * dz);
	if (dlen > 1e-4f) {
		L->dir_x = dx / dlen;
		L->dir_y = dy / dlen;
		L->dir_z = dz / dlen;
	} else {
		L->dir_z = 1.0f;
	}
	L->color_r = r;
	L->color_g = g;
	L->color_b = b;
	L->intensity = intensity;
	L->range = range;
	L->spot_cos_cutoff = cosf(cutoff_deg * (PI_F / 180.0f));
	L->spot_exponent = exponent;
	L->type = (float)ND_LIGHT_TYPE_SPOT;
	return (int)idx;
}

uint32_t nd_seven_gl_get_active_light_count(void)
{
	return s_gl.active_light_count;
}

uint32_t nd_seven_gl_get_active_cluster_count(void)
{
	return s_gl.active_cluster_count;
}

static inline uint8_t gl_quantize_u8(float v)
{
	if (v <= 0.0f) return 0u;
	if (v >= 1.0f) return 255u;
	return (uint8_t)(v * 255.0f + 0.5f);
}

static void gl_update_clustered_cloud_lighting(float out_depth_params[4])
{
	float near_d = (s_gl.cur_near > 16.0f) ? s_gl.cur_near : 16.0f;
	float far_d  = (s_gl.cur_dist * 4.5f > near_d * 4.0f) ? (s_gl.cur_dist * 4.5f) : (near_d * 4.0f);
	if (far_d > 32000.0f) far_d = 32000.0f;
	float log_ratio = logf(far_d / near_d);
	if (log_ratio < 1e-3f) log_ratio = 1.0f;
	float log_scale = (float)ND_CLUSTER_GRID_Z / log_ratio;
	float log_bias  = -(float)ND_CLUSTER_GRID_Z * logf(near_d) / log_ratio;

	out_depth_params[0] = near_d;
	out_depth_params[1] = far_d;
	out_depth_params[2] = log_scale;
	out_depth_params[3] = log_bias;

	if (s_gl.cur_dist >= 6000.0f && s_gl.cluster_ssbo_valid) {
		s_gl.active_light_count = 2;
		s_gl.active_cluster_count = ND_CLUSTER_TOTAL;
		return;
	}

	uint32_t count = 0;

	/* Light 0: Primary directional Sun */
	{
		nd_cluster_light_t *sun = &s_gl.active_lights[count++];
		memset(sun, 0, sizeof(*sun));
		sun->dir_x = s_gl.cur_sun_dir[0];
		sun->dir_y = s_gl.cur_sun_dir[1];
		sun->dir_z = s_gl.cur_sun_dir[2];
		sun->color_r = 1.0f;
		sun->color_g = 0.96f;
		sun->color_b = 0.88f;
		sun->intensity = 1.0f;
		sun->type = (float)ND_LIGHT_TYPE_DIRECTIONAL;
	}

	/* Light 1: Secondary directional Moon */
	{
		nd_cluster_light_t *moon = &s_gl.active_lights[count++];
		memset(moon, 0, sizeof(*moon));
		moon->dir_x = s_gl.cur_moon_dir[0];
		moon->dir_y = s_gl.cur_moon_dir[1];
		moon->dir_z = s_gl.cur_moon_dir[2];
		moon->color_r = 0.52f;
		moon->color_g = 0.68f;
		moon->color_b = 0.96f;
		moon->intensity = 0.42f;
		moon->type = (float)ND_LIGHT_TYPE_DIRECTIONAL;
	}

	/* Auto-populate universe-owner glowing Lamb point light if active & within visible range */
	if (s_gl.cur_lamb_light[3] > 0.01f && s_gl.cur_dist < 6000.0f && count < ND_MAX_TOTAL_LIGHTS) {
		nd_cluster_light_t *lamb = &s_gl.active_lights[count++];
		memset(lamb, 0, sizeof(*lamb));
		lamb->pos_x = s_gl.cur_lamb_light[0] - s_gl.cur_tx;
		lamb->pos_y = s_gl.cur_lamb_light[1] - s_gl.cur_ty;
		lamb->pos_z = s_gl.cur_lamb_light[2] + 380.0f; /* Project upward into low cloud base */
		lamb->pos_w = 1.0f;
		lamb->color_r = 1.0f;
		lamb->color_g = 0.84f;
		lamb->color_b = 0.46f;
		lamb->intensity = fminf(4.0f, s_gl.cur_lamb_light[3] * 1.45f);
		lamb->range = 1600.0f;
		lamb->type = (float)ND_LIGHT_TYPE_POINT;
	}

	/* Append user/entity local point & spot lights (converted to camera-relative XY) */
	if (s_gl.cur_dist < 6000.0f) {
		for (uint32_t i = 0; i < s_gl.local_light_count && count < ND_MAX_TOTAL_LIGHTS; i++) {
			nd_cluster_light_t L = s_gl.local_lights[i];
			if ((int)(L.type + 0.5f) != ND_LIGHT_TYPE_DIRECTIONAL) {
				L.pos_x -= s_gl.cur_tx;
				L.pos_y -= s_gl.cur_ty;
			}
			if (fabsf(L.pos_x) > 32760.0f || fabsf(L.pos_y) > 32760.0f)
				continue;
			s_gl.active_lights[count++] = L;
		}
	}

	s_gl.active_light_count = count;

	/* Pack active lights into 4 x ND_MAX_TOTAL_LIGHTS RGBA8 texture data */
	memset(s_gl.cluster_light_tex_data, 0, count * 16);
	for (uint32_t i = 0; i < count; i++) {
		const nd_cluster_light_t *L = &s_gl.active_lights[i];
		uint8_t *dst = &s_gl.cluster_light_tex_data[i * 16];
		/* Texel 0: encoded camera-relative position */
		dst[0] = gl_quantize_u8(L->pos_x / 65536.0f + 0.5f);
		dst[1] = gl_quantize_u8(L->pos_y / 65536.0f + 0.5f);
		dst[2] = gl_quantize_u8(L->pos_z / 8192.0f + 0.1f);
		dst[3] = gl_quantize_u8(L->pos_w);
		/* Texel 1: encoded unit direction */
		dst[4] = gl_quantize_u8(L->dir_x * 0.5f + 0.5f);
		dst[5] = gl_quantize_u8(L->dir_y * 0.5f + 0.5f);
		dst[6] = gl_quantize_u8(L->dir_z * 0.5f + 0.5f);
		dst[7] = 0u;
		/* Texel 2: color.rgb and intensity/4.0 */
		dst[8]  = gl_quantize_u8(L->color_r);
		dst[9]  = gl_quantize_u8(L->color_g);
		dst[10] = gl_quantize_u8(L->color_b);
		dst[11] = gl_quantize_u8(L->intensity * 0.25f);
		/* Texel 3: range/8192, spot_cos_cutoff, spot_exponent/16, type */
		dst[12] = gl_quantize_u8(L->range / 8192.0f);
		dst[13] = gl_quantize_u8(L->spot_cos_cutoff);
		dst[14] = gl_quantize_u8(L->spot_exponent / 16.0f);
		dst[15] = (uint8_t)((int)(L->type + 0.5f));
	}

	const uint32_t stride = ND_MAX_LIGHTS_PER_CLUSTER + 1;
	static uint32_t s_prev_cl_hash = 0;
	static uint32_t s_prev_cl_count = 0;
	uint32_t cl_hash = count * 2654435761u;
	cl_hash ^= (uint32_t)(int32_t)(s_gl.cur_tx * 0.25f) * 2246822519u;
	cl_hash ^= (uint32_t)(int32_t)(s_gl.cur_ty * 0.25f) * 3266489917u;
	cl_hash ^= (uint32_t)(int32_t)(s_gl.cur_dist * 0.25f) * 668265263u;
	cl_hash ^= (uint32_t)(int32_t)(s_gl.cur_yaw_deg * 2.0f) * 374761393u;
	cl_hash ^= (uint32_t)(int32_t)(s_gl.cur_pitch_deg * 2.0f) * 1103515245u;

	bool need_list_upload = true;
	if (count == 2 && s_gl.cluster_ssbo_valid && s_gl.cluster_dirs_only_cached) {
		need_list_upload = false;
		s_gl.active_cluster_count = ND_CLUSTER_TOTAL;
	} else if (s_gl.cluster_ssbo_valid && count == s_prev_cl_count && cl_hash == s_prev_cl_hash) {
		need_list_upload = false;
		s_gl.active_cluster_count = ND_CLUSTER_TOTAL;
	} else {
		s_prev_cl_hash = cl_hash;
		s_prev_cl_count = count;
		/* Fast initialize all 6,144 clusters with the 2 primary directional lights (Sun=0, Moon=1) */
		for (uint32_t c = 0; c < ND_CLUSTER_TOTAL; c++) {
			uint8_t *entry = &s_gl.cluster_list_tex_data[c * stride];
			entry[0] = 2u;
			entry[1] = 0u;
			entry[2] = 1u;
		}

		/* Assign remaining active lights (li >= 2) to 16x16x24 clusters */
		const float *mvp = s_gl.cur_mvp;
		for (uint32_t li = 2; li < count; li++) {
			const nd_cluster_light_t *L = &s_gl.active_lights[li];
			int ltype = (int)(L->type + 0.5f);
			if (ltype == ND_LIGHT_TYPE_DIRECTIONAL) {
				/* Additional directional lights illuminate all clusters */
				for (uint32_t c = 0; c < ND_CLUSTER_TOTAL; c++) {
					uint8_t *entry = &s_gl.cluster_list_tex_data[c * stride];
					uint8_t n = entry[0];
					if (n < ND_MAX_LIGHTS_PER_CLUSTER) {
						entry[1 + n] = (uint8_t)li;
						entry[0] = n + 1;
					}
				}
				continue;
			}

			/* Transform point/spot light from tangent-relative coordinates to 3D torus frame relative to cur_origin */
			double lpos_d[3];
			torus_frame_double((double)(s_gl.cur_tx + L->pos_x),
			                   (double)(s_gl.cur_ty + L->pos_y),
			                   (double)L->pos_z,
			                   &s_gl.cur_pdef, lpos_d, NULL, NULL, NULL);
			float rx = (float)(lpos_d[0] - s_gl.cur_origin[0]);
			float ry = (float)(lpos_d[1] - s_gl.cur_origin[1]);
			float rz = (float)(lpos_d[2] - s_gl.cur_origin[2]);

			float dx = rx - s_gl.cur_eye[0];
			float dy = ry - s_gl.cur_eye[1];
			float dz = rz - s_gl.cur_eye[2];
			float dist = sqrtf(dx * dx + dy * dy + dz * dz);
			float rad = L->range;

			if (dist - rad > far_d || dist + rad < near_d)
				continue;

			float d_min = fmaxf(near_d, dist - rad);
			float d_max = fminf(far_d, dist + rad);
			int z0 = (int)floorf(logf(d_min) * log_scale + log_bias);
			int z1 = (int)floorf(logf(d_max) * log_scale + log_bias);
			if (z0 < 0) z0 = 0;
			if (z1 >= ND_CLUSTER_GRID_Z) z1 = ND_CLUSTER_GRID_Z - 1;

			/* Project bounding sphere to screen-space tiles [x0..x1, y0..y1] */
			int x0 = 0, x1 = ND_CLUSTER_GRID_X - 1;
			int y0 = 0, y1 = ND_CLUSTER_GRID_Y - 1;
			float clip_x = mvp[0] * rx + mvp[4] * ry + mvp[8]  * rz + mvp[12];
			float clip_y = mvp[1] * rx + mvp[5] * ry + mvp[9]  * rz + mvp[13];
			float clip_w = mvp[3] * rx + mvp[7] * ry + mvp[11] * rz + mvp[15];
			if (clip_w > rad * 0.5f && s_gl.cur_tan_half > 1e-4f) {
				float ndc_x = clip_x / clip_w;
				float ndc_y = clip_y / clip_w;
				float r_ndc_y = (rad / fmaxf(1.0f, clip_w - rad)) / s_gl.cur_tan_half;
				float r_ndc_x = r_ndc_y / fmaxf(0.1f, s_gl.cur_aspect);
				float u_min = (ndc_x - r_ndc_x) * 0.5f + 0.5f;
				float u_max = (ndc_x + r_ndc_x) * 0.5f + 0.5f;
				float v_min = (ndc_y - r_ndc_y) * 0.5f + 0.5f;
				float v_max = (ndc_y + r_ndc_y) * 0.5f + 0.5f;
				if (u_max < 0.0f || u_min > 1.0f || v_max < 0.0f || v_min > 1.0f)
					continue;
				x0 = (int)floorf(u_min * (float)ND_CLUSTER_GRID_X);
				x1 = (int)floorf(u_max * (float)ND_CLUSTER_GRID_X);
				y0 = (int)floorf(v_min * (float)ND_CLUSTER_GRID_Y);
				y1 = (int)floorf(v_max * (float)ND_CLUSTER_GRID_Y);
				if (x0 < 0) x0 = 0;
				if (x1 >= ND_CLUSTER_GRID_X) x1 = ND_CLUSTER_GRID_X - 1;
				if (y0 < 0) y0 = 0;
				if (y1 >= ND_CLUSTER_GRID_Y) y1 = ND_CLUSTER_GRID_Y - 1;
			}

			for (int cz = z0; cz <= z1; cz++) {
				for (int cy = y0; cy <= y1; cy++) {
					for (int cx = x0; cx <= x1; cx++) {
						uint32_t c_idx = (uint32_t)(cx + ND_CLUSTER_GRID_X * (cy + ND_CLUSTER_GRID_Y * cz));
						uint8_t *entry = &s_gl.cluster_list_tex_data[c_idx * stride];
						uint8_t n = entry[0];
						if (n < ND_MAX_LIGHTS_PER_CLUSTER) {
							entry[1 + n] = (uint8_t)li;
							entry[0] = n + 1;
						}
					}
				}
			}
		}

		s_gl.active_cluster_count = ND_CLUSTER_TOTAL;
		s_gl.cluster_dirs_only_cached = (count == 2);
	}

	glActiveTexture(GL_TEXTURE0);
	bool init_storage = false;
	if (!s_gl.cluster_ssbo_valid) {
		s_gl.cluster_light_tex = glCreateTexture();
		glBindTexture(GL_TEXTURE_2D, s_gl.cluster_light_tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		s_gl.cluster_list_tex = glCreateTexture();
		glBindTexture(GL_TEXTURE_2D, s_gl.cluster_list_tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		s_gl.cluster_ssbo_valid = 1;
		need_list_upload = true;
		init_storage = true;
	}

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glBindTexture(GL_TEXTURE_2D, s_gl.cluster_light_tex);
	if (init_storage) {
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, ND_MAX_TOTAL_LIGHTS, 0,
		             GL_RGBA, GL_UNSIGNED_BYTE, s_gl.cluster_light_tex_data);
	} else if (count > 0) {
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, (GLsizei)count,
		                GL_RGBA, GL_UNSIGNED_BYTE, s_gl.cluster_light_tex_data);
	}

	if (need_list_upload) {
		glBindTexture(GL_TEXTURE_2D, s_gl.cluster_list_tex);
		if (init_storage) {
			glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, (GLsizei)stride, ND_CLUSTER_TOTAL, 0,
			             GL_RED, GL_UNSIGNED_BYTE, s_gl.cluster_list_tex_data);
		} else {
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)stride, ND_CLUSTER_TOTAL,
			                GL_RED, GL_UNSIGNED_BYTE, s_gl.cluster_list_tex_data);
		}
	}
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glBindTexture(GL_TEXTURE_2D, 0);
}

void nd_seven_gl_flush_clouds(void)
{
	int cq = nd_seven_get_cloud_quality();
	bool use_vol = (s_gl.cloud_vol_pipe.prog != 0 && cq != ND_CLOUD_QUALITY_OFF);
	bool has_legacy = (s_gl.cloud_count > 0 && s_gl.cur_dist > 480.0f &&
	                   (s_gl.cloud_pipes[ND_CLOUD_VAR_FULL].prog ||
	                    s_gl.cloud_pipes[ND_CLOUD_VAR_WIDE].prog ||
	                    s_gl.cloud_pipes[ND_CLOUD_VAR_WIDE_CSM].prog));

	if (!s_gl.initialized || !s_gl.pipelines_ready || (!use_vol && !has_legacy)) {
		s_gl.cloud_count = 0;
		s_gl.cloud_hist_valid = false;
		return;
	}

	gl_end_terrain_pass();
	float cluster_depth_params[4] = { 16.0f, 32000.0f, 1.0f, 0.0f };
	gl_update_clustered_cloud_lighting(cluster_depth_params);

	bool half_res = (s_gl.scene_ok && s_gl.cloud_fbo != 0 && s_gl.cloud_comp_pipe.prog != 0);
	uint32_t rw = (s_gl.render_w > 0 && s_gl.render_w <= s_gl.scene_w) ? s_gl.render_w : s_gl.scene_w;
	uint32_t rh = (s_gl.render_h > 0 && s_gl.render_h <= s_gl.scene_h) ? s_gl.render_h : s_gl.scene_h;
	uint32_t crw = (s_gl.cloud_rw > 0 && s_gl.cloud_rw <= s_gl.cloud_w) ? s_gl.cloud_rw : s_gl.cloud_w;
	uint32_t crh = (s_gl.cloud_rh > 0 && s_gl.cloud_rh <= s_gl.cloud_h) ? s_gl.cloud_rh : s_gl.cloud_h;
	float suvx = (s_gl.scene_w > 0u) ? ((float)rw / (float)s_gl.scene_w) : 1.0f;
	float suvy = (s_gl.scene_h > 0u) ? ((float)rh / (float)s_gl.scene_h) : 1.0f;

	/* Volumetric clouds render directly to scaled cloud_fbo (zero 2x2 temporal flicker/zoom wipes);
	 * keep 2x2 temporal checkerboard only for the legacy multi-mesh fallback path. */
	bool use_temporal = (!use_vol && half_res && s_gl.cloud_temporal_ok &&
	                     s_gl.cloud_reproj_pipe.prog != 0 &&
	                     crw >= 4u && crh >= 4u);
	uint32_t qrw = use_temporal ? (crw >> 1) : crw;
	uint32_t qrh = use_temporal ? (crh >> 1) : crh;
	uint32_t trw = use_temporal ? (qrw << 1) : crw;
	uint32_t trh = use_temporal ? (qrh << 1) : crh;
	int sub_px = 0, sub_py = 0;
	float jx = 0.0f, jy = 0.0f;
	if (use_temporal)
		nd_seven_gl_cloud_subpixel(s_gl.cloud_phase, qrw, qrh, &sub_px, &sub_py, &jx, &jy);

	glTimerBegin(ND_GPU_PASS_CLOUDS);

	if (use_vol) {
		/* Phase 2: Single depth-aware fullscreen 3D volumetric raymarch pass.
		 * Reads resolved scene depth directly (zero cloud depth FBO blits!), works from
		 * ground close-up looking up through clouds all the way to full 3D torus orbit. */
		if (s_gl.scene_ok)
			gl_resolve_scene(GL_DEPTH_BUFFER_BIT);

		if (half_res) {
			glBindFramebuffer(GL_FRAMEBUFFER, s_gl.cloud_fbo);
			glViewport(0, 0, (GLsizei)trw, (GLsizei)trh);
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			glDisable(GL_BLEND);
		} else {
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
			glEnable(GL_BLEND);
			glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		}

		glDisable(GL_STENCIL_TEST);
		glDisable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);
		glDisable(GL_CULL_FACE);

		const gl_cloud_vol_pipe_t *cvp = &s_gl.cloud_vol_pipe;
		glUseProgram(cvp->prog);

		const gl_cloud_wth_ring_t *r0 = &s_gl.cloud_wth_rings[0];
		const gl_cloud_wth_ring_t *r1 = &s_gl.cloud_wth_rings[1];
		glActiveTexture(GL_TEXTURE0 + 5);
		glBindTexture(GL_TEXTURE_2D, s_gl.res_depth_tex);
		glActiveTexture(GL_TEXTURE0 + 7);
		glBindTexture(GL_TEXTURE_2D, r0->valid ? r0->tex : 0);
		glActiveTexture(GL_TEXTURE0 + 8);
		glBindTexture(GL_TEXTURE_2D, r1->valid ? r1->tex : 0);
		glActiveTexture(GL_TEXTURE0 + 13);
		glBindTexture(GL_TEXTURE_2D, s_gl.noise2d_tex);
		glActiveTexture(GL_TEXTURE0 + 15);
		glBindTexture(GL_TEXTURE_3D, s_gl.cloud_shape3d_tex);
		glActiveTexture(GL_TEXTURE0);

		float eye_local[3] = {
			s_gl.cur_eye[0] * s_gl.cur_E[0] + s_gl.cur_eye[1] * s_gl.cur_E[1] + s_gl.cur_eye[2] * s_gl.cur_E[2],
			s_gl.cur_eye[0] * s_gl.cur_S[0] + s_gl.cur_eye[1] * s_gl.cur_S[1] + s_gl.cur_eye[2] * s_gl.cur_S[2],
			s_gl.cur_eye[0] * s_gl.cur_N[0] + s_gl.cur_eye[1] * s_gl.cur_N[1] + s_gl.cur_eye[2] * s_gl.cur_N[2]
		};

		float major_r = (s_gl.cur_pdef.major_r > 1.0f) ? s_gl.cur_pdef.major_r : 2000000.0f;
		float minor_r = (s_gl.cur_pdef.minor_r > 1.0f) ? s_gl.cur_pdef.minor_r : 1335000.0f;
		float cv0 = s_gl.cur_ref_trig[2];
		float kappa_e = cv0 / fmaxf(1.0f, major_r + minor_r * cv0);
		float kappa_s = 1.0f / fmaxf(1.0f, minor_r);
		float torus_sx = (TWO_PI_F * fmaxf(1.0f, major_r + minor_r * cv0)) / (float)ND_SEVEN_WORLD_DIM_W;
		float torus_sy = (TWO_PI_F * fmaxf(1.0f, minor_r)) / (float)ND_SEVEN_WORLD_DIM_H;
		float inv_torus_sx = 1.0f / fmaxf(0.05f, torus_sx);
		float inv_torus_sy = 1.0f / fmaxf(0.05f, torus_sy);

		/* Evaluate exact synoptic wind direction once per frame (mirrors chaff_eval_synoptic_flow_2d) */
		float t_flow_raw = s_gl.cur_anim_time * 0.005f;
		float t_flow = t_flow_raw - 1024.0f * floorf(t_flow_raw / 1024.0f);
		float psi0_v = 0.0f, psi0_gx = 0.0f, psi0_gy = 0.0f;
		gl_chaff_grad_noise2d(s_gl.cur_tx * 0.00000042f - 0.741f * (t_flow * 0.40f),
		                      s_gl.cur_ty * 0.00000042f + 0.671f * (t_flow * 0.40f),
		                      0x4359434Cu, &psi0_v, &psi0_gx, &psi0_gy);
		float psi1_v = 0.0f, psi1_gx = 0.0f, psi1_gy = 0.0f;
		gl_chaff_grad_noise2d(s_gl.cur_tx * 0.00000180f - psi0_gy * 0.30f - 0.741f * t_flow,
		                      s_gl.cur_ty * 0.00000180f + psi0_gx * 0.30f + 0.671f * t_flow,
		                      0x5354524Du, &psi1_v, &psi1_gx, &psi1_gy);
		float wd_x =  0.741f - psi0_gy * 0.48f - psi1_gy * 0.28f;
		float wd_y = -0.671f + psi0_gx * 0.48f + psi1_gx * 0.28f;
		float wd_inv = 1.0f / sqrtf(wd_x * wd_x + wd_y * wd_y + 1e-6f);
		wd_x *= wd_inv;
		wd_y *= wd_inv;

		bool has_rot = (s_gl.cur_pdef.rot[0] != 0.0f || s_gl.cur_pdef.rot[4] != 0.0f || s_gl.cur_pdef.rot[8] != 0.0f);
		float body_rot[16] = {
			has_rot ? s_gl.cur_pdef.rot[0] : 1.0f, has_rot ? s_gl.cur_pdef.rot[3] : 0.0f, has_rot ? s_gl.cur_pdef.rot[6] : 0.0f, 0.0f,
			has_rot ? s_gl.cur_pdef.rot[1] : 0.0f, has_rot ? s_gl.cur_pdef.rot[4] : 1.0f, has_rot ? s_gl.cur_pdef.rot[7] : 0.0f, 0.0f,
			has_rot ? s_gl.cur_pdef.rot[2] : 0.0f, has_rot ? s_gl.cur_pdef.rot[5] : 0.0f, has_rot ? s_gl.cur_pdef.rot[8] : 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		};

		double dx = (s_gl.cur_origin[0] + (double)s_gl.cur_eye[0]) - s_gl.cur_pdef.center[0];
		double dy = (s_gl.cur_origin[1] + (double)s_gl.cur_eye[1]) - s_gl.cur_pdef.center[1];
		double dz = (s_gl.cur_origin[2] + (double)s_gl.cur_eye[2]) - s_gl.cur_pdef.center[2];
		float torus_eye[3];
		if (has_rot) {
			torus_eye[0] = (float)((double)s_gl.cur_pdef.rot[0] * dx + (double)s_gl.cur_pdef.rot[3] * dy + (double)s_gl.cur_pdef.rot[6] * dz);
			torus_eye[1] = (float)((double)s_gl.cur_pdef.rot[1] * dx + (double)s_gl.cur_pdef.rot[4] * dy + (double)s_gl.cur_pdef.rot[7] * dz);
			torus_eye[2] = (float)((double)s_gl.cur_pdef.rot[2] * dx + (double)s_gl.cur_pdef.rot[5] * dy + (double)s_gl.cur_pdef.rot[8] * dz);
		} else {
			torus_eye[0] = (float)dx;
			torus_eye[1] = (float)dy;
			torus_eye[2] = (float)dz;
		}

		float view_steps  = (cq <= ND_CLOUD_QUALITY_LOW) ? 18.0f : ((cq == ND_CLOUD_QUALITY_MEDIUM) ? 26.0f : 36.0f);
		float light_steps = (cq <= ND_CLOUD_QUALITY_LOW) ? 2.0f  : ((cq == ND_CLOUD_QUALITY_MEDIUM) ? 3.0f  : 4.0f);

		if (cvp->uCamFwd >= 0)        glUniform3f(cvp->uCamFwd, s_gl.cur_cam_fwd[0], s_gl.cur_cam_fwd[1], s_gl.cur_cam_fwd[2]);
		if (cvp->uCamRight >= 0)      glUniform3f(cvp->uCamRight, s_gl.cur_cam_right[0], s_gl.cur_cam_right[1], s_gl.cur_cam_right[2]);
		if (cvp->uCamUp >= 0)         glUniform3f(cvp->uCamUp, s_gl.cur_cam_up[0], s_gl.cur_cam_up[1], s_gl.cur_cam_up[2]);
		if (cvp->uTanHalfAspect >= 0) glUniform2f(cvp->uTanHalfAspect, s_gl.cur_tan_half, s_gl.cur_aspect);
		if (cvp->uNearFar >= 0)       glUniform2f(cvp->uNearFar, s_gl.cur_near, s_gl.cur_far);
		if (cvp->uCloudJitter >= 0)   glUniform2f(cvp->uCloudJitter, jx, jy);
		if (cvp->uEast >= 0)          glUniform3f(cvp->uEast, s_gl.cur_E[0], s_gl.cur_E[1], s_gl.cur_E[2]);
		if (cvp->uSouth >= 0)         glUniform3f(cvp->uSouth, s_gl.cur_S[0], s_gl.cur_S[1], s_gl.cur_S[2]);
		if (cvp->uZenith >= 0)        glUniform3f(cvp->uZenith, s_gl.cur_N[0], s_gl.cur_N[1], s_gl.cur_N[2]);
		if (cvp->uEyeLocal >= 0)      glUniform3f(cvp->uEyeLocal, eye_local[0], eye_local[1], eye_local[2]);
		if (cvp->uRefWorldXY >= 0)    glUniform2f(cvp->uRefWorldXY, s_gl.cur_tx, s_gl.cur_ty);
		if (cvp->uInvTorusScale >= 0) glUniform2f(cvp->uInvTorusScale, inv_torus_sx, inv_torus_sy);
		if (cvp->uCurvature >= 0)     glUniform2f(cvp->uCurvature, kappa_e, kappa_s);
		if (cvp->uTorusEye >= 0)      glUniform3f(cvp->uTorusEye, torus_eye[0], torus_eye[1], torus_eye[2]);
		if (cvp->uTorusRadii >= 0)    glUniform2f(cvp->uTorusRadii, major_r, minor_r);
		if (cvp->uBodyRot >= 0)       glUniformMatrix4fv(cvp->uBodyRot, 1, GL_FALSE, body_rot);
		if (cvp->uCloudParams >= 0)   glUniform4f(cvp->uCloudParams, 720.0f, 3100.0f, view_steps, light_steps);
		if (cvp->uWthNearWin >= 0)    glUniform3f(cvp->uWthNearWin, r0->cx, r0->cy, r0->valid ? r0->half : -1.0f);
		if (cvp->uWthFarWin >= 0)     glUniform3f(cvp->uWthFarWin,  r1->cx, r1->cy, r1->valid ? r1->half : -1.0f);
		if (cvp->uCloudWindDir >= 0)  glUniform2f(cvp->uCloudWindDir, wd_x, wd_y);
		if (cvp->uSceneUvScale >= 0)  glUniform2f(cvp->uSceneUvScale, suvx, suvy);
		if (cvp->uSeaLevel >= 0)      glUniform1f(cvp->uSeaLevel, s_gl.cur_base_sea_level);

		glDrawArrays(GL_TRIANGLES, 0, 3);

		glActiveTexture(GL_TEXTURE0 + 8);
		glBindTexture(GL_TEXTURE_2D, 0);
		glActiveTexture(GL_TEXTURE0 + 7);
		glBindTexture(GL_TEXTURE_2D, 0);
		glActiveTexture(GL_TEXTURE0 + 5);
		glBindTexture(GL_TEXTURE_2D, 0);
		glActiveTexture(GL_TEXTURE0);
	} else {
		if (half_res) {
			/* Resolve MSAA depth to res_fbo, then downsample depth to cloud_fbo (for bilateral
			 * upsampling & temporal reprojection) and cloud_q_fbo (when temporal 2x2 is active)
			 * so hardware GL_DEPTH_TEST (GL_LEQUAL) occludes far-side torus cloud shells. */
			gl_resolve_scene(GL_DEPTH_BUFFER_BIT);
			glBindFramebuffer(GL_READ_FRAMEBUFFER, s_gl.res_fbo);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_gl.cloud_fbo);
			glBlitFramebuffer(0, 0, (GLint)rw, (GLint)rh,
			                  0, 0, (GLint)trw, (GLint)trh,
			                  GL_DEPTH_BUFFER_BIT, GL_NEAREST);
			if (use_temporal) {
				glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_gl.cloud_q_fbo);
				glBlitFramebuffer(0, 0, (GLint)rw, (GLint)rh,
				                  0, 0, (GLint)qrw, (GLint)qrh,
				                  GL_DEPTH_BUFFER_BIT, GL_NEAREST);
				glBindFramebuffer(GL_FRAMEBUFFER, s_gl.cloud_q_fbo);
				glViewport(0, 0, (GLsizei)qrw, (GLsizei)qrh);
			} else {
				glBindFramebuffer(GL_FRAMEBUFFER, s_gl.cloud_fbo);
				glViewport(0, 0, (GLsizei)trw, (GLsizei)trh);
			}
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
			glStencilMask(0xFF);
			glClearStencil(1);
			glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
			glClearStencil(0);
			glEnable(GL_STENCIL_TEST);
			glStencilFunc(GL_LESS, 0, 0xFF);
			glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
		} else {
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
		}

		/* Keep hardware depth test (GL_LEQUAL) enabled so the solid front ring of the torus
		 * properly occludes cloud shells on the far side of the torus at orbital min-zoom! */
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LEQUAL);
		glDepthMask(GL_FALSE);
		glDisable(GL_CULL_FACE);
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

		/* Bind shared textures once before command-ring chunk dispatch (sampler units bound at link time) */
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D_ARRAY, s_gl.substrate_array);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D_ARRAY, s_gl.height_array);
		gl_bind_csm(-1);
		if (s_gl.cur_dist < 6000.0f) {
			glActiveTexture(GL_TEXTURE0 + 9);
			glBindTexture(GL_TEXTURE_2D, s_gl.cluster_light_tex);
			glActiveTexture(GL_TEXTURE0 + 10);
			glBindTexture(GL_TEXTURE_2D, s_gl.cluster_list_tex);
			glActiveTexture(GL_TEXTURE0);
		}

		uint32_t vp_w = half_res ? (use_temporal ? qrw : trw) : rw;
		uint32_t vp_h = half_res ? (use_temporal ? qrh : trh) : rh;
		float res_sc = (half_res && use_temporal) ? 0.65f : 1.0f;

		float max_steps = (cq <= ND_CLOUD_QUALITY_MEDIUM) ? 3.0f : 4.0f;
		float deck_base = 420.0f;
		float deck_span = 2400.0f;
		float eff_orbit_lift = fminf(16000.0f, fmaxf(0.0f, (s_gl.cur_dist - 1800.0f) * 0.065f));
		float target_shell_z = 1620.0f + eff_orbit_lift * 0.50f;
		float max_safe_z = s_gl.cur_lamb_light[2] + s_gl.cur_dist * 0.58f;
		float eff_shell_z = fminf(target_shell_z, fmaxf(24.0f, max_safe_z));

		bool is_wide = (s_gl.cur_dist >= 6000.0f || s_gl.active_light_count <= 2);
		bool can_csm_only = (is_wide && s_gl.cur_dist < 35000.0f && s_gl.cloud_pipes[ND_CLOUD_VAR_WIDE_CSM].prog != 0);
		float csm_sun_margin = fminf(24000.0f, fmaxf(90.0f, s_gl.cur_dist * 0.025f)) + 4800.0f;
		int cur_var = -1;
		bool var_inited[ND_CLOUD_VAR_COUNT] = { false, false, false };
		bool var_body_valid[ND_CLOUD_VAR_COUNT] = { false, false, false };
		float var_rel_center[ND_CLOUD_VAR_COUNT][3];
		float var_rot[ND_CLOUD_VAR_COUNT][16];
		float var_major_r[ND_CLOUD_VAR_COUNT];
		float var_minor_r[ND_CLOUD_VAR_COUNT];

		int bound_hi = -1;
		glEnableVertexAttribArray(0);

		for (uint32_t i = 0; i < s_gl.cloud_count; i++) {
			const gl_water_chunk_t *chk = &s_gl.cloud_queue[i];

			int want_var = ND_CLOUD_VAR_FULL;
			if (is_wide) {
				bool csm_safe = false;
				if (can_csm_only) {
					if (!s_gl.csm_valid) {
						csm_safe = s_gl.csm_clear_sky;
					} else {
						float rx = fmaxf(fabsf((float)chk->ox - s_gl.csm_win.cx),
						                 fabsf((float)(chk->ox + chk->span_w) - s_gl.csm_win.cx)) + csm_sun_margin;
						float ry = fmaxf(fabsf((float)chk->oy - s_gl.csm_win.cy),
						                 fabsf((float)(chk->oy + chk->span_h) - s_gl.csm_win.cy)) + csm_sun_margin;
						csm_safe = (rx <= 0.94f * s_gl.csm_win.half &&
						            ry <= 0.94f * s_gl.csm_win.half);
					}
				}
				if (csm_safe)
					want_var = ND_CLOUD_VAR_WIDE_CSM;
				else if (s_gl.cloud_pipes[ND_CLOUD_VAR_WIDE].prog)
					want_var = ND_CLOUD_VAR_WIDE;
			}
			if (!s_gl.cloud_pipes[want_var].prog) {
				if (s_gl.cloud_pipes[ND_CLOUD_VAR_WIDE].prog)
					want_var = ND_CLOUD_VAR_WIDE;
				else if (s_gl.cloud_pipes[ND_CLOUD_VAR_WIDE_CSM].prog)
					want_var = ND_CLOUD_VAR_WIDE_CSM;
				else if (s_gl.cloud_pipes[ND_CLOUD_VAR_FULL].prog)
					want_var = ND_CLOUD_VAR_FULL;
				else
					continue;
			}

			const gl_cloud_pipe_t *cp = &s_gl.cloud_pipes[want_var];
			if (want_var != cur_var) {
				lam_gl_cmd_use_program(&s_gl.cmd_buf, cp->prog);
				if (!var_inited[want_var]) {
					if (cp->uViewport >= 0)
						lam_gl_cmd_uniform2f(&s_gl.cmd_buf, cp->uViewport, (float)vp_w, (float)vp_h);
					if (cp->uCloudResScale >= 0)
						lam_gl_cmd_uniform1f(&s_gl.cmd_buf, cp->uCloudResScale, res_sc);
					if (cp->uCloudDeck >= 0)
						lam_gl_cmd_uniform4f(&s_gl.cmd_buf, cp->uCloudDeck, deck_base, deck_span, max_steps, eff_shell_z);
					if (cp->uSeaLevel >= 0)
						lam_gl_cmd_uniform1f(&s_gl.cmd_buf, cp->uSeaLevel, s_gl.cur_base_sea_level);
					if (cp->uCloudJitter >= 0)
						lam_gl_cmd_uniform2f(&s_gl.cmd_buf, cp->uCloudJitter, jx, jy);
					if (want_var == ND_CLOUD_VAR_FULL) {
						if (cp->uClusterGridDim >= 0)
							lam_gl_cmd_uniform3f(&s_gl.cmd_buf, cp->uClusterGridDim,
							                     (float)ND_CLUSTER_GRID_X, (float)ND_CLUSTER_GRID_Y, (float)ND_CLUSTER_GRID_Z);
						if (cp->uClusterDepthParams >= 0)
							lam_gl_cmd_uniform4f(&s_gl.cmd_buf, cp->uClusterDepthParams,
							                     cluster_depth_params[0], cluster_depth_params[1],
							                     cluster_depth_params[2], cluster_depth_params[3]);
						if (cp->uClusterLightCount >= 0)
							lam_gl_cmd_uniform1i(&s_gl.cmd_buf, cp->uClusterLightCount, (GLint)s_gl.active_light_count);
					}
					var_inited[want_var] = true;
				}
				cur_var = want_var;
			}

			int want_hi = (chk->level >= 8u || s_gl.cur_dist >= 120000.0f) ? 1 : 0;
			if (want_hi != bound_hi) {
				lam_gl_cmd_bind_buffer(&s_gl.cmd_buf, GL_ARRAY_BUFFER, want_hi ? s_gl.grid_vbo_hi : s_gl.grid_vbo_macro);
				lam_gl_cmd_vertex_attrib_ptr(&s_gl.cmd_buf, 0, 2, GL_FLOAT, GL_FALSE, 8, 0);
				lam_gl_cmd_bind_buffer(&s_gl.cmd_buf, GL_ELEMENT_ARRAY_BUFFER, want_hi ? s_gl.grid_ibo_hi : s_gl.grid_ibo_macro);
				bound_hi = want_hi;
			}
			GLsizei c_indices = want_hi ? GL_TOTAL_INDICES_HI : GL_TOTAL_INDICES_MACRO;

			if (cp->uChunkLayer >= 0)
				lam_gl_cmd_uniform1f(&s_gl.cmd_buf, cp->uChunkLayer, (float)chk->slot_layer);

			if (!var_body_valid[want_var] ||
			    chk->rel_center[0] != var_rel_center[want_var][0] ||
			    chk->rel_center[1] != var_rel_center[want_var][1] ||
			    chk->rel_center[2] != var_rel_center[want_var][2] ||
			    chk->major_r != var_major_r[want_var] ||
			    chk->minor_r != var_minor_r[want_var] ||
			    memcmp(chk->rot_mat4, var_rot[want_var], sizeof(var_rot[want_var])) != 0) {
				lam_gl_cmd_uniform3f(&s_gl.cmd_buf, cp->uPlanetCenter,
				                     chk->rel_center[0], chk->rel_center[1], chk->rel_center[2]);
				lam_gl_cmd_uniform_mat4(&s_gl.cmd_buf, cp->uBodyRot, GL_FALSE, chk->rot_mat4);
				lam_gl_cmd_uniform2f(&s_gl.cmd_buf, cp->uTorusRadii, chk->major_r, chk->minor_r);
				memcpy(var_rel_center[want_var], chk->rel_center, sizeof(var_rel_center[want_var]));
				memcpy(var_rot[want_var], chk->rot_mat4, sizeof(var_rot[want_var]));
				var_major_r[want_var] = chk->major_r;
				var_minor_r[want_var] = chk->minor_r;
				var_body_valid[want_var] = true;
			}

			lam_gl_cmd_uniform2f(&s_gl.cmd_buf, cp->uChunkOrigin, (float)chk->ox, (float)chk->oy);
			if (cp->uChunkRelOrigin >= 0) {
				lam_gl_cmd_uniform2f(&s_gl.cmd_buf, cp->uChunkRelOrigin,
				                     (float)((double)chk->ox - (double)s_gl.cur_tx),
				                     (float)((double)chk->oy - (double)s_gl.cur_ty));
			}
			lam_gl_cmd_uniform2f(&s_gl.cmd_buf, cp->uChunkSpan, (float)chk->span_w, (float)chk->span_h);

			lam_gl_cmd_draw_elements(&s_gl.cmd_buf, GL_TRIANGLES, c_indices, GL_UNSIGNED_SHORT, 0);
			if (s_gl.indirect_cmd_count < ND_GPU_INDIRECT_MAX) {
				nd_gpu_draw_elements_indirect_cmd_t *cmd = &s_gl.indirect_cmds[s_gl.indirect_cmd_count++];
				cmd->count = (uint32_t)c_indices;
				cmd->instance_count = 1u;
				cmd->first_index = 0u;
				cmd->base_vertex = 0;
				cmd->base_instance = s_gl.indirect_cmd_count - 1u;
			}
		}
		gl_flush_cmd_ring();

		glDisableVertexAttribArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	}

	if (half_res) {
		GLuint comp_cloud_tex = s_gl.cloud_tex;
		float cuvx = (s_gl.cloud_w > 0) ? ((float)trw / (float)s_gl.cloud_w) : 1.0f;
		float cuvy = (s_gl.cloud_h > 0) ? ((float)trh / (float)s_gl.cloud_h) : 1.0f;

		glDisable(GL_STENCIL_TEST);
		glDisable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);

		if (use_temporal) {
			int write_idx = s_gl.cloud_hist_idx & 1;
			int read_idx = write_idx ^ 1;
			float reproj_mat[16] = {
				1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1
			};
			bool hist_ok = s_gl.cloud_hist_valid &&
			               s_gl.cloud_prev_trw == trw &&
			               s_gl.cloud_prev_trh == trh;
			if (hist_ok) {
				double odx = s_gl.cur_origin[0] - s_gl.cloud_prev_origin[0];
				double ody = s_gl.cur_origin[1] - s_gl.cloud_prev_origin[1];
				double odz = s_gl.cur_origin[2] - s_gl.cloud_prev_origin[2];
				if (odx * odx + ody * ody + odz * odz > 200000.0 * 200000.0) {
					hist_ok = false;
				} else if (!nd_seven_gl_compute_reproj_mat(s_gl.cur_mvp, s_gl.cur_origin,
				                                           s_gl.cloud_prev_mvp, s_gl.cloud_prev_origin,
				                                           reproj_mat)) {
					hist_ok = false;
				}
			}

			glBindFramebuffer(GL_FRAMEBUFFER, s_gl.cloud_hist_fbo[write_idx]);
			glViewport(0, 0, (GLsizei)trw, (GLsizei)trh);
			glDisable(GL_BLEND);
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			glUseProgram(s_gl.cloud_reproj_pipe.prog);

			glActiveTexture(GL_TEXTURE0 + 6);
			glBindTexture(GL_TEXTURE_2D, s_gl.cloud_q_tex);
			glActiveTexture(GL_TEXTURE0 + 11);
			glBindTexture(GL_TEXTURE_2D, use_vol ? s_gl.res_depth_tex : s_gl.cloud_depth_tex);
			glActiveTexture(GL_TEXTURE0 + 12);
			glBindTexture(GL_TEXTURE_2D, s_gl.cloud_hist_tex[read_idx]);

			float quvx = (s_gl.cloud_qw > 0u) ? ((float)qrw / (float)s_gl.cloud_qw) : 1.0f;
			float quvy = (s_gl.cloud_qh > 0u) ? ((float)qrh / (float)s_gl.cloud_qh) : 1.0f;
			if (s_gl.cloud_reproj_pipe.uReprojMat >= 0)
				glUniformMatrix4fv(s_gl.cloud_reproj_pipe.uReprojMat, 1, GL_FALSE, reproj_mat);
			if (s_gl.cloud_reproj_pipe.uQuarterUvScale >= 0)
				glUniform2f(s_gl.cloud_reproj_pipe.uQuarterUvScale, quvx, quvy);
			if (s_gl.cloud_reproj_pipe.uPrevUvScale >= 0)
				glUniform2f(s_gl.cloud_reproj_pipe.uPrevUvScale,
				            hist_ok ? s_gl.cloud_prev_uv_scale[0] : cuvx,
				            hist_ok ? s_gl.cloud_prev_uv_scale[1] : cuvy);
			if (s_gl.cloud_reproj_pipe.uCloudUvScale >= 0)
				glUniform2f(s_gl.cloud_reproj_pipe.uCloudUvScale,
				            use_vol ? suvx : cuvx, use_vol ? suvy : cuvy);
			if (s_gl.cloud_reproj_pipe.uCloudSubpixel >= 0)
				glUniform1i(s_gl.cloud_reproj_pipe.uCloudSubpixel, sub_px | (sub_py << 1));
			if (s_gl.cloud_reproj_pipe.uHistoryValid >= 0)
				glUniform1f(s_gl.cloud_reproj_pipe.uHistoryValid, hist_ok ? 1.0f : 0.0f);

			glActiveTexture(GL_TEXTURE0);
			glDrawArrays(GL_TRIANGLES, 0, 3);

			glActiveTexture(GL_TEXTURE0 + 12);
			glBindTexture(GL_TEXTURE_2D, 0);

			comp_cloud_tex = s_gl.cloud_hist_tex[write_idx];
			memcpy(s_gl.cloud_prev_mvp, s_gl.cur_mvp, sizeof(s_gl.cloud_prev_mvp));
			memcpy(s_gl.cloud_prev_origin, s_gl.cur_origin, sizeof(s_gl.cloud_prev_origin));
			s_gl.cloud_prev_uv_scale[0] = cuvx;
			s_gl.cloud_prev_uv_scale[1] = cuvy;
			s_gl.cloud_prev_trw = trw;
			s_gl.cloud_prev_trh = trh;
			s_gl.cloud_hist_valid = true;
			s_gl.cloud_hist_idx = write_idx ^ 1;
			s_gl.cloud_phase++;
		} else {
			s_gl.cloud_hist_valid = false;
		}

		gl_bind_scene_target();
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
		glUseProgram(s_gl.cloud_comp_pipe.prog);
		glActiveTexture(GL_TEXTURE0 + 6);
		glBindTexture(GL_TEXTURE_2D, comp_cloud_tex);
		glUniform1i(s_gl.cloud_comp_pipe.uCloudTex, 6);
		if (s_gl.cloud_comp_pipe.uCloudUvScale >= 0)
			glUniform2f(s_gl.cloud_comp_pipe.uCloudUvScale, cuvx, cuvy);
		if (s_gl.cloud_comp_pipe.uSceneUvScale >= 0)
			glUniform2f(s_gl.cloud_comp_pipe.uSceneUvScale, suvx, suvy);
		bool use_bilateral = (s_gl.res_depth_tex != 0 && (use_vol || s_gl.cloud_depth_tex != 0) &&
		                      (trw < rw || trh < rh));
		if (s_gl.cloud_comp_pipe.uSceneDepthTex >= 0) {
			glActiveTexture(GL_TEXTURE0 + 5);
			glBindTexture(GL_TEXTURE_2D, s_gl.res_depth_tex);
			glUniform1i(s_gl.cloud_comp_pipe.uSceneDepthTex, 5);
		}
		if (s_gl.cloud_comp_pipe.uCloudDepthTex >= 0) {
			glActiveTexture(GL_TEXTURE0 + 11);
			glBindTexture(GL_TEXTURE_2D, use_vol ? s_gl.res_depth_tex : s_gl.cloud_depth_tex);
			glUniform1i(s_gl.cloud_comp_pipe.uCloudDepthTex, 11);
		}
		if (s_gl.cloud_comp_pipe.uBilateralActive >= 0) {
			float bilat_mode = use_bilateral ? (use_vol ? 2.0f : 1.0f) : 0.0f;
			glUniform1f(s_gl.cloud_comp_pipe.uBilateralActive, bilat_mode);
		}
		glActiveTexture(GL_TEXTURE0);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glActiveTexture(GL_TEXTURE0 + 11);
		glBindTexture(GL_TEXTURE_2D, 0);
		glActiveTexture(GL_TEXTURE0 + 6);
		glBindTexture(GL_TEXTURE_2D, 0);
		glActiveTexture(GL_TEXTURE0 + 5);
		glBindTexture(GL_TEXTURE_2D, 0);
		glActiveTexture(GL_TEXTURE0);
	}

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glActiveTexture(GL_TEXTURE0);
	glTimerEnd(ND_GPU_PASS_CLOUDS);
	s_gl.res_color_valid = false;
	s_gl.cloud_count = 0;
}
