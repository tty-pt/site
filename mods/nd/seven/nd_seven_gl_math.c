/*
 * nd_seven_gl_math.c — Torus Geometry, Camera MVP, Height/Water Sampling, and CPU Noise/Weather Mirrors.
 */

#include "nd_seven_gl_internal.h"

grass_lamb_pos_t s_grass_lamb_pos = { 0.0f, 0.0f };
float s_grass_anim_time = 0.0f;
float s_view_wind_sx = 0.0f;
float s_view_wind_sy = 0.0f;
float s_view_wind_dz = 0.0f;
float s_view_wind_rustle = 0.0f;
float s_view_tree_sx = 0.0f;
float s_view_tree_sy = 0.0f;
float s_view_tree_rustle = 0.0f;
int   s_view_wind_ready = 0;

void gl_update_view_wind(float wx, float wy, float anim_time)
{
	float wDirX = 0.7071f, wDirY = -0.7071f, gust = 1.0f, catspaw = 0.5f;
	gl_eval_recursive_wind(wx, wy, anim_time, &wDirX, &wDirY, &gust, &catspaw);

	float slow_sway = sinf(anim_time * ND_TREE_TRUNK_SWAY_FREQ_RAD) * (0.10f + 0.12f * gust);
	float bough_nod = sinf(anim_time * ND_TREE_BOUGH_NOD_FREQ_RAD) * (0.04f + 0.06f * catspaw);
	float tree_mag = ND_TREE_WIND_BASE_LEAN + ND_TREE_WIND_GUST_SCALE * gust + ND_TREE_WIND_CATSPAW_SCALE * catspaw + slow_sway + bough_nod;
	s_view_tree_sx = wDirX * tree_mag + (-wDirY) * (bough_nod * 0.10f);
	s_view_tree_sy = wDirY * tree_mag + ( wDirX) * (bough_nod * 0.10f);
	s_view_tree_rustle = 0.04f * catspaw;

	float wave_arg = anim_time * ND_GRASS_WAVE_FREQ_RAD;
	float primary_wave = sinf(wave_arg) + 0.28f * sinf(wave_arg * 2.0f - 0.65f);
	float gust_env = fmaxf(0.0f, gust - 0.35f);
	float gust_wave = primary_wave * (ND_GRASS_WAVE_AMP * (0.45f + 0.65f * gust));
	float flutter = sinf(anim_time * ND_GRASS_FLUTTER_FREQ_RAD) * (ND_GRASS_FLUTTER_AMP * (0.40f + 0.60f * catspaw));
	float cross_sway = cosf(wave_arg * 0.85f) * (0.16f + 0.22f * catspaw) + flutter * 0.30f;
	float wind_mag = ND_GRASS_WIND_BASE_LEAN + ND_GRASS_WIND_GUST_SCALE * gust_env + ND_GRASS_WIND_CATSPAW_SCALE * catspaw + gust_wave + flutter;
	if (wind_mag < -0.18f) wind_mag = -0.18f;
	s_view_wind_sx = wDirX * wind_mag + (-wDirY) * cross_sway;
	s_view_wind_sy = wDirY * wind_mag + ( wDirX) * cross_sway;
	s_view_wind_dz = (s_view_wind_sx * s_view_wind_sx + s_view_wind_sy * s_view_wind_sy) * 0.075f;
	s_view_wind_rustle = 0.08f * catspaw + 0.14f * fmaxf(0.0f, primary_wave) * gust_env;
	s_view_wind_ready = 1;
}

void torus_frame_double(double wx, double wy, double wz,
                               const nd_planet_def_t *pdef,
                               double out_pos[3],
                               float out_E[3],
                               float out_S[3],
                               float out_N[3])
{
	double R = (pdef && pdef->major_r > 0.0f) ? (double)pdef->major_r : 2000000.0;
	double r0 = (pdef && pdef->minor_r > 0.0f) ? (double)pdef->minor_r : 1335000.0;
	double cx = pdef ? (double)pdef->center[0] : 0.0;
	double cy = pdef ? (double)pdef->center[1] : 0.0;
	double cz = pdef ? (double)pdef->center[2] : 0.0;
	double wdim_x = (double)ND_SEVEN_WORLD_DIM_W;
	double wdim_y = (double)ND_SEVEN_WORLD_DIM_H;

	corolla_surface_t surf = corolla_torus(R, r0);
	/* In corolla_eval_frame, theta = 2*pi*(v - 0.5), so v = (wy / wdim_y) + 0.5
	 * places wy = 0 at the outer equator (theta = 0). */
	double u = wx / wdim_x;
	double v = (wy / wdim_y) + 0.5;
	corolla_frame_t fr = {0};
	corolla_eval_frame(&surf, u, v, wz, &fr);

	bool has_rot = pdef && (pdef->rot[0] != 0.0f || pdef->rot[4] != 0.0f || pdef->rot[8] != 0.0f);
	if (has_rot) {
		double r00 = (double)pdef->rot[0], r01 = (double)pdef->rot[1], r02 = (double)pdef->rot[2];
		double r10 = (double)pdef->rot[3], r11 = (double)pdef->rot[4], r12 = (double)pdef->rot[5];
		double r20 = (double)pdef->rot[6], r21 = (double)pdef->rot[7], r22 = (double)pdef->rot[8];

		double px = fr.pos[0], py = fr.pos[1], pz = fr.pos[2];
		fr.pos[0] = r00 * px + r01 * py + r02 * pz;
		fr.pos[1] = r10 * px + r11 * py + r12 * pz;
		fr.pos[2] = r20 * px + r21 * py + r22 * pz;

		double ux = fr.tan_u[0], uy = fr.tan_u[1], uz = fr.tan_u[2];
		fr.tan_u[0] = r00 * ux + r01 * uy + r02 * uz;
		fr.tan_u[1] = r10 * ux + r11 * uy + r12 * uz;
		fr.tan_u[2] = r20 * ux + r21 * uy + r22 * uz;

		double vx = fr.tan_v[0], vy = fr.tan_v[1], vz = fr.tan_v[2];
		fr.tan_v[0] = r00 * vx + r01 * vy + r02 * vz;
		fr.tan_v[1] = r10 * vx + r11 * vy + r12 * vz;
		fr.tan_v[2] = r20 * vx + r21 * vy + r22 * vz;

		double nx = fr.norm[0], ny = fr.norm[1], nz = fr.norm[2];
		fr.norm[0] = r00 * nx + r01 * ny + r02 * nz;
		fr.norm[1] = r10 * nx + r11 * ny + r12 * nz;
		fr.norm[2] = r20 * nx + r21 * ny + r22 * nz;
	}

	if (out_pos) {
		out_pos[0] = cx + fr.pos[0];
		out_pos[1] = cy + fr.pos[1];
		out_pos[2] = cz + fr.pos[2];
	}
	if (out_E) {
		out_E[0] = (float)fr.tan_u[0];
		out_E[1] = (float)fr.tan_u[1];
		out_E[2] = (float)fr.tan_u[2];
	}
	if (out_S) {
		out_S[0] = (float)fr.tan_v[0];
		out_S[1] = (float)fr.tan_v[1];
		out_S[2] = (float)fr.tan_v[2];
	}
	if (out_N) {
		out_N[0] = (float)fr.norm[0];
		out_N[1] = (float)fr.norm[1];
		out_N[2] = (float)fr.norm[2];
	}
}

void nd_seven_gl_torus_frame(float wx, float wy, float wz,
                             const nd_planet_def_t *pdef,
                             float out_pos[3],
                             float out_E[3],
                             float out_S[3],
                             float out_N[3])
{
	double pos_d[3];
	torus_frame_double((double)wx, (double)wy, (double)wz, pdef, pos_d, out_E, out_S, out_N);
	if (out_pos) {
		out_pos[0] = (float)pos_d[0];
		out_pos[1] = (float)pos_d[1];
		out_pos[2] = (float)pos_d[2];
	}
}

bool nd_seven_gl_cloud_shell_ray_hit(const float ray_orig[3],
                                     const float ray_dir[3],
                                     float shell_z,
                                     const nd_planet_def_t *pdef,
                                     float *out_wx,
                                     float *out_wy,
                                     float *out_t)
{
	if (!ray_orig || !ray_dir)
		return false;

	double R = (pdef && pdef->major_r > 0.0f) ? (double)pdef->major_r : 2000000.0;
	double r0 = (pdef && pdef->minor_r > 0.0f) ? (double)pdef->minor_r : 1335000.0;
	double cx = pdef ? (double)pdef->center[0] : 0.0;
	double cy = pdef ? (double)pdef->center[1] : 0.0;
	double cz = pdef ? (double)pdef->center[2] : 0.0;
	double ox = (double)ray_orig[0] - cx;
	double oy = (double)ray_orig[1] - cy;
	double oz = (double)ray_orig[2] - cz;
	double dx = (double)ray_dir[0];
	double dy = (double)ray_dir[1];
	double dz = (double)ray_dir[2];

	bool has_rot = pdef && (pdef->rot[0] != 0.0f || pdef->rot[4] != 0.0f || pdef->rot[8] != 0.0f);
	double orig_b[3] = { ox, oy, oz };
	double dir_b[3]  = { dx, dy, dz };
	if (has_rot) {
		/* Transform inertial ray into body frame via transpose of pdef->rot */
		double r00 = (double)pdef->rot[0], r01 = (double)pdef->rot[1], r02 = (double)pdef->rot[2];
		double r10 = (double)pdef->rot[3], r11 = (double)pdef->rot[4], r12 = (double)pdef->rot[5];
		double r20 = (double)pdef->rot[6], r21 = (double)pdef->rot[7], r22 = (double)pdef->rot[8];
		orig_b[0] = r00 * ox + r10 * oy + r20 * oz;
		orig_b[1] = r01 * ox + r11 * oy + r21 * oz;
		orig_b[2] = r02 * ox + r12 * oy + r22 * oz;
		dir_b[0]  = r00 * dx + r10 * dy + r20 * dz;
		dir_b[1]  = r01 * dx + r11 * dy + r21 * dz;
		dir_b[2]  = r02 * dx + r12 * dy + r22 * dz;
	}

	corolla_surface_t surf = corolla_torus(R, r0 + (double)shell_z);
	corolla_hit_t hit = {0};
	if (!corolla_ray_hit(&surf, orig_b, dir_b, 1.0, 200000000.0, &hit) || !hit.hit)
		return false;

	/* Front-hull test: ray must enter the cloud shell from outside (dir . norm < 0) */
	double ndotd = hit.norm[0] * dir_b[0] + hit.norm[1] * dir_b[1] + hit.norm[2] * dir_b[2];
	if (ndotd >= -0.01)
		return false;

	double wdim_x = (double)ND_SEVEN_WORLD_DIM_W;
	double wdim_y = (double)ND_SEVEN_WORLD_DIM_H;
	double v_shift = hit.v - 0.5;
	if (v_shift < 0.0)
		v_shift += 1.0;

	if (out_wx) *out_wx = (float)(hit.u * wdim_x);
	if (out_wy) *out_wy = (float)(v_shift * wdim_y);
	if (out_t)  *out_t  = (float)hit.t;
	return true;
}

void nd_seven_gl_compute_mvp_ex(uint32_t vp_w, uint32_t vp_h,
                                float tx, float ty, float tz,
                                float dist, float yaw_deg, float pitch_deg, float fov_deg,
                                const nd_planet_def_t *pdef,
                                float out_mvp[16],
                                float out_eye[3],
                                float out_target[3],
                                float out_up[3])
{
	float target[3], E[3], S[3], N[3];
	nd_seven_gl_torus_frame(tx, ty, tz, pdef, target, E, S, N);

	float yaw_rad = yaw_deg * (PI_F / 180.0f);
	float cos_y = cosf(yaw_rad);
	float sin_y = sinf(yaw_rad);
	float pitch_rad = pitch_deg * (PI_F / 180.0f);
	float cos_p = cosf(pitch_rad);
	float sin_p = sinf(pitch_rad);
	float d = (dist > 1.0f) ? dist : ND_SEVEN_CAM_DIST;

	float horiz[3] = {
		sin_y * E[0] + cos_y * S[0],
		sin_y * E[1] + cos_y * S[1],
		sin_y * E[2] + cos_y * S[2]
	};

	float eye[3] = {
		target[0] + d * (cos_p * horiz[0] + sin_p * N[0]),
		target[1] + d * (cos_p * horiz[1] + sin_p * N[1]),
		target[2] + d * (cos_p * horiz[2] + sin_p * N[2])
	};
	float up[3] = {
		-sin_p * horiz[0] + cos_p * N[0],
		-sin_p * horiz[1] + cos_p * N[1],
		-sin_p * horiz[2] + cos_p * N[2]
	};

	if (out_eye) { out_eye[0] = eye[0]; out_eye[1] = eye[1]; out_eye[2] = eye[2]; }
	if (out_target) { out_target[0] = target[0]; out_target[1] = target[1]; out_target[2] = target[2]; }
	if (out_up) { out_up[0] = up[0]; out_up[1] = up[1]; out_up[2] = up[2]; }

	if (out_mvp) {
		float modelview[16], proj[16];
		ar_mat4_lookat_lh(modelview, eye, target, up);

		float fov_rad = fov_deg * (PI_F / 180.0f);
		float aspect = (vp_h > 0) ? ((float)vp_w / (float)vp_h) : 1.333333f;
		float near_z = (d * 0.005f > 2.0f) ? (d * 0.005f) : 2.0f;
		float far_z = (d * 20.0f > 80000000.0f) ? (d * 20.0f) : 80000000.0f;

		ar_mat4_perspective(proj, fov_rad, aspect, near_z, far_z);
		ar_mat4_multiply(out_mvp, proj, modelview);
	}
}

void nd_seven_gl_compute_mvp(uint32_t vp_w, uint32_t vp_h,
                             float tx, float ty, float tz,
                             float dist, float pitch_deg, float fov_deg,
                             const nd_planet_def_t *pdef,
                             float out_mvp[16],
                             float out_eye[3],
                             float out_target[3],
                             float out_up[3])
{
	nd_seven_gl_compute_mvp_ex(vp_w, vp_h, tx, ty, tz, dist, 0.0f, pitch_deg, fov_deg,
	                           pdef, out_mvp, out_eye, out_target, out_up);
}

void nd_seven_gl_compute_mvp_rel_ex(uint32_t vp_w, uint32_t vp_h,
                                    float tx, float ty, float tz,
                                    float dist, float yaw_deg, float pitch_deg, float fov_deg,
                                    const nd_planet_def_t *pdef,
                                    float out_mvp[16],
                                    double out_origin_d[3],
                                    float out_eye_rel[3],
                                    float out_eye_abs[3])
{
	double origin_d[3];
	float E[3], S[3], N[3];
	torus_frame_double((double)tx, (double)ty, 0.0, pdef, origin_d, E, S, N);

	float yaw_rad = yaw_deg * (PI_F / 180.0f);
	float cos_y = cosf(yaw_rad);
	float sin_y = sinf(yaw_rad);
	float pitch_rad = pitch_deg * (PI_F / 180.0f);
	float cos_p = cosf(pitch_rad);
	float sin_p = sinf(pitch_rad);
	float d = (dist > 1.0f) ? dist : ND_SEVEN_CAM_DIST;

	float horiz[3] = {
		sin_y * E[0] + cos_y * S[0],
		sin_y * E[1] + cos_y * S[1],
		sin_y * E[2] + cos_y * S[2]
	};
	float target_rel[3] = {
		tz * N[0],
		tz * N[1],
		tz * N[2]
	};
	float view_to_eye[3] = {
		cos_p * horiz[0] + sin_p * N[0],
		cos_p * horiz[1] + sin_p * N[1],
		cos_p * horiz[2] + sin_p * N[2]
	};
	float eye_rel[3] = {
		target_rel[0] + d * view_to_eye[0],
		target_rel[1] + d * view_to_eye[1],
		target_rel[2] + d * view_to_eye[2]
	};
	float up[3] = {
		-sin_p * horiz[0] + cos_p * N[0],
		-sin_p * horiz[1] + cos_p * N[1],
		-sin_p * horiz[2] + cos_p * N[2]
	};

	if (out_origin_d) {
		out_origin_d[0] = origin_d[0];
		out_origin_d[1] = origin_d[1];
		out_origin_d[2] = origin_d[2];
	}
	if (out_eye_rel) {
		out_eye_rel[0] = eye_rel[0];
		out_eye_rel[1] = eye_rel[1];
		out_eye_rel[2] = eye_rel[2];
	}
	if (out_eye_abs) {
		out_eye_abs[0] = (float)(origin_d[0] + (double)eye_rel[0]);
		out_eye_abs[1] = (float)(origin_d[1] + (double)eye_rel[1]);
		out_eye_abs[2] = (float)(origin_d[2] + (double)eye_rel[2]);
	}

	if (out_mvp) {
		float modelview[16], proj[16];
		ar_mat4_lookat_lh(modelview, eye_rel, target_rel, up);

		float fov_rad = fov_deg * (PI_F / 180.0f);
		float aspect = (vp_h > 0) ? ((float)vp_w / (float)vp_h) : 1.333333f;
		float near_z = (d * 0.005f > 2.0f) ? (d * 0.005f) : 2.0f;
		float far_z = (d * 20.0f > 80000000.0f) ? (d * 20.0f) : 80000000.0f;

		ar_mat4_perspective(proj, fov_rad, aspect, near_z, far_z);
		ar_mat4_multiply(out_mvp, proj, modelview);
	}
}

void nd_seven_gl_compute_flat_mvp_ex(uint32_t vp_w, uint32_t vp_h,
                                     float tx, float ty, float tz,
                                     float dist, float yaw_deg, float pitch_deg, float fov_deg,
                                     float out_mvp[16])
{
	float yaw_rad = yaw_deg * (PI_F / 180.0f);
	float cos_y = cosf(yaw_rad);
	float sin_y = sinf(yaw_rad);
	float pitch_rad = pitch_deg * (PI_F / 180.0f);
	float cos_p = cosf(pitch_rad);
	float sin_p = sinf(pitch_rad);
	float d = (dist > 1.0f) ? dist : ND_SEVEN_CAM_DIST;

	float hx = sin_y;
	float hy = cos_y;
	float eye[3] = { tx + d * cos_p * hx, ty + d * cos_p * hy, tz + d * sin_p };
	float target[3] = { tx, ty, tz };
	float up[3] = { -sin_p * hx, -sin_p * hy, cos_p };

	float modelview[16], proj[16];
	ar_mat4_lookat_lh(modelview, eye, target, up);

	float fov_rad = fov_deg * (PI_F / 180.0f);
	float aspect = (vp_h > 0) ? ((float)vp_w / (float)vp_h) : 1.333333f;
	float near_z = (d * 0.005f > 2.0f) ? (d * 0.005f) : 2.0f;
	float far_z = (d * 25.0f > 40000.0f) ? (d * 25.0f) : 40000.0f;

	ar_mat4_perspective(proj, fov_rad, aspect, near_z, far_z);
	ar_mat4_multiply(out_mvp, proj, modelview);
}

