/*
 * nd_seven_gl_billboard.c — Billboard Basis, Quad & Shadow Evaluation, Instance Packing, and 3D Mesh Instanced Flush.
 */

#include "nd_seven_gl_internal.h"

void nd_seven_gl_billboard_basis_ex(float wx, float wy, float wz,
                                    float yaw_deg, float pitch_deg,
                                    const nd_planet_def_t *pdef,
                                    float out_base_pos[3],
                                    float out_right[3],
                                    float out_up[3],
                                    float out_view_dir[3])
{
	float E[3], S[3], N[3];
	nd_seven_gl_torus_frame(wx, wy, wz, pdef, out_base_pos, E, S, N);

	float yaw_rad = yaw_deg * (PI_F / 180.0f);
	float cos_y = cosf(yaw_rad);
	float sin_y = sinf(yaw_rad);
	float pitch_rad = pitch_deg * (PI_F / 180.0f);
	float cos_p = cosf(pitch_rad);
	float sin_p = sinf(pitch_rad);

	float horiz[3] = {
		sin_y * E[0] + cos_y * S[0],
		sin_y * E[1] + cos_y * S[1],
		sin_y * E[2] + cos_y * S[2]
	};

	if (out_right) {
		out_right[0] = cos_y * E[0] - sin_y * S[0];
		out_right[1] = cos_y * E[1] - sin_y * S[1];
		out_right[2] = cos_y * E[2] - sin_y * S[2];
	}
	if (out_up) {
		out_up[0] = cos_p * N[0] - sin_p * horiz[0];
		out_up[1] = cos_p * N[1] - sin_p * horiz[1];
		out_up[2] = cos_p * N[2] - sin_p * horiz[2];
	}
	if (out_view_dir) {
		out_view_dir[0] = sin_p * N[0] + cos_p * horiz[0];
		out_view_dir[1] = sin_p * N[1] + cos_p * horiz[1];
		out_view_dir[2] = sin_p * N[2] + cos_p * horiz[2];
	}
}

void nd_seven_gl_billboard_basis(float wx, float wy, float wz,
                                 float pitch_deg,
                                 const nd_planet_def_t *pdef,
                                 float out_base_pos[3],
                                 float out_right[3],
                                 float out_up[3],
                                 float out_view_dir[3])
{
	nd_seven_gl_billboard_basis_ex(wx, wy, wz, 0.0f, pitch_deg, pdef,
	                               out_base_pos, out_right, out_up, out_view_dir);
}


const double *s_bb_ref_origin = NULL;
const nd_planet_def_t *s_bb_ref_pdef = NULL;
float s_bb_tx = 0.0f;
float s_bb_ty = 0.0f;
float s_bb_dist = 0.0f;
float s_bb_ref_trig[4] = { 1.0f, 0.0f, 1.0f, 0.0f };

typedef struct {
	float wx, wy;
	float decay;
	float vnx_reb;
	float vny_reb;
} bb_cached_wake_t;

static bb_cached_wake_t s_bb_wake_cache[16];
static int s_bb_wake_cache_n = 0;
static float s_bb_wake_cache_time = -99999.0f;

static void eval_billboard_basis(const lam_billboard_gpu_t *b,
                                 const nd_planet_def_t *pdef,
                                 const double ref_origin[3],
                                 billboard_basis_t *out)
{
	bool fast_rel = (ref_origin != NULL && ref_origin == s_bb_ref_origin &&
	                 pdef != NULL && pdef == s_bb_ref_pdef);
	bool did_fast = false;
	if (fast_rel) {
		float rel_x = b->x - s_bb_tx;
		float rel_y = b->y - s_bb_ty;
		float du = TWO_PI_F * (rel_x * (1.0f / (float)ND_SEVEN_WORLD_DIM_W));
		float dv = TWO_PI_F * (rel_y * (1.0f / (float)ND_SEVEN_WORLD_DIM_H));
		float dcu, sdu, dcv, sdv;
		if (fabsf(du) <= 0.10f && fabsf(dv) <= 0.10f) {
			float du2 = du * du;
			float dv2 = dv * dv;
			dcu = du2 * (-0.5f + du2 * (1.0f / 24.0f));
			sdu = du * (1.0f - du2 * (1.0f / 6.0f));
			dcv = dv2 * (-0.5f + dv2 * (1.0f / 24.0f));
			sdv = dv * (1.0f - dv2 * (1.0f / 6.0f));
		} else {
			float shdu = sinf(0.5f * du);
			float shdv = sinf(0.5f * dv);
			dcu = -2.0f * shdu * shdu;
			sdu = sinf(du);
			dcv = -2.0f * shdv * shdv;
			sdv = sinf(dv);
		}

		float cu0 = s_bb_ref_trig[0];
		float su0 = s_bb_ref_trig[1];
		float cv0 = s_bb_ref_trig[2];
		float sv0 = s_bb_ref_trig[3];

		float d_cu = cu0 * dcu - su0 * sdu;
		float d_su = su0 * dcu + cu0 * sdu;
		float d_cv = cv0 * dcv - sv0 * sdv;
		float d_sv = sv0 * dcv + cv0 * sdv;

		float cu = cu0 + d_cu;
		float su = su0 + d_su;
		float cv = cv0 + d_cv;
		float sv = sv0 + d_sv;

		float R = (pdef->major_r > 0.0f) ? pdef->major_r : 2000000.0f;
		float r0 = (pdef->minor_r > 0.0f) ? pdef->minor_r : 1335000.0f;
		float r_eff0 = R + r0 * cv0;
		float d_r = r0 * d_cv + b->z * cv;

		float px = r_eff0 * d_cu + d_r * cu;
		float py = r_eff0 * d_su + d_r * su;
		float pz = r0 * d_sv + b->z * sv;

		float ex = -su, ey = cu, ez = 0.0f;
		float sx = -sv * cu, sy = -sv * su, sz = cv;
		float nx = cv * cu, ny = cv * su, nz = sv;

		if (pdef->rot[0] != 0.0f || pdef->rot[4] != 0.0f || pdef->rot[8] != 0.0f) {
			const float *r = pdef->rot;
			out->base_pos[0] = r[0] * px + r[1] * py + r[2] * pz;
			out->base_pos[1] = r[3] * px + r[4] * py + r[5] * pz;
			out->base_pos[2] = r[6] * px + r[7] * py + r[8] * pz;
			out->E[0] = r[0] * ex + r[1] * ey + r[2] * ez;
			out->E[1] = r[3] * ex + r[4] * ey + r[5] * ez;
			out->E[2] = r[6] * ex + r[7] * ey + r[8] * ez;
			out->S[0] = r[0] * sx + r[1] * sy + r[2] * sz;
			out->S[1] = r[3] * sx + r[4] * sy + r[5] * sz;
			out->S[2] = r[6] * sx + r[7] * sy + r[8] * sz;
			out->N[0] = r[0] * nx + r[1] * ny + r[2] * nz;
			out->N[1] = r[3] * nx + r[4] * ny + r[5] * nz;
			out->N[2] = r[6] * nx + r[7] * ny + r[8] * nz;
		} else {
			out->base_pos[0] = px; out->base_pos[1] = py; out->base_pos[2] = pz;
			out->E[0] = ex; out->E[1] = ey; out->E[2] = ez;
			out->S[0] = sx; out->S[1] = sy; out->S[2] = sz;
			out->N[0] = nx; out->N[1] = ny; out->N[2] = nz;
		}
		did_fast = true;
	}

	if (!did_fast) {
		double pos_d[3];
		torus_frame_double((double)b->x, (double)b->y, (double)b->z, pdef, pos_d, out->E, out->S, out->N);

		out->base_pos[0] = (float)(pos_d[0] - (ref_origin ? ref_origin[0] : 0.0));
		out->base_pos[1] = (float)(pos_d[1] - (ref_origin ? ref_origin[1] : 0.0));
		out->base_pos[2] = (float)(pos_d[2] - (ref_origin ? ref_origin[2] : 0.0));
	}

	out->u_min = fminf(b->u0, b->u1);
	out->u_max = fmaxf(b->u0, b->u1);
	out->v_min = fminf(b->v0, b->v1);
	out->v_max = fmaxf(b->v0, b->v1);

	out->grass_sx = 0.0f;
	out->grass_sy = 0.0f;
	out->grass_dz = 0.0f;
	out->grass_rustle = 0.0f;
	out->flora_sway_scale = 0.0f;

	int is_woody_tree = 0;
	if (b->style == ND_BB_STYLE_GRASS) {
		out->flora_sway_scale = (b->height > 0.0f && b->height < 14.0f)
		                        ? fmaxf(0.36f, b->height * (1.0f / 14.0f))
		                        : 1.0f;
	} else if (b->style == ND_BB_STYLE_LEAF_TINT || b->style == ND_BB_STYLE_FLORA) {
		uint8_t pad_kind = b->pad0 & 0x03u;
		if (pad_kind == 1u && b->height > 0.0f) {
			float h_norm = fminf(1.25f, fmaxf(0.55f, b->height * (1.0f / 26.0f)));
			out->flora_sway_scale = 0.92f * h_norm;
		} else if (pad_kind == 0u && b->height > 0.0f && b->height <= 30.0f) {
			float size_t = (30.0f - b->height) * (1.0f / 18.0f);
			if (size_t < 0.0f) size_t = 0.0f;
			if (size_t > 1.0f) size_t = 1.0f;
			out->flora_sway_scale = 0.52f + 0.33f * size_t;
		} else if (pad_kind == 2u && b->height > 0.0f) {
			float h_norm = fminf(1.30f, fmaxf(0.75f, b->height * (1.0f / 80.0f)));
			out->flora_sway_scale = 1.05f * h_norm;
			is_woody_tree = 1;
		}
	}

	if (fast_rel && out->flora_sway_scale > 0.0f) {
		if (s_bb_dist >= 2400.0f) {
			out->flora_sway_scale = 0.0f;
		} else if (s_bb_dist > 1600.0f) {
			float t_sway = (2400.0f - s_bb_dist) * (1.0f / 800.0f);
			out->flora_sway_scale *= t_sway * t_sway * (3.0f - 2.0f * t_sway);
		}
	}

	if (out->flora_sway_scale > 0.0f) {
		if (!s_view_wind_ready)
			gl_update_view_wind(0.0f, 0.0f, s_grass_anim_time);
		if (is_woody_tree) {
			out->grass_sx = s_view_tree_sx * out->flora_sway_scale;
			out->grass_sy = s_view_tree_sy * out->flora_sway_scale;
			out->grass_dz = 0.0f;
			out->grass_rustle = s_view_tree_rustle;
		} else {
			float sx = s_view_wind_sx;
			float sy = s_view_wind_sy;
			float dz = s_view_wind_dz;
			float rustle = s_view_wind_rustle;

			float dx = b->x - s_grass_lamb_pos.x;
			float dy = b->y - s_grass_lamb_pos.y;
			float d2 = dx * dx + dy * dy;
			const float R_touch = 11.0f;
			if (d2 < R_touch * R_touch) {
				float dist = sqrtf(d2);
				float t = 1.0f - (dist / R_touch);
				float weight = t * t * (3.0f - 2.0f * t);
				float rx = (dist > 0.25f) ? (dx / dist) : ((((int32_t)b->x & 1) != 0) ? 1.0f : -1.0f);
				float ry = (dist > 0.25f) ? (dy / dist) : -0.25f;
				sx += rx * (2.55f * weight);
				sy += ry * (0.85f * weight);
				dz += 2.25f * weight;
				rustle += 0.50f * weight;
				if (dy < 0.5f && sy > 0.35f)
					sy = 0.35f;
			}
			if (d2 < 64.0f * 64.0f) {
				if (s_bb_wake_cache_time != s_grass_anim_time) {
					s_bb_wake_cache_time = s_grass_anim_time;
					s_bb_wake_cache_n = 0;
					int wake_n = 0;
					const nd_grass_wake_step_t *wake = nd_seven_get_wake_steps(&wake_n);
					if (wake && wake_n > 0) {
						const float tau_max = 1.40f;
						for (int i = 0; i < wake_n && s_bb_wake_cache_n < 16; i++) {
							if (!wake[i].valid)
								continue;
							float tau = s_grass_anim_time - wake[i].t_sec;
							if (tau < 0.0f || tau > tau_max)
								continue;
							float decay = expf(-2.2f * tau) * (1.0f - (tau / tau_max));
							float rebound = expf(-1.6f * tau);
							float vlen = sqrtf(wake[i].vx * wake[i].vx + wake[i].vy * wake[i].vy);
							float vnx = (vlen > 1.0f) ? (wake[i].vx / vlen) : 0.0f;
							float vny = (vlen > 1.0f) ? (wake[i].vy / vlen) : 0.0f;
							s_bb_wake_cache[s_bb_wake_cache_n].wx = wake[i].wx;
							s_bb_wake_cache[s_bb_wake_cache_n].wy = wake[i].wy;
							s_bb_wake_cache[s_bb_wake_cache_n].decay = decay;
							s_bb_wake_cache[s_bb_wake_cache_n].vnx_reb = vnx * 0.65f * rebound;
							s_bb_wake_cache[s_bb_wake_cache_n].vny_reb = vny * 0.45f * rebound;
							s_bb_wake_cache_n++;
						}
					}
				}
				const float R_wake = 11.5f;
				for (int i = 0; i < s_bb_wake_cache_n; i++) {
					float wdx = b->x - s_bb_wake_cache[i].wx;
					float wdy = b->y - s_bb_wake_cache[i].wy;
					float wd2 = wdx * wdx + wdy * wdy;
					if (wd2 >= R_wake * R_wake)
						continue;
					float wdist = sqrtf(wd2);
					float wt = 1.0f - (wdist / R_wake);
					float w_spatial = wt * wt * (3.0f - 2.0f * wt);
					float wrx = (wdist > 0.25f) ? (wdx / wdist) : 1.0f;
					float wry = (wdist > 0.25f) ? (wdy / wdist) : 0.0f;
					float env = w_spatial * s_bb_wake_cache[i].decay;
					sx += (wrx * 1.45f + s_bb_wake_cache[i].vnx_reb) * env;
					sy += (wry * 0.85f + s_bb_wake_cache[i].vny_reb) * env;
					dz += 1.85f * env;
					rustle += 0.40f * env;
				}
			}
			if (sx < -4.2f) sx = -4.2f;
			if (sx >  4.2f) sx =  4.2f;
			if (sy < -3.2f) sy = -3.2f;
			if (sy >  3.2f) sy =  3.2f;
			if (dz <  0.0f) dz =  0.0f;
			if (dz >  3.4f) dz =  3.4f;
			if (rustle > 1.0f) rustle = 1.0f;

			out->grass_sx = sx * out->flora_sway_scale;
			out->grass_sy = sy * out->flora_sway_scale;
			out->grass_dz = dz * ((b->style == ND_BB_STYLE_GRASS) ? out->flora_sway_scale : (out->flora_sway_scale * 0.45f));
			out->grass_rustle = rustle * fminf(1.0f, out->flora_sway_scale);
		}
	}
}

