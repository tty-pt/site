/*
 * nd_seven_gl_clouds.c — Cloud Shell Frustum Culling, 16x16x24 Clustered Cloud Lighting, and Volumetric Cloud Pass.
 */

#include "nd_seven_gl_internal.h"

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

	/* Gate 3: CPU Synoptic Clear-Sky Pre-Cull for regional/local chunks up to 524,288 wu */
	if (span_w <= 524288 && span_h <= 524288) {
		float sea_wet = (s_gl.cur_sea_level > -900.0f) ? 1.0f : 0.0f;
		float fw_est = fmaxf(0.18f, s_gl.cur_dist * 0.0014f);
		float pad = fminf(4800.0f, fmaxf(1200.0f, s_gl.cur_dist * 0.25f));
		float x0 = (float)ox - pad;
		float y0 = (float)oy - pad;
		float w_pad = (float)span_w + 2.0f * pad;
		float h_pad = (float)span_h + 2.0f * pad;
		int steps = (span_w > 65536 || span_h > 65536) ? 4 : ((span_w > 8192 || span_h > 8192) ? 3 : 2);
		float inv_steps = 1.0f / (float)steps;
		float margin = (span_w > 65536 || span_h > 65536) ? 0.040f : 0.024f;
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

	/* Auto-populate universe-owner glowing Lamb point light if active */
	if (s_gl.cur_lamb_light[3] > 0.01f && count < ND_MAX_TOTAL_LIGHTS) {
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
	bool need_list_upload = true;
	if (count == 2 && s_gl.cluster_ssbo_valid && s_gl.cluster_dirs_only_cached) {
		need_list_upload = false;
		s_gl.active_cluster_count = ND_CLUSTER_TOTAL;
	} else {
		/* Clear cluster light lists (stride = 33 bytes per cluster: byte 0 = count, bytes 1..32 = light indices) */
		for (uint32_t c = 0; c < ND_CLUSTER_TOTAL; c++)
			s_gl.cluster_list_tex_data[c * stride] = 0u;

		/* Assign active lights to 16x16x24 clusters */
		const float *mvp = s_gl.cur_mvp;
		for (uint32_t li = 0; li < count; li++) {
			const nd_cluster_light_t *L = &s_gl.active_lights[li];
			int ltype = (int)(L->type + 0.5f);
			if (ltype == ND_LIGHT_TYPE_DIRECTIONAL) {
				/* Directional lights illuminate all clusters */
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

		uint32_t active_clusters = 0;
		for (uint32_t c = 0; c < ND_CLUSTER_TOTAL; c++) {
			if (s_gl.cluster_list_tex_data[c * stride] > 0)
				active_clusters++;
		}
		s_gl.active_cluster_count = active_clusters;
		s_gl.cluster_dirs_only_cached = (count == 2);
	}

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
	}

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glBindTexture(GL_TEXTURE_2D, s_gl.cluster_light_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, ND_MAX_TOTAL_LIGHTS, 0,
	             GL_RGBA, GL_UNSIGNED_BYTE, s_gl.cluster_light_tex_data);

	if (need_list_upload) {
		glBindTexture(GL_TEXTURE_2D, s_gl.cluster_list_tex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, (GLsizei)stride, ND_CLUSTER_TOTAL, 0,
		             GL_RED, GL_UNSIGNED_BYTE, s_gl.cluster_list_tex_data);
	}
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glBindTexture(GL_TEXTURE_2D, 0);
}

void nd_seven_gl_flush_clouds(void)
{
	if (!s_gl.initialized || !s_gl.pipelines_ready || !s_gl.cloud_pipe.prog || s_gl.cloud_count == 0 ||
	    s_gl.cur_dist <= 480.0f) {
		s_gl.cloud_count = 0;
		return;
	}

	float cluster_depth_params[4] = { 16.0f, 32000.0f, 1.0f, 0.0f };
	gl_update_clustered_cloud_lighting(cluster_depth_params);

	bool half_res = (s_gl.scene_ok && s_gl.cloud_fbo != 0 && s_gl.cloud_comp_pipe.prog != 0);
	if (half_res) {
		/* Resolve MSAA depth to res_fbo, then downsample depth to half-res cloud_fbo
		 * so hardware GL_DEPTH_TEST (GL_LEQUAL) occludes far-side torus cloud shells. */
		gl_resolve_scene(GL_DEPTH_BUFFER_BIT);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, s_gl.res_fbo);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_gl.cloud_fbo);
		glBlitFramebuffer(0, 0, (GLint)s_gl.scene_w, (GLint)s_gl.scene_h,
		                  0, 0, (GLint)s_gl.cloud_w, (GLint)s_gl.cloud_h,
		                  GL_DEPTH_BUFFER_BIT, GL_NEAREST);
		glBindFramebuffer(GL_FRAMEBUFFER, s_gl.cloud_fbo);
		glViewport(0, 0, (GLsizei)s_gl.cloud_w, (GLsizei)s_gl.cloud_h);
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
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

	glUseProgram(s_gl.cloud_pipe.prog);
	gl_bind_csm(s_gl.cloud_pipe.uCloudShadowMap, s_gl.cloud_pipe.uCsm);

	/* Bind Clustered Cloud Lighting textures and uniforms */
	{
		uint32_t vp_w = half_res ? s_gl.cloud_w : s_gl.stage_w;
		uint32_t vp_h = half_res ? s_gl.cloud_h : s_gl.stage_h;
		glUniform2f(s_gl.cloud_pipe.uViewport, (float)vp_w, (float)vp_h);
		glUniform3fv(s_gl.cloud_pipe.uMoonDir, 1, s_gl.cur_moon_dir);
		glUniform3f(s_gl.cloud_pipe.uClusterGridDim,
		            (float)ND_CLUSTER_GRID_X, (float)ND_CLUSTER_GRID_Y, (float)ND_CLUSTER_GRID_Z);
		glUniform4f(s_gl.cloud_pipe.uClusterDepthParams,
		            cluster_depth_params[0], cluster_depth_params[1],
		            cluster_depth_params[2], cluster_depth_params[3]);
		glUniform1i(s_gl.cloud_pipe.uClusterLightCount, (GLint)s_gl.active_light_count);

		glActiveTexture(GL_TEXTURE0 + 9);
		glBindTexture(GL_TEXTURE_2D, s_gl.cluster_light_tex);
		glUniform1i(s_gl.cloud_pipe.uClusterLightData, 9);

		glActiveTexture(GL_TEXTURE0 + 10);
		glBindTexture(GL_TEXTURE_2D, s_gl.cluster_list_tex);
		glUniform1i(s_gl.cloud_pipe.uClusterLightListsTex, 10);
		glActiveTexture(GL_TEXTURE0);
	}

	/* Temporal CSM reprojection: bind history texture and set previous frame uniforms */
	if (s_gl.csm_history_valid && s_gl.cloud_pipe.uCloudShadowMapHistory >= 0) {
		glActiveTexture(GL_TEXTURE0 + 8);
		glBindTexture(GL_TEXTURE_2D, s_gl.csm_history_tex);
		glUniform1i(s_gl.cloud_pipe.uCloudShadowMapHistory, 8);
		glActiveTexture(GL_TEXTURE0);
		glUniform3f(s_gl.cloud_pipe.uPrevCsm,
		            s_gl.csm_prev_win.cx, s_gl.csm_prev_win.cy, s_gl.csm_prev_win.half);
		glUniform3fv(s_gl.cloud_pipe.uPrevSunDir, 1, s_gl.csm_prev_sun_dir);
		/* Confidence based on camera motion and sun change */
		float cam_motion = sqrtf((s_gl.cur_tx - s_gl.csm_prev_win.cx) * (s_gl.cur_tx - s_gl.csm_prev_win.cx) +
		                         (s_gl.cur_ty - s_gl.csm_prev_win.cy) * (s_gl.cur_ty - s_gl.csm_prev_win.cy)) / fmaxf(1.0f, s_gl.cur_dist * 0.01f);
		float sun_change = 1.0f - (s_gl.cur_sun_dir[0] * s_gl.csm_prev_sun_dir[0] +
		                            s_gl.cur_sun_dir[1] * s_gl.csm_prev_sun_dir[1] +
		                            s_gl.cur_sun_dir[2] * s_gl.csm_prev_sun_dir[2]);
		float confidence = fmaxf(0.0f, 1.0f - cam_motion - sun_change * 2.0f);
		glUniform1f(s_gl.cloud_pipe.uCsmConfidence, confidence);
		/* Jitter delta for temporal reprojection */
		float jitter_delta_x = s_gl.csm_jitter_x - s_gl.csm_prev_jitter_x;
		float jitter_delta_y = s_gl.csm_jitter_y - s_gl.csm_prev_jitter_y;
		glUniform2f(s_gl.cloud_pipe.uCsmJitterDelta, jitter_delta_x, jitter_delta_y);
	} else {
		glUniform1f(s_gl.cloud_pipe.uCsmConfidence, 0.0f);
		glUniform2f(s_gl.cloud_pipe.uCsmJitterDelta, 0.0f, 0.0f);
	}

	float zero_center[3] = { 0.0f, 0.0f, 0.0f };
	glUniformMatrix4fv(s_gl.cloud_pipe.uMVP, 1, GL_FALSE, s_gl.cur_mvp);
	glUniform4f(s_gl.cloud_pipe.uRefTrig,
	            s_gl.cur_ref_trig[0], s_gl.cur_ref_trig[1],
	            s_gl.cur_ref_trig[2], s_gl.cur_ref_trig[3]);
	glUniform3fv(s_gl.cloud_pipe.uCameraPos, 1, s_gl.cur_eye);
	glUniform3fv(s_gl.cloud_pipe.uPlanetCenter, 1, zero_center);
	glUniform2f(s_gl.cloud_pipe.uTorusRadii, s_gl.cur_pdef.major_r, s_gl.cur_pdef.minor_r);
	glUniform2f(s_gl.cloud_pipe.uWorldDim, ND_SEVEN_WORLD_DIM_W, ND_SEVEN_WORLD_DIM_H);

	float space_t = fminf(1.0f, fmaxf(0.0f, (s_gl.cur_dist - 12000.0f) / 40000.0f));
	float fog_scale = 1.0f + space_t * 250.0f;
	glUniform1f(s_gl.cloud_pipe.uFogStart, s_gl.cur_dist * 1.5f * fog_scale);
	glUniform1f(s_gl.cloud_pipe.uFogEnd, s_gl.cur_dist * 4.0f * fog_scale);
	glUniform1f(s_gl.cloud_pipe.uAnimTime, s_gl.cur_anim_time);
	glUniform1f(s_gl.cloud_pipe.uCamDist, s_gl.cur_dist);
	glUniform3fv(s_gl.cloud_pipe.uSunDir, 1, s_gl.cur_sun_dir);
	{
		/* Place proxy shell at the exact vertical midpoint of [zBase, zTopProj] (1,620 wu + 0.5 * eff_orbit_lift)
		 * so sMidSlab = 0.5 * (sEnter + sExit) is identically 0 and entryWorldPos == vWorldPos! */
		int cq = nd_seven_get_cloud_quality();
		float max_steps = (cq <= ND_CLOUD_QUALITY_LOW) ? 2.0f : ((cq == ND_CLOUD_QUALITY_MEDIUM) ? 3.0f : 4.0f);
		float deck_base = 420.0f;
		float deck_span = 2400.0f;
		float eff_orbit_lift = fminf(16000.0f, fmaxf(0.0f, (s_gl.cur_dist - 1800.0f) * 0.065f));
		float target_shell_z = 1620.0f + eff_orbit_lift * 0.50f;
		float max_safe_z = s_gl.cur_lamb_light[2] + s_gl.cur_dist * 0.58f;
		float eff_shell_z = fminf(target_shell_z, fmaxf(24.0f, max_safe_z));
		glUniform4f(s_gl.cloud_pipe.uCloudDeck, deck_base, deck_span, max_steps, eff_shell_z);
	}

	glUniform3f(s_gl.cloud_pipe.uSkyColor,
	            0.55f * (1.0f - space_t) + 0.012f * space_t,
	            0.70f * (1.0f - space_t) + 0.016f * space_t,
	            0.85f * (1.0f - space_t) + 0.035f * space_t);
	glUniform1f(s_gl.cloud_pipe.uAtmoIntensity, fminf(1.0f, fmaxf(0.0f, (s_gl.cur_dist - 1200.0f) / 8000.0f)));
	glUniform1f(s_gl.cloud_pipe.uSeaLevel, s_gl.cur_base_sea_level);

	/* Always bind the lightweight 16x16 macro grid (512 triangles) for the smooth concentric
	 * screen-projection proxy shell — eliminates 81x redundant vertex shader & triangle setup overhead! */
	glBindBuffer(GL_ARRAY_BUFFER, s_gl.grid_vbo_macro);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_gl.grid_ibo_macro);

	for (uint32_t i = 0; i < s_gl.cloud_count; i++) {
		const gl_water_chunk_t *chk = &s_gl.cloud_queue[i];
		GLsizei c_indices = GL_TOTAL_INDICES_MACRO;

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, chk->substrate_tex);
		glUniform1i(s_gl.cloud_pipe.uSubstrateTex, 0);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, chk->height_tex);
		glUniform1i(s_gl.cloud_pipe.uHeightTex, 1);

		glUniform3fv(s_gl.cloud_pipe.uPlanetCenter, 1, chk->rel_center);
		glUniformMatrix4fv(s_gl.cloud_pipe.uBodyRot, 1, GL_FALSE, chk->rot_mat4);
		glUniform2f(s_gl.cloud_pipe.uTorusRadii, chk->major_r, chk->minor_r);
		glUniform2f(s_gl.cloud_pipe.uChunkOrigin, (float)chk->ox, (float)chk->oy);
		glUniform2f(s_gl.cloud_pipe.uChunkRelOrigin,
		            (float)((double)chk->ox - (double)s_gl.cur_tx),
		            (float)((double)chk->oy - (double)s_gl.cur_ty));
		glUniform2f(s_gl.cloud_pipe.uChunkSpan, (float)chk->span_w, (float)chk->span_h);

		glDrawElements(GL_TRIANGLES, c_indices, GL_UNSIGNED_SHORT, 0);
		if (s_gl.indirect_cmd_count < ND_GPU_INDIRECT_MAX) {
			nd_gpu_draw_elements_indirect_cmd_t *cmd = &s_gl.indirect_cmds[s_gl.indirect_cmd_count++];
			cmd->count = (uint32_t)c_indices;
			cmd->instance_count = 1u;
			cmd->first_index = 0u;
			cmd->base_vertex = 0;
			cmd->base_instance = s_gl.indirect_cmd_count - 1u;
		}
	}

	glDisableVertexAttribArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

	if (half_res) {
		glDisable(GL_STENCIL_TEST);
		gl_bind_scene_target();
		glDisable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
		glUseProgram(s_gl.cloud_comp_pipe.prog);
		glActiveTexture(GL_TEXTURE0 + 6);
		glBindTexture(GL_TEXTURE_2D, s_gl.cloud_tex);
		glUniform1i(s_gl.cloud_comp_pipe.uCloudTex, 6);
		glActiveTexture(GL_TEXTURE0);
		glDrawArrays(GL_TRIANGLES, 0, 3);
	}

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	s_gl.cloud_count = 0;
}