void nd_seven_gl_compute_flat_mvp(uint32_t vp_w, uint32_t vp_h,
                                  float tx, float ty, float tz,
                                  float dist, float pitch_deg, float fov_deg,
                                  float out_mvp[16])
{
	nd_seven_gl_compute_flat_mvp_ex(vp_w, vp_h, tx, ty, tz, dist, 0.0f, pitch_deg, fov_deg, out_mvp);
}


static inline float get_h32(const int32_t *buf, int px, int py, int w, int h)
{
	if (px < 0) px = 0;
	if (px >= w) px = w - 1;
	if (py < 0) py = 0;
	if (py >= h) py = h - 1;
	return (float)buf[py * w + px];
}

float nd_seven_gl_sample_height_smooth(const int32_t *height_i32,
                                       uint32_t tex_w, uint32_t tex_h,
                                       float u, float v,
                                       float span_x, float span_y,
                                       float *out_grad_x, float *out_grad_y)
{
	if (!height_i32 || tex_w < 2 || tex_h < 2) return 0.0f;

	float max_xf = (float)(tex_w - 1);
	float max_yf = (float)(tex_h - 1);

	float uc = u < 0.0f ? 0.0f : (u > 1.0f ? 1.0f : u);
	float vc = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);

	float px = uc * max_xf;
	float py = vc * max_yf;

	int ix = (int)floorf(px);
	int iy = (int)floorf(py);

	float fx = px - (float)ix;
	float fy = py - (float)iy;

	int w = (int)tex_w;
	int h = (int)tex_h;

	float wx[4] = {
		fx * (-0.5f + fx * (1.0f - 0.5f * fx)),
		1.0f + fx * fx * (-2.5f + 1.5f * fx),
		fx * (0.5f + fx * (2.0f - 1.5f * fx)),
		fx * fx * (-0.5f + 0.5f * fx)
	};
	float wy[4] = {
		fy * (-0.5f + fy * (1.0f - 0.5f * fy)),
		1.0f + fy * fy * (-2.5f + 1.5f * fy),
		fy * (0.5f + fy * (2.0f - 1.5f * fy)),
		fy * fy * (-0.5f + 0.5f * fy)
	};
	float dwx[4] = {
		-0.5f + fx * (2.0f - 1.5f * fx),
		fx * (-5.0f + 4.5f * fx),
		0.5f + fx * (4.0f - 4.5f * fx),
		fx * (-1.0f + 1.5f * fx)
	};
	float dwy[4] = {
		-0.5f + fy * (2.0f - 1.5f * fy),
		fy * (-5.0f + 4.5f * fy),
		0.5f + fy * (4.0f - 4.5f * fy),
		fy * (-1.0f + 1.5f * fy)
	};

	float h_bicubic = 0.0f;
	float gx_bicubic = 0.0f;
	float gy_bicubic = 0.0f;

	for (int j = 0; j < 4; j++) {
		float row_h = 0.0f;
		float row_dx = 0.0f;
		int cy = iy + j - 1;
		for (int i = 0; i < 4; i++) {
			int cx = ix + i - 1;
			float val = get_h32(height_i32, cx, cy, w, h);
			row_h += val * wx[i];
			row_dx += val * dwx[i];
		}
		h_bicubic += row_h * wy[j];
		gx_bicubic += row_dx * wy[j];
		gy_bicubic += row_h * dwy[j];
	}

	float border_x = fminf(px, max_xf - px);
	float border_y = fminf(py, max_yf - py);
	float border_dist = fminf(border_x, border_y);
	float border_blend = border_dist < 0.0f ? 0.0f : (border_dist > 1.0f ? 1.0f : border_dist);

	float h00 = get_h32(height_i32, ix,     iy,     w, h);
	float h10 = get_h32(height_i32, ix + 1, iy,     w, h);
	float h01 = get_h32(height_i32, ix,     iy + 1, w, h);
	float h11 = get_h32(height_i32, ix + 1, iy + 1, w, h);

	float b0 = h00 + (h10 - h00) * fx;
	float b1 = h01 + (h11 - h01) * fx;
	float h_linear = b0 + (b1 - b0) * fy;

	float raw_h = h_linear + (h_bicubic - h_linear) * border_blend;

	if (out_grad_x || out_grad_y) {
		float dx_linear = (h10 - h00) + ((h11 - h01) - (h10 - h00)) * fy;
		float dy_linear = (h01 - h00) + ((h11 - h10) - (h01 - h00)) * fx;
		float final_gx = dx_linear + (gx_bicubic - dx_linear) * border_blend;
		float final_gy = dy_linear + (gy_bicubic - dy_linear) * border_blend;
		if (out_grad_x) *out_grad_x = final_gx * (max_xf / (span_x > 0.0f ? span_x : 160.0f));
		if (out_grad_y) *out_grad_y = final_gy * (max_yf / (span_y > 0.0f ? span_y : 128.0f));
	}

	return raw_h * 0.0625f;
}

float nd_seven_gl_eval_water_radius(const int32_t *height_i32,
                                    uint32_t tex_w, uint32_t tex_h,
                                    float u, float v,
                                    float span_x, float span_y,
                                    float r0, float sea_level,
                                    float *out_wz, float *out_water_depth)
{
	float wz = nd_seven_gl_sample_height_smooth(height_i32, tex_w, tex_h, u, v, span_x, span_y, NULL, NULL);
	float depth = sea_level - wz;

	if (out_wz) *out_wz = wz;
	if (out_water_depth) *out_water_depth = depth;

	if (depth <= 0.0f) {
		return r0 + fminf(wz, sea_level) - 2.0f;
	} else {
		return r0 + sea_level;
	}
}

static inline float chunk_h_extrap(const int32_t *h, int32_t x, int32_t y,
                                   int32_t w, int32_t h_dim)
{
	int32_t cx = (x < 0) ? 0 : ((x >= w) ? (w - 1) : x);
	int32_t cy = (y < 0) ? 0 : ((y >= h_dim) ? (h_dim - 1) : y);
	float hc = (float)h[(size_t)cy * (size_t)w + (size_t)cx];
	if (cx == x && cy == y)
		return hc;
	int32_t sx = (cx > x) ? 1 : ((cx < x) ? -1 : 0);
	int32_t sy = (cy > y) ? 1 : ((cy < y) ? -1 : 0);
	int32_t ix = cx + sx;
	int32_t iy = cy + sy;
	if (ix < 0) ix = 0; else if (ix >= w) ix = w - 1;
	if (iy < 0) iy = 0; else if (iy >= h_dim) iy = h_dim - 1;
	float hin = (float)h[(size_t)iy * (size_t)w + (size_t)ix];
	return 2.0f * hc - hin;
}

void nd_seven_gl_bake_chunk_height_derived(const int32_t *height_i32,
                                           uint32_t tex_w, uint32_t tex_h,
                                           float span_x, float span_y,
                                           float *out_rgba)
{
	if (!out_rgba || tex_w == 0u || tex_h == 0u)
		return;
	size_t n = (size_t)tex_w * (size_t)tex_h;
	if (!height_i32) {
		memset(out_rgba, 0, n * 4u * sizeof(float));
		return;
	}
	int32_t iw = (int32_t)tex_w;
	int32_t ih = (int32_t)tex_h;
	float max_x = (tex_w > 1u) ? (float)(tex_w - 1u) : 1.0f;
	float max_y = (tex_h > 1u) ? (float)(tex_h - 1u) : 1.0f;
	float t2w_x = max_x / fmaxf(1.0f, span_x);
	float t2w_y = max_y / fmaxf(1.0f, span_y);
	float gx_scale = 0.5f * 0.0625f * t2w_x;
	float gy_scale = 0.5f * 0.0625f * t2w_y;
	float lap_scale = 0.16f * (t2w_x + t2w_y);

	for (int32_t y = 0; y < ih; y++) {
		int32_t y_m = y - 1;
		int32_t y_p = y + 1;
		bool y_interior = (y > 0 && y < ih - 1);
		const int32_t *row_c = height_i32 + (size_t)y * (size_t)iw;
		const int32_t *row_m = y_interior ? (row_c - iw) : NULL;
		const int32_t *row_p = y_interior ? (row_c + iw) : NULL;
		float *dst = out_rgba + (size_t)y * (size_t)iw * 4u;
		for (int32_t x = 0; x < iw; x++) {
			float hc = (float)row_c[x];
			float hl, hr, hd, hu;
			if (y_interior && x > 0 && x < iw - 1) {
				hl = (float)row_c[x - 1];
				hr = (float)row_c[x + 1];
				hd = (float)row_m[x];
				hu = (float)row_p[x];
			} else {
				hl = chunk_h_extrap(height_i32, x - 1, y, iw, ih);
				hr = chunk_h_extrap(height_i32, x + 1, y, iw, ih);
				hd = chunk_h_extrap(height_i32, x, y_m, iw, ih);
				hu = chunk_h_extrap(height_i32, x, y_p, iw, ih);
			}
			dst[0] = hc * 0.0625f;
			dst[1] = (hr - hl) * gx_scale;
			dst[2] = (hu - hd) * gy_scale;
			dst[3] = (hl + hr + hd + hu - 4.0f * hc) * lap_scale;
			dst += 4;
		}
	}
}

void nd_seven_gl_bake_chunk_river(const int32_t *height_i32,
                                  const uint32_t *color_rgba,
                                  uint32_t tex_w, uint32_t tex_h,
                                  float base_sea_level,
                                  float *out_rgba)
{
	if (!out_rgba || tex_w == 0u || tex_h == 0u)
		return;
	size_t n = (size_t)tex_w * (size_t)tex_h;
	if (!height_i32 || !color_rgba || base_sea_level <= -900.0f) {
		memset(out_rgba, 0, n * 4u * sizeof(float));
		return;
	}
	const uint8_t *rgba_b = (const uint8_t *)color_rgba;
	int32_t iw = (int32_t)tex_w;
	int32_t ih = (int32_t)tex_h;
	static const float k_w1[3] = { 0.5f, 1.5f, 0.5f };

	for (int32_t y = 0; y < ih; y++) {
		bool y_border = (y == 0 || y == ih - 1);
		float *dst = out_rgba + (size_t)y * (size_t)iw * 4u;
		for (int32_t x = 0; x < iw; x++) {
			bool is_border = (y_border || x == 0 || x == iw - 1);
			float sum_w = 0.0f;
			float sum_z_rel = 0.0f;
			float max_c_nb = 0.0f;
			if (is_border) {
				size_t idx = (size_t)y * (size_t)iw + (size_t)x;
				uint8_t a_u8 = rgba_b[idx * 4u + 3u];
				if (a_u8 < 254u) {
					float c_i = (1.0f - (float)a_u8 * (1.0f / 255.0f)) * 15.9375f;
					if (c_i > 0.05f) {
						float h_i = (float)height_i32[idx] * 0.0625f;
						float w_surf_i = fmaxf(base_sea_level, h_i + c_i - 3.0f);
						sum_w = c_i;
						sum_z_rel = c_i * (w_surf_i - base_sea_level);
						max_c_nb = c_i * 0.75f;
					}
				}
			} else {
				for (int32_t dy = -1; dy <= 1; dy++) {
					int32_t ty = y + dy;
					float wy = k_w1[dy + 1];
					const int32_t *h_row = height_i32 + (size_t)ty * (size_t)iw;
					const uint8_t *c_row = rgba_b + (size_t)ty * (size_t)iw * 4u;
					for (int32_t dx = -1; dx <= 1; dx++) {
						int32_t tx = x + dx;
						uint8_t a_u8 = c_row[(size_t)tx * 4u + 3u];
						if (a_u8 >= 254u)
							continue;
						float c_i = (1.0f - (float)a_u8 * (1.0f / 255.0f)) * 15.9375f;
						if (c_i <= 0.05f)
							continue;
						float wxy = k_w1[dx + 1] * wy;
						float w = wxy * c_i;
						float h_i = (float)h_row[tx] * 0.0625f;
						float w_surf_i = fmaxf(base_sea_level, h_i + c_i - 3.0f);
						sum_w += w;
						sum_z_rel += w * (w_surf_i - base_sea_level);
						float nb_scale = wxy * 0.75f;
						if (nb_scale > 1.0f) nb_scale = 1.0f;
						float c_nb = c_i * nb_scale;
						if (c_nb > max_c_nb) max_c_nb = c_nb;
					}
				}
			}
			if (sum_w > 1e-4f) {
				float r_z_rel = sum_z_rel / sum_w;
				float r_w = sum_w * (1.0f / 16.0f);
				if (r_w > 1.0f) r_w = 1.0f;
				if (r_w < 0.01f) r_w = 0.01f;
				dst[0] = r_z_rel * r_w;
				dst[1] = r_w;
				dst[2] = max_c_nb;
				dst[3] = 0.0f;
			} else {
				dst[0] = 0.0f;
				dst[1] = 0.0f;
				dst[2] = 0.0f;
				dst[3] = 0.0f;
			}
			dst += 4;
		}
	}
}

static inline uint8_t nd_lut_quant_u8(float v)
{
	if (v <= 0.0f) return 0u;
	if (v >= 1.0f) return 255u;
	int q = (int)(v * 255.0f + 0.5f);
	return (uint8_t)(q < 0 ? 0 : (q > 255 ? 255 : q));
}

void nd_seven_gl_bake_noise2d_lut(uint8_t *out_rgba, uint32_t dim, uint32_t period)
{
	if (!out_rgba || dim == 0u || period == 0u)
		return;
	size_t n_lat = (size_t)period * (size_t)period;
	float *g1x = (float *)malloc(n_lat * 4u * sizeof(float));
	float *tab = (float *)malloc((size_t)dim * 3u * sizeof(float));
	uint32_t *idx_tab = (uint32_t *)malloc((size_t)dim * 2u * sizeof(uint32_t));
	if (!g1x || !tab || !idx_tab) {
		free(g1x);
		free(tab);
		free(idx_tab);
		memset(out_rgba, 128, (size_t)dim * (size_t)dim * 4u);
		return;
	}
	float *g1y = g1x + n_lat;
	float *g2x = g1y + n_lat;
	float *g2y = g2x + n_lat;

	for (uint32_t iy = 0; iy < period; iy++) {
		for (uint32_t ix = 0; ix < period; ix++) {
			size_t k = (size_t)iy * (size_t)period + (size_t)ix;
			gl_chaff_grad_dir((int32_t)ix, (int32_t)iy, 0x517CC1B7u, &g1x[k], &g1y[k]);
			gl_chaff_grad_dir((int32_t)ix, (int32_t)iy, 0x9E3779B9u, &g2x[k], &g2y[k]);
		}
	}

	float scale = (float)period / (float)dim;
	for (uint32_t k = 0; k < dim; k++) {
		float p = ((float)k + 0.5f) * scale;
		float ip = floorf(p);
		float f = p - ip;
		uint32_t i0 = (uint32_t)((int32_t)ip) % period;
		uint32_t i1 = (i0 + 1u) % period;
		idx_tab[k * 2u]      = i0;
		idx_tab[k * 2u + 1u] = i1;
		tab[k * 3u]      = f;
		tab[k * 3u + 1u] = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
		tab[k * 3u + 2u] = 30.0f * f * f * (f * (f - 2.0f) + 1.0f);
	}

	const float gn = 0.70710678f;
	uint8_t *dst = out_rgba;
	for (uint32_t y = 0; y < dim; y++) {
		uint32_t iy0 = idx_tab[y * 2u];
		uint32_t iy1 = idx_tab[y * 2u + 1u];
		float fy = tab[y * 3u];
		float uy = tab[y * 3u + 1u];
		float duy = tab[y * 3u + 2u];
		size_t row0 = (size_t)iy0 * (size_t)period;
		size_t row1 = (size_t)iy1 * (size_t)period;
		for (uint32_t x = 0; x < dim; x++) {
			uint32_t ix0 = idx_tab[x * 2u];
			uint32_t ix1 = idx_tab[x * 2u + 1u];
			float fx = tab[x * 3u];
			float ux = tab[x * 3u + 1u];
			float dux = tab[x * 3u + 2u];
			size_t c00 = row0 + ix0;
			size_t c10 = row0 + ix1;
			size_t c01 = row1 + ix0;
			size_t c11 = row1 + ix1;

			float v00 = g1x[c00] * fx          + g1y[c00] * fy;
			float v10 = g1x[c10] * (fx - 1.0f) + g1y[c10] * fy;
			float v01 = g1x[c01] * fx          + g1y[c01] * (fy - 1.0f);
			float v11 = g1x[c11] * (fx - 1.0f) + g1y[c11] * (fy - 1.0f);
			float k0 = v10 - v00;
			float k1 = v01 - v00;
			float k2 = v11 - v01 - k0;
			float v1 = 0.5f + gn * (v00 + k0 * ux + k1 * uy + k2 * ux * uy);
			float gix = g1x[c00] + (g1x[c10] - g1x[c00]) * ux + (g1x[c01] - g1x[c00]) * uy
			          + (g1x[c11] - g1x[c01] - g1x[c10] + g1x[c00]) * (ux * uy);
			float giy = g1y[c00] + (g1y[c10] - g1y[c00]) * ux + (g1y[c01] - g1y[c00]) * uy
			          + (g1y[c11] - g1y[c01] - g1y[c10] + g1y[c00]) * (ux * uy);
			float dx1 = gn * (gix + dux * (k0 + k2 * uy));
			float dy1 = gn * (giy + duy * (k1 + k2 * ux));

			float w00 = g2x[c00] * fx          + g2y[c00] * fy;
			float w10 = g2x[c10] * (fx - 1.0f) + g2y[c10] * fy;
			float w01 = g2x[c01] * fx          + g2y[c01] * (fy - 1.0f);
			float w11 = g2x[c11] * (fx - 1.0f) + g2y[c11] * (fy - 1.0f);
			float j0 = w10 - w00;
			float j1 = w01 - w00;
			float j2 = w11 - w01 - j0;
			float v2 = 0.5f + gn * (w00 + j0 * ux + j1 * uy + j2 * ux * uy);

			dst[0] = nd_lut_quant_u8(v1);
			dst[1] = nd_lut_quant_u8(dx1 * 0.25f + 0.5f);
			dst[2] = nd_lut_quant_u8(dy1 * 0.25f + 0.5f);
			dst[3] = nd_lut_quant_u8(v2);
			dst += 4;
		}
	}

	free(g1x);
	free(tab);
	free(idx_tab);
}