static void eval_billboard_quad_from_basis(const lam_billboard_gpu_t *b,
                                           const billboard_basis_t *basis,
                                           float cos_y, float sin_y,
                                           float cos_p, float sin_p,
                                           nd_seven_gl_bb_vert_t out_verts[6])
{
	const float *E = basis->E;
	const float *S = basis->S;
	const float *N = basis->N;
	const float *base_pos = basis->base_pos;

	float horiz[3] = {
		sin_y * E[0] + cos_y * S[0],
		sin_y * E[1] + cos_y * S[1],
		sin_y * E[2] + cos_y * S[2]
	};
	float right[3] = {
		cos_y * E[0] - sin_y * S[0],
		cos_y * E[1] - sin_y * S[1],
		cos_y * E[2] - sin_y * S[2]
	};
	float up[3] = {
		cos_p * N[0] - sin_p * horiz[0],
		cos_p * N[1] - sin_p * horiz[1],
		cos_p * N[2] - sin_p * horiz[2]
	};

	float half_w = b->width * 0.5f;
	float h = b->height;
	float ay = b->anchor_y;
	float wz_off = b->water_z_off;

	float tint[4] = {
		(float)((b->tint_rgba >> 24) & 0xFF) / 255.0f,
		(float)((b->tint_rgba >> 16) & 0xFF) / 255.0f,
		(float)((b->tint_rgba >> 8) & 0xFF) / 255.0f,
		(float)(b->tint_rgba & 0xFF) / 255.0f
	};

	float style_f = (float)b->style + ((b->pad0 & 0x80u) ? 16.0f : 0.0f);

	struct { float dx, dy, u, v, loc_z, qx, qy; } c[4] = {
		{ -half_w, -ay,     b->u0, b->v1, b->z - wz_off,     -1.0f, 0.0f }, /* BL (0) */
		{  half_w, -ay,     b->u1, b->v1, b->z - wz_off,      1.0f, 0.0f }, /* BR (1) */
		{ -half_w, h - ay,  b->u0, b->v0, b->z + h - wz_off, -1.0f, 1.0f }, /* TL (2) */
		{  half_w, h - ay,  b->u1, b->v0, b->z + h - wz_off,  1.0f, 1.0f }  /* TR (3) */
	};

	int idx[6] = { 0, 1, 2, 1, 3, 2 };

	float grass_sx = basis->grass_sx;
	float grass_sy = basis->grass_sy;
	float grass_dz = basis->grass_dz;
	float grass_rustle = basis->grass_rustle;

	/* Prevent vertical screen-height collapse (-horiz_sway * sin_p - dz * cos_p) on short scaled meadow tufts
	 * so trampled or wind-bent tufts always retain at least 60% of their vertical screen height */
	if (basis->flora_sway_scale > 0.0f && h > 0.0f) {
		float max_vert_loss = 0.40f * h;
		float horiz_sway = grass_sx * sin_y + grass_sy * cos_y;
		float vert_loss = horiz_sway * sin_p + grass_dz * cos_p;
		if (vert_loss > max_vert_loss && vert_loss > 1e-4f) {
			float keep = max_vert_loss / vert_loss;
			grass_sx *= keep;
			grass_sy *= keep;
			grass_dz *= keep;
		}
	}

	for (int i = 0; i < 6; i++) {
		int k = idx[i];
		float tip_mask = (basis->flora_sway_scale > 0.0f && c[k].qy > 0.5f) ? 1.0f : 0.0f;
		float sx = grass_sx * tip_mask;
		float sy = grass_sy * tip_mask;
		float dz = grass_dz * tip_mask;

		out_verts[i].world_pos[0] = base_pos[0] + c[k].dx * right[0] + c[k].dy * up[0] + sx * E[0] + sy * S[0] - dz * N[0];
		out_verts[i].world_pos[1] = base_pos[1] + c[k].dx * right[1] + c[k].dy * up[1] + sx * E[1] + sy * S[1] - dz * N[1];
		out_verts[i].world_pos[2] = base_pos[2] + c[k].dx * right[2] + c[k].dy * up[2] + sx * E[2] + sy * S[2] - dz * N[2];

		out_verts[i].uv[0] = c[k].u;
		out_verts[i].uv[1] = c[k].v;

		float sheen = (b->style == ND_BB_STYLE_GRASS || (b->pad0 & 0x03u) == 1u) ? (tip_mask * grass_rustle * 0.14f) : 0.0f;
		out_verts[i].tint[0] = fminf(1.0f, tint[0] + sheen);
		out_verts[i].tint[1] = fminf(1.0f, tint[1] + sheen * 1.1f);
		out_verts[i].tint[2] = fminf(1.0f, tint[2] + sheen * 0.5f);
		out_verts[i].tint[3] = tint[3];

		out_verts[i].local_pos[0] = b->x + c[k].dx * cos_y - c[k].dy * sin_p * sin_y + sx;
		out_verts[i].local_pos[1] = b->y - c[k].dx * sin_y - c[k].dy * sin_p * cos_y + sy;
		out_verts[i].local_pos[2] = c[k].loc_z - dz;

		out_verts[i].quad_uv[0] = c[k].qx;
		out_verts[i].quad_uv[1] = c[k].qy;
		out_verts[i].quad_uv[2] = b->z;
		out_verts[i].quad_uv[3] = style_f;

		out_verts[i].sprite_bounds[0] = basis->u_min;
		out_verts[i].sprite_bounds[1] = basis->v_min;
		out_verts[i].sprite_bounds[2] = basis->u_max;
		out_verts[i].sprite_bounds[3] = basis->v_max;
	}
}

static void eval_billboard_quad_with_origin(const lam_billboard_gpu_t *b,
                                            float yaw_deg, float pitch_deg,
                                            const nd_planet_def_t *pdef,
                                            const double ref_origin[3],
                                            nd_seven_gl_bb_vert_t out_verts[6])
{
	if (!b || !out_verts) return;
	billboard_basis_t basis;
	eval_billboard_basis(b, pdef, ref_origin, &basis);
	float yaw_rad = yaw_deg * (PI_F / 180.0f);
	float pitch_rad = pitch_deg * (PI_F / 180.0f);
	eval_billboard_quad_from_basis(b, &basis, cosf(yaw_rad), sinf(yaw_rad), cosf(pitch_rad), sinf(pitch_rad), out_verts);
}

void nd_seven_gl_eval_billboard_quad_ex(const lam_billboard_gpu_t *b,
                                        float yaw_deg, float pitch_deg,
                                        const nd_planet_def_t *pdef,
                                        nd_seven_gl_bb_vert_t out_verts[6])
{
	eval_billboard_quad_with_origin(b, yaw_deg, pitch_deg, pdef, NULL, out_verts);
}

void nd_seven_gl_eval_billboard_quad(const lam_billboard_gpu_t *b,
                                     float pitch_deg,
                                     const nd_planet_def_t *pdef,
                                     nd_seven_gl_bb_vert_t out_verts[6])
{
	eval_billboard_quad_with_origin(b, 0.0f, pitch_deg, pdef, NULL, out_verts);
}

float s_shadow_sun_dir[3] = { 0.0f, 0.0f, 0.0f };

static void eval_billboard_shadow_quad_from_basis(const lam_billboard_gpu_t *b,
                                                  const billboard_basis_t *basis,
                                                  nd_seven_gl_bb_vert_t out_verts[6])
{
	const float *E = basis->E;
	const float *S = basis->S;
	const float *N = basis->N;
	float north_shift = (b->mesh_id > 0 && b->anchor_y > 0.0f) ? (b->anchor_y * 0.5f) : 0.0f;
	float base_pos[3] = {
		basis->base_pos[0] - S[0] * north_shift,
		basis->base_pos[1] - S[1] * north_shift,
		basis->base_pos[2] - S[2] * north_shift
	};
	float bx = b->x;
	float by = b->y - north_shift;

	float rx = (b->shadow_rx > 0) ? (float)b->shadow_rx : fmaxf(2.0f, b->width * 0.34f);
	float ry = (b->shadow_ry > 0) ? (float)b->shadow_ry : fmaxf(1.0f, rx * 0.45f);
	float half_w = fmaxf(rx, b->width * 0.5f);
	float h = fmaxf(ry * 2.0f, b->height);

	float sun_len2 = s_shadow_sun_dir[0] * s_shadow_sun_dir[0] +
	                 s_shadow_sun_dir[1] * s_shadow_sun_dir[1] +
	                 s_shadow_sun_dir[2] * s_shadow_sun_dir[2];
	float sun_e = -0.50f, sun_s = -0.42f, sun_n = 0.76f;
	if (sun_len2 > 1e-4f) {
		float le = s_shadow_sun_dir[0] * E[0] + s_shadow_sun_dir[1] * E[1] + s_shadow_sun_dir[2] * E[2];
		float ls = s_shadow_sun_dir[0] * S[0] + s_shadow_sun_dir[1] * S[1] + s_shadow_sun_dir[2] * S[2];
		float ln = s_shadow_sun_dir[0] * N[0] + s_shadow_sun_dir[1] * N[1] + s_shadow_sun_dir[2] * N[2];
		if (ln > -0.05f) {
			sun_e = -0.46f + 0.18f * le;
			sun_s = -0.40f + 0.18f * ls;
			sun_n = fmaxf(0.42f, 0.65f + 0.25f * ln);
		}
	}

	float cast_scale = 0.74f / sun_n;
	if (cast_scale > 1.65f) cast_scale = 1.65f;
	float cast_dx = -sun_e * h * cast_scale;
	float cast_dy = -sun_s * h * cast_scale;

	float sway_dx = basis->grass_sx;
	float sway_dy = basis->grass_sy;

	float base_pad_y = fminf(ry * 0.55f, 4.5f);
	float lift_z = 0.28f;
	float alpha = (float)(b->tint_rgba & 0xFF) / 255.0f;

	struct { float dx, dy, u, v, qx, qy; } c[4] = {
		{ -half_w,                     -base_pad_y,          b->u0, b->v1, -1.0f, -1.0f }, /* Base-Left (0) */
		{  half_w,                     -base_pad_y,          b->u1, b->v1,  1.0f, -1.0f }, /* Base-Right (1) */
		{ -half_w + cast_dx + sway_dx,  cast_dy + sway_dy,   b->u0, b->v0, -1.0f,  1.0f }, /* Crown-Left (2) */
		{  half_w + cast_dx + sway_dx,  cast_dy + sway_dy,   b->u1, b->v0,  1.0f,  1.0f }  /* Crown-Right (3) */
	};

	float c_zn[4];
	for (int k = 0; k < 4; k++)
		c_zn[k] = lift_z + shadow_corner_terrain_dz(bx + c[k].dx, by + c[k].dy, b->z, c[k].dx, c[k].dy);

	int idx[6] = { 0, 1, 2, 1, 3, 2 };

	for (int i = 0; i < 6; i++) {
		int k = idx[i];
		out_verts[i].world_pos[0] = base_pos[0] + c[k].dx * E[0] + c[k].dy * S[0] + c_zn[k] * N[0];
		out_verts[i].world_pos[1] = base_pos[1] + c[k].dx * E[1] + c[k].dy * S[1] + c_zn[k] * N[1];
		out_verts[i].world_pos[2] = base_pos[2] + c[k].dx * E[2] + c[k].dy * S[2] + c_zn[k] * N[2];

		out_verts[i].uv[0] = c[k].u;
		out_verts[i].uv[1] = c[k].v;

		out_verts[i].tint[0] = 0.0f;
		out_verts[i].tint[1] = 0.0f;
		out_verts[i].tint[2] = 0.0f;
		out_verts[i].tint[3] = alpha;

		out_verts[i].local_pos[0] = bx + c[k].dx;
		out_verts[i].local_pos[1] = by + c[k].dy;
		out_verts[i].local_pos[2] = b->z;

		out_verts[i].quad_uv[0] = c[k].qx;
		out_verts[i].quad_uv[1] = c[k].qy;
		out_verts[i].quad_uv[2] = b->z;
		out_verts[i].quad_uv[3] = -1.0f; /* -1.0f marks primary 3D sun-projected silhouette shadow quad */

		out_verts[i].sprite_bounds[0] = basis->u_min;
		out_verts[i].sprite_bounds[1] = basis->v_min;
		out_verts[i].sprite_bounds[2] = basis->u_max;
		out_verts[i].sprite_bounds[3] = basis->v_max;
	}
}