void nd_seven_gl_bake_noise3d_lut(uint8_t *out_rgba, uint32_t dim, uint32_t period)
{
	if (!out_rgba || dim == 0u || period == 0u)
		return;
	size_t p2 = (size_t)period * (size_t)period;
	size_t n_lat = p2 * (size_t)period;
	float *gx_lat = (float *)malloc(n_lat * 3u * sizeof(float));
	float *tab = (float *)malloc((size_t)dim * 3u * sizeof(float));
	uint32_t *idx_tab = (uint32_t *)malloc((size_t)dim * 2u * sizeof(uint32_t));
	if (!gx_lat || !tab || !idx_tab) {
		free(gx_lat);
		free(tab);
		free(idx_tab);
		memset(out_rgba, 128, (size_t)dim * (size_t)dim * (size_t)dim * 4u);
		return;
	}
	float *gy_lat = gx_lat + n_lat;
	float *gz_lat = gy_lat + n_lat;

	for (uint32_t iz = 0; iz < period; iz++) {
		for (uint32_t iy = 0; iy < period; iy++) {
			for (uint32_t ix = 0; ix < period; ix++) {
				uint32_t h1 = gl_chaff_hash3((int32_t)ix, (int32_t)iy, (int32_t)iz, 0x7A3D91E5u);
				uint32_t h2 = gl_chaff_hash3((int32_t)ix, (int32_t)iy, (int32_t)iz, 0x7A3D91E5u ^ 0x9E3779B9u);
				float x = (float)((int32_t)(h1 & 0xFFFFu) - 32768) * (1.0f / 32768.0f);
				float y = (float)((int32_t)(h1 >> 16u)   - 32768) * (1.0f / 32768.0f);
				float z = (float)((int32_t)(h2 & 0xFFFFu) - 32768) * (1.0f / 32768.0f);
				float inv_l = 1.0f / sqrtf(x * x + y * y + z * z + 1e-6f);
				size_t k = (size_t)iz * p2 + (size_t)iy * (size_t)period + (size_t)ix;
				gx_lat[k] = x * inv_l;
				gy_lat[k] = y * inv_l;
				gz_lat[k] = z * inv_l;
			}
		}
	}

	float scale = (float)period / (float)dim;
	for (uint32_t k = 0; k < dim; k++) {
		float p = ((float)k + 0.5f) * scale;
		float ip = floorf(p);
		float f = p - ip;
		uint32_t i0 = (uint32_t)((int32_t)ip) % period;
		uint32_t i1 = (i0 + 1u) % period;
		idx_tab[k * 2u]      = i0;
		idx_tab[k * 2u + 1u] = i1;
		tab[k * 3u]      = f;
		tab[k * 3u + 1u] = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
		tab[k * 3u + 2u] = 30.0f * f * f * (f * (f - 2.0f) + 1.0f);
	}

	const float gn = 0.72f;
	uint8_t *dst = out_rgba;
	for (uint32_t z = 0; z < dim; z++) {
		size_t sl0 = (size_t)idx_tab[z * 2u]      * p2;
		size_t sl1 = (size_t)idx_tab[z * 2u + 1u] * p2;
		float fz = tab[z * 3u];
		float uz = tab[z * 3u + 1u];
		float duz = tab[z * 3u + 2u];
		float fz1 = fz - 1.0f;
		for (uint32_t y = 0; y < dim; y++) {
			size_t r0 = (size_t)idx_tab[y * 2u]      * (size_t)period;
			size_t r1 = (size_t)idx_tab[y * 2u + 1u] * (size_t)period;
			float fy = tab[y * 3u];
			float uy = tab[y * 3u + 1u];
			float duy = tab[y * 3u + 2u];
			float fy1 = fy - 1.0f;
			size_t s00 = sl0 + r0;
			size_t s01 = sl0 + r1;
			size_t s10 = sl1 + r0;
			size_t s11 = sl1 + r1;
			for (uint32_t x = 0; x < dim; x++) {
				uint32_t ix0 = idx_tab[x * 2u];
				uint32_t ix1 = idx_tab[x * 2u + 1u];
				float fx = tab[x * 3u];
				float ux = tab[x * 3u + 1u];
				float dux = tab[x * 3u + 2u];
				float fx1 = fx - 1.0f;

				size_t c000 = s00 + ix0, c100 = s00 + ix1;
				size_t c010 = s01 + ix0, c110 = s01 + ix1;
				size_t c001 = s10 + ix0, c101 = s10 + ix1;
				size_t c011 = s11 + ix0, c111 = s11 + ix1;

				float v000 = gx_lat[c000] * fx  + gy_lat[c000] * fy  + gz_lat[c000] * fz;
				float v100 = gx_lat[c100] * fx1 + gy_lat[c100] * fy  + gz_lat[c100] * fz;
				float v010 = gx_lat[c010] * fx  + gy_lat[c010] * fy1 + gz_lat[c010] * fz;
				float v110 = gx_lat[c110] * fx1 + gy_lat[c110] * fy1 + gz_lat[c110] * fz;
				float v001 = gx_lat[c001] * fx  + gy_lat[c001] * fy  + gz_lat[c001] * fz1;
				float v101 = gx_lat[c101] * fx1 + gy_lat[c101] * fy  + gz_lat[c101] * fz1;
				float v011 = gx_lat[c011] * fx  + gy_lat[c011] * fy1 + gz_lat[c011] * fz1;
				float v111 = gx_lat[c111] * fx1 + gy_lat[c111] * fy1 + gz_lat[c111] * fz1;

				float vx00 = v000 + (v100 - v000) * ux;
				float vx10 = v010 + (v110 - v010) * ux;
				float vx01 = v001 + (v101 - v001) * ux;
				float vx11 = v011 + (v111 - v011) * ux;
				float vxy0 = vx00 + (vx10 - vx00) * uy;
				float vxy1 = vx01 + (vx11 - vx01) * uy;
				float val  = 0.5f + gn * (vxy0 + (vxy1 - vxy0) * uz);

				/* Exact analytical partial derivatives */
				float gx00 = gx_lat[c000] + (gx_lat[c100] - gx_lat[c000]) * ux;
				float gx10 = gx_lat[c010] + (gx_lat[c110] - gx_lat[c010]) * ux;
				float gx01 = gx_lat[c001] + (gx_lat[c101] - gx_lat[c001]) * ux;
				float gx11 = gx_lat[c011] + (gx_lat[c111] - gx_lat[c011]) * ux;
				float gix  = (gx00 + (gx10 - gx00) * uy) + ((gx01 + (gx11 - gx01) * uy) - (gx00 + (gx10 - gx00) * uy)) * uz;

				float gy00 = gy_lat[c000] + (gy_lat[c100] - gy_lat[c000]) * ux;
				float gy10 = gy_lat[c010] + (gy_lat[c110] - gy_lat[c010]) * ux;
				float gy01 = gy_lat[c001] + (gy_lat[c101] - gy_lat[c001]) * ux;
				float gy11 = gy_lat[c011] + (gy_lat[c111] - gy_lat[c011]) * ux;
				float giy  = (gy00 + (gy10 - gy00) * uy) + ((gy01 + (gy11 - gy01) * uy) - (gy00 + (gy10 - gy00) * uy)) * uz;

				float gz00 = gz_lat[c000] + (gz_lat[c100] - gz_lat[c000]) * ux;
				float gz10 = gz_lat[c010] + (gz_lat[c110] - gz_lat[c010]) * ux;
				float gz01 = gz_lat[c001] + (gz_lat[c101] - gz_lat[c001]) * ux;
				float gz11 = gz_lat[c011] + (gz_lat[c111] - gz_lat[c011]) * ux;
				float giz  = (gz00 + (gz10 - gz00) * uy) + ((gz01 + (gz11 - gz01) * uy) - (gz00 + (gz10 - gz00) * uy)) * uz;

				float dx_0 = (v100 - v000) + ((v110 - v010) - (v100 - v000)) * uy;
				float dx_1 = (v101 - v001) + ((v111 - v011) - (v101 - v001)) * uy;
				float dvx  = gn * (gix + dux * (dx_0 + (dx_1 - dx_0) * uz));
				float dvy  = gn * (giy + duy * ((vx10 - vx00) + ((vx11 - vx01) - (vx10 - vx00)) * uz));
				float dvz  = gn * (giz + duz * (vxy1 - vxy0));

				dst[0] = nd_lut_quant_u8(val);
				dst[1] = nd_lut_quant_u8(dvx * 0.25f + 0.5f);
				dst[2] = nd_lut_quant_u8(dvy * 0.25f + 0.5f);
				dst[3] = nd_lut_quant_u8(dvz * 0.25f + 0.5f);
				dst += 4;
			}
		}
	}

	free(gx_lat);
	free(tab);
	free(idx_tab);
}

static void gl_build_worley3d_points(float *pts_xyz, uint32_t cells, uint32_t seed)
{
	const float inv16 = 1.0f / 65535.0f;
	for (uint32_t iz = 0; iz < cells; iz++) {
		for (uint32_t iy = 0; iy < cells; iy++) {
			for (uint32_t ix = 0; ix < cells; ix++) {
				uint32_t h1 = gl_chaff_hash3((int32_t)ix, (int32_t)iy, (int32_t)iz, seed);
				uint32_t h2 = gl_chaff_hash3((int32_t)ix, (int32_t)iy, (int32_t)iz, seed ^ 0x9E3779B9u);
				size_t k = ((size_t)iz * (size_t)cells + (size_t)iy) * (size_t)cells + (size_t)ix;
				pts_xyz[k * 3u + 0u] = 0.12f + 0.76f * ((float)(h1 & 0xFFFFu) * inv16);
				pts_xyz[k * 3u + 1u] = 0.12f + 0.76f * ((float)(h1 >> 16u)    * inv16);
				pts_xyz[k * 3u + 2u] = 0.12f + 0.76f * ((float)(h2 & 0xFFFFu) * inv16);
			}
		}
	}
}

static inline float gl_eval_worley3d_inv(const float *pts_xyz, uint32_t cells,
                                         float px, float py, float pz)
{
	uint32_t mask = cells - 1u;
	int32_t cx = (int32_t)floorf(px);
	int32_t cy = (int32_t)floorf(py);
	int32_t cz = (int32_t)floorf(pz);
	float fx = px - (float)cx;
	float fy = py - (float)cy;
	float fz = pz - (float)cz;
	float min_d2 = 4.0f;
	for (int32_t oz = -1; oz <= 1; oz++) {
		uint32_t nz = (uint32_t)(cz + oz) & mask;
		size_t sl = (size_t)nz * (size_t)cells;
		float bz = (float)oz - fz;
		for (int32_t oy = -1; oy <= 1; oy++) {
			uint32_t ny = (uint32_t)(cy + oy) & mask;
			size_t row = (sl + (size_t)ny) * (size_t)cells;
			float by = (float)oy - fy;
			for (int32_t ox = -1; ox <= 1; ox++) {
				uint32_t nx = (uint32_t)(cx + ox) & mask;
				const float *p = &pts_xyz[(row + (size_t)nx) * 3u];
				float dx = (float)ox + p[0] - fx;
				float dy = by + p[1];
				float dz = bz + p[2];
				float d2 = dx * dx + dy * dy + dz * dz;
				if (d2 < min_d2) min_d2 = d2;
			}
		}
	}
	/* Parabolic cellular dome: rounded spherical top (f'(0)=0) with tucked crevices between bubbles */
	float v = 1.0f - min_d2 * 1.25f;
	return (v < 0.0f) ? 0.0f : ((v > 1.0f) ? 1.0f : v);
}

static void gl_build_perlin3d_grads(float *g_xyz, uint32_t period, uint32_t seed)
{
	for (uint32_t iz = 0; iz < period; iz++) {
		for (uint32_t iy = 0; iy < period; iy++) {
			for (uint32_t ix = 0; ix < period; ix++) {
				uint32_t h1 = gl_chaff_hash3((int32_t)ix, (int32_t)iy, (int32_t)iz, seed);
				uint32_t h2 = gl_chaff_hash3((int32_t)ix, (int32_t)iy, (int32_t)iz, seed ^ 0x85EBCA6Bu);
				float x = (float)((int32_t)(h1 & 0xFFFFu) - 32768) * (1.0f / 32768.0f);
				float y = (float)((int32_t)(h1 >> 16u)    - 32768) * (1.0f / 32768.0f);
				float z = (float)((int32_t)(h2 & 0xFFFFu) - 32768) * (1.0f / 32768.0f);
				float inv_l = 1.0f / sqrtf(x * x + y * y + z * z + 1e-6f);
				size_t k = ((size_t)iz * (size_t)period + (size_t)iy) * (size_t)period + (size_t)ix;
				g_xyz[k * 3u + 0u] = x * inv_l;
				g_xyz[k * 3u + 1u] = y * inv_l;
				g_xyz[k * 3u + 2u] = z * inv_l;
			}
		}
	}
}

static inline float gl_eval_perlin3d_periodic(const float *g_xyz, uint32_t period,
                                              float px, float py, float pz)
{
	uint32_t mask = period - 1u;
	int32_t ix = (int32_t)floorf(px);
	int32_t iy = (int32_t)floorf(py);
	int32_t iz = (int32_t)floorf(pz);
	float fx = px - (float)ix, fy = py - (float)iy, fz = pz - (float)iz;
	float fx1 = fx - 1.0f, fy1 = fy - 1.0f, fz1 = fz - 1.0f;
	float ux = fx * fx * fx * (fx * (fx * 6.0f - 15.0f) + 10.0f);
	float uy = fy * fy * fy * (fy * (fy * 6.0f - 15.0f) + 10.0f);
	float uz = fz * fz * fz * (fz * (fz * 6.0f - 15.0f) + 10.0f);
	uint32_t x0 = (uint32_t)ix & mask, x1 = (x0 + 1u) & mask;
	uint32_t y0 = (uint32_t)iy & mask, y1 = (y0 + 1u) & mask;
	uint32_t z0 = (uint32_t)iz & mask, z1 = (z0 + 1u) & mask;
	size_t p = (size_t)period;
	size_t s00 = ((size_t)z0 * p + (size_t)y0) * p;
	size_t s01 = ((size_t)z0 * p + (size_t)y1) * p;
	size_t s10 = ((size_t)z1 * p + (size_t)y0) * p;
	size_t s11 = ((size_t)z1 * p + (size_t)y1) * p;
	const float *g000 = &g_xyz[(s00 + x0) * 3u], *g100 = &g_xyz[(s00 + x1) * 3u];
	const float *g010 = &g_xyz[(s01 + x0) * 3u], *g110 = &g_xyz[(s01 + x1) * 3u];
	const float *g001 = &g_xyz[(s10 + x0) * 3u], *g101 = &g_xyz[(s10 + x1) * 3u];
	const float *g011 = &g_xyz[(s11 + x0) * 3u], *g111 = &g_xyz[(s11 + x1) * 3u];
	float v000 = g000[0] * fx  + g000[1] * fy  + g000[2] * fz;
	float v100 = g100[0] * fx1 + g100[1] * fy  + g100[2] * fz;
	float v010 = g010[0] * fx  + g010[1] * fy1 + g010[2] * fz;
	float v110 = g110[0] * fx1 + g110[1] * fy1 + g110[2] * fz;
	float v001 = g001[0] * fx  + g001[1] * fy  + g001[2] * fz1;
	float v101 = g101[0] * fx1 + g101[1] * fy  + g101[2] * fz1;
	float v011 = g011[0] * fx  + g011[1] * fy1 + g011[2] * fz1;
	float v111 = g111[0] * fx1 + g111[1] * fy1 + g111[2] * fz1;
	float vx00 = v000 + (v100 - v000) * ux;
	float vx10 = v010 + (v110 - v010) * ux;
	float vx01 = v001 + (v101 - v001) * ux;
	float vx11 = v011 + (v111 - v011) * ux;
	float vxy0 = vx00 + (vx10 - vx00) * uy;
	float vxy1 = vx01 + (vx11 - vx01) * uy;
	float v = 0.5f + 0.85f * (vxy0 + (vxy1 - vxy0) * uz);
	return (v < 0.0f) ? 0.0f : ((v > 1.0f) ? 1.0f : v);
}

void nd_seven_gl_bake_cloud_shape3d_lut(uint8_t *out_rgba, uint32_t dim)
{
	if (!out_rgba || dim == 0u)
		return;

	/* L1-resident periodic feature/gradient tables: Worley (4, 8, 16) + Perlin (4, 8) */
	float wpts4[4 * 4 * 4 * 3];
	float wpts8[8 * 8 * 8 * 3];
	float *wpts16 = (float *)malloc(16u * 16u * 16u * 3u * sizeof(float));
	float pgrad4[4 * 4 * 4 * 3];
	float pgrad8[8 * 8 * 8 * 3];
	if (!wpts16) {
		memset(out_rgba, 128, (size_t)dim * (size_t)dim * (size_t)dim * 4u);
		return;
	}
	gl_build_worley3d_points(wpts4,   4u, 0x574F5234u);
	gl_build_worley3d_points(wpts8,   8u, 0x574F5238u);
	gl_build_worley3d_points(wpts16, 16u, 0x574F5236u);
	gl_build_perlin3d_grads(pgrad4,   4u, 0x50455234u);
	gl_build_perlin3d_grads(pgrad8,   8u, 0x50455238u);

	float inv_d = 1.0f / (float)dim;
	uint8_t *dst = out_rgba;
	for (uint32_t z = 0; z < dim; z++) {
		float uz = ((float)z + 0.5f) * inv_d;
		for (uint32_t y = 0; y < dim; y++) {
			float uy = ((float)y + 0.5f) * inv_d;
			for (uint32_t x = 0; x < dim; x++) {
				float ux = ((float)x + 0.5f) * inv_d;
				float w4  = gl_eval_worley3d_inv(wpts4,   4u, ux *  4.0f, uy *  4.0f, uz *  4.0f);
				float w8  = gl_eval_worley3d_inv(wpts8,   8u, ux *  8.0f, uy *  8.0f, uz *  8.0f);
				float w16 = gl_eval_worley3d_inv(wpts16, 16u, ux * 16.0f, uy * 16.0f, uz * 16.0f);
				float p4  = gl_eval_perlin3d_periodic(pgrad4, 4u, ux * 4.0f, uy * 4.0f, uz * 4.0f);
				float p8  = gl_eval_perlin3d_periodic(pgrad8, 8u, ux * 8.0f, uy * 8.0f, uz * 8.0f);
				float perlin = 0.68f * p4 + 0.32f * p8;
				float w_fbm  = 0.625f * w4 + 0.25f * w8 + 0.125f * w16;
				/* Schneider Perlin-Worley base shape: maps dilated Perlin onto [1 - w_fbm, 1] -> [0, 1] */
				float pw = 1.0f - (0.50f * (1.0f - perlin)) / (w_fbm > 1e-4f ? w_fbm : 1e-4f);
				dst[0] = nd_lut_quant_u8(pw);
				dst[1] = nd_lut_quant_u8(w4);
				dst[2] = nd_lut_quant_u8(w8);
				dst[3] = nd_lut_quant_u8(w16);
				dst += 4;
			}
		}
	}
	free(wpts16);
}

void nd_seven_gl_downsample_rgba8_3d(const uint8_t *src, uint32_t src_dim, uint8_t *dst)
{
	if (!src || !dst || src_dim < 2u)
		return;
	uint32_t d = src_dim >> 1u;
	size_t s_row = (size_t)src_dim * 4u;
	size_t s_sl  = (size_t)src_dim * s_row;
	for (uint32_t z = 0; z < d; z++) {
		const uint8_t *z0 = src + (size_t)(z * 2u) * s_sl;
		const uint8_t *z1 = z0 + s_sl;
		for (uint32_t y = 0; y < d; y++) {
			const uint8_t *y00 = z0 + (size_t)(y * 2u) * s_row;
			const uint8_t *y01 = y00 + s_row;
			const uint8_t *y10 = z1 + (size_t)(y * 2u) * s_row;
			const uint8_t *y11 = y10 + s_row;
			for (uint32_t x = 0; x < d; x++) {
				size_t xo = (size_t)(x * 2u) * 4u;
				for (uint32_t c = 0; c < 4u; c++) {
					uint32_t sum = (uint32_t)y00[xo + c] + (uint32_t)y00[xo + 4u + c]
					             + (uint32_t)y01[xo + c] + (uint32_t)y01[xo + 4u + c]
					             + (uint32_t)y10[xo + c] + (uint32_t)y10[xo + 4u + c]
					             + (uint32_t)y11[xo + c] + (uint32_t)y11[xo + 4u + c];
					*dst++ = (uint8_t)((sum + 4u) >> 3u);
				}
			}
		}
	}
}

static void gl_quad_add_roots(float a, float b, float c, float max_t, float *ts, int *n)
{
	if (fabsf(a) < 1e-11f) {
		if (fabsf(b) > 1e-7f) {
			float r = -c / b;
			if (r > 0.0f && r < max_t && *n < 6)
				ts[(*n)++] = r;
		}
		return;
	}
	float disc = b * b - 4.0f * a * c;
	if (disc < 0.0f) return;
	float sd = sqrtf(disc);
	float inv2a = 0.5f / a;
	float r0 = (-b - sd) * inv2a;
	float r1 = (-b + sd) * inv2a;
	if (r0 > 0.0f && r0 < max_t && *n < 6) ts[(*n)++] = r0;
	if (r1 > 0.0f && r1 < max_t && *n < 6) ts[(*n)++] = r1;
}

bool nd_seven_gl_cloud_quadric_intersect(const float eye_l[3], const float ray_l[3],
                                         float kappa_e, float kappa_s,
                                         float z_bot, float z_top, float max_dist,
                                         float *out_t_enter, float *out_t_exit)
{
	if (!eye_l || !ray_l || max_dist <= 0.0f || z_top <= z_bot)
		return false;
	float A  = 0.5f * (kappa_e * ray_l[0] * ray_l[0] + kappa_s * ray_l[1] * ray_l[1]);
	float B  = ray_l[2] + kappa_e * eye_l[0] * ray_l[0] + kappa_s * eye_l[1] * ray_l[1];
	float C0 = eye_l[2] + 0.5f * (kappa_e * eye_l[0] * eye_l[0] + kappa_s * eye_l[1] * eye_l[1]);

	float ts[6];
	int n = 0;
	ts[n++] = 0.0f;
	ts[n++] = max_dist;
	gl_quad_add_roots(A, B, C0 - z_bot, max_dist, ts, &n);
	gl_quad_add_roots(A, B, C0 - z_top, max_dist, ts, &n);

	/* Insertion sort <= 6 breakpoints */
	for (int i = 1; i < n; i++) {
		float key = ts[i];
		int j = i - 1;
		while (j >= 0 && ts[j] > key) {
			ts[j + 1] = ts[j];
			j--;
		}
		ts[j + 1] = key;
	}

	bool found = false;
	float t0 = 0.0f, t1 = 0.0f;
	for (int i = 0; i < n - 1; i++) {
		float a_t = ts[i], b_t = ts[i + 1];
		if (b_t - a_t <= 1e-3f) continue;
		float tm = 0.5f * (a_t + b_t);
		float alt = (A * tm + B) * tm + C0;
		if (alt >= z_bot - 1e-2f && alt <= z_top + 1e-2f) {
			if (!found) {
				t0 = a_t;
				t1 = b_t;
				found = true;
			} else if (fabsf(a_t - t1) <= 1e-2f) {
				t1 = b_t;
			} else {
				break;
			}
		} else if (found) {
			break;
		}
	}
	if (!found || t1 <= t0 + 1e-3f)
		return false;
	if (out_t_enter) *out_t_enter = t0;
	if (out_t_exit)  *out_t_exit  = t1;
	return true;
}

uint32_t nd_seven_gl_grid_quads_for_zoom(uint8_t level, float cam_dist)
{
	/* Single unified LOD system: the libareole quadtree determines chunk spatial scale
	 * from 3D camera distance, while every chunk's 3D vertex mesh always matches its
	 * 49x49 heightmap texture 1:1 (48x48 quads) so no heightmap vertices are ever discarded. */
	(void)level;
	(void)cam_dist;
	return GL_GRID_QUADS_HI;
}

uint32_t nd_seven_gl_grid_quads_for_level(uint8_t level)
{
	(void)level;
	return GL_GRID_QUADS_HI;
}

float nd_seven_gl_eval_terrain_detail(float wx, float wy,
                                      float cam_dist, float pixel_footprint,
                                      float *out_grad_x, float *out_grad_y,
                                      float *out_cavity_ao)
{
	static const float freqs[4] = { 0.16f, 0.68f, 1.85f, 6.20f };
	static const float amps[4]  = { 0.28f, 0.25f, 0.22f, 0.16f };
	static const float dFar[4]  = { 9500.0f, 4200.0f, 1850.0f, 720.0f };
	static const float dNear[4] = { 2400.0f, 1050.0f,  420.0f, 165.0f };
	static const uint32_t seeds[4] = {
		0xA341316Cu, 0xC8013EA4u, 0x3891053Bu, 0x85EBCA6Bu
	};

	float fw = (pixel_footprint > 1e-4f) ? pixel_footprint : 1e-4f;
	float h = 0.0f;
	float gx = 0.0f, gy = 0.0f;
	float cavity = 0.0f;
	float prx = wx, pry = wy;
	/* curInvRot column-major 2x2: initially identity */
	float ir00 = 1.0f, ir01 = 0.0f, ir10 = 0.0f, ir11 = 1.0f;

	for (int i = 0; i < 4; i++) {
		float span = dFar[i] - dNear[i];
		float wDist = (dFar[i] - cam_dist) / (span > 1.0f ? span : 1.0f);
		if (wDist < 0.0f) wDist = 0.0f;
		if (wDist > 1.0f) wDist = 1.0f;

		float wNyq = gl_smoothstep(0.92f, 0.28f, freqs[i] * fw);
		float w = wDist * wNyq;
		if (w > 0.001f) {
			float val = 0.0f, ogx = 0.0f, ogy = 0.0f;
			gl_chaff_grad_noise2d(prx * freqs[i], pry * freqs[i], seeds[i], &val, &ogx, &ogy);
			float signedVal = val - 0.5f;
			float wgx = ir00 * ogx + ir10 * ogy;
			float wgy = ir01 * ogx + ir11 * ogy;
			h  += signedVal * amps[i] * w;
			gx += wgx * (freqs[i] * amps[i] * w);
			gy += wgy * (freqs[i] * amps[i] * w);
			if (i >= 1 && val < 0.48f) {
				cavity += (0.48f - val) * (amps[i] * 2.4f * w);
			}
			prx += ogx * (0.08f * w);
			pry += ogy * (0.08f * w);
		}
		/* Rotate pRot by mat2(0.8, -0.6, 0.6, 0.8) */
		float n_prx =  0.8f * prx + 0.6f * pry;
		float n_pry = -0.6f * prx + 0.8f * pry;
		prx = n_prx;
		pry = n_pry;
		/* Update curInvRot = curInvRot * mat2(0.8, 0.6, -0.6, 0.8) */
		float nir00 = ir00 * 0.8f + ir10 * 0.6f;
		float nir01 = ir01 * 0.8f + ir11 * 0.6f;
		float nir10 = ir00 * (-0.6f) + ir10 * 0.8f;
		float nir11 = ir01 * (-0.6f) + ir11 * 0.8f;
		ir00 = nir00; ir01 = nir01; ir10 = nir10; ir11 = nir11;
	}

	if (cavity > 0.45f) cavity = 0.45f;
	if (out_grad_x) *out_grad_x = gx;
	if (out_grad_y) *out_grad_y = gy;
	if (out_cavity_ao) *out_cavity_ao = cavity;
	return h;
}

float nd_seven_gl_eval_water_waves(float wx, float wy,
                                   float anim_time,
                                   float cam_dist, float pixel_footprint,
                                   float *out_grad_x, float *out_grad_y,
                                   float *out_crest)
{
	static const float freqs[6] = { 0.024f, 0.072f, 0.220f, 0.680f, 2.150f, 6.800f };
	static const float amps[6]  = { 1.450f, 0.680f, 0.280f, 0.115f, 0.045f, 0.018f };
	static const float spds[6]  = { 0.180f, 0.340f, 0.620f, 1.050f, 1.750f, 2.800f };
	static const float dFar[6]  = { 65000.0f, 18000.0f, 4800.0f, 1600.0f, 520.0f, 200.0f };
	static const float dNear[6] = { 14000.0f,  3600.0f, 1100.0f,  360.0f, 130.0f,  42.0f };
	static const uint32_t seeds[6] = {
		0x4F434530u, 0x5357454Cu, 0x57415645u, 0x52495050u, 0x46494E45u, 0x53505241u
	};
	static const float dirs[6][2] = {
		{  0.8575f, -0.5145f },
		{  0.6402f, -0.7682f },
		{  0.9285f, -0.3714f },
		{  0.5145f, -0.8575f },
		{  0.7809f, -0.6247f },
		{  0.3714f, -0.9285f }
	};

	float wDirX = 0.7071f, wDirY = -0.7071f, gust = 1.0f, catspaw = 0.5f;
	gl_eval_recursive_wind(wx, wy, anim_time, &wDirX, &wDirY, &gust, &catspaw);
	float wPerpX = -wDirY, wPerpY = wDirX;

	float fw = (pixel_footprint > 1e-4f) ? pixel_footprint : 1e-4f;
	float h = 0.0f;
	float gx = 0.0f, gy = 0.0f;
	float crest = 0.0f;
	float prx = wx, pry = wy;
	float ir00 = 1.0f, ir01 = 0.0f, ir10 = 0.0f, ir11 = 1.0f;

	for (int i = 0; i < 6; i++) {
		float span = dFar[i] - dNear[i];
		float wDist = (dFar[i] - cam_dist) / (span > 1.0f ? span : 1.0f);
		float minDistW = (i == 0) ? 0.35f : 0.0f;
		if (wDist < minDistW) wDist = minDistW;
		if (wDist > 1.0f) wDist = 1.0f;

		float wNyq = gl_smoothstep(0.55f, 0.16f, freqs[i] * fw);
		float gustScale = (i >= 3) ? (0.75f + 0.45f * gust) : (0.85f + 0.20f * gust);
		float w = wDist * wNyq * gustScale;
		if (w > 0.001f) {
			float raw_phase = anim_time * spds[i];
			float phase = raw_phase - 1024.0f * floorf(raw_phase / 1024.0f);
			float warpX = (wDirX * (gust - 0.65f) + wPerpX * (catspaw - 0.5f)) * 0.45f;
			float warpY = (wDirY * (gust - 0.65f) + wPerpY * (catspaw - 0.5f)) * 0.45f;
			float qx = prx * freqs[i] + warpX - dirs[i][0] * phase;
			float qy = pry * freqs[i] + warpY - dirs[i][1] * phase;
			float val = 0.0f, ogx = 0.0f, ogy = 0.0f;
			gl_chaff_grad_noise2d(qx, qy, seeds[i], &val, &ogx, &ogy);
			float signedVal = val - 0.5f;
			float ridge = 0.5f - fabsf(signedVal);
			float shaped = signedVal * 0.65f + (ridge - 0.18f) * 0.35f;
			float dShape = 0.85f + 0.45f * ridge;
			float wgx = ir00 * ogx + ir10 * ogy;
			float wgy = ir01 * ogx + ir11 * ogy;
			h  += shaped * amps[i] * w;
			gx += wgx * (dShape * freqs[i] * amps[i] * w);
			gy += wgy * (dShape * freqs[i] * amps[i] * w);
			if (ridge > 0.22f) {
				crest += (ridge - 0.22f) * (w * ((i <= 2) ? 1.4f : 0.8f));
			}
			prx += ogx * (0.11f * w);
			pry += ogy * (0.11f * w);
		}
		float n_prx =  0.8f * prx + 0.6f * pry;
		float n_pry = -0.6f * prx + 0.8f * pry;
		prx = n_prx;
		pry = n_pry;
		float nir00 = ir00 * 0.8f + ir10 * 0.6f;
		float nir01 = ir01 * 0.8f + ir11 * 0.6f;
		float nir10 = ir00 * (-0.6f) + ir10 * 0.8f;
		float nir11 = ir01 * (-0.6f) + ir11 * 0.8f;
		ir00 = nir00; ir01 = nir01; ir10 = nir10; ir11 = nir11;
	}

	if (crest > 1.0f) crest = 1.0f;
	if (out_grad_x) *out_grad_x = gx;
	if (out_grad_y) *out_grad_y = gy;
	if (out_crest) *out_crest = crest;
	return h;
}

float nd_seven_gl_eval_billboard_lighting(float qx, float qy,
                                          float wx, float wz, float ground_z,
                                          float cam_dist,
                                          float out_normal[3],
                                          float *out_contact_ao)
{
	float c_qx = qx < -1.0f ? -1.0f : (qx > 1.0f ? 1.0f : qx);
	float c_qy = qy <  0.0f ?  0.0f : (qy > 1.0f ? 1.0f : qy);

	float dist_fade = 1.0f - (cam_dist - 900.0f) / 2400.0f;
	if (dist_fade < 0.0f) dist_fade = 0.0f;
	if (dist_fade > 1.0f) dist_fade = 1.0f;

	float g1_v = 0.5f, g1_x = 0.0f, g1_y = 0.0f;
	float g2_v = 0.5f, g2_x = 0.0f, g2_y = 0.0f;
	gl_chaff_grad_noise2d(wx * 0.24f, wz * 0.24f, 0xA341316Cu, &g1_v, &g1_x, &g1_y);
	if (dist_fade > 0.01f) {
		gl_chaff_grad_noise2d(wx * 0.78f + 17.3f, wz * 0.78f - 29.1f, 0xC8013EA4u, &g2_v, &g2_x, &g2_y);
	}

	float micro_h = (g1_v - 0.5f) * 0.26f + (g2_v - 0.5f) * 0.16f * dist_fade;
	float mg_x = g1_x * 0.22f + g2_x * (0.32f * dist_fade);
	float mg_y = g1_y * 0.22f + g2_y * (0.32f * dist_fade);

	float cav1 = (0.48f - g1_v) > 0.0f ? (0.48f - g1_v) * 0.42f : 0.0f;
	float cav2 = (0.48f - g2_v) > 0.0f ? (0.48f - g2_v) * 0.32f * dist_fade : 0.0f;
	float micro_cavity = cav1 + cav2;
	if (micro_cavity > 0.34f) micro_cavity = 0.34f;

	float pnx = c_qx * 0.65f;
	float pny = 0.55f - 0.15f * c_qy;
	float pnz = 0.22f + 0.68f * c_qy;
	float plen = sqrtf(pnx * pnx + pny * pny + pnz * pnz);
	if (plen > 1e-6f) {
		pnx /= plen; pny /= plen; pnz /= plen;
	}

	float nx = pnx - mg_x * 0.38f;
	float ny = pny;
	float nz = pnz - mg_y * 0.38f;
	float nlen = sqrtf(nx * nx + ny * ny + nz * nz);
	if (nlen > 1e-6f) {
		nx /= nlen; ny /= nlen; nz /= nlen;
	}
	if (out_normal) {
		out_normal[0] = nx;
		out_normal[1] = ny;
		out_normal[2] = nz;
	}

	/* Northwest directional sun: normalize(-0.577, -0.577, 0.816) */
	const float lx = -0.509184f;
	const float ly = -0.509184f;
	const float lz =  0.693954f;
	float sun_diff = nx * lx + ny * ly + nz * lz;
	if (sun_diff < 0.0f) sun_diff = 0.0f;

	float cavity_ao = 1.0f + micro_h * 0.28f - micro_cavity;
	if (cavity_ao < 0.68f) cavity_ao = 0.68f;
	if (cavity_ao > 1.15f) cavity_ao = 1.15f;

	float h_above = (wz - ground_z) > 0.0f ? (wz - ground_z) : 0.0f;
	float contact_ao = 0.68f + 0.32f * gl_smoothstep(0.0f, 5.5f, h_above);
	if (out_contact_ao) *out_contact_ao = contact_ao;

	float hemi_sky = 0.5f + 0.5f * nz;
	float amb_luma = 0.55f + 0.18f * hemi_sky;
	float light_scalar = (amb_luma * 0.52f + 1.01f * (0.58f * sun_diff)) * (cavity_ao * contact_ao);
	return 0.22f + 0.78f * light_scalar;
}