static void eval_billboard_shadow_quad_with_origin(const lam_billboard_gpu_t *b,
                                                   const nd_planet_def_t *pdef,
                                                   const double ref_origin[3],
                                                   nd_seven_gl_bb_vert_t out_verts[6])
{
	if (!b || !out_verts) return;
	billboard_basis_t basis;
	eval_billboard_basis(b, pdef, ref_origin, &basis);
	eval_billboard_shadow_quad_from_basis(b, &basis, out_verts);
}

void nd_seven_gl_eval_billboard_shadow_quad(const lam_billboard_gpu_t *b,
                                            const nd_planet_def_t *pdef,
                                            nd_seven_gl_bb_vert_t out_verts[6])
{
	eval_billboard_shadow_quad_with_origin(b, pdef, NULL, out_verts);
}

float s_lamb_shadow_light[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

static bool eval_billboard_lamb_shadow_quad_from_basis(const lam_billboard_gpu_t *b,
                                                       const billboard_basis_t *basis,
                                                       nd_seven_gl_bb_vert_t out_verts[6])
{
	if (!b || !basis || !out_verts) return false;
	float l_int = s_lamb_shadow_light[3];
	if (l_int <= 0.01f) return false;

	const float *E = basis->E;
	const float *S = basis->S;
	const float *N = basis->N;
	float north_shift = (b->mesh_id > 0 && b->anchor_y > 0.0f) ? (b->anchor_y * 0.5f) : 0.0f;
	float base_pos[3] = {
		basis->base_pos[0] - S[0] * north_shift,
		basis->base_pos[1] - S[1] * north_shift,
		basis->base_pos[2] - S[2] * north_shift
	};
	float bx = b->x;
	float by = b->y - north_shift;

	float dx_l = bx - s_lamb_shadow_light[0];
	float dy_l = by - s_lamb_shadow_light[1];
	float dist2 = dx_l * dx_l + dy_l * dy_l;
	/* Exclude the Lamb itself (dist < 2.0 wu) and objects outside the 290 wu glow shadow radius */
	if (dist2 < 4.0f || dist2 > 290.0f * 290.0f) return false;

	float dist = sqrtf(dist2);
	float t_fall = 1.0f - (dist * (1.0f / 290.0f));
	float atten = t_fall * t_fall * (3.0f - 2.0f * t_fall) * l_int;
	float base_alpha = (float)(b->tint_rgba & 0xFF) / 255.0f;
	float alpha = base_alpha * (0.40f + 0.58f * atten) * fminf(1.0f, t_fall * 2.5f);
	if (alpha <= 0.01f) return false;

	float rx = (b->shadow_rx > 0) ? (float)b->shadow_rx : fmaxf(2.0f, b->width * 0.34f);
	float ry = (b->shadow_ry > 0) ? (float)b->shadow_ry : fmaxf(1.0f, rx * 0.45f);
	float half_w = fmaxf(rx, b->width * 0.5f);
	float h = fmaxf(ry * 2.0f, b->height);

	float rad_x = dx_l / dist;
	float rad_y = dy_l / dist;
	/* Transverse unit vector perpendicular to radial shadow ray in (East, South) plane */
	float perp_x = -rad_y;
	float perp_y =  rad_x;

	float lamb_h = fmaxf(10.0f, fminf(28.0f, (s_lamb_shadow_light[2] + 12.0f) - b->z));
	float cast_scale = fminf(1.65f, fmaxf(0.58f, (dist * (1.0f / 54.0f)) * (24.0f / (lamb_h + 10.0f))));
	float cast_len = fminf(h * cast_scale, 175.0f);
	float cast_dx = rad_x * cast_len;
	float cast_dy = rad_y * cast_len;

	float sway_dx = basis->grass_sx * 0.55f;
	float sway_dy = basis->grass_sy * 0.55f;

	/* Point-light radial divergence widens the projected crown silhouette with distance from the base */
	float top_half_w = half_w * (1.0f + fminf(0.45f, cast_len / fmaxf(26.0f, dist)));
	float base_pad = fminf(ry * 0.55f, 4.5f);
	float lift_z = 0.34f;

	struct { float dx, dy, u, v, qx, qy; } c[4] = {
		{ -half_w * perp_x - base_pad * rad_x,            -half_w * perp_y - base_pad * rad_y,            b->u0, b->v1, -1.0f, -1.0f }, /* Base-Left (0) */
		{  half_w * perp_x - base_pad * rad_x,             half_w * perp_y - base_pad * rad_y,            b->u1, b->v1,  1.0f, -1.0f }, /* Base-Right (1) */
		{ -top_half_w * perp_x + cast_dx + sway_dx,       -top_half_w * perp_y + cast_dy + sway_dy,       b->u0, b->v0, -1.0f,  1.0f }, /* Crown-Left (2) */
		{  top_half_w * perp_x + cast_dx + sway_dx,        top_half_w * perp_y + cast_dy + sway_dy,        b->u1, b->v0,  1.0f,  1.0f }  /* Crown-Right (3) */
	};

	float c_zn[4];
	for (int k = 0; k < 4; k++)
		c_zn[k] = lift_z + shadow_corner_terrain_dz(bx + c[k].dx, by + c[k].dy, b->z, c[k].dx, c[k].dy);

	int idx[6] = { 0, 1, 2, 1, 3, 2 };

	for (int i = 0; i < 6; i++) {
		int k = idx[i];
		out_verts[i].world_pos[0] = base_pos[0] + c[k].dx * E[0] + c[k].dy * S[0] + c_zn[k] * N[0];
		out_verts[i].world_pos[1] = base_pos[1] + c[k].dx * E[1] + c[k].dy * S[1] + c_zn[k] * N[1];
		out_verts[i].world_pos[2] = base_pos[2] + c[k].dx * E[2] + c[k].dy * S[2] + c_zn[k] * N[2];

		out_verts[i].uv[0] = c[k].u;
		out_verts[i].uv[1] = c[k].v;

		out_verts[i].tint[0] = 0.0f;
		out_verts[i].tint[1] = 0.0f;
		out_verts[i].tint[2] = 0.0f;
		out_verts[i].tint[3] = alpha;

		out_verts[i].local_pos[0] = bx + c[k].dx;
		out_verts[i].local_pos[1] = by + c[k].dy;
		out_verts[i].local_pos[2] = b->z;

		out_verts[i].quad_uv[0] = c[k].qx;
		out_verts[i].quad_uv[1] = c[k].qy;
		out_verts[i].quad_uv[2] = b->z;
		out_verts[i].quad_uv[3] = -2.0f; /* -2.0f marks secondary Lamb point-light silhouette shadow quad */

		out_verts[i].sprite_bounds[0] = basis->u_min;
		out_verts[i].sprite_bounds[1] = basis->v_min;
		out_verts[i].sprite_bounds[2] = basis->u_max;
		out_verts[i].sprite_bounds[3] = basis->v_max;
	}
	return true;
}

static bool eval_billboard_lamb_shadow_quad_with_origin(const lam_billboard_gpu_t *b,
                                                        const nd_planet_def_t *pdef,
                                                        const double ref_origin[3],
                                                        nd_seven_gl_bb_vert_t out_verts[6])
{
	if (!b || !out_verts) return false;
	billboard_basis_t basis;
	eval_billboard_basis(b, pdef, ref_origin, &basis);
	return eval_billboard_lamb_shadow_quad_from_basis(b, &basis, out_verts);
}

bool nd_seven_gl_eval_billboard_lamb_shadow_quad(const lam_billboard_gpu_t *b,
                                                 const nd_planet_def_t *pdef,
                                                 nd_seven_gl_bb_vert_t out_verts[6])
{
	return eval_billboard_lamb_shadow_quad_with_origin(b, pdef, NULL, out_verts);
}

static nd_seven_gl_bb_inst_t s_bb_instances[MAX_BB_INSTANCES];
static nd_seven_gl_bb_inst_t s_bb_other_instances[MAX_BB_INSTANCES];

static inline void pack_bb_quad_instance(const lam_billboard_gpu_t *b,
                                         const billboard_basis_t *basis,
                                         float cos_y, float sin_y,
                                         float cos_p, float sin_p,
                                         nd_seven_gl_bb_inst_t *out)
{
	const float *E = basis->E;
	const float *S = basis->S;
	const float *N = basis->N;
	const float *base_pos = basis->base_pos;

	float horiz[3] = {
		sin_y * E[0] + cos_y * S[0],
		sin_y * E[1] + cos_y * S[1],
		sin_y * E[2] + cos_y * S[2]
	};
	float right[3] = {
		cos_y * E[0] - sin_y * S[0],
		cos_y * E[1] - sin_y * S[1],
		cos_y * E[2] - sin_y * S[2]
	};
	float up[3] = {
		cos_p * N[0] - sin_p * horiz[0],
		cos_p * N[1] - sin_p * horiz[1],
		cos_p * N[2] - sin_p * horiz[2]
	};

	float half_w = b->width * 0.5f;
	float h = b->height;
	float ay = b->anchor_y;

	float tint[4] = {
		(float)((b->tint_rgba >> 24) & 0xFF) / 255.0f,
		(float)((b->tint_rgba >> 16) & 0xFF) / 255.0f,
		(float)((b->tint_rgba >> 8) & 0xFF) / 255.0f,
		(float)(b->tint_rgba & 0xFF) / 255.0f
	};

	float grass_sx = basis->grass_sx;
	float grass_sy = basis->grass_sy;
	float grass_dz = basis->grass_dz;
	float grass_rustle = basis->grass_rustle;

	if (basis->flora_sway_scale > 0.0f && h > 0.0f) {
		float max_vert_loss = 0.40f * h;
		float horiz_sway = grass_sx * sin_y + grass_sy * cos_y;
		float vert_loss = horiz_sway * sin_p + grass_dz * cos_p;
		if (vert_loss > max_vert_loss && vert_loss > 1e-4f) {
			float keep = max_vert_loss / vert_loss;
			grass_sx *= keep;
			grass_sy *= keep;
			grass_dz *= keep;
		}
	}

	float sx = (basis->flora_sway_scale > 0.0f) ? grass_sx : 0.0f;
	float sy = (basis->flora_sway_scale > 0.0f) ? grass_sy : 0.0f;
	float dz = (basis->flora_sway_scale > 0.0f) ? grass_dz : 0.0f;
	float sway_right = sx * cos_y - sy * sin_y;

	out->pos0[0] = base_pos[0] - half_w * right[0] - ay * up[0];
	out->pos0[1] = base_pos[1] - half_w * right[1] - ay * up[1];
	out->pos0[2] = base_pos[2] - half_w * right[2] - ay * up[2];
	out->pos0[3] = -half_w;

	out->pos1[0] = base_pos[0] + half_w * right[0] - ay * up[0];
	out->pos1[1] = base_pos[1] + half_w * right[1] - ay * up[1];
	out->pos1[2] = base_pos[2] + half_w * right[2] - ay * up[2];
	out->pos1[3] = half_w;

	out->pos2[0] = base_pos[0] - half_w * right[0] + (h - ay) * up[0] + sx * E[0] + sy * S[0] - dz * N[0];
	out->pos2[1] = base_pos[1] - half_w * right[1] + (h - ay) * up[1] + sx * E[1] + sy * S[1] - dz * N[1];
	out->pos2[2] = base_pos[2] - half_w * right[2] + (h - ay) * up[2] + sx * E[2] + sy * S[2] - dz * N[2];
	out->pos2[3] = -half_w + sway_right;

	out->pos3[0] = base_pos[0] + half_w * right[0] + (h - ay) * up[0] + sx * E[0] + sy * S[0] - dz * N[0];
	out->pos3[1] = base_pos[1] + half_w * right[1] + (h - ay) * up[1] + sx * E[1] + sy * S[1] - dz * N[1];
	out->pos3[2] = base_pos[2] + half_w * right[2] + (h - ay) * up[2] + sx * E[2] + sy * S[2] - dz * N[2];
	out->pos3[3] = half_w + sway_right;

	out->uv[0] = b->u0;
	out->uv[1] = b->v0;
	out->uv[2] = b->u1;
	out->uv[3] = b->v1;

	float sheen = (b->style == ND_BB_STYLE_GRASS || (b->pad0 & 0x03u) == 1u) ? (grass_rustle * 0.14f) : 0.0f;
	out->tint[0] = fminf(1.0f, tint[0] + sheen);
	out->tint[1] = fminf(1.0f, tint[1] + sheen * 1.1f);
	out->tint[2] = fminf(1.0f, tint[2] + sheen * 0.5f);
	out->tint[3] = tint[3];

	out->param[0] = b->x;
	out->param[1] = b->y;
	out->param[2] = b->z;
	out->param[3] = (float)b->style + ((b->pad0 & 0x80u) ? 16.0f : 0.0f);
}

static inline void pack_bb_shadow_quad_instance(const lam_billboard_gpu_t *b,
                                                const billboard_basis_t *basis,
                                                nd_seven_gl_bb_inst_t *out)
{
	const float *E = basis->E;
	const float *S = basis->S;
	const float *N = basis->N;
	float north_shift = (b->mesh_id > 0 && b->anchor_y > 0.0f) ? (b->anchor_y * 0.5f) : 0.0f;
	float base_pos[3] = {
		basis->base_pos[0] - S[0] * north_shift,
		basis->base_pos[1] - S[1] * north_shift,
		basis->base_pos[2] - S[2] * north_shift
	};

	float rx = (b->shadow_rx > 0) ? (float)b->shadow_rx : fmaxf(2.0f, b->width * 0.34f);
	float ry = (b->shadow_ry > 0) ? (float)b->shadow_ry : fmaxf(1.0f, rx * 0.45f);
	float half_w = fmaxf(rx, b->width * 0.5f);
	float h = fmaxf(ry * 2.0f, b->height);

	float sun_len2 = s_shadow_sun_dir[0] * s_shadow_sun_dir[0] +
	                 s_shadow_sun_dir[1] * s_shadow_sun_dir[1] +
	                 s_shadow_sun_dir[2] * s_shadow_sun_dir[2];
	float sun_e = -0.50f, sun_s = -0.42f, sun_n = 0.76f;
	if (sun_len2 > 1e-4f) {
		float le = s_shadow_sun_dir[0] * E[0] + s_shadow_sun_dir[1] * E[1] + s_shadow_sun_dir[2] * E[2];
		float ls = s_shadow_sun_dir[0] * S[0] + s_shadow_sun_dir[1] * S[1] + s_shadow_sun_dir[2] * S[2];
		float ln = s_shadow_sun_dir[0] * N[0] + s_shadow_sun_dir[1] * N[1] + s_shadow_sun_dir[2] * N[2];
		if (ln > -0.05f) {
			sun_e = -0.46f + 0.18f * le;
			sun_s = -0.40f + 0.18f * ls;
			sun_n = fmaxf(0.42f, 0.65f + 0.25f * ln);
		}
	}

	float cast_scale = 0.74f / sun_n;
	if (cast_scale > 1.65f) cast_scale = 1.65f;
	float cast_dx = -sun_e * h * cast_scale;
	float cast_dy = -sun_s * h * cast_scale;

	float sway_dx = basis->grass_sx;
	float sway_dy = basis->grass_sy;

	float base_pad_y = fminf(ry * 0.55f, 4.5f);
	float base_alpha = (float)(b->tint_rgba & 0xFF) / 255.0f;
	float alpha = base_alpha * 0.76f;
	float lift_z = 0.28f;
	float bx = b->x;
	float by = b->y - north_shift;

	float c_dx[4] = { -half_w, half_w, -half_w + cast_dx + sway_dx, half_w + cast_dx + sway_dx };
	float c_dy[4] = { -base_pad_y, -base_pad_y, cast_dy + sway_dy, cast_dy + sway_dy };
	float c_zn[4] = { lift_z, lift_z, lift_z, lift_z };
	float dx_cam = bx - s_gl.cur_tx;
	float dy_cam = by - s_gl.cur_ty;
	if (s_gl.cur_dist < 850.0f && (dx_cam * dx_cam + dy_cam * dy_cam) < (850.0f * 850.0f)) {
		for (int k = 0; k < 4; k++)
			c_zn[k] = lift_z + shadow_corner_terrain_dz(bx + c_dx[k], by + c_dy[k], b->z, c_dx[k], c_dy[k]);
	}

	out->pos0[0] = base_pos[0] + c_dx[0] * E[0] + c_dy[0] * S[0] + c_zn[0] * N[0];
	out->pos0[1] = base_pos[1] + c_dx[0] * E[1] + c_dy[0] * S[1] + c_zn[0] * N[1];
	out->pos0[2] = base_pos[2] + c_dx[0] * E[2] + c_dy[0] * S[2] + c_zn[0] * N[2];
	out->pos0[3] = c_dx[0];

	out->pos1[0] = base_pos[0] + c_dx[1] * E[0] + c_dy[1] * S[0] + c_zn[1] * N[0];
	out->pos1[1] = base_pos[1] + c_dx[1] * E[1] + c_dy[1] * S[1] + c_zn[1] * N[1];
	out->pos1[2] = base_pos[2] + c_dx[1] * E[2] + c_dy[1] * S[2] + c_zn[1] * N[2];
	out->pos1[3] = c_dx[1];

	out->pos2[0] = base_pos[0] + c_dx[2] * E[0] + c_dy[2] * S[0] + c_zn[2] * N[0];
	out->pos2[1] = base_pos[1] + c_dx[2] * E[1] + c_dy[2] * S[1] + c_zn[2] * N[1];
	out->pos2[2] = base_pos[2] + c_dx[2] * E[2] + c_dy[2] * S[2] + c_zn[2] * N[2];
	out->pos2[3] = c_dx[2];

	out->pos3[0] = base_pos[0] + c_dx[3] * E[0] + c_dy[3] * S[0] + c_zn[3] * N[0];
	out->pos3[1] = base_pos[1] + c_dx[3] * E[1] + c_dy[3] * S[1] + c_zn[3] * N[1];
	out->pos3[2] = base_pos[2] + c_dx[3] * E[2] + c_dy[3] * S[2] + c_zn[3] * N[2];
	out->pos3[3] = c_dx[3];

	out->uv[0] = b->u0;
	out->uv[1] = b->v0;
	out->uv[2] = b->u1;
	out->uv[3] = b->v1;

	out->tint[0] = 0.0f;
	out->tint[1] = 0.0f;
	out->tint[2] = 0.0f;
	out->tint[3] = alpha;

	out->param[0] = b->x;
	out->param[1] = b->y - north_shift;
	out->param[2] = b->z;
	out->param[3] = -1.0f;
}

static inline bool pack_bb_lamb_shadow_quad_instance(const lam_billboard_gpu_t *b,
                                                     const billboard_basis_t *basis,
                                                     nd_seven_gl_bb_inst_t *out)
{
	if (!b || !basis || !out) return false;
	float l_int = s_lamb_shadow_light[3];
	if (l_int <= 0.01f) return false;

	const float *E = basis->E;
	const float *S = basis->S;
	const float *N = basis->N;
	float north_shift = (b->mesh_id > 0 && b->anchor_y > 0.0f) ? (b->anchor_y * 0.5f) : 0.0f;
	float base_pos[3] = {
		basis->base_pos[0] - S[0] * north_shift,
		basis->base_pos[1] - S[1] * north_shift,
		basis->base_pos[2] - S[2] * north_shift
	};
	float bx = b->x;
	float by = b->y - north_shift;

	float dx_l = bx - s_lamb_shadow_light[0];
	float dy_l = by - s_lamb_shadow_light[1];
	float dist2 = dx_l * dx_l + dy_l * dy_l;
	if (dist2 < 4.0f || dist2 > 290.0f * 290.0f) return false;

	float dist = sqrtf(dist2);
	float t_fall = 1.0f - (dist * (1.0f / 290.0f));
	float atten = t_fall * t_fall * (3.0f - 2.0f * t_fall) * l_int;
	float base_alpha = (float)(b->tint_rgba & 0xFF) / 255.0f;
	float alpha = base_alpha * (0.40f + 0.58f * atten) * fminf(1.0f, t_fall * 2.5f);
	if (alpha <= 0.01f) return false;

	float rx = (b->shadow_rx > 0) ? (float)b->shadow_rx : fmaxf(2.0f, b->width * 0.34f);
	float ry = (b->shadow_ry > 0) ? (float)b->shadow_ry : fmaxf(1.0f, rx * 0.45f);
	float half_w = fmaxf(rx, b->width * 0.5f);
	float h = fmaxf(ry * 2.0f, b->height);

	/* (dx_l, dy_l) is already in the local 2D (East, South) tangent plane */
	float rad_x = dx_l / dist;
	float rad_y = dy_l / dist;
	/* Transverse unit vector perpendicular to the radial light-to-object shadow ray */
	float perp_x = -rad_y;
	float perp_y =  rad_x;

	float lamb_h = fmaxf(10.0f, fminf(28.0f, (s_lamb_shadow_light[2] + 12.0f) - b->z));
	float cast_scale = fminf(1.65f, fmaxf(0.58f, (dist * (1.0f / 54.0f)) * (24.0f / (lamb_h + 10.0f))));
	float cast_len = fminf(h * cast_scale, 175.0f);
	float cast_dx = rad_x * cast_len;
	float cast_dy = rad_y * cast_len;

	float sway_dx = basis->grass_sx * 0.55f;
	float sway_dy = basis->grass_sy * 0.55f;

	float base_pad = fminf(ry * 0.55f, 4.5f);
	float top_half_w = half_w * (1.0f + fminf(0.45f, cast_len / fmaxf(26.0f, dist)));
	float lift_z = 0.34f;

	float c_dx[4] = {
		-half_w * perp_x - base_pad * rad_x,
		 half_w * perp_x - base_pad * rad_x,
		-top_half_w * perp_x + cast_dx + sway_dx,
		 top_half_w * perp_x + cast_dx + sway_dy * 0.0f + sway_dx
	};
	float c_dy[4] = {
		-half_w * perp_y - base_pad * rad_y,
		 half_w * perp_y - base_pad * rad_y,
		-top_half_w * perp_y + cast_dy + sway_dy,
		 top_half_w * perp_y + cast_dy + sway_dy
	};
	float c_zn[4];
	for (int k = 0; k < 4; k++)
		c_zn[k] = lift_z + shadow_corner_terrain_dz(bx + c_dx[k], by + c_dy[k], b->z, c_dx[k], c_dy[k]);

	out->pos0[0] = base_pos[0] + c_dx[0] * E[0] + c_dy[0] * S[0] + c_zn[0] * N[0];
	out->pos0[1] = base_pos[1] + c_dx[0] * E[1] + c_dy[0] * S[1] + c_zn[0] * N[1];
	out->pos0[2] = base_pos[2] + c_dx[0] * E[2] + c_dy[0] * S[2] + c_zn[0] * N[2];
	out->pos0[3] = c_dx[0];

	out->pos1[0] = base_pos[0] + c_dx[1] * E[0] + c_dy[1] * S[0] + c_zn[1] * N[0];
	out->pos1[1] = base_pos[1] + c_dx[1] * E[1] + c_dy[1] * S[1] + c_zn[1] * N[1];
	out->pos1[2] = base_pos[2] + c_dx[1] * E[2] + c_dy[1] * S[2] + c_zn[1] * N[2];
	out->pos1[3] = c_dx[1];

	out->pos2[0] = base_pos[0] + c_dx[2] * E[0] + c_dy[2] * S[0] + c_zn[2] * N[0];
	out->pos2[1] = base_pos[1] + c_dx[2] * E[1] + c_dy[2] * S[1] + c_zn[2] * N[1];
	out->pos2[2] = base_pos[2] + c_dx[2] * E[2] + c_dy[2] * S[2] + c_zn[2] * N[2];
	out->pos2[3] = c_dx[2];

	out->pos3[0] = base_pos[0] + c_dx[3] * E[0] + c_dy[3] * S[0] + c_zn[3] * N[0];
	out->pos3[1] = base_pos[1] + c_dx[3] * E[1] + c_dy[3] * S[1] + c_zn[3] * N[1];
	out->pos3[2] = base_pos[2] + c_dx[3] * E[2] + c_dy[3] * S[2] + c_zn[3] * N[2];
	out->pos3[3] = c_dx[3];

	out->uv[0] = b->u0;
	out->uv[1] = b->v0;
	out->uv[2] = b->u1;
	out->uv[3] = b->v1;

	out->tint[0] = 0.0f;
	out->tint[1] = 0.0f;
	out->tint[2] = 0.0f;
	out->tint[3] = alpha;

	out->param[0] = bx;
	out->param[1] = by;
	out->param[2] = b->z;
	out->param[3] = -2.0f;
	return true;
}


static nd_seven_gl_mesh3d_inst_t s_mesh3d_staging[MAX_BB_INSTANCES];
static uint8_t s_mesh3d_staging_ids[MAX_BB_INSTANCES];
static nd_seven_gl_mesh3d_inst_t s_mesh3d_sorted[MAX_BB_INSTANCES];
static uint32_t s_mesh3d_counts[ND_SEVEN_MESH3D_TOTAL];
static uint32_t s_mesh3d_offsets[ND_SEVEN_MESH3D_TOTAL];
static uint32_t s_mesh3d_cursors[ND_SEVEN_MESH3D_TOTAL];

bool nd_seven_gl_billboard_is_mesh3d_only(const lam_billboard_gpu_t *b)
{
	if (!b || b->mesh_id == 0)
		return false;
	if (b->mesh_id >= ND_MESH3D_UNDERSTORY_BASE && b->mesh_id < ND_SEVEN_MESH3D_TOTAL)
		return true;
	if (b->v0 == b->v1)
		return true;
	if (b->v0 <= 0.0001f && b->v1 >= 0.9999f && fabsf(b->u1 - b->u0) >= 0.999f)
		return true;
	return false;
}

void nd_seven_gl_eval_mesh3d_anchor(const lam_billboard_gpu_t *b,
                                    float out_grid_pos[3])
{
	/* Center 3D trunks/rocks at (b->x, b->y - anchor_y * 0.5f) along North (-S);
	 * 3D mesh vertices are rooted at local z = 0 on basis->base_pos (at b->z), so
	 * param[2] must stay at b->z to keep the 3D shadow map, cutaway, and water line aligned. */
	bool is_grass = (b->style == ND_BB_STYLE_GRASS);
	float north_shift = (!is_grass && b->anchor_y > 0.0f) ? (b->anchor_y * 0.5f) : 0.0f;
	out_grid_pos[0] = b->x;
	out_grid_pos[1] = b->y - north_shift;
	out_grid_pos[2] = b->z;
}

void nd_seven_gl_unpack_rgba_u32(uint32_t rgba, float out_rgba[4])
{
	out_rgba[0] = (float)((rgba >> 24) & 0xFFu) / 255.0f;
	out_rgba[1] = (float)((rgba >> 16) & 0xFFu) / 255.0f;
	out_rgba[2] = (float)((rgba >>  8) & 0xFFu) / 255.0f;
	out_rgba[3] = (float)( rgba        & 0xFFu) / 255.0f;
}

uint8_t nd_seven_gl_pack_pad0(uint8_t kind_or_zone, unsigned biome, bool can_submerge)
{
	uint8_t bio_slot = (biome <= 18u) ? (uint8_t)(biome + 1u) : 0u;
	return (uint8_t)((kind_or_zone & 3u) | ((bio_slot & 31u) << 2u) | (can_submerge ? 0x80u : 0u));
}

unsigned nd_seven_gl_unpack_pad0_biome(uint8_t pad0, bool *out_has_biome)
{
	uint8_t bio_slot = (pad0 >> 2u) & 31u;
	if (bio_slot == 0u) {
		if (out_has_biome) *out_has_biome = false;
		return 0u;
	}
	if (out_has_biome) *out_has_biome = true;
	return (unsigned)(bio_slot - 1u);
}

static inline void pack_mesh3d_instance(const lam_billboard_gpu_t *b,
                                        const billboard_basis_t *basis,
                                        const nd_mesh3d_slice_t *sl,
                                        bool want_shadow,
                                        bool want_lamb_shadow,
                                        nd_seven_gl_mesh3d_inst_t *out)
{
	float ref_w = (sl && sl->mesh_w_wu > 0.1f) ? sl->mesh_w_wu : ((sl && sl->sprite_w > 0.1f) ? (float)sl->sprite_w : 12.0f);
	float ref_h = (sl && sl->mesh_h_wu > 0.1f) ? sl->mesh_h_wu : ((sl && sl->sprite_h > 0.1f) ? (float)sl->sprite_h : 12.0f);
	float scale_xy = fmaxf(0.15f, b->width / ref_w);
	float scale_z  = fmaxf(0.15f, b->height / ref_h);

	/* Deterministic full 360-deg 3D yaw rotation and horizontal mirror from u0 > u1 */
	uint32_t h = (uint32_t)(b->x * 73.0f) * 0x9E3779B1u ^ (uint32_t)(b->y * 131.0f) * 0x85EBCA77u ^
	             ((uint32_t)b->mesh_id * 0xC2B2AE3Du);
	h ^= h >> 16;
	h *= 0x7FEB352Du;
	h ^= h >> 15;
	float yaw_norm = ((float)(h & 0xFFFFu) * (1.0f / 65535.0f)) - 0.5f;
	float flip = (b->u0 > b->u1) ? -1.0f : 1.0f;
	const float *E = basis->E;
	const float *S = basis->S;
	const float *N = basis->N;

	float wind_sx = (basis->flora_sway_scale > 0.0f) ? basis->grass_sx : 0.0f;
	float wind_sy = (basis->flora_sway_scale > 0.0f) ? basis->grass_sy : 0.0f;

	float anchor[3];
	nd_seven_gl_eval_mesh3d_anchor(b, anchor);
	float bx = anchor[0];
	float by = anchor[1];
	float bz = anchor[2];
	float north_shift = b->y - by;
	out->base_scale[0] = basis->base_pos[0] - S[0] * north_shift;
	out->base_scale[1] = basis->base_pos[1] - S[1] * north_shift;
	out->base_scale[2] = basis->base_pos[2] - S[2] * north_shift;
	out->base_scale[3] = scale_xy;

	out->axis_e[0] = E[0];
	out->axis_e[1] = E[1];
	out->axis_e[2] = E[2];
	out->axis_e[3] = scale_z;

	out->axis_s[0] = S[0];
	out->axis_s[1] = S[1];
	out->axis_s[2] = S[2];
	out->axis_s[3] = wind_sx;

	out->axis_n[0] = N[0];
	out->axis_n[1] = N[1];
	out->axis_n[2] = N[2];
	out->axis_n[3] = wind_sy;

	float rgba[4];
	nd_seven_gl_unpack_rgba_u32(b->tint_rgba, rgba);
	float sheen = (b->style == ND_BB_STYLE_GRASS || (b->pad0 & 0x03u) == 1u) ? (basis->grass_rustle * 0.14f) : 0.0f;
	out->tint[0] = fminf(1.0f, rgba[0] + sheen);
	out->tint[1] = fminf(1.0f, rgba[1] + sheen * 1.1f);
	out->tint[2] = fminf(1.0f, rgba[2] + sheen * 0.5f);
	out->tint[3] = rgba[3];

	uint8_t bio_slot = (b->pad0 >> 2u) & 31u;
	out->param[0] = bx;
	out->param[1] = by;
	out->param[2] = bz;
	out->param[3] = (float)b->style + ((b->pad0 & 0x80u) ? 16.0f : 0.0f) + (float)bio_slot * 32.0f;

	/* Local terrain plane (gx, gy, curv) and packed 3D shadow flags + yaw01 in a_iterr */
	float gx = 0.0f, gy = 0.0f, curv = 0.0f;
	float dx_c = bx - s_gl.cur_tx;
	float dy_c = by - s_gl.cur_ty;
	bool want_slope_norm = (b->style == ND_BB_STYLE_GRASS || b->style == ND_BB_STYLE_FLORA) &&
	                       (dx_c * dx_c + dy_c * dy_c < 650.0f * 650.0f);
	bool need_terr_plane = want_lamb_shadow || want_slope_norm || (want_shadow && !s_gl.tree_shadow_ready);
	if (need_terr_plane && nd_seven_get_chunk_sys()) {
		static struct {
			int32_t  qx, qy, qz;
			uint32_t key_epoch;
			float    gx, gy, curv;
		} s_terr_cache[2048][4];
		static uint8_t s_terr_rr[2048];
		int32_t qx = (int32_t)floorf(bx + 0.5f);
		int32_t qy = (int32_t)floorf(by + 0.5f);
		int32_t qz = (int32_t)floorf(bz * 16.0f + 0.5f);
		uint32_t world_id = (uint32_t)nd_seven_get_world();
		uint32_t lod_bkt = (uint32_t)(s_gl.cur_dist * 0.01f);
		uint32_t epoch = ((nd_seven_ground_forget_seq() & 0xFFFFu) << 16) ^
		                 ((world_id & 0xFFu) << 8) ^ (lod_bkt & 0xFFu);
		if (epoch == 0u) epoch = 1u;
		uint32_t h = ((uint32_t)qx * 2654435761u) ^ ((uint32_t)qy * 2246822519u) ^
		             ((uint32_t)qz * 3266489917u) ^ (epoch * 668265263u);
		uint32_t set = (h ^ (h >> 11)) & 2047u;
		int hit = -1;
		for (int w = 0; w < 4; w++) {
			if (s_terr_cache[set][w].key_epoch == epoch &&
			    s_terr_cache[set][w].qx == qx &&
			    s_terr_cache[set][w].qy == qy &&
			    s_terr_cache[set][w].qz == qz) {
				hit = w;
				break;
			}
		}
		if (hit >= 0) {
			gx = s_terr_cache[set][hit].gx;
			gy = s_terr_cache[set][hit].gy;
			curv = s_terr_cache[set][hit].curv;
		} else {
			const float step = 16.0f;
			float dz_e = shadow_corner_terrain_dz(bx + step, by, bz, step, 0.0f);
			float dz_w = shadow_corner_terrain_dz(bx - step, by, bz, -step, 0.0f);
			float dz_s = shadow_corner_terrain_dz(bx, by + step, bz, 0.0f, step);
			float dz_n = shadow_corner_terrain_dz(bx, by - step, bz, 0.0f, -step);
			gx = (dz_e - dz_w) * (0.5f / step);
			gy = (dz_s - dz_n) * (0.5f / step);
			curv = (dz_e + dz_w + dz_s + dz_n) * (0.25f / (step * step));
			int dst_w = -1;
			for (int w = 0; w < 4; w++) {
				if (s_terr_cache[set][w].key_epoch != epoch) {
					dst_w = w;
					break;
				}
			}
			if (dst_w < 0)
				dst_w = (int)(s_terr_rr[set]++ & 3u);
			s_terr_cache[set][dst_w].qx = qx;
			s_terr_cache[set][dst_w].qy = qy;
			s_terr_cache[set][dst_w].qz = qz;
			s_terr_cache[set][dst_w].key_epoch = epoch;
			s_terr_cache[set][dst_w].gx = gx;
			s_terr_cache[set][dst_w].gy = gy;
			s_terr_cache[set][dst_w].curv = curv;
		}
	}
	float sh_flags = (want_shadow ? 1.0f : 0.0f) + (want_lamb_shadow ? 2.0f : 0.0f);
	float yaw01 = fminf(0.999f, fmaxf(0.001f, yaw_norm + 0.5f));
	out->terr[0] = gx;
	out->terr[1] = gy;
	out->terr[2] = curv;
	out->terr[3] = flip * (sh_flags + yaw01);
}

static uint8_t s_mesh3d_has_fade[ND_SEVEN_MESH3D_TOTAL];

static void gl_bind_mesh3d_pipe_uniforms(const gl_mesh3d_pipe_t *pipe,
                                         float bb_sea_level, float space_t,
                                         bool bind_textures)
{
	(void)space_t;
	glUseProgram(pipe->prog);
	gl_bind_csm(pipe->uCloudShadowMap);
	if (s_gl.fol_atlas_tex && pipe->uBranchletTex >= 0) {
		if (bind_textures) {
			glActiveTexture(GL_TEXTURE0 + 3);
			glBindTexture(GL_TEXTURE_2D, s_gl.fol_atlas_tex);
		}
		glUniform1i(pipe->uBranchletTex, 3);
		if (s_gl.fol_atlas_norm_tex && pipe->uBranchletNorm >= 0) {
			if (bind_textures) {
				glActiveTexture(GL_TEXTURE0 + 4);
				glBindTexture(GL_TEXTURE_2D, s_gl.fol_atlas_norm_tex);
			}
			glUniform1i(pipe->uBranchletNorm, 4);
		}
	}
	if (s_gl.fol_grass_tex && pipe->uGrassTex >= 0) {
		if (bind_textures) {
			glActiveTexture(GL_TEXTURE0 + 5);
			glBindTexture(GL_TEXTURE_2D, s_gl.fol_grass_tex);
		}
		glUniform1i(pipe->uGrassTex, 5);
		if (s_gl.fol_grass_norm_tex && pipe->uGrassNorm >= 0) {
			if (bind_textures) {
				glActiveTexture(GL_TEXTURE0 + 6);
				glBindTexture(GL_TEXTURE_2D, s_gl.fol_grass_norm_tex);
			}
			glUniform1i(pipe->uGrassNorm, 6);
		}
	}
	if (s_gl.fol_bark_tex && pipe->uBarkTex >= 0) {
		if (bind_textures) {
			glActiveTexture(GL_TEXTURE0 + 8);
			glBindTexture(GL_TEXTURE_2D, s_gl.fol_bark_tex);
		}
		glUniform1i(pipe->uBarkTex, 8);
		if (s_gl.fol_bark_norm_tex && pipe->uBarkNorm >= 0) {
			if (bind_textures) {
				glActiveTexture(GL_TEXTURE0 + 9);
				glBindTexture(GL_TEXTURE_2D, s_gl.fol_bark_norm_tex);
			}
			glUniform1i(pipe->uBarkNorm, 9);
		}
	}
	if (bind_textures)
		glActiveTexture(GL_TEXTURE0);
	glUniform1f(pipe->uSeaLevel, bb_sea_level);
}

static void gl_draw_mesh3d_bucket(uint32_t m)
{
	uint32_t icnt = s_mesh3d_counts[m];
	if (icnt == 0) return;
	const nd_mesh3d_slice_t *sl = nd_seven_mesh3d_slice((uint8_t)m);
	if (!sl || sl->idx_count == 0) return;

	size_t inst_base_bytes = (size_t)s_mesh3d_offsets[m] * sizeof(nd_seven_gl_mesh3d_inst_t);
	for (GLuint a = 0; a < 7; a++) {
		lam_gl_cmd_vertex_attrib_ptr(&s_gl.cmd_buf, 3 + a, 4, GL_FLOAT, GL_FALSE,
		                             sizeof(nd_seven_gl_mesh3d_inst_t),
		                             inst_base_bytes + (size_t)(a * 16));
	}
	lam_gl_cmd_draw_elements_instanced(&s_gl.cmd_buf, GL_TRIANGLES, (GLsizei)sl->idx_count, GL_UNSIGNED_INT,
	                                   (size_t)sl->first_idx * sizeof(uint32_t), (GLsizei)icnt);
	if (s_gl.indirect_cmd_count < ND_GPU_INDIRECT_MAX) {
		nd_gpu_draw_elements_indirect_cmd_t *cmd = &s_gl.indirect_cmds[s_gl.indirect_cmd_count++];
		cmd->count = sl->idx_count;
		cmd->instance_count = icnt;
		cmd->first_index = sl->first_idx;
		cmd->base_vertex = 0;
		cmd->base_instance = s_mesh3d_offsets[m];
	}
}

static void flush_mesh3d_staging(uint32_t mesh3d_n, float bb_sea_level, float space_t,
                                 bool any_sun_shadow, bool any_lamb_shadow)
{
	if (mesh3d_n == 0 || !s_gl.mesh3d_pipe.prog || !s_gl.mesh3d_vbo || !s_gl.mesh3d_ibo || !s_gl.mesh3d_inst_vbo)
		return;

	memset(s_mesh3d_counts, 0, sizeof(s_mesh3d_counts));
	memset(s_mesh3d_has_fade, 0, sizeof(s_mesh3d_has_fade));
	for (uint32_t i = 0; i < mesh3d_n; i++) {
		uint8_t mid = s_mesh3d_staging_ids[i];
		if (mid > 0 && mid < ND_SEVEN_MESH3D_TOTAL) {
			s_mesh3d_counts[mid]++;
			if (s_mesh3d_staging[i].tint[3] < 0.995f)
				s_mesh3d_has_fade[mid] = 1u;
		}
	}
	uint32_t running = 0;
	bool any_opaque_c1 = false;
	bool any_alpha_c2 = false;
	bool has_opaque_prog = (s_gl.mesh3d_opaque_pipe.prog != 0);
	for (uint32_t m = 1; m < ND_SEVEN_MESH3D_TOTAL; m++) {
		s_mesh3d_offsets[m] = running;
		s_mesh3d_cursors[m] = running;
		running += s_mesh3d_counts[m];
		if (s_mesh3d_counts[m] > 0) {
			if (has_opaque_prog && nd_seven_mesh3d_is_opaque((uint8_t)m) && !s_mesh3d_has_fade[m])
				any_opaque_c1 = true;
			else
				any_alpha_c2 = true;
		}
	}
	if (running == 0) return;

	for (uint32_t i = 0; i < mesh3d_n; i++) {
		uint8_t mid = s_mesh3d_staging_ids[i];
		if (mid > 0 && mid < ND_SEVEN_MESH3D_TOTAL)
			s_mesh3d_sorted[s_mesh3d_cursors[mid]++] = s_mesh3d_staging[i];
	}

	gl_end_terrain_pass();
	gl_bind_mesh3d_pipe_uniforms(&s_gl.mesh3d_pipe, bb_sea_level, space_t, true);

	/* Bind 3D Mega-VBO (locations 0..2, divisor 0) + Mega-IBO */
	glBindBuffer(GL_ARRAY_BUFFER, s_gl.mesh3d_vbo);
	for (GLuint a = 0; a < 3; a++) {
		glEnableVertexAttribArray(a);
		glVertexAttribPointer(a, 4, GL_FLOAT, GL_FALSE, sizeof(nd_mesh3d_vert_t), (size_t)(a * 16));
		glVertexAttribDivisor(a, 0);
	}
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_gl.mesh3d_ibo);
	glBindBuffer(GL_ARRAY_BUFFER, s_gl.mesh3d_inst_vbo);
	for (GLuint a = 3; a < 10; a++) {
		glEnableVertexAttribArray(a);
		glVertexAttribDivisor(a, 1);
	}
	glBufferSubData(GL_ARRAY_BUFFER, 0, running * sizeof(nd_seven_gl_mesh3d_inst_t), s_mesh3d_sorted);

	/* Pass A: True 3D directional depth shadow map pass (uShadowPass = 3) into ping-pong 1024x1024 FBO.
	 * Unbinds unit 12 before writing to guarantee zero WebGL2 FBO/texture feedback loops. */
	if (s_gl.tree_shadow_ready && !s_gl.tree_shadow_skip_write && s_gl.cur_dist < 2300.0f) {
		glTimerBegin(ND_GPU_PASS_TREE_SHADOW);
		int write_idx = 1 - s_gl.tree_shadow_read_idx;
		GLuint dummy = gl_ensure_tree_shadow_dummy();
		glActiveTexture(GL_TEXTURE0 + 12);
		glBindTexture(GL_TEXTURE_2D, dummy);
		if (s_gl.mesh3d_pipe.uTreeShadowTex >= 0)
			glUniform1i(s_gl.mesh3d_pipe.uTreeShadowTex, 12);
		glActiveTexture(GL_TEXTURE0);
		glBindFramebuffer(GL_FRAMEBUFFER, s_gl.tree_shadow_fbo[write_idx]);
		glViewport(0, 0, ND_TREE_SHADOW_RES, ND_TREE_SHADOW_RES);
		glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
		glDisable(GL_BLEND);
		glDisable(GL_STENCIL_TEST);
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LEQUAL);
		glDepthMask(GL_TRUE);
		if (s_gl.terr_shadow_q_count > 0) {
			gl_flush_terrain_shadows_to_fbo();
			glUseProgram(s_gl.mesh3d_pipe.prog);
			glBindBuffer(GL_ARRAY_BUFFER, s_gl.mesh3d_vbo);
			for (GLuint a = 0; a < 3; a++) {
				glEnableVertexAttribArray(a);
				glVertexAttribPointer(a, 4, GL_FLOAT, GL_FALSE, sizeof(nd_mesh3d_vert_t), (size_t)(a * 16));
				glVertexAttribDivisor(a, 0);
			}
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_gl.mesh3d_ibo);
			glBindBuffer(GL_ARRAY_BUFFER, s_gl.mesh3d_inst_vbo);
			for (GLuint a = 3; a < 10; a++) {
				glEnableVertexAttribArray(a);
				glVertexAttribDivisor(a, 1);
			}
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, s_gl.atlas_tex);
		}
		if (s_gl.mesh3d_pipe.uTreeShadowWriteOrigin >= 0)
			glUniform3fv(s_gl.mesh3d_pipe.uTreeShadowWriteOrigin, 1, s_gl.tree_shadow_write_origin);
		if (s_gl.mesh3d_pipe.uTreeShadowWriteVP >= 0)
			glUniformMatrix4fv(s_gl.mesh3d_pipe.uTreeShadowWriteVP, 1, GL_FALSE, s_gl.tree_shadow_write_vp);
		glUniform1i(s_gl.mesh3d_pipe.uShadowPass, 3);
		for (uint32_t m = 1; m < ND_SEVEN_MESH3D_TOTAL; m++) {
			if (m >= ND_MESH3D_MEADOW_BASE && m < ND_MESH3D_MEADOW_BASE + ND_MESH3D_MEADOW_COUNT)
				continue;
			uint32_t icnt = s_mesh3d_counts[m];
			if (icnt == 0) continue;
			const nd_mesh3d_slice_t *sl = nd_seven_mesh3d_slice((uint8_t)m);
			if (!sl || sl->idx_count == 0) continue;

			size_t inst_base_bytes = (size_t)s_mesh3d_offsets[m] * sizeof(nd_seven_gl_mesh3d_inst_t);
			for (GLuint a = 0; a < 7; a++) {
				lam_gl_cmd_vertex_attrib_ptr(&s_gl.cmd_buf, 3 + a, 4, GL_FLOAT, GL_FALSE,
				                             sizeof(nd_seven_gl_mesh3d_inst_t),
				                             inst_base_bytes + (size_t)(a * 16));
			}
			lam_gl_cmd_draw_elements_instanced(&s_gl.cmd_buf, GL_TRIANGLES, (GLsizei)sl->idx_count, GL_UNSIGNED_INT,
			                                   (size_t)sl->first_idx * sizeof(uint32_t), (GLsizei)icnt);
		}
		gl_flush_cmd_ring();
		glTimerEnd(ND_GPU_PASS_TREE_SHADOW);
		s_gl.tree_shadow_write_draws = true;
		gl_bind_scene_target();
	}

	/* Bind read-side 3D directional shadow map for main 3D tree self-shadowing */
	gl_bind_tree_shadow_read(s_gl.mesh3d_pipe.uTreeShadowTex);

	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_STENCIL_TEST);

	/* Pass B: Radial Lamb point-light ground shadow (uShadowPass = 2), plus planar sun shadow
	 * fallback (uShadowPass = 1) only if the 3D directional depth shadow FBO is unavailable. */
	bool run_planar_sun = any_sun_shadow && !s_gl.tree_shadow_ready;
	if (run_planar_sun || any_lamb_shadow) {
		glDepthMask(GL_FALSE);
		for (int sp = 1; sp <= 2; sp++) {
			if (sp == 1 && !run_planar_sun) continue;
			if (sp == 2 && !any_lamb_shadow) continue;
			GLuint sbit = (sp == 1) ? 0x02u : 0x04u;
			glUniform1i(s_gl.mesh3d_pipe.uShadowPass, sp);
			glStencilMask(sbit);
			glStencilFunc(GL_EQUAL, 0, sbit);
			glStencilOp(GL_KEEP, GL_KEEP, GL_INVERT);
			for (uint32_t m = 1; m < ND_SEVEN_MESH3D_TOTAL; m++) {
				uint32_t icnt = s_mesh3d_counts[m];
				if (icnt == 0) continue;
				const nd_mesh3d_slice_t *sl = nd_seven_mesh3d_slice((uint8_t)m);
				if (!sl || sl->idx_count == 0) continue;

				size_t inst_base_bytes = (size_t)s_mesh3d_offsets[m] * sizeof(nd_seven_gl_mesh3d_inst_t);
				for (GLuint a = 0; a < 7; a++) {
					lam_gl_cmd_vertex_attrib_ptr(&s_gl.cmd_buf, 3 + a, 4, GL_FLOAT, GL_FALSE,
					                             sizeof(nd_seven_gl_mesh3d_inst_t),
					                             inst_base_bytes + (size_t)(a * 16));
				}
				lam_gl_cmd_draw_elements_instanced(&s_gl.cmd_buf, GL_TRIANGLES, (GLsizei)sl->idx_count, GL_UNSIGNED_INT,
				                                   (size_t)sl->first_idx * sizeof(uint32_t), (GLsizei)icnt);
			}
			gl_flush_cmd_ring();
		}
	}

	/* Pass C: Main 3D mesh geometry pass — split into Pass C1 (opaque Early-Z, zero discard)
	 * and Pass C2 (alpha-tested foliage / LOD-fading meshes). */
	glDepthMask(GL_TRUE);
	glStencilMask(0x01);
	glStencilFunc(GL_ALWAYS, 1, 0x01);
	glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);

	if (any_opaque_c1) {
		glTimerBegin(ND_GPU_PASS_MESH3D_OPAQUE);
		glDisable(GL_BLEND);
		gl_bind_mesh3d_pipe_uniforms(&s_gl.mesh3d_opaque_pipe, bb_sea_level, space_t, false);
		gl_bind_tree_shadow_read(s_gl.mesh3d_opaque_pipe.uTreeShadowTex);
		if (s_gl.mesh3d_opaque_pipe.uShadowPass >= 0)
			glUniform1i(s_gl.mesh3d_opaque_pipe.uShadowPass, 0);
		for (uint32_t m = 1; m < ND_SEVEN_MESH3D_TOTAL; m++) {
			if (s_mesh3d_counts[m] == 0) continue;
			if (nd_seven_mesh3d_is_opaque((uint8_t)m) && !s_mesh3d_has_fade[m])
				gl_draw_mesh3d_bucket(m);
		}
		gl_flush_cmd_ring();
		glTimerEnd(ND_GPU_PASS_MESH3D_OPAQUE);
	}

	if (any_alpha_c2) {
		glTimerBegin(ND_GPU_PASS_MESH3D_ALPHA);
		if (any_opaque_c1) {
			glUseProgram(s_gl.mesh3d_pipe.prog);
			glEnable(GL_BLEND);
		}
		glUniform1i(s_gl.mesh3d_pipe.uShadowPass, 0);
		for (uint32_t m = 1; m < ND_SEVEN_MESH3D_TOTAL; m++) {
			if (s_mesh3d_counts[m] == 0) continue;
			if (!has_opaque_prog || !nd_seven_mesh3d_is_opaque((uint8_t)m) || s_mesh3d_has_fade[m])
				gl_draw_mesh3d_bucket(m);
		}
		gl_flush_cmd_ring();
		glTimerEnd(ND_GPU_PASS_MESH3D_ALPHA);
	}

	s_gl.res_depth_valid = false;
	s_gl.res_color_valid = false;

	glStencilMask(0xFF);
	glDisable(GL_STENCIL_TEST);
	for (GLuint a = 3; a < 10; a++) {
		glVertexAttribDivisor(a, 0);
		glDisableVertexAttribArray(a);
	}
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