void nd_seven_gl_eval_grass_displacement(float wx, float wy,
                                         float anim_time,
                                         float lamb_wx, float lamb_wy,
                                         float *out_shear_x, float *out_shear_y,
                                         float *out_depress_z, float *out_rustle)
{
	/* 1. Continuous 3-octave recursive wind sway (matches ocean waves & terrain meadow gusts) */
	float wDirX = 0.7071f, wDirY = -0.7071f, gust = 1.0f, catspaw = 0.5f;
	gl_eval_recursive_wind(wx, wy, anim_time, &wDirX, &wDirY, &gust, &catspaw);

	/* Per-tuft deterministic phase & stiffness from chaff_hash3 (subtle offset so neighbors move in cohesive waves) */
	uint32_t h = gl_chaff_hash3((int32_t)floorf(wx), (int32_t)floorf(wy), 0, 0x47524153u);
	float phase = (float)(h & 0xFFFFu) * (TWO_PI_F / 65536.0f);
	float stiff = 0.84f + 0.32f * ((float)((h >> 16u) & 0xFFu) / 255.0f);

	float wind_proj = wx * wDirX + wy * wDirY;
	float cross_proj = wx * (-wDirY) + wy * wDirX;
	/* Traveling multi-harmonic wind wave: asymmetric gust surge + elastic springback */
	float wave_arg = anim_time * ND_GRASS_WAVE_FREQ_RAD - wind_proj * 0.038f + cross_proj * 0.012f + phase * 0.22f;
	float primary_wave = sinf(wave_arg) + 0.28f * sinf(wave_arg * 2.0f - 0.65f);
	float gust_env = fmaxf(0.0f, gust - 0.35f);
	float gust_wave = primary_wave * (ND_GRASS_WAVE_AMP * (0.45f + 0.65f * gust));
	float flutter = sinf(anim_time * ND_GRASS_FLUTTER_FREQ_RAD - wind_proj * 0.062f + cross_proj * 0.032f + phase * 0.30f) * (ND_GRASS_FLUTTER_AMP * (0.40f + 0.60f * catspaw));
	float cross_sway = cosf(wave_arg * 0.85f + phase * 0.35f) * (0.16f + 0.22f * catspaw) + flutter * 0.30f;
	float wind_mag = (ND_GRASS_WIND_BASE_LEAN + ND_GRASS_WIND_GUST_SCALE * gust_env + ND_GRASS_WIND_CATSPAW_SCALE * catspaw + gust_wave + flutter) * stiff;
	if (wind_mag < -0.18f) wind_mag = -0.18f;
	float sx = wDirX * wind_mag + (-wDirY) * (cross_sway * stiff);
	float sy = wDirY * wind_mag + ( wDirX) * (cross_sway * stiff);
	/* Kinematic arc depression: when a blade bends horizontally by (sx, sy), its tip dips slightly */
	float bend_sq = sx * sx + sy * sy;
	float dz = bend_sq * 0.075f;
	float rustle = 0.08f * catspaw + 0.14f * fmaxf(0.0f, primary_wave) * gust_env;

	/* 2. Calm physical hoof/ankle contact parting & trampling within R = 11.0 world units
	 *    (no artificial high-frequency sine tremor so blades close to the Lamb never tremble) */
	float dx = wx - lamb_wx;
	float dy = wy - lamb_wy;
	float d2 = dx * dx + dy * dy;
	const float R_touch = 11.0f;
	if (d2 < R_touch * R_touch) {
		float dist = sqrtf(d2);
		float t = 1.0f - (dist / R_touch);
		float weight = t * t * (3.0f - 2.0f * t); /* smoothstep falloff */
		float rx = 0.0f, ry = 0.0f;
		if (dist > 0.25f) {
			rx = dx / dist;
			ry = dy / dist;
		} else {
			/* Directly underfoot: part deterministically left or right based on tuft hash */
			rx = ((h & 1u) != 0u) ? 1.0f : -1.0f;
			ry = -0.25f;
		}
		sx += rx * (2.55f * weight);
		sy += ry * (0.85f * weight);
		dz += 2.25f * weight;
		rustle += 0.50f * weight;
		/* Prevent blades North of the Lamb from bending southward into the Lamb's body */
		if (dy < 0.5f && sy > 0.35f)
			sy = 0.35f;
	}

	/* 3. Smooth over-damped viscoelastic footstep wake trail recovery over 1.4s (zero cosine ringing) */
	int wake_n = 0;
	const nd_grass_wake_step_t *wake = nd_seven_get_wake_steps(&wake_n);
	if (wake && wake_n > 0) {
		const float R_wake = 11.5f;
		const float tau_max = 1.40f;
		for (int i = 0; i < wake_n; i++) {
			if (!wake[i].valid)
				continue;
			float tau = anim_time - wake[i].t_sec;
			if (tau < 0.0f || tau > tau_max)
				continue;
			float wdx = wx - wake[i].wx;
			float wdy = wy - wake[i].wy;
			float wd2 = wdx * wdx + wdy * wdy;
			if (wd2 >= R_wake * R_wake)
				continue;
			float wdist = sqrtf(wd2);
			float wt = 1.0f - (wdist / R_wake);
			float w_spatial = wt * wt * (3.0f - 2.0f * wt);
			float decay = expf(-2.2f * tau) * (1.0f - (tau / tau_max));
			/* Over-damped monotonic recovery curve without sign-flipping cosine oscillation */
			float rebound = expf(-1.6f * tau);
			float wrx = (wdist > 0.25f) ? (wdx / wdist) : (((h & 2u) != 0u) ? 1.0f : -1.0f);
			float wry = (wdist > 0.25f) ? (wdy / wdist) : 0.0f;

			/* Blend radial push with actor's step velocity vector */
			float vlen = sqrtf(wake[i].vx * wake[i].vx + wake[i].vy * wake[i].vy);
			float vnx = (vlen > 1.0f) ? (wake[i].vx / vlen) : 0.0f;
			float vny = (vlen > 1.0f) ? (wake[i].vy / vlen) : 0.0f;

			float env = w_spatial * decay;
			sx += (wrx * 1.45f + vnx * 0.65f * rebound) * env;
			sy += (wry * 0.85f + vny * 0.45f * rebound) * env;
			dz += 1.85f * env;
			rustle += 0.40f * env;
		}
	}

	/* Clamp maximum tip shear & depression to preserve natural blade proportions */
	if (sx < -4.2f) sx = -4.2f;
	if (sx >  4.2f) sx =  4.2f;
	if (sy < -3.2f) sy = -3.2f;
	if (sy >  3.2f) sy =  3.2f;
	if (dz <  0.0f) dz =  0.0f;
	if (dz >  3.4f) dz =  3.4f;
	if (rustle > 1.0f) rustle = 1.0f;

	if (out_shear_x)   *out_shear_x   = sx;
	if (out_shear_y)   *out_shear_y   = sy;
	if (out_depress_z) *out_depress_z = dz;
	if (out_rustle)    *out_rustle    = rustle;
}

static inline uint32_t gl_chaff_hash4(int32_t px, int32_t py, int32_t pz, int32_t pw, uint32_t seed)
{
	uint32_t v1 = ((uint32_t)py & 0xFFFFu) | (((uint32_t)px & 0xFFFFu) << 16u);
	uint32_t v2 = ((uint32_t)pz & 0xFFFFu) | (((uint32_t)pw & 0xFFFFu) << 16u);
	uint32_t h = seed + 0x165667B1u + 8u;
	h += v1 * 0xC2B2AE3Du;
	h = gl_chaff_rotl32(h, 17) * 0x27D4EB2Fu;
	h += v2 * 0xC2B2AE3Du;
	h = gl_chaff_rotl32(h, 17) * 0x27D4EB2Fu;
	h ^= h >> 15u;
	h *= 0x85EBCA77u;
	h ^= h >> 13u;
	h *= 0xC2B2AE3Du;
	h ^= h >> 16u;
	return h;
}

static inline void gl_chaff_grad4(int32_t cx, int32_t cy, int32_t cz, int32_t cw, uint32_t seed,
                                  float *gx, float *gy, float *gz, float *gw)
{
	uint32_t h = gl_chaff_hash4(cx, cy, cz, cw, seed);
	float x = (float)((int32_t)( h         & 0xFFu) - 128);
	float y = (float)((int32_t)((h >> 8u)  & 0xFFu) - 128);
	float z = (float)((int32_t)((h >> 16u) & 0xFFu) - 128);
	float w = (float)((int32_t)((h >> 24u) & 0xFFu) - 128);
	float inv = 1.0f / sqrtf(x * x + y * y + z * z + w * w + 1e-6f);
	*gx = x * inv;
	*gy = y * inv;
	*gz = z * inv;
	*gw = w * inv;
}

static inline void gl_chaff_grad_noise4d(float px, float py, float pz, float pw, uint32_t seed,
                                         float *out_val,
                                         float *out_gx, float *out_gy,
                                         float *out_gz, float *out_gw)
{
	float ipx = floorf(px), ipy = floorf(py), ipz = floorf(pz), ipw = floorf(pw);
	float fpx = px - ipx,   fpy = py - ipy,   fpz = pz - ipz,   fpw = pw - ipw;
	float ux = fpx * fpx * fpx * (fpx * (fpx * 6.0f - 15.0f) + 10.0f);
	float uy = fpy * fpy * fpy * (fpy * (fpy * 6.0f - 15.0f) + 10.0f);
	float uz = fpz * fpz * fpz * (fpz * (fpz * 6.0f - 15.0f) + 10.0f);
	float uw = fpw * fpw * fpw * (fpw * (fpw * 6.0f - 15.0f) + 10.0f);
	float dux = 30.0f * fpx * fpx * (fpx * (fpx - 2.0f) + 1.0f);
	float duy = 30.0f * fpy * fpy * (fpy * (fpy - 2.0f) + 1.0f);
	float duz = 30.0f * fpz * fpz * (fpz * (fpz - 2.0f) + 1.0f);
	float duw = 30.0f * fpw * fpw * (fpw * (fpw - 2.0f) + 1.0f);

	int32_t cx = (int32_t)ipx;
	int32_t cy = (int32_t)ipy;
	int32_t cz = (int32_t)ipz;
	int32_t cw = (int32_t)ipw;

	float d[8], ddw[8], gx_w[8], gy_w[8], gz_w[8];
	for (int i = 0; i < 8; i++) {
		int32_t ox = i & 1;
		int32_t oy = (i >> 1) & 1;
		int32_t oz = (i >> 2) & 1;
		float g0x, g0y, g0z, g0w, g1x, g1y, g1z, g1w;
		gl_chaff_grad4(cx + ox, cy + oy, cz + oz, cw,     seed, &g0x, &g0y, &g0z, &g0w);
		gl_chaff_grad4(cx + ox, cy + oy, cz + oz, cw + 1, seed, &g1x, &g1y, &g1z, &g1w);
		float dx = fpx - (float)ox;
		float dy = fpy - (float)oy;
		float dz = fpz - (float)oz;
		float v0 = g0x * dx + g0y * dy + g0z * dz + g0w * fpw;
		float v1 = g1x * dx + g1y * dy + g1z * dz + g1w * (fpw - 1.0f);
		d[i]    = v0 + (v1 - v0) * uw;
		ddw[i]  = (g0w + (g1w - g0w) * uw) + (v1 - v0) * duw;
		gx_w[i] = g0x + (g1x - g0x) * uw;
		gy_w[i] = g0y + (g1y - g0y) * uw;
		gz_w[i] = g0z + (g1z - g0z) * uw;
	}

	float c0 = d[0] + (d[4] - d[0]) * uz;
	float c1 = d[1] + (d[5] - d[1]) * uz;
	float c2 = d[2] + (d[6] - d[2]) * uz;
	float c3 = d[3] + (d[7] - d[3]) * uz;

	float b0 = c0 + (c2 - c0) * uy;
	float b1 = c1 + (c3 - c1) * uy;
	float raw = 0.5f + 0.72f * (b0 + (b1 - b0) * ux);
	if (raw < 0.0f) raw = 0.0f;
	if (raw > 1.0f) raw = 1.0f;
	if (out_val) *out_val = raw;

	if (out_gx) {
		float gx_z0 = gx_w[0] + (gx_w[4] - gx_w[0]) * uz;
		float gx_z1 = gx_w[1] + (gx_w[5] - gx_w[1]) * uz;
		float gx_z2 = gx_w[2] + (gx_w[6] - gx_w[2]) * uz;
		float gx_z3 = gx_w[3] + (gx_w[7] - gx_w[3]) * uz;
		float gx_y0 = gx_z0 + (gx_z2 - gx_z0) * uy;
		float gx_y1 = gx_z1 + (gx_z3 - gx_z1) * uy;
		float gx_interp = gx_y0 + (gx_y1 - gx_y0) * ux;
		*out_gx = 0.72f * (gx_interp + (b1 - b0) * dux);
	}
	if (out_gy) {
		float gy_z0 = gy_w[0] + (gy_w[4] - gy_w[0]) * uz;
		float gy_z1 = gy_w[1] + (gy_w[5] - gy_w[1]) * uz;
		float gy_z2 = gy_w[2] + (gy_w[6] - gy_w[2]) * uz;
		float gy_z3 = gy_w[3] + (gy_w[7] - gy_w[3]) * uz;
		float gy_y0 = gy_z0 + (gy_z2 - gy_z0) * uy;
		float gy_y1 = gy_z1 + (gy_z3 - gy_z1) * uy;
		float gy_interp = gy_y0 + (gy_y1 - gy_y0) * ux;
		float dy0 = c2 - c0;
		float dy1 = c3 - c1;
		*out_gy = 0.72f * (gy_interp + (dy0 + (dy1 - dy0) * ux) * duy);
	}
	if (out_gz) {
		float gz_z0 = gz_w[0] + (gz_w[4] - gz_w[0]) * uz;
		float gz_z1 = gz_w[1] + (gz_w[5] - gz_w[1]) * uz;
		float gz_z2 = gz_w[2] + (gz_w[6] - gz_w[2]) * uz;
		float gz_z3 = gz_w[3] + (gz_w[7] - gz_w[3]) * uz;
		float gz_y0 = gz_z0 + (gz_z2 - gz_z0) * uy;
		float gz_y1 = gz_z1 + (gz_z3 - gz_z1) * uy;
		float gz_interp = gz_y0 + (gz_y1 - gz_y0) * ux;
		float dz00 = d[4] - d[0], dz10 = d[5] - d[1];
		float dz01 = d[6] - d[2], dz11 = d[7] - d[3];
		float dz0  = dz00 + (dz10 - dz00) * ux;
		float dz1  = dz01 + (dz11 - dz01) * ux;
		*out_gz = 0.72f * (gz_interp + (dz0 + (dz1 - dz0) * uy) * duz);
	}
	if (out_gw) {
		float dw0 = ddw[0] + (ddw[4] - ddw[0]) * uz;
		float dw1 = ddw[1] + (ddw[5] - ddw[1]) * uz;
		float dw2 = ddw[2] + (ddw[6] - ddw[2]) * uz;
		float dw3 = ddw[3] + (ddw[7] - ddw[3]) * uz;
		float dwb0 = dw0 + (dw2 - dw0) * uy;
		float dwb1 = dw1 + (dw3 - dw1) * uy;
		*out_gw = 0.72f * (dwb0 + (dwb1 - dwb0) * ux);
	}
}

static inline float gl_cloud_remap(float v, float l0, float h0, float l1, float h1)
{
	float denom = (h0 - l0) > 1e-5f ? (h0 - l0) : 1e-5f;
	return l1 + (v - l0) * (h1 - l1) / denom;
}

static inline void gl_eval_synoptic_macro_cloud_fw(float wx, float wy, float anim_time, float humidity, float fw,
                                                   float *out_coverage, float *out_front,
                                                   float *out_warp_x, float *out_warp_y,
                                                   float *out_gx, float *out_gy, float *out_gt,
                                                   float *out_high_cirrus)
{
	const float prevDirX = 0.741f, prevDirY = -0.671f;
	const float prevPerpX = 0.671f, prevPerpY = 0.741f;
	float raw_tFlow = anim_time * 0.005f;
	float tFlow = raw_tFlow - 1024.0f * floorf(raw_tFlow / 1024.0f);

	float psi0_v = 0.5f, psi0_gx = 0.0f, psi0_gy = 0.0f;
	gl_chaff_grad_noise2d(wx * 0.00000042f - prevDirX * (tFlow * 0.40f),
	                      wy * 0.00000042f - prevDirY * (tFlow * 0.40f),
	                      0x4359434Cu, &psi0_v, &psi0_gx, &psi0_gy);

	float psi1_v = 0.5f, psi1_gx = 0.0f, psi1_gy = 0.0f;
	gl_chaff_grad_noise2d(wx * 0.00000180f + (-psi0_gy) * 0.30f - prevDirX * tFlow,
	                      wy * 0.00000180f + psi0_gx * 0.30f - prevDirY * tFlow,
	                      0x5354524Du, &psi1_v, &psi1_gx, &psi1_gy);

	float synWarpX = (-psi0_gy) * 260000.0f + (-psi1_gy) * 68000.0f;
	float synWarpY = psi0_gx * 260000.0f + psi1_gx * 68000.0f;

	float pDispX = wx + synWarpX;
	float pDispY = wy + synWarpY;
	float pAlong = pDispX * prevDirX + pDispY * prevDirY;
	float pCross = pDispX * prevPerpX + pDispY * prevPerpY;
	float pZonalX = prevDirX * (pAlong * 0.65f) + prevPerpX * (pCross * 1.55f);
	float pZonalY = prevDirY * (pAlong * 0.65f) + prevPerpY * (pCross * 1.55f);

	float raw_p0 = anim_time * 0.004f;
	float phase0 = raw_p0 - 1024.0f * floorf(raw_p0 / 1024.0f);
	float raw_p1 = anim_time * 0.008f;
	float phase1 = raw_p1 - 1024.0f * floorf(raw_p1 / 1024.0f);

	float oct0_v = 0.5f, oct0_gx = 0.0f, oct0_gy = 0.0f;
	gl_chaff_grad_noise2d(pZonalX * 0.00000050f - prevDirX * phase0,
	                      pZonalY * 0.00000050f - prevDirY * phase0,
	                      0x434C4430u, &oct0_v, &oct0_gx, &oct0_gy);

	float wOct1 = gl_smoothstep(0.32f, 0.08f, 0.00000240f * fw);
	float pRotX = 0.8f * pZonalX + 0.6f * pZonalY;
	float pRotY = -0.6f * pZonalX + 0.8f * pZonalY;
	float oct1_v = 0.5f, oct1_gx = 0.0f, oct1_gy = 0.0f;
	if (wOct1 > 0.001f) {
		gl_chaff_grad_noise2d(pRotX * 0.00000240f - prevDirX * phase1,
		                      pRotY * 0.00000240f - prevDirY * phase1,
		                      0x434C4431u, &oct1_v, &oct1_gx, &oct1_gy);
	}

	float r0 = 1.0f - 2.0f * fabsf(oct0_v - 0.5f);
	float r1 = 1.0f - 2.0f * fabsf(oct1_v - 0.5f);
	float front0 = oct0_v + (r0 - 0.68f) * 0.28f;
	float front1 = oct1_v + (r1 - 0.68f) * 0.24f;

	float synFront = 0.50f + (front0 - 0.50f) * 0.72f + (front1 - 0.50f) * (0.54f * wOct1) + (psi0_v - 0.50f) * 0.34f;
	float wReg2 = gl_smoothstep(0.26f, 0.07f, 0.00001150f * fw);
	float wReg3 = gl_smoothstep(0.24f, 0.06f, 0.00005500f * fw);
	float maxFront = synFront + 0.116f * wReg2 + 0.070f * wReg3;
	if (maxFront <= (0.555f - 0.105f * humidity) && (!out_high_cirrus || (synFront + 0.118f) <= (0.56f - 0.08f * humidity))) {
		if (out_coverage) *out_coverage = 0.0f;
		if (out_front) *out_front = synFront;
		if (out_warp_x) *out_warp_x = synWarpX;
		if (out_warp_y) *out_warp_y = synWarpY;
		if (out_gx) *out_gx = 0.0f;
		if (out_gy) *out_gy = 0.0f;
		if (out_gt) *out_gt = 0.0f;
		if (out_high_cirrus) *out_high_cirrus = 0.0f;
		return;
	}

	/* High-troposphere jet-stream cirrus & frontal outflow shield (evaluated only when requested for Deck 3) */
	float highCirrus = 0.0f;
	if (out_high_cirrus) {
		float pJetX = 0.8f * pZonalX + 0.6f * pZonalY + 38000.0f;
		float pJetY = -0.6f * pZonalX + 0.8f * pZonalY - 52000.0f;
		float cir0_v = 0.5f, cir0_gx = 0.0f, cir0_gy = 0.0f;
		gl_chaff_grad_noise2d(pJetX * 0.00000115f - prevDirX * (phase1 * 1.35f),
		                      pJetY * 0.00000115f - prevDirY * (phase1 * 1.35f),
		                      0x43495230u, &cir0_v, &cir0_gx, &cir0_gy);
		float wCir = gl_smoothstep(0.32f, 0.08f, 0.00000380f * fw);
		float cir_v = 0.5f, cir_gx = 0.0f, cir_gy = 0.0f;
		if (wCir > 0.001f) {
			float pIrotX = 0.8f * pJetX + 0.6f * pJetY;
			float pIrotY = -0.6f * pJetX + 0.8f * pJetY;
			gl_chaff_grad_noise2d(pIrotX * 0.00000380f - prevDirX * (phase1 * 1.45f),
			                      pIrotY * 0.00000380f - prevDirY * (phase1 * 1.45f),
			                      0x43495252u, &cir_v, &cir_gx, &cir_gy);
		}
		float cirStreak0 = 1.0f - 2.0f * fabsf(cir0_v - 0.5f);
		float cirStreak1 = 1.0f - 2.0f * fabsf(cir_v - 0.5f);
		float cirStreak  = (1.0f - wCir) * cirStreak0 + wCir * (0.55f * cirStreak0 + 0.45f * cirStreak1);
		highCirrus = gl_smoothstep(0.56f - 0.08f * humidity, 0.78f - 0.06f * humidity, synFront + (cirStreak - 0.56f) * 0.26f) *
		             gl_smoothstep(0.42f, 0.84f, cirStreak);
	}

	/* Regional squall bands (87 km & 18 km) band-limited strictly by screen-space Nyquist fw */
	if (wReg2 > 0.001f) {
		float raw_p2 = anim_time * 0.015f;
		float phase2 = raw_p2 - 1024.0f * floorf(raw_p2 / 1024.0f);
		float rx1 = 0.8f * wx + 0.6f * wy;
		float ry1 = -0.6f * wx + 0.8f * wy;
		float p2x = 0.8f * rx1 + 0.6f * ry1 + synWarpX * 0.14f;
		float p2y = -0.6f * rx1 + 0.8f * ry1 + synWarpY * 0.14f;
		float oct2_v = 0.5f, oct2_gx = 0.0f, oct2_gy = 0.0f;
		gl_chaff_grad_noise2d(p2x * 0.00001150f - prevDirX * phase2,
		                      p2y * 0.00001150f - prevDirY * phase2,
		                      0x434C4432u, &oct2_v, &oct2_gx, &oct2_gy);
		float r2 = 1.0f - 2.0f * fabsf(oct2_v - 0.5f);
		float squall2 = (oct2_v - 0.50f) + (r2 - 0.68f) * 0.25f;
		synFront += squall2 * 0.20f * wReg2;
		if (wReg3 > 0.001f) {
			float raw_p3 = anim_time * 0.028f;
			float phase3 = raw_p3 - 1024.0f * floorf(raw_p3 / 1024.0f);
			float p3x = 0.8f * p2x + 0.6f * p2y + synWarpX * 0.04f;
			float p3y = -0.6f * p2x + 0.8f * p2y + synWarpY * 0.04f;
			float oct3_v = 0.5f, oct3_gx = 0.0f, oct3_gy = 0.0f;
			gl_chaff_grad_noise2d(p3x * 0.00005500f - prevDirX * phase3,
			                      p3y * 0.00005500f - prevDirY * phase3,
			                      0x434C4433u, &oct3_v, &oct3_gx, &oct3_gy);
			synFront += (oct3_v - 0.50f) * 0.14f * wReg3;
		}
	}

	float synRegime = gl_smoothstep(0.555f - 0.105f * humidity, 0.750f - 0.075f * humidity, synFront);
	float overcastShield = gl_smoothstep(0.66f, 0.90f, synRegime);

	/* Analytical band-limited synoptic envelope (orbitCov) matches the spatial expectation of cumulusCells
	 * so zooming across the mesoscale Nyquist threshold (wMeso0: 1 -> 0) never causes a coverage jump! */
	float orbitArm = gl_smoothstep(0.32f, 0.72f, r0);
	float cellExpect = synRegime * (0.42f + 0.44f * synRegime) * (0.78f + 0.22f * orbitArm);
	float orbitCov = (1.0f - overcastShield) * cellExpect + overcastShield * (synRegime * (0.80f + 0.20f * orbitArm));
	float macroCoverage = orbitCov;

	float wMeso0 = gl_smoothstep(0.22f, 0.06f, 0.00024000f * fw);
	if (wMeso0 > 0.001f && synRegime > 0.005f) {
		float raw_pM0 = anim_time * 0.048f;
		float phaseM0 = raw_pM0 - 1024.0f * floorf(raw_pM0 / 1024.0f);
		float dotDir  = wx * prevDirX + wy * prevDirY;
		float dotPerp = wx * prevPerpX + wy * prevPerpY;
		float pStreetX = dotDir * 0.72f + synWarpX * 0.012f;
		float pStreetY = dotPerp * 1.32f + synWarpY * 0.012f;

		float m0_v = 0.5f, m0_gx = 0.0f, m0_gy = 0.0f;
		gl_chaff_grad_noise2d(pStreetX * 0.00024000f - prevDirX * phaseM0,
		                      pStreetY * 0.00024000f - prevDirY * phaseM0,
		                      0x4D455330u, &m0_v, &m0_gx, &m0_gy);

		float wMeso0b = gl_smoothstep(0.22f, 0.06f, 0.00038000f * fw);
		float m1_v = 0.5f, m1_gx = 0.0f, m1_gy = 0.0f;
		if (wMeso0b > 0.001f) {
			float rotStreetX = 0.8f * pStreetX + 0.6f * pStreetY;
			float rotStreetY = -0.6f * pStreetX + 0.8f * pStreetY;
			float raw_pM0b = phaseM0 * 1.25f;
			float phaseM0b = raw_pM0b - 1024.0f * floorf(raw_pM0b / 1024.0f);
			gl_chaff_grad_noise2d(rotStreetX * 0.00038000f - prevDirX * phaseM0b,
			                      rotStreetY * 0.00038000f - prevDirY * phaseM0b,
			                      0x4D455331u, &m1_v, &m1_gx, &m1_gy);
		}

		float b0 = 1.0f - 2.0f * fabsf(m0_v - 0.5f);
		float b1 = 1.0f - 2.0f * fabsf(m1_v - 0.5f);
		float b1SqMix = (1.0f - wMeso0b) * 0.25f + wMeso0b * (b1 * b1);
		float cellField = 0.42f * m0_v + 0.34f * (b0 * b0) + 0.24f * b1SqMix;

		float wMeso1 = gl_smoothstep(0.20f, 0.05f, 0.00090000f * fw);
		if (wMeso1 > 0.001f) {
			float raw_pM1 = anim_time * 0.082f;
			float phaseM1 = raw_pM1 - 1024.0f * floorf(raw_pM1 / 1024.0f);
			float rx2 = 0.8f * (0.8f * wx + 0.6f * wy) + 0.6f * (-0.6f * wx + 0.8f * wy);
			float ry2 = -0.6f * (0.8f * wx + 0.6f * wy) + 0.8f * (-0.6f * wx + 0.8f * wy);
			float m2_v = 0.5f, m2_gx = 0.0f, m2_gy = 0.0f;
			gl_chaff_grad_noise2d(rx2 * 0.00090000f - prevDirX * phaseM1,
			                      ry2 * 0.00090000f - prevDirY * phaseM1,
			                      0x4D455332u, &m2_v, &m2_gx, &m2_gy);
			float b2 = 1.0f - 2.0f * fabsf(m2_v - 0.5f);
			float m2Cell = 0.45f * m2_v + 0.55f * (b2 * b2);
			cellField = (1.0f - 0.55f * wMeso1) * cellField + (0.55f * wMeso1) * (0.55f * cellField + 0.45f * m2Cell);
		}

		float threshLo = (1.0f - synRegime) * 0.56f + synRegime * 0.20f;
		float threshHi = (1.0f - synRegime) * 0.76f + synRegime * 0.50f;
		float cumulusCells = gl_smoothstep(threshLo, threshHi, cellField) * gl_smoothstep(0.02f, 0.18f, synRegime);
		float structuredCov = (1.0f - overcastShield) * cumulusCells + overcastShield * (cumulusCells > (synRegime * 0.88f + 0.12f * cellField) ? cumulusCells : (synRegime * 0.88f + 0.12f * cellField));
		macroCoverage = (1.0f - wMeso0) * orbitCov + wMeso0 * structuredCov;
	}

	if (out_coverage) *out_coverage = macroCoverage;
	if (out_front) *out_front = synFront;
	if (out_warp_x) *out_warp_x = synWarpX;
	if (out_warp_y) *out_warp_y = synWarpY;
	if (out_gx) *out_gx = (oct0_gx * 0.00000050f * 0.72f + oct1_gx * 0.00000240f * 0.54f * wOct1);
	if (out_gy) *out_gy = (oct0_gy * 0.00000050f * 0.72f + oct1_gy * 0.00000240f * 0.54f * wOct1);
	if (out_gt) *out_gt = -((oct0_gx * prevDirX + oct0_gy * prevDirY) * 0.004f * 0.72f +
	                        (oct1_gx * prevDirX + oct1_gy * prevDirY) * 0.008f * 0.54f * wOct1);
	if (out_high_cirrus) *out_high_cirrus = highCirrus;
}

static inline void gl_eval_synoptic_macro_cloud(float wx, float wy, float anim_time, float humidity,
                                                float *out_coverage, float *out_front,
                                                float *out_warp_x, float *out_warp_y,
                                                float *out_gx, float *out_gy, float *out_gt)
{
	gl_eval_synoptic_macro_cloud_fw(wx, wy, anim_time, humidity, 12000.0f,
	                                out_coverage, out_front, out_warp_x, out_warp_y,
	                                out_gx, out_gy, out_gt, NULL);
}