/* Single-pass frame staging accumulators: queue across all CPU scenery passes and flush ONCE/frame */
static uint32_t s_bb_frame_core_n = 0;
static uint32_t s_bb_frame_other_n = 0;
static uint32_t s_bb_frame_mesh3d_n = 0;
static bool     s_bb_frame_mesh3d_sun_sh = false;
static bool     s_bb_frame_mesh3d_lamb_sh = false;
static const uint32_t *s_bb_frame_atlas_rgba = NULL;

void gl_reset_billboard_frame_state(void)
{
	s_bb_frame_core_n = 0;
	s_bb_frame_other_n = 0;
	s_bb_frame_mesh3d_n = 0;
	s_bb_frame_mesh3d_sun_sh = false;
	s_bb_frame_mesh3d_lamb_sh = false;
	s_bb_frame_atlas_rgba = NULL;
}

static void gl_upload_atlas_if_needed(const uint32_t *atlas_rgba)
{
	uint32_t cur_ver = atlas_rgba_version();
	if ((!s_gl.atlas_tex || !s_gl.atlas_norm_tex || s_gl.atlas_ver != cur_ver) && atlas_rgba) {
		const uint32_t *norm_rgba = atlas_norm_rgba();
		bool first_alloc = (!s_gl.atlas_tex || !s_gl.atlas_norm_tex || s_gl.atlas_ver == 0);
		uint16_t dirty_y0 = 0, dirty_h = 0;
		int has_dirty = atlas_take_dirty_rows(&dirty_y0, &dirty_h);

		glActiveTexture(GL_TEXTURE0);
		if (first_alloc || !has_dirty) {
			if (!s_gl.atlas_tex)
				s_gl.atlas_tex = glCreateTexture();
			glBindTexture(GL_TEXTURE_2D, s_gl.atlas_tex);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ND_SEVEN_ATLAS_W, ND_SEVEN_RUNTIME_ATLAS_H, 0,
			             GL_RGBA, GL_UNSIGNED_BYTE, atlas_rgba);

			if (norm_rgba) {
				if (!s_gl.atlas_norm_tex)
					s_gl.atlas_norm_tex = glCreateTexture();
				glBindTexture(GL_TEXTURE_2D, s_gl.atlas_norm_tex);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
				glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ND_SEVEN_ATLAS_W, ND_SEVEN_RUNTIME_ATLAS_H, 0,
				             GL_RGBA, GL_UNSIGNED_BYTE, norm_rgba);
			}
		} else {
			glBindTexture(GL_TEXTURE_2D, s_gl.atlas_tex);
			glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, (GLint)dirty_y0,
			                ND_SEVEN_ATLAS_W, (GLsizei)dirty_h,
			                GL_RGBA, GL_UNSIGNED_BYTE,
			                &atlas_rgba[(size_t)dirty_y0 * ND_SEVEN_ATLAS_W]);
			if (norm_rgba) {
				glBindTexture(GL_TEXTURE_2D, s_gl.atlas_norm_tex);
				glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
				glTexSubImage2D(GL_TEXTURE_2D, 0, 0, (GLint)dirty_y0,
				                ND_SEVEN_ATLAS_W, (GLsizei)dirty_h,
				                GL_RGBA, GL_UNSIGNED_BYTE,
				                &norm_rgba[(size_t)dirty_y0 * ND_SEVEN_ATLAS_W]);
			}
		}
		s_gl.atlas_ver = cur_ver;
	}
}