float nd_seven_gl_eval_zoom_cloud_density(float wx, float wy, float wz,
                                          float anim_time,
                                          float climate_humidity,
                                          float cam_dist, float pixel_footprint,
                                          float *out_grad_x, float *out_grad_y,
                                          float *out_grad_z, float *out_grad_t)
{
	if (out_grad_x) *out_grad_x = 0.0f;
	if (out_grad_y) *out_grad_y = 0.0f;
	if (out_grad_z) *out_grad_z = 0.0f;
	if (out_grad_t) *out_grad_t = 0.0f;
	const float zBase = 420.0f;
	const float zTop  = 2820.0f;
	if (wz <= zBase || wz >= zTop)
		return 0.0f;

	float hNorm = (wz - zBase) / (zTop - zBase);
	float fw = (pixel_footprint > 1e-4f) ? pixel_footprint : 1e-4f;
	float deckIdx = (hNorm < 0.31f) ? 0.0f : ((hNorm < 0.68f) ? 1.0f : 2.0f);

	/* Evaluate 3D wind shear */
	float atmoDirX = 0.741f, atmoDirY = -0.671f;
	float shearX = 0.0f, shearY = 0.0f;
	float atmoGust = 0.65f, updraft = 0.50f;
	chaff_glsl_ref_eval_wind_3d(wx, wy, 380.0f + hNorm * 600.0f, 380.0f, 980.0f, anim_time, fw,
	                            &atmoDirX, &atmoDirY, &shearX, &shearY, &atmoGust, &updraft);

	float windPerpX = -atmoDirY;
	float windPerpY =  atmoDirX;
	int upperDeck = (hNorm >= 0.68f);
	float shearedX = wx - shearX - atmoDirX * (hNorm * 1400.0f);
	float shearedY = wy - shearY - atmoDirY * (hNorm * 1400.0f);
	float synQueryX = upperDeck
	                ? (shearedX - atmoDirX * 14000.0f + windPerpX * 8000.0f)
	                : ((deckIdx > 0.5f) ? (shearedX + windPerpX * 3200.0f + atmoDirX * 2000.0f) : shearedX);
	float synQueryY = upperDeck
	                ? (shearedY - atmoDirY * 14000.0f + windPerpY * 8000.0f)
	                : ((deckIdx > 0.5f) ? (shearedY + windPerpY * 3200.0f + atmoDirY * 2000.0f) : shearedY);
	float synFw = upperDeck ? fmaxf(fw, 4000.0f) : fw;

	float macroCoverage = 0.0f, contFront = 0.0f, synWarp0X = 0.0f, synWarp0Y = 0.0f;
	float mgx = 0.0f, mgy = 0.0f, mgt = 0.0f, highCirrus = 0.0f;
	gl_eval_synoptic_macro_cloud_fw(synQueryX, synQueryY, anim_time, climate_humidity, synFw,
	                                &macroCoverage, &contFront, &synWarp0X, &synWarp0Y,
	                                &mgx, &mgy, &mgt, upperDeck ? &highCirrus : NULL);
	if (macroCoverage <= 0.008f && (!upperDeck || highCirrus <= 0.015f))
		return 0.0f;

	float coreStrength = gl_smoothstep(0.06f, 0.75f, macroCoverage);

	/* 1. Deck 1: Lower Cumulus / Stratocumulus Deck */
	float hTop1 = 0.14f + 0.13f * coreStrength;
	float deck1Prof = gl_smoothstep(0.0f, 0.045f, hNorm) * (1.0f - gl_smoothstep(hTop1 - 0.06f, hTop1, hNorm));
	float deck1Cov  = macroCoverage * deck1Prof;

	/* 2. Deck 2: cross-wind (4.8 km) / along-wind (3.2 km) displaced altocumulus bands + deepest convective cores */
	float altoShelfProf = gl_smoothstep(0.35f, 0.42f, hNorm) * (1.0f - gl_smoothstep(0.57f, 0.64f, hNorm));
	float altoShelf = 0.0f;
	if (altoShelfProf > 0.001f) {
		float wAlto = gl_smoothstep(0.30f, 0.08f, 0.00001600f * fw);
		float raw_altoPhase = anim_time * 0.018f;
		float altoPhase = raw_altoPhase - 1024.0f * floorf(raw_altoPhase / 1024.0f);
		float altoPosX = 0.60f * synQueryX - 0.80f * synQueryY + synWarp0X * 0.18f + 42000.0f;
		float altoPosY = 0.80f * synQueryX + 0.60f * synQueryY + synWarp0Y * 0.18f - 31000.0f;
		float alto0_v = 0.55f, alto0_gx = 0.0f, alto0_gy = 0.0f;
		gl_chaff_grad_noise2d(altoPosX * 0.00000145f - atmoDirX * (altoPhase * 0.45f),
		                      altoPosY * 0.00000145f - atmoDirY * (altoPhase * 0.45f),
		                      0x414C5430u, &alto0_v, &alto0_gx, &alto0_gy);
		float alto_v = 0.55f, alto_gx = 0.0f, alto_gy = 0.0f;
		if (wAlto > 0.001f) {
			gl_chaff_grad_noise2d(altoPosX * 0.00001600f - atmoDirX * altoPhase,
			                      altoPosY * 0.00001600f - atmoDirY * altoPhase,
			                      0x414C544Fu, &alto_v, &alto_gx, &alto_gy);
		}
		float altoWave0 = gl_smoothstep(0.36f, 0.78f, 1.0f - 2.0f * fabsf(alto0_v - 0.5f));
		float altoWave1 = gl_smoothstep(0.34f, 0.74f, 1.0f - 2.0f * fabsf(alto_v - 0.5f));
		float altoRibs  = (1.0f - wAlto) * altoWave0 + wAlto * (0.52f * altoWave0 + 0.48f * altoWave1);
		altoShelf = altoShelfProf * gl_smoothstep(0.18f, 0.56f, contFront - (0.44f - 0.06f * climate_humidity)) * altoRibs * 0.72f;
	}
	float deepCore  = gl_smoothstep(0.62f, 0.92f, coreStrength);
	float towerTopH = 0.24f + (0.86f - 0.24f) * (deepCore * (0.45f + 0.55f * updraft));
	float towerProf = gl_smoothstep(0.12f, 0.24f, hNorm) * (1.0f - gl_smoothstep(towerTopH - 0.10f, towerTopH, hNorm)) * deepCore;
	float deck2Cov  = fmaxf(macroCoverage * towerProf, altoShelf);

	/* 3. Deck 3: downwind anvil plumes & jet-stream cirrus (reuses single synCloud evaluation at synQueryXY) */
	float anvilProf = gl_smoothstep(0.72f, 0.81f, hNorm) * (1.0f - gl_smoothstep(0.92f, 1.0f, hNorm));
	float anvilCov  = gl_smoothstep(0.42f, 0.80f, macroCoverage) * (0.50f + 0.50f * updraft) * 0.74f;
	float deck3Cov  = fmaxf(anvilCov, highCirrus * 0.70f) * anvilProf;

	float multiDeck = fmaxf(deck1Cov, fmaxf(deck2Cov, deck3Cov));
	float vProf     = fminf(1.0f, fmaxf(0.0f, fmaxf(deck1Prof, fmaxf(fmaxf(altoShelfProf, towerProf), anvilProf))));
	float baseCloud = gl_cloud_remap(multiDeck, 0.06f * (1.0f - 0.50f * coreStrength), 0.78f, 0.0f, 1.0f);
	if (baseCloud < 0.0f) baseCloud = 0.0f;
	if (baseCloud > 1.0f) baseCloud = 1.0f;
	if (baseCloud <= 0.001f)
		return 0.0f;

	/* 4-octave geometric 4D turbulence cascade with sliding 2-octave band-pass (mirrors s_fs_cloud) */
	static const float freqs[4] = { 0.00026000f, 0.00160000f, 0.00960000f, 0.05800000f };
	static const float spds[4]  = { 0.045f, 0.095f, 0.195f, 0.360f };
	static const uint32_t seeds[4] = { 0x434C4434u, 0x434C4435u, 0x434C4437u, 0x434C4438u };
	static const float zScales[4] = { 1.00f, 1.55f, 2.40f, 3.70f };
	static const float wScales[4] = { 1.00f, 1.50f, 2.25f, 3.40f };
	static const float amps[4]    = { 0.38f, 0.30f, 0.24f, 0.18f };
	static const float dFar[4]    = { 650000.0f, 95000.0f, 14000.0f, 2200.0f };
	static const float dNear[4]   = { 150000.0f, 18000.0f,  2600.0f,  380.0f };
	static const float wDispScale[4] = { 1.00f, 0.50f, 0.22f, 0.08f };

	float wRaw[4];
	for (int j = 0; j < 4; j++) {
		float span = dFar[j] - dNear[j];
		float wDist = (dFar[j] - cam_dist) / (span > 1.0f ? span : 1.0f);
		if (wDist < 0.0f) wDist = 0.0f;
		if (wDist > 1.0f) wDist = 1.0f;
		float wNyq = gl_smoothstep(3.50f, 10.00f, (1.0f / freqs[j]) / fw);
		wRaw[j] = wDist * wNyq;
	}

	float raw_wBoil = anim_time * (0.055f * atmoGust * updraft);
	float wBoil = raw_wBoil - 512.0f * floorf(raw_wBoil / 512.0f);
	float zBaseSlice = hNorm * 38.0f + deckIdx * 23.0f;
	float wSlice     = hNorm * 22.0f + deckIdx * 15.7f;

	float df_dx = mgx;
	float df_dy = mgy;
	float df_dz = 0.0f;
	float df_dt = mgt;

	float prx = synQueryX;
	float pry = synQueryY;
	float cur_rot_00 = 1.0f, cur_rot_01 = 0.0f;
	float cur_rot_10 = 0.0f, cur_rot_11 = 1.0f;

	float billowFbm = 0.0f;
	float turretShift = 0.0f;
	float sliceAngle = hNorm * 18.0f + deckIdx * 2.1f;
	float sliceDirX = cosf(sliceAngle);
	float sliceDirY = sinf(sliceAngle);

	for (int j = 0; j < 4; j++) {
		float wBand = (j < 2) ? (1.0f - wRaw[j + 2]) : 1.0f;
		float w = wRaw[j] * wBand;
		if (w > 0.002f) {
			float raw_phase = anim_time * spds[j];
			float phase = raw_phase - 1024.0f * floorf(raw_phase / 1024.0f);
			float qx = (prx + shearX * wDispScale[j]) * freqs[j] - atmoDirX * phase;
			float qy = (pry + shearY * wDispScale[j]) * freqs[j] - atmoDirY * phase;
			float n = 0.5f, ogx = 0.0f, ogy = 0.0f, ogz = 0.0f, ogw = 0.0f;
			gl_chaff_grad_noise4d(qx, qy, zBaseSlice * zScales[j], wBoil * wScales[j] + wSlice * zScales[j],
			                      seeds[j], &n, &ogx, &ogy, &ogz, &ogw);
			float billow = 1.0f - 2.0f * fabsf(n - 0.5f);
			float effAmp = amps[j] * w;
			billowFbm += (billow - 0.50f) * effAmp;

			float dBill = 4.0f * (0.5f - n);
			float wgx = cur_rot_00 * ogx + cur_rot_10 * ogy;
			float wgy = cur_rot_01 * ogx + cur_rot_11 * ogy;
			turretShift += (wgx * sliceDirX + wgy * sliceDirY) * effAmp;
			df_dx += wgx * (dBill * freqs[j] * effAmp);
			df_dy += wgy * (dBill * freqs[j] * effAmp);
			df_dz += ogz * (dBill * 0.0160f * zScales[j] * effAmp);
			df_dt += (ogw * (0.055f * atmoGust * updraft * wScales[j]) - (ogx * atmoDirX + ogy * atmoDirY) * spds[j]) * (dBill * effAmp);
		}

		float n_prx = 0.8f * prx + 0.6f * pry;
		float n_pry = -0.6f * prx + 0.8f * pry;
		prx = n_prx;
		pry = n_pry;
		float nir00 = cur_rot_00 * 0.8f + cur_rot_10 * 0.6f;
		float nir01 = cur_rot_01 * 0.8f + cur_rot_11 * 0.6f;
		float nir10 = cur_rot_00 * (-0.6f) + cur_rot_10 * 0.8f;
		float nir11 = cur_rot_01 * (-0.6f) + cur_rot_11 * 0.8f;
		cur_rot_00 = nir00; cur_rot_01 = nir01;
		cur_rot_10 = nir10; cur_rot_11 = nir11;
	}

	/* 3D height-erosive sculpting (mirrors s_fs_cloud): broad base, eroded crown + lateral 4D turret shift */
	float hLocal = (hNorm < 0.31f) ? fminf(1.0f, fmaxf(0.0f, hNorm / fmaxf(hTop1, 0.05f)))
	             : ((hNorm < 0.68f) ? fminf(1.0f, fmaxf(0.0f, (hNorm - 0.35f) / 0.29f))
	                                : fminf(1.0f, fmaxf(0.0f, (hNorm - 0.72f) / 0.28f)));
	float crownRamp  = gl_smoothstep(0.18f, 0.95f, hLocal);
	float crownErode = crownRamp * (0.32f * (1.0f - 0.45f * coreStrength));
	float sculpted   = baseCloud - crownErode + (billowFbm + turretShift * 0.48f) * (0.66f + 0.58f * hLocal);
	sculpted = fminf(1.0f, fmaxf(0.0f, sculpted));
	df_dz -= crownRamp * 0.05f * (1.0f - 0.45f * coreStrength);

	float dens = gl_smoothstep(0.04f, 0.58f, sculpted) * vProf;
	if (out_grad_x) *out_grad_x = df_dx * vProf;
	if (out_grad_y) *out_grad_y = df_dy * vProf;
	if (out_grad_z) *out_grad_z = df_dz * vProf;
	if (out_grad_t) *out_grad_t = df_dt * vProf;
	return dens;
}

float nd_seven_gl_eval_cloud_density(float wx, float wy, float wz,
                                     float anim_time,
                                     float climate_humidity,
                                     float *out_grad_x, float *out_grad_y,
                                     float *out_grad_z, float *out_grad_t)
{
	return nd_seven_gl_eval_zoom_cloud_density(wx, wy, wz, anim_time, climate_humidity,
	                                          450.0f, 0.25f,
	                                          out_grad_x, out_grad_y, out_grad_z, out_grad_t);
}

void nd_seven_gl_eval_celestial_dirs(float t_sec,
                                     float out_sun_dir[3],
                                     float out_moon_dir[3])
{
	double sun_d[3] = { 1.0, 0.0, 0.0 };
	double moon_d[3] = { 1.0, 0.0, 0.0 };
	nd_field_eval_celestial_ephemeris(s_gl.cur_pdef.world_id, (double)t_sec,
	                                  sun_d, moon_d, NULL, NULL);
	if (out_sun_dir) {
		out_sun_dir[0] = (float)sun_d[0];
		out_sun_dir[1] = (float)sun_d[1];
		out_sun_dir[2] = (float)sun_d[2];
	}
	if (out_moon_dir) {
		out_moon_dir[0] = (float)moon_d[0];
		out_moon_dir[1] = (float)moon_d[1];
		out_moon_dir[2] = (float)moon_d[2];
	}
}

float nd_seven_gl_eval_astronomical_tide(float wx, float wy,
                                         float sea_level, float base_sea_level,
                                         float t_sec,
                                         const nd_planet_def_t *pdef)
{
	if (sea_level <= -900.0f)
		return 0.0f;
	float is_ocean = 1.0f - gl_smoothstep(0.25f, 0.75f, fabsf(sea_level - base_sea_level));
	if (is_ocean <= 0.001f)
		return 0.0f;

	float N[3], sun_dir[3], moon_dir[3];
	nd_seven_gl_torus_frame(wx, wy, 0.0f, pdef, NULL, NULL, NULL, N);
	nd_seven_gl_eval_celestial_dirs(t_sec, sun_dir, moon_dir);

	float cos_moon = N[0] * moon_dir[0] + N[1] * moon_dir[1] + N[2] * moon_dir[2];
	float cos_sun  = N[0] * sun_dir[0]  + N[1] * sun_dir[1]  + N[2] * sun_dir[2];
	float p2_moon  = 0.5f * (3.0f * cos_moon * cos_moon - 1.0f);
	float p2_sun   = 0.5f * (3.0f * cos_sun  * cos_sun  - 1.0f);
	return is_ocean * (0.68f * p2_moon + 0.31f * p2_sun);
}

float nd_seven_gl_eval_shore_swash(float wx, float wy,
                                   float sea_level, float base_sea_level,
                                   float terrain_z,
                                   float slope_gx, float slope_gy,
                                   float t_sec,
                                   const nd_planet_def_t *pdef,
                                   float *out_high_water_z,
                                   float *out_splash_foam)
{
	float astro_tide = nd_seven_gl_eval_astronomical_tide(wx, wy, sea_level, base_sea_level, t_sec, pdef);
	float base_tide_z = sea_level + astro_tide;
	float is_ocean = 1.0f - gl_smoothstep(0.25f, 0.75f, fabsf(sea_level - base_sea_level));
	float slope_mag = sqrtf(slope_gx * slope_gx + slope_gy * slope_gy);
	float near_shore = gl_smoothstep(8.0f, 0.0f, fabsf(terrain_z - base_tide_z));

	if (near_shore <= 0.001f) {
		float tide_high = sea_level + (0.45f + 0.85f * is_ocean);
		if (out_high_water_z) *out_high_water_z = fmaxf(base_tide_z, tide_high);
		if (out_splash_foam) *out_splash_foam = 0.0f;
		return base_tide_z;
	}

	float na_v = 0.5f, na_x = 0.0f, na_y = 0.0f;
	gl_chaff_grad_noise2d(wx * 0.015f + 13.7f, wy * 0.015f - 9.2f, 0x53574153u, &na_v, &na_x, &na_y);

	float group_env = 0.78f + 0.22f * sinf(fmodf(t_sec * 0.24f, 628.3185f) + (na_v - 0.5f) * 2.2f);
	float rel_z = fmaxf(-6.0f, fminf(6.0f, terrain_z - base_tide_z));
	float wave_arg = fmodf(t_sec * 0.92f, 628.3185f) - rel_z * 0.32f + (na_v - 0.5f) * 1.8f;
	float swell_wave = sinf(wave_arg) + 0.22f * sinf(2.0f * wave_arg - 0.35f);

	float wave_crest = 0.0f;
	/* Pass pixel_footprint = 2.5f so only smooth low-frequency wave octaves 0..2 (wavelengths 4.5..42 units) contribute */
	float wave_h = nd_seven_gl_eval_water_waves(wx, wy, t_sec, 280.0f, 2.5f, NULL, NULL, &wave_crest);
	float slope_resp = 0.62f + 0.38f * gl_smoothstep(0.004f, 0.10f, slope_mag);
	float wavelet_runup = wave_h * (0.48f + 0.24f * is_ocean) * slope_resp;
	float swell_runup = swell_wave * (0.34f + 0.28f * is_ocean) * group_env * slope_resp;
	float swash_surf_z = base_tide_z + (wavelet_runup + swell_runup) * near_shore;
	float recent_top = base_tide_z + (0.62f + 0.58f * is_ocean) * (0.82f + 0.18f * group_env) * slope_resp;
	float high_water_z = fmaxf(swash_surf_z, recent_top + is_ocean * 0.20f);

	float wave_phase_lift = gl_smoothstep(-0.20f, 0.45f, wave_h * 1.2f + swell_wave * 0.50f);
	float edge_lip = gl_smoothstep(0.55f, 0.0f, fabsf(terrain_z - swash_surf_z)) *
	                 (0.30f + 0.70f * wave_phase_lift);
	float rock_impact = gl_smoothstep(0.12f, 0.26f, slope_mag) *
	                    gl_smoothstep(0.05f, 0.52f, wave_h + swell_wave * 0.35f);
	float body_foam_scale = 0.65f + 0.35f * is_ocean;
	float foam = (edge_lip * (0.14f + 0.20f * wave_crest) + rock_impact * 0.14f) * body_foam_scale;
	if (foam > 0.32f) foam = 0.32f;
	if (foam < 0.0f) foam = 0.0f;

	if (out_high_water_z) *out_high_water_z = high_water_z;
	if (out_splash_foam) *out_splash_foam = foam;
	return swash_surf_z;
}