static void gl_submit_staged_billboards(const uint32_t *atlas_rgba, bool final_submit)
{
	uint32_t mesh3d_n = s_bb_frame_mesh3d_n;
	uint32_t core_n = s_bb_frame_core_n;
	uint32_t other_n = s_bb_frame_other_n;
	bool mesh3d_sun_sh = s_bb_frame_mesh3d_sun_sh;
	bool mesh3d_lamb_sh = s_bb_frame_mesh3d_lamb_sh;

	s_bb_frame_core_n = 0;
	s_bb_frame_other_n = 0;
	s_bb_frame_mesh3d_n = 0;
	s_bb_frame_mesh3d_sun_sh = false;
	s_bb_frame_mesh3d_lamb_sh = false;

	uint32_t total_items = mesh3d_n + core_n + other_n;
	bool need_terr_shadow = (final_submit && s_gl.terr_shadow_q_count > 0 &&
	                         s_gl.tree_shadow_ready && !s_gl.tree_shadow_skip_write &&
	                         s_gl.cur_dist < 2300.0f);
	if (total_items == 0 && !need_terr_shadow)
		return;

	gl_end_terrain_pass();

	if (total_items > 0) {
		s_gl.perf_bb_flushes++;
		/* Build GPU Hi-Z depth pyramid once after terrain pass and before first non-trivial batch */
		if (!s_gl.gpu_hiz_built && s_gl.scene_ok && s_gl.hiz_valid &&
		    !(total_items <= 2u && s_gl.cur_dist > 2500.0f)) {
			(void)gl_build_hiz_pyramid();
		}
		gl_upload_atlas_if_needed(atlas_rgba);
	}

	float bb_sea_level = (s_gl.focal_sea_level > -900.0f) ? s_gl.focal_sea_level : s_gl.cur_sea_level;
	float space_t = fminf(1.0f, fmaxf(0.0f, (s_gl.cur_dist - 12000.0f) / 40000.0f));

	if (mesh3d_n > 0) {
		s_gl.perf_mesh3d_count += mesh3d_n;
		flush_mesh3d_staging(mesh3d_n, bb_sea_level, space_t, mesh3d_sun_sh, mesh3d_lamb_sh);
	}

	if (core_n > 0 || other_n > 0) {
		float cos_y = s_gl.cur_cos_y;
		float sin_y = s_gl.cur_sin_y;
		float cos_p = s_gl.cur_cos_p;
		float sin_p = s_gl.cur_sin_p;
		if (cos_y * cos_y + sin_y * sin_y < 0.01f) {
			float pitch_rad = ((s_gl.cur_pitch_deg > 1.0f) ? s_gl.cur_pitch_deg : 55.0f) * (PI_F / 180.0f);
			float yaw_rad = s_gl.cur_yaw_deg * (PI_F / 180.0f);
			cos_y = cosf(yaw_rad);
			sin_y = sinf(yaw_rad);
			cos_p = cosf(pitch_rad);
			sin_p = sinf(pitch_rad);
		}

		glTimerBegin(ND_GPU_PASS_BILLBOARDS);
		glUseProgram(s_gl.bb_pipe.prog);
		glUniform4f(s_gl.bb_pipe.uCamAngles, cos_y, sin_y, cos_p, sin_p);
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LEQUAL);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, s_gl.atlas_tex);
		glUniform1i(s_gl.bb_pipe.uAtlasTex, 0);
		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, s_gl.atlas_norm_tex);
		glUniform1i(s_gl.bb_pipe.uAtlasNormTex, 2);
		{
			int hiz_lvl = (s_gl.hiz_levels > 2) ? 2 : (s_gl.hiz_levels > 0 ? (s_gl.hiz_levels - 1) : 0);
			GLuint hiz_tex = (s_gl.gpu_hiz_built && hiz_lvl >= 0) ? s_gl.hiz_mip_tex[hiz_lvl] : 0;
			glActiveTexture(GL_TEXTURE0 + 11);
			glBindTexture(GL_TEXTURE_2D, hiz_tex);
			glUniform1i(s_gl.bb_pipe.uHiZTex, 11);
			glUniform1f(s_gl.bb_pipe.uHiZActive, (s_gl.gpu_hiz_built && hiz_tex != 0) ? 1.0f : 0.0f);
			glActiveTexture(GL_TEXTURE0);
		}
		gl_bind_csm(s_gl.bb_pipe.uCloudShadowMap);
		gl_bind_tree_shadow_read(s_gl.bb_pipe.uTreeShadowTex);
		glUniform1f(s_gl.bb_pipe.uSeaLevel, bb_sea_level);

		glBindBuffer(GL_ARRAY_BUFFER, s_gl.bb_vbo);
		for (GLuint a = 0; a < 7; a++) {
			glEnableVertexAttribArray(a);
			glVertexAttribPointer(a, 4, GL_FLOAT, GL_FALSE, sizeof(nd_seven_gl_bb_inst_t), (size_t)(a * 16));
			glVertexAttribDivisor(a, 1);
		}

		if (core_n > 0) {
			s_gl.perf_bb_count += core_n;
			glBufferSubData(GL_ARRAY_BUFFER, 0, core_n * sizeof(nd_seven_gl_bb_inst_t), s_bb_instances);
			/* pass 0: core */
			glUniform1i(s_gl.bb_pipe.uPass, 0);
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			glDisable(GL_BLEND);
			glDepthMask(GL_TRUE);
			glEnable(GL_STENCIL_TEST);
			glStencilMask(0x01);
			glStencilFunc(GL_ALWAYS, 1, 0x01);
			glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
			glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)core_n);
			glStencilMask(0xFF);
			glDisable(GL_STENCIL_TEST);
			/* pass 1: anti-aliased fringe */
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
			glUniform1i(s_gl.bb_pipe.uPass, 1);
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDepthMask(GL_FALSE);
			glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)core_n);
			s_gl.res_depth_valid = false;
			s_gl.res_color_valid = false;
			if (s_gl.indirect_cmd_count < ND_GPU_INDIRECT_MAX) {
				nd_gpu_draw_elements_indirect_cmd_t *cmd = &s_gl.indirect_cmds[s_gl.indirect_cmd_count++];
				cmd->count = 4u;
				cmd->instance_count = core_n;
				cmd->first_index = 0u;
				cmd->base_vertex = 0;
				cmd->base_instance = s_gl.indirect_cmd_count - 1u;
			}
		}
		if (other_n > 0) {
			glBufferSubData(GL_ARRAY_BUFFER, 0,
			                other_n * sizeof(nd_seven_gl_bb_inst_t), s_bb_other_instances);
			glUniform1i(s_gl.bb_pipe.uPass, 2);
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDepthMask(GL_FALSE);
			glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)other_n);
			s_gl.res_color_valid = false;
			if (s_gl.indirect_cmd_count < ND_GPU_INDIRECT_MAX) {
				nd_gpu_draw_elements_indirect_cmd_t *cmd = &s_gl.indirect_cmds[s_gl.indirect_cmd_count++];
				cmd->count = 4u;
				cmd->instance_count = other_n;
				cmd->first_index = 0u;
				cmd->base_vertex = 0;
				cmd->base_instance = s_gl.indirect_cmd_count - 1u;
			}
		}

		glTimerEnd(ND_GPU_PASS_BILLBOARDS);
		for (GLuint a = 0; a < 7; a++) {
			glVertexAttribDivisor(a, 0);
			if (a > 0) glDisableVertexAttribArray(a);
		}
	}

	if (final_submit && s_gl.terr_shadow_q_count > 0 && s_gl.tree_shadow_ready &&
	    !s_gl.tree_shadow_skip_write && s_gl.cur_dist < 2300.0f) {
		glTimerBegin(ND_GPU_PASS_TREE_SHADOW);
		int write_idx = 1 - s_gl.tree_shadow_read_idx;
		GLuint dummy = gl_ensure_tree_shadow_dummy();
		glActiveTexture(GL_TEXTURE0 + 12);
		glBindTexture(GL_TEXTURE_2D, dummy);
		glActiveTexture(GL_TEXTURE0);
		glBindFramebuffer(GL_FRAMEBUFFER, s_gl.tree_shadow_fbo[write_idx]);
		glViewport(0, 0, ND_TREE_SHADOW_RES, ND_TREE_SHADOW_RES);
		glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
		glDisable(GL_BLEND);
		glDisable(GL_STENCIL_TEST);
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LEQUAL);
		glDepthMask(GL_TRUE);
		gl_flush_terrain_shadows_to_fbo();
		glTimerEnd(ND_GPU_PASS_TREE_SHADOW);
		s_gl.tree_shadow_write_draws = true;
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		gl_bind_scene_target();
	}
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDepthMask(GL_TRUE);
}

void nd_seven_gl_queue_billboards(const lam_billboard_gpu_t *batch,
                                  uint32_t count,
                                  const uint32_t *atlas_rgba)
{
	if (!batch || count == 0) return;
	if (!s_gl.initialized || !s_gl.pipelines_ready || !s_gl.bb_pipe.prog) return;
	if (atlas_rgba)
		s_bb_frame_atlas_rgba = atlas_rgba;

	float det_scale = nd_seven_get_detail_scale();
	bool emit_shadows = (s_gl.cur_dist < 5000.0f * det_scale);
	bool emit_grass_shadows = (s_gl.cur_dist < 1800.0f * det_scale);
	bool emit_lamb_shadows = (s_lamb_shadow_light[3] > 0.01f && s_gl.cur_dist < 3800.0f * det_scale);
	float cos_y = s_gl.cur_cos_y;
	float sin_y = s_gl.cur_sin_y;
	float cos_p = s_gl.cur_cos_p;
	float sin_p = s_gl.cur_sin_p;
	if (cos_y * cos_y + sin_y * sin_y < 0.01f) {
		float pitch_rad = ((s_gl.cur_pitch_deg > 1.0f) ? s_gl.cur_pitch_deg : 55.0f) * (PI_F / 180.0f);
		float yaw_rad = s_gl.cur_yaw_deg * (PI_F / 180.0f);
		cos_y = cosf(yaw_rad);
		sin_y = sinf(yaw_rad);
		cos_p = cosf(pitch_rad);
		sin_p = sinf(pitch_rad);
	}
	float bb_sea_level = (s_gl.focal_sea_level > -900.0f) ? s_gl.focal_sea_level : s_gl.cur_sea_level;

	for (uint32_t i = 0; i < count; i++) {
		bool is_grass = (batch[i].style == ND_BB_STYLE_GRASS);
		bool is_understory = (batch[i].mesh_id >= ND_MESH3D_UNDERSTORY_BASE &&
		                      batch[i].mesh_id < ND_MESH3D_TOTAL);
		bool is_mesh3d_only = nd_seven_gl_billboard_is_mesh3d_only(&batch[i]);
		bool is_other = (batch[i].style == ND_BB_STYLE_GLOW) || (batch[i].style == ND_BB_STYLE_PARTICLE);
		bool is_tall_tree = (!is_grass && !is_other &&
		                     (batch[i].height >= 90.0f ||
		                      (is_understory && batch[i].height >= 64.0f)) &&
		                     batch[i].mesh_id >= ND_MESH3D_PLANT_BASE &&
		                     batch[i].mesh_id < ND_MESH3D_TOTAL);
		bool is_mesh3d = false, emit_impostor_crossfade = false, is_crossed_tree = false;
		bool want_shadow = false, want_lamb_shadow = false;
		float lod_mesh_keep = 1.0f;
		const nd_mesh3d_slice_t *m3d_slice = NULL;

		if (!is_other && batch[i].mesh_id > 0 && batch[i].mesh_id < ND_SEVEN_MESH3D_TOTAL && s_gl.mesh3d_pipe.prog != 0) {
			float dx_m = batch[i].x - s_gl.cur_tx;
			float dy_m = batch[i].y - s_gl.cur_ty;
			float d2_m = s_gl.cur_dist * s_gl.cur_dist + 0.35f * (dx_m * dx_m + dy_m * dy_m);
			float max_m3d_dist = is_grass ? fmaxf(720.0f, 1150.0f * det_scale)
			                              : (is_tall_tree ? fmaxf(1550.0f, 2500.0f * det_scale)
			                                              : fmaxf(1050.0f, 1650.0f * det_scale));
			float cross_band = max_m3d_dist * 0.08f;
			float cross_start = max_m3d_dist - cross_band;
			if (d2_m <= max_m3d_dist * max_m3d_dist) {
				m3d_slice = nd_seven_mesh3d_slice(batch[i].mesh_id);
				if (m3d_slice && m3d_slice->idx_count > 0) {
					is_mesh3d = true;
					float d_m = sqrtf(d2_m);
					if (d_m > cross_start) {
						float lod_t = (d_m - cross_start) / fmaxf(1.0f, cross_band);
						lod_mesh_keep = fmaxf(0.02f, fminf(0.98f, 1.0f - lod_t));
						if (!is_mesh3d_only &&
						    batch[i].u0 != batch[i].u1 && batch[i].v0 != batch[i].v1)
							emit_impostor_crossfade = true;
					}
				}
			}
			if (!is_mesh3d && is_tall_tree && !is_mesh3d_only &&
			    batch[i].u0 != batch[i].u1 && batch[i].v0 != batch[i].v1) {
				float max_cross_d = max_m3d_dist * 2.2f;
				if (d2_m <= max_cross_d * max_cross_d)
					is_crossed_tree = true;
			}
		}
		if (!is_other && is_mesh3d_only && !is_mesh3d)
			continue;
		if (batch[i].style == ND_BB_STYLE_GLOW && s_gl.local_light_count < ND_MAX_TOTAL_LIGHTS) {
			float grgba[4];
			nd_seven_gl_unpack_rgba_u32(batch[i].tint_rgba, grgba);
			if (grgba[3] > 0.05f) {
				float dx_l = batch[i].x - s_gl.cur_lamb_light[0];
				float dy_l = batch[i].y - s_gl.cur_lamb_light[1];
				/* Skip duplicate of primary Lamb glow if within 4 world units */
				if (s_gl.cur_lamb_light[3] <= 0.01f || (dx_l * dx_l + dy_l * dy_l) > 16.0f) {
					(void)nd_seven_gl_add_point_light(batch[i].x, batch[i].y, batch[i].z + 360.0f,
					                                  grgba[0], grgba[1], grgba[2], grgba[3] * 1.25f, 1200.0f);
				}
			}
		}
		if (!is_other && (batch[i].tint_rgba & 0xFFu) >= 90u) {
			if (is_grass)
				want_shadow = emit_grass_shadows && (batch[i].width >= 4.0f && batch[i].height >= 4.0f);
			else
				want_shadow = emit_shadows && (batch[i].width >= 8.0f && batch[i].height >= 8.0f);
		}
		if (want_shadow && emit_lamb_shadows) {
			float north_shift = (batch[i].mesh_id > 0 && batch[i].anchor_y > 0.0f) ? (batch[i].anchor_y * 0.5f) : 0.0f;
			float dx_l = batch[i].x - s_lamb_shadow_light[0];
			float dy_l = (batch[i].y - north_shift) - s_lamb_shadow_light[1];
			float d2_l = dx_l * dx_l + dy_l * dy_l;
			want_lamb_shadow = (d2_l >= 4.0f && d2_l <= 290.0f * 290.0f);
		}
		uint32_t need_core   = (is_other || (is_mesh3d && !emit_impostor_crossfade)) ? 0u : (is_crossed_tree ? 2u : 1u);
		uint32_t need_mesh3d = is_mesh3d ? 1u : 0u;
		uint32_t need_other  = (is_other ? 1u : 0u) +
		                       ((!is_mesh3d && want_shadow) ? 1u : 0u) +
		                       ((!is_mesh3d && want_lamb_shadow) ? 1u : 0u);

		if (s_bb_frame_core_n + need_core > MAX_BB_INSTANCES ||
		    s_bb_frame_other_n + need_other > MAX_BB_INSTANCES ||
		    s_bb_frame_mesh3d_n + need_mesh3d > MAX_BB_INSTANCES) {
			gl_submit_staged_billboards(s_bb_frame_atlas_rgba, false);
		}

		billboard_basis_t basis;
		eval_billboard_basis(&batch[i], &s_gl.cur_pdef, s_gl.cur_origin, &basis);
		if (!is_mesh3d) {
			if (want_shadow) {
				pack_bb_shadow_quad_instance(&batch[i], &basis, &s_bb_other_instances[s_bb_frame_other_n++]);
			}
			if (want_lamb_shadow) {
				if (pack_bb_lamb_shadow_quad_instance(&batch[i], &basis, &s_bb_other_instances[s_bb_frame_other_n]))
					s_bb_frame_other_n++;
			}
		}
		if (is_other) {
			pack_bb_quad_instance(&batch[i], &basis, cos_y, sin_y, cos_p, sin_p, &s_bb_other_instances[s_bb_frame_other_n++]);
		} else if (is_mesh3d) {
			if (want_shadow) s_bb_frame_mesh3d_sun_sh = true;
			if (want_lamb_shadow) s_bb_frame_mesh3d_lamb_sh = true;
			s_mesh3d_staging_ids[s_bb_frame_mesh3d_n] = batch[i].mesh_id;
			pack_mesh3d_instance(&batch[i], &basis, m3d_slice, want_shadow, want_lamb_shadow,
			                     &s_mesh3d_staging[s_bb_frame_mesh3d_n]);
			if (lod_mesh_keep < 0.995f)
				s_mesh3d_staging[s_bb_frame_mesh3d_n].tint[3] *= lod_mesh_keep;
			s_bb_frame_mesh3d_n++;
			if (emit_impostor_crossfade) {
				pack_bb_quad_instance(&batch[i], &basis, cos_y, sin_y, cos_p, sin_p, &s_bb_instances[s_bb_frame_core_n]);
				int k31 = (int)roundf(lod_mesh_keep * 31.0f);
				if (k31 < 1) k31 = 1;
				if (k31 > 30) k31 = 30;
				s_bb_instances[s_bb_frame_core_n].param[3] += (float)k31 * 32.0f;
				s_bb_frame_core_n++;
			}
			bool can_sub = ((batch[i].pad0 & 0x80u) != 0) &&
			               (batch[i].style == ND_BB_STYLE_STANDARD || batch[i].style == ND_BB_STYLE_MINERAL);
			if (can_sub && bb_sea_level > -900.0f && batch[i].z < bb_sea_level + 0.15f)
				s_gl.sprites_in_water = true;
		} else if (is_crossed_tree) {
			/* Crossed +/-28 deg impostor cards give far trees volumetric depth across oblique views */
			const float c28 = 0.8829476f;
			const float s28 = 0.4694716f;
			float cos_p28 = cos_y * c28 - sin_y * s28;
			float sin_p28 = sin_y * c28 + cos_y * s28;
			float cos_m28 = cos_y * c28 + sin_y * s28;
			float sin_m28 = sin_y * c28 - cos_y * s28;
			pack_bb_quad_instance(&batch[i], &basis, cos_p28, sin_p28, cos_p, sin_p, &s_bb_instances[s_bb_frame_core_n++]);
			pack_bb_quad_instance(&batch[i], &basis, cos_m28, sin_m28, cos_p, sin_p, &s_bb_instances[s_bb_frame_core_n++]);
			bool can_sub = ((batch[i].pad0 & 0x80u) != 0) &&
			               (batch[i].style == ND_BB_STYLE_STANDARD || batch[i].style == ND_BB_STYLE_MINERAL);
			if (can_sub && bb_sea_level > -900.0f && (batch[i].z - batch[i].water_z_off) < bb_sea_level + 0.15f)
				s_gl.sprites_in_water = true;
		} else {
			pack_bb_quad_instance(&batch[i], &basis, cos_y, sin_y, cos_p, sin_p, &s_bb_instances[s_bb_frame_core_n++]);
			bool can_sub = ((batch[i].pad0 & 0x80u) != 0) &&
			               (batch[i].style == ND_BB_STYLE_STANDARD || batch[i].style == ND_BB_STYLE_MINERAL);
			if (can_sub && bb_sea_level > -900.0f && (batch[i].z - batch[i].water_z_off) < bb_sea_level + 0.15f)
				s_gl.sprites_in_water = true;
		}
	}
}

void nd_seven_gl_submit_billboards(const uint32_t *atlas_rgba)
{
	if (!s_gl.initialized || !s_gl.pipelines_ready || !s_gl.bb_pipe.prog) return;
	if (atlas_rgba)
		s_bb_frame_atlas_rgba = atlas_rgba;
	gl_submit_staged_billboards(s_bb_frame_atlas_rgba, true);
}

void nd_seven_gl_flush_billboards(const lam_billboard_gpu_t *batch,
                                  uint32_t count,
                                  const uint32_t *atlas_rgba)
{
	nd_seven_gl_queue_billboards(batch, count, atlas_rgba);
	nd_seven_gl_submit_billboards(atlas_rgba);
}