void nd_seven_gl_set_lamb_wake(float wx, float wy, float ground_z,
                               int dir, int walking, float anim_time)
{
	/* Match nd_seven_motion.h: DIR_DOWN=0 (+Y), DIR_UP=1 (-Y), DIR_LEFT=2 (-X), DIR_RIGHT=3 (+X) */
	float dir_vx = 0.0f, dir_vy = 1.0f;
	if (dir == 0) { dir_vx = 0.0f; dir_vy = 1.0f; }
	else if (dir == 1) { dir_vx = 0.0f; dir_vy = -1.0f; }
	else if (dir == 2) { dir_vx = -1.0f; dir_vy = 0.0f; }
	else if (dir == 3) { dir_vx = 1.0f; dir_vy = 0.0f; }

	/* Determine if lamb is in water (ground_z < local water level + 0.65) */
	float sea_lvl = s_gl.cur_sea_level;
	bool in_water = (sea_lvl > -900.0f && ground_z < sea_lvl + 0.65f);

	float dt = anim_time - s_gl.cur_wake_last_t;
	float dx = wx - s_gl.cur_wake_last_x;
	float dy = wy - s_gl.cur_wake_last_y;
	float dist_moved = sqrtf(dx * dx + dy * dy);

	if (in_water) {
		/* Reset trail if entering water fresh or after teleport / time reset */
		if (s_gl.cur_wake_last_t <= 0.0f || dt < 0.0f || dist_moved > 64.0f || s_gl.cur_wake_vel[0][2] <= 0.001f) {
			memset(s_gl.cur_wake_xyt, 0, sizeof(s_gl.cur_wake_xyt));
			memset(s_gl.cur_wake_vel, 0, sizeof(s_gl.cur_wake_vel));
			float water_depth = fmaxf(0.15f, sea_lvl - ground_z);
			float sub_atten = gl_smoothstep01(22.0f, 8.0f, water_depth);
			float init_spd = walking ? 38.0f : 8.0f;
			float energy = fminf(1.0f, 0.35f + 0.65f * fminf(1.0f, water_depth / 2.5f)) * (walking ? 1.0f : 0.42f) * sub_atten;
			s_gl.cur_wake_xyt[0][0] = wx;
			s_gl.cur_wake_xyt[0][1] = wy;
			s_gl.cur_wake_xyt[0][2] = anim_time;
			s_gl.cur_wake_xyt[0][3] = water_depth;
			s_gl.cur_wake_vel[0][0] = dir_vx * init_spd;
			s_gl.cur_wake_vel[0][1] = dir_vy * init_spd;
			s_gl.cur_wake_vel[0][2] = energy;
			s_gl.cur_wake_vel[0][3] = (float)dir;

			/* Seed slot 1 slightly behind slot 0 along -dir so segment 0->1 is immediately valid */
			s_gl.cur_wake_xyt[1][0] = wx - dir_vx * 3.5f;
			s_gl.cur_wake_xyt[1][1] = wy - dir_vy * 3.5f;
			s_gl.cur_wake_xyt[1][2] = anim_time - 0.12f;
			s_gl.cur_wake_xyt[1][3] = water_depth;
			s_gl.cur_wake_vel[1][0] = dir_vx * init_spd;
			s_gl.cur_wake_vel[1][1] = dir_vy * init_spd;
			s_gl.cur_wake_vel[1][2] = energy * 0.85f;
			s_gl.cur_wake_vel[1][3] = (float)dir;

			s_gl.cur_wake_last_x = wx;
			s_gl.cur_wake_last_y = wy;
			s_gl.cur_wake_last_t = anim_time;
		} else {
			/* Shift ordered trail [7..2] <- [6..1] when lamb advances >= 3.2 units from anchor slot 1 */
			if (dist_moved >= 3.2f || (walking && dt >= 0.18f && dist_moved >= 1.0f)) {
				for (int k = ND_LAMB_WAKE_NODES - 1; k >= 2; k--) {
					memcpy(s_gl.cur_wake_xyt[k], s_gl.cur_wake_xyt[k - 1], sizeof(float) * 4);
					memcpy(s_gl.cur_wake_vel[k], s_gl.cur_wake_vel[k - 1], sizeof(float) * 4);
				}
				s_gl.cur_wake_xyt[1][0] = s_gl.cur_wake_last_x;
				s_gl.cur_wake_xyt[1][1] = s_gl.cur_wake_last_y;
				s_gl.cur_wake_xyt[1][2] = s_gl.cur_wake_last_t;
				s_gl.cur_wake_xyt[1][3] = s_gl.cur_wake_xyt[0][3];
				memcpy(s_gl.cur_wake_vel[1], s_gl.cur_wake_vel[0], sizeof(float) * 4);

				s_gl.cur_wake_last_x = wx;
				s_gl.cur_wake_last_y = wy;
				s_gl.cur_wake_last_t = anim_time;
			}

			/* Slot 0 always tracks the live lamb position and current heading */
			float adx = wx - s_gl.cur_wake_xyt[1][0];
			float ady = wy - s_gl.cur_wake_xyt[1][1];
			float adist = sqrtf(adx * adx + ady * ady);
			float ux = (adist > 0.5f) ? (adx / adist) : dir_vx;
			float uy = (adist > 0.5f) ? (ady / adist) : dir_vy;
			/* Blend actual displacement direction with facing direction so turns are smooth */
			float fx = ux * 0.65f + dir_vx * 0.35f;
			float fy = uy * 0.65f + dir_vy * 0.35f;
			float flen = sqrtf(fx * fx + fy * fy);
			if (flen > 1e-4f) { fx /= flen; fy /= flen; }
			else { fx = dir_vx; fy = dir_vy; }

			float spd = walking ? 38.0f : 8.0f;
			if (dt > 0.01f && dist_moved > 0.25f)
				spd = fminf(56.0f, fmaxf(12.0f, dist_moved / dt));

			float water_depth = fmaxf(0.15f, sea_lvl - ground_z);
			float sub_atten = gl_smoothstep01(22.0f, 8.0f, water_depth);
			float energy = fminf(1.0f, 0.35f + 0.65f * fminf(1.0f, water_depth / 2.5f)) * (walking ? 1.0f : 0.42f) * sub_atten;

			s_gl.cur_wake_xyt[0][0] = wx;
			s_gl.cur_wake_xyt[0][1] = wy;
			s_gl.cur_wake_xyt[0][2] = anim_time;
			s_gl.cur_wake_xyt[0][3] = water_depth;
			s_gl.cur_wake_vel[0][0] = fx * spd;
			s_gl.cur_wake_vel[0][1] = fy * spd;
			s_gl.cur_wake_vel[0][2] = energy;
			s_gl.cur_wake_vel[0][3] = (float)dir;
		}

		s_gl.cur_wake_head[0] = wx;
		s_gl.cur_wake_head[1] = wy;
		s_gl.cur_wake_head[2] = ground_z;
		s_gl.cur_wake_head[3] = 1.0f;
	} else {
		/* Smoothly decay head energy over 4.0 seconds after leaving water so existing trail lingers naturally */
		float time_since_water = anim_time - s_gl.cur_wake_last_t;
		s_gl.cur_wake_head[3] = (time_since_water < 4.0f && s_gl.cur_wake_last_t > 0.0f)
		                        ? fmaxf(0.0f, 1.0f - time_since_water / 4.0f)
		                        : 0.0f;
	}
}

void nd_seven_gl_get_lamb_wake(float out_head[4],
                               float out_nodes_xy_t[ND_LAMB_WAKE_NODES][4],
                               float out_nodes_vel[ND_LAMB_WAKE_NODES][4])
{
	if (out_head)
		memcpy(out_head, s_gl.cur_wake_head, sizeof(s_gl.cur_wake_head));
	if (out_nodes_xy_t)
		memcpy(out_nodes_xy_t, s_gl.cur_wake_xyt, sizeof(s_gl.cur_wake_xyt));
	if (out_nodes_vel)
		memcpy(out_nodes_vel, s_gl.cur_wake_vel, sizeof(s_gl.cur_wake_vel));
}

float nd_seven_gl_eval_lamb_wake(float frag_wx, float frag_wy,
                                 float anim_time,
                                 float *out_grad_x, float *out_grad_y,
                                 float *out_foam)
{
	if (out_grad_x) *out_grad_x = 0.0f;
	if (out_grad_y) *out_grad_y = 0.0f;
	if (out_foam) *out_foam = 0.0f;

	float head_energy = s_gl.cur_wake_head[3];
	if (head_energy <= 0.001f)
		return 0.0f;

	float dhx = frag_wx - s_gl.cur_wake_head[0];
	float dhy = frag_wy - s_gl.cur_wake_head[1];
	if (dhx * dhx + dhy * dhy > 140.0f * 140.0f)
		return 0.0f;

	float hvx = s_gl.cur_wake_vel[0][0];
	float hvy = s_gl.cur_wake_vel[0][1];
	float hspd = sqrtf(hvx * hvx + hvy * hvy);
	float hfwd_x = (hspd > 0.5f) ? (hvx / hspd) : 0.0f;
	float hfwd_y = (hspd > 0.5f) ? (hvy / hspd) : 1.0f;

	float wt_v = 0.5f, wt_x = 0.0f, wt_y = 0.0f;
	gl_chaff_grad_noise2d(frag_wx * 0.38f + fmodf(anim_time * 0.75f, 256.0f),
	                      frag_wy * 0.38f - fmodf(anim_time * 0.55f, 256.0f),
	                      0x57414B45u, &wt_v, &wt_x, &wt_y);
	float warp_x = wt_x * 0.85f;
	float warp_y = wt_y * 0.85f;

	float total_h = 0.0f;
	float total_gx = 0.0f, total_gy = 0.0f;
	float total_foam = 0.0f;
	float path_arc = 0.0f;

	for (int i = 0; i < ND_LAMB_WAKE_NODES - 1; i++) {
		float energy_a = s_gl.cur_wake_vel[i][2];
		if (energy_a <= 0.002f)
			continue;

		int j = i + 1;
		float energy_b = s_gl.cur_wake_vel[j][2];
		float vx_a = s_gl.cur_wake_vel[i][0];
		float vy_a = s_gl.cur_wake_vel[i][1];
		float spd_a = sqrtf(vx_a * vx_a + vy_a * vy_a);
		float fwd_ax = (spd_a > 0.5f) ? (vx_a / spd_a) : hfwd_x;
		float fwd_ay = (spd_a > 0.5f) ? (vy_a / spd_a) : hfwd_y;

		float pa_x = s_gl.cur_wake_xyt[i][0];
		float pa_y = s_gl.cur_wake_xyt[i][1];
		float ta   = s_gl.cur_wake_xyt[i][2];

		float pb_x = (energy_b > 0.002f) ? s_gl.cur_wake_xyt[j][0] : (pa_x - fwd_ax * 3.5f);
		float pb_y = (energy_b > 0.002f) ? s_gl.cur_wake_xyt[j][1] : (pa_y - fwd_ay * 3.5f);
		float tb   = (energy_b > 0.002f) ? s_gl.cur_wake_xyt[j][2] : (ta - 0.18f);

		float seg_x = pa_x - pb_x;
		float seg_y = pa_y - pb_y;
		float seg_len = sqrtf(seg_x * seg_x + seg_y * seg_y);
		float tdir_x = (seg_len > 0.15f) ? (seg_x / seg_len) : fwd_ax;
		float tdir_y = (seg_len > 0.15f) ? (seg_y / seg_len) : fwd_ay;
		float ndir_x = -tdir_y;
		float ndir_y =  tdir_x;
		float span = fmaxf(seg_len, 0.25f);

		float rel_bx = frag_wx - pb_x;
		float rel_by = frag_wy - pb_y;
		float proj = rel_bx * tdir_x + rel_by * tdir_y;
		float t_seg = fminf(1.0f, fmaxf(0.0f, proj / span));
		float closest_x = pb_x + tdir_x * (t_seg * seg_len);
		float closest_y = pb_y + tdir_y * (t_seg * seg_len);
		float seg_time = tb + (ta - tb) * t_seg;
		float age = anim_time - seg_time;
		if (age < 0.0f) age = 0.0f;
		if (age >= 4.15f) {
			path_arc += seg_len;
			continue;
		}

		float eb_eff = (energy_b > 0.002f) ? energy_b : (energy_a * 0.5f);
		float seg_energy = eb_eff + (energy_a - eb_eff) * t_seg;
		float age_decay = expf(-0.80f * age) * gl_smoothstep(4.0f, 0.15f, age) * seg_energy * head_energy;

		float s_over_a = fmaxf(0.0f, proj - seg_len);
		float s_over_b = fmaxf(0.0f, -proj);
		float head_cap_sigma = (i == 0) ? 1.85f : 2.60f;
		float tail_cap_sigma = 3.20f + age * 2.80f;
		float long_win = expf(-(s_over_a * s_over_a) / (head_cap_sigma * head_cap_sigma)) *
		                 expf(-(s_over_b * s_over_b) / (tail_cap_sigma * tail_cap_sigma));
		float joint_w = (i == 0)
		                ? gl_smoothstep(-tail_cap_sigma, 0.0f, proj)
		                : (gl_smoothstep(-tail_cap_sigma, 0.0f, proj) *
		                   gl_smoothstep(seg_len + head_cap_sigma, seg_len, proj + 0.5f * head_cap_sigma));
		float env_base = age_decay * long_win * (0.45f + 0.55f * joint_w);
		float cur_arc = path_arc + (1.0f - t_seg) * seg_len + s_over_a;
		path_arc += seg_len;
		if (env_base <= 0.002f)
			continue;

		float wx_w = (frag_wx + warp_x * (0.16f + 0.14f * age)) - closest_x;
		float wy_w = (frag_wy + warp_y * (0.16f + 0.14f * age)) - closest_y;
		float raw_u = wx_w * ndir_x + wy_w * ndir_y;
		float u = fabsf(raw_u);
		float sign_u = (raw_u >= 0.0f) ? 1.0f : -1.0f;

		float wake_hw = 1.35f + cur_arc * 0.16f + age * 2.10f;
		float edge_sigma = 1.15f + age * 0.85f;
		float d_edge = (u - wake_hw) / edge_sigma;

		float rim_env = expf(-(d_edge * d_edge)) * expf(-cur_arc * 0.028f) * env_base;
		float spd_scale = fminf(1.15f, fmaxf(0.30f, spd_a / 36.0f));
		if (rim_env > 0.002f) {
			float h_rim = 0.20f * rim_env * spd_scale;
			total_h += h_rim;
			float d_rim_du = (-2.2f * d_edge / edge_sigma) * h_rim;
			total_gx += (ndir_x * sign_u - tdir_x * 0.20f) * d_rim_du;
			total_gy += (ndir_y * sign_u - tdir_y * 0.20f) * d_rim_du;
			total_foam += rim_env * spd_scale * expf(-age * 1.15f) * (0.14f + 0.20f * wt_v);
		}

		float core_env = gl_smoothstep(wake_hw + edge_sigma * 0.80f, wake_hw * 0.15f, u) *
		                 expf(-cur_arc * 0.035f) * env_base;
		if (core_env > 0.003f) {
			float eddy = core_env * spd_scale;
			float body_foam_ramp = gl_smoothstep(0.5f, 4.5f, cur_arc);
			total_h -= 0.11f * eddy;
			total_gx += wt_x * (0.42f * eddy) + ndir_x * (sign_u * 0.20f * eddy);
			total_gy += wt_y * (0.42f * eddy) + ndir_y * (sign_u * 0.20f * eddy);
			float bubble = gl_smoothstep(0.36f, 0.74f, wt_v) * expf(-age * 1.25f);
			total_foam += eddy * (0.10f + bubble * 0.34f) * (0.35f + 0.65f * body_foam_ramp);
		}
	}

	if (total_h < -0.42f) total_h = -0.42f;
	if (total_h >  0.42f) total_h =  0.42f;
	if (total_foam > 0.55f) total_foam = 0.55f;

	if (out_grad_x) *out_grad_x = total_gx;
	if (out_grad_y) *out_grad_y = total_gy;
	if (out_foam) *out_foam = total_foam;
	return total_h;
}

void nd_seven_gl_cloud_subpixel(uint32_t phase, uint32_t qrw, uint32_t qrh,
                                int *out_px, int *out_py,
                                float *out_jx, float *out_jy)
{
	/* Diagonal 2x2 Bayer sequence spreads samples maximally over every 2-frame window */
	static const int k_px[4] = { 0, 1, 1, 0 };
	static const int k_py[4] = { 0, 1, 0, 1 };
	uint32_t idx = phase & 3u;
	int px = k_px[idx];
	int py = k_py[idx];
	if (out_px) *out_px = px;
	if (out_py) *out_py = py;
	if (out_jx) *out_jx = (qrw > 0u) ? ((0.5f - (float)px) / (float)qrw) : 0.0f;
	if (out_jy) *out_jy = (qrh > 0u) ? ((0.5f - (float)py) / (float)qrh) : 0.0f;
}

static bool gl_invert_mat4_d(const double m[16], double inv_out[16])
{
	/* Column-major 4x4 Gauss-Jordan inversion with partial pivoting */
	double aug[4][8];
	for (int r = 0; r < 4; r++) {
		for (int c = 0; c < 4; c++) {
			aug[r][c] = m[c * 4 + r];
			aug[r][c + 4] = (r == c) ? 1.0 : 0.0;
		}
	}
	for (int col = 0; col < 4; col++) {
		int piv = col;
		double best = fabs(aug[col][col]);
		for (int r = col + 1; r < 4; r++) {
			double v = fabs(aug[r][col]);
			if (v > best) {
				best = v;
				piv = r;
			}
		}
		if (best < 1e-24 || !isfinite(best))
			return false;
		if (piv != col) {
			for (int j = 0; j < 8; j++) {
				double tmp = aug[col][j];
				aug[col][j] = aug[piv][j];
				aug[piv][j] = tmp;
			}
		}
		double inv_p = 1.0 / aug[col][col];
		for (int j = 0; j < 8; j++)
			aug[col][j] *= inv_p;
		for (int r = 0; r < 4; r++) {
			if (r == col)
				continue;
			double f = aug[r][col];
			if (f == 0.0)
				continue;
			for (int j = 0; j < 8; j++)
				aug[r][j] -= f * aug[col][j];
		}
	}
	for (int r = 0; r < 4; r++) {
		for (int c = 0; c < 4; c++) {
			double v = aug[r][c + 4];
			if (!isfinite(v))
				return false;
			inv_out[c * 4 + r] = v;
		}
	}
	return true;
}

static void gl_mul_mat4_d(const double a[16], const double b[16], double out[16])
{
	for (int c = 0; c < 4; c++) {
		for (int r = 0; r < 4; r++) {
			out[c * 4 + r] =
				a[0 * 4 + r] * b[c * 4 + 0] +
				a[1 * 4 + r] * b[c * 4 + 1] +
				a[2 * 4 + r] * b[c * 4 + 2] +
				a[3 * 4 + r] * b[c * 4 + 3];
		}
	}
}

bool nd_seven_gl_compute_reproj_mat(const float cur_mvp[16], const double cur_origin[3],
                                    const float prev_mvp[16], const double prev_origin[3],
                                    float out_reproj[16])
{
	if (!cur_mvp || !prev_mvp || !out_reproj)
		return false;

	/* Fast exact identity path when camera and origin are completely stationary */
	double dx = (cur_origin && prev_origin) ? (cur_origin[0] - prev_origin[0]) : 0.0;
	double dy = (cur_origin && prev_origin) ? (cur_origin[1] - prev_origin[1]) : 0.0;
	double dz = (cur_origin && prev_origin) ? (cur_origin[2] - prev_origin[2]) : 0.0;
	if (dx == 0.0 && dy == 0.0 && dz == 0.0 &&
	    memcmp(cur_mvp, prev_mvp, 16 * sizeof(float)) == 0) {
		for (int i = 0; i < 16; i++)
			out_reproj[i] = ((i % 5) == 0) ? 1.0f : 0.0f;
		return true;
	}

	double cur_d[16], prev_d[16], inv_cur[16], trans[16], tmp[16], reproj_d[16];
	for (int i = 0; i < 16; i++) {
		cur_d[i] = (double)cur_mvp[i];
		prev_d[i] = (double)prev_mvp[i];
		trans[i] = ((i % 5) == 0) ? 1.0 : 0.0;
	}
	trans[12] = dx;
	trans[13] = dy;
	trans[14] = dz;

	if (!gl_invert_mat4_d(cur_d, inv_cur)) {
		for (int i = 0; i < 16; i++)
			out_reproj[i] = ((i % 5) == 0) ? 1.0f : 0.0f;
		return false;
	}
	gl_mul_mat4_d(prev_d, trans, tmp);
	gl_mul_mat4_d(tmp, inv_cur, reproj_d);
	for (int i = 0; i < 16; i++) {
		if (!isfinite(reproj_d[i])) {
			for (int j = 0; j < 16; j++)
				out_reproj[j] = ((j % 5) == 0) ? 1.0f : 0.0f;
			return false;
		}
		out_reproj[i] = (float)reproj_d[i];
	}
	return true;
}
