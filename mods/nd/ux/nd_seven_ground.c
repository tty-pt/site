#include "nd_seven_ground.h"
#include "nd_seven_atlas_draw.h"
#include "nd_seven_motion.h"
#include <ttypt/areole.h>
#include <nd/field.h>
#include <math.h>
#include <string.h>

__attribute__((weak)) ar_system_t *nd_seven_get_chunk_sys(void) { return NULL; }
__attribute__((weak)) void ar_system_clear(ar_system_t *cs) { (void)cs; }
__attribute__((weak)) bool ar_system_probe_view(ar_system_t *cs, uint8_t view_id, uint16_t world, int32_t wx, int32_t wy, ar_sample_t *out)
{
	(void)cs; (void)view_id; (void)world; (void)wx; (void)wy; (void)out;
	return false;
}

uint32_t alpha_blend_rgb(uint32_t bg, uint32_t fg, int alpha)
{
	if (alpha <= 0) return bg;
	if (alpha >= 256) return fg;
	uint32_t inv = (uint32_t)(256 - alpha);
	uint32_t a   = (uint32_t)alpha;
	uint32_t rb  = (((bg & 0x00FF00FFu) * inv + (fg & 0x00FF00FFu) * a) >> 8) & 0x00FF00FFu;
	uint32_t g   = (((bg & 0x0000FF00u) * inv + (fg & 0x0000FF00u) * a) >> 8) & 0x0000FF00u;
	return 0xFF000000u | rb | g;
}

static inline uint32_t lerp_rgb(uint32_t c1, uint32_t c2, int num, int den)
{
	if (num <= 0) return c1;
	if (num >= den) return c2;
	int r1 = (int)(c1 & 0xFF), g1 = (int)((c1 >> 8) & 0xFF), b1 = (int)((c1 >> 16) & 0xFF);
	int r2 = (int)(c2 & 0xFF), g2 = (int)((c2 >> 8) & 0xFF), b2 = (int)((c2 >> 16) & 0xFF);
	int r = r1 + ((r2 - r1) * num) / den;
	int g = g1 + ((g2 - g1) * num) / den;
	int b = b1 + ((b2 - b1) * num) / den;
	return RGB(r, g, b);
}

uint32_t water_color_at_depth(int depth, int r, int gg, int b)
{
	static const uint32_t C_SHALLOW = RGB(64, 218, 235);
	static const uint32_t C_SHELF   = RGB(52, 178, 228);
	static const uint32_t C_OUTER   = RGB(36, 122, 202);
	static const uint32_t C_ABYSS   = RGB(14, 50, 135);
	const int shallow_max = 4;
	const int shelf_max   = 11;
	const int max_depth   = 28;

	uint32_t water_col;
	if (depth <= shallow_max) {
		water_col = lerp_rgb(C_SHALLOW, C_SHELF, depth - 1, shallow_max - 1);
	} else if (depth <= shelf_max) {
		water_col = lerp_rgb(C_SHELF, C_OUTER, depth - shallow_max, shelf_max - shallow_max);
	} else {
		int t = depth - shelf_max;
		int span = max_depth - shelf_max;
		if (t > span) t = span;
		water_col = lerp_rgb(C_OUTER, C_ABYSS, t, span);
	}
	return tint_col(water_col, r, gg, b);
}

uint32_t shade_rgb(uint32_t col, int shade256)
{
	if (shade256 == 256) return col;
	int r = (int)(col & 0xFF) * shade256 / 256;
	int g = (int)((col >> 8) & 0xFF) * shade256 / 256;
	int b = (int)((col >> 16) & 0xFF) * shade256 / 256;
	if (r > 255) r = 255;
	if (g > 255) g = 255;
	if (b > 255) b = 255;
	return RGB(r, g, b);
}

int water_alpha_at_depth(int depth)
{
	static const uint8_t s_beer_lambert_alpha[33] = {
		0,
		32,  48,  64,  82, 102, 124, 146, 168, 188, 206,
		220, 232, 240, 246, 250, 252, 253, 254, 254, 255,
		255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
		255, 255
	};
	if (depth <= 0) return 0;
	if (depth >= 28) return 255;
	return (int)s_beer_lambert_alpha[depth];
}

static uint32_t ndw_ground_base_rgb(const ndw_ground_t *g)
{
	int r, gg, b;
	switch (g->kind) {
	case NDW_G_WATER:
	case NDW_G_SHALLOW_WATER:
	case NDW_G_SAND:   r = 226; gg = 200; b = 140; break;
	case NDW_G_SNOW:   r = 248; gg = 252; b = 255; break;
	case NDW_G_TUNDRA: r = 136; gg = 142; b = 132; break;
	case NDW_G_ASH:    r = 54;  gg = 48;  b = 52;  break;
	default:           r = 78;  gg = 128; b = 60;  break;
	}
	r = r * g->r / 128; gg = gg * g->g / 128; b = b * g->b / 128;
	if (r > 255) r = 255;
	if (gg > 255) gg = 255;
	if (b > 255) b = 255;
	return RGB(r, gg, b);
}

uint32_t ndw_biome_base_rgb(unsigned biome)
{
	switch (biome) {
	case 0:  return RGB( 64, 156, 196); /* Water */
	case 1:  return RGB(244, 249, 255); /* Permanent Ice */
	case 2:  return RGB(134, 140, 126); /* Tundra */
	case 3:  return RGB( 88, 148, 106); /* Alpine Meadow */
	case 4:  return RGB(178, 132,  86); /* Mediterranean (sun-baked terracotta/ochre) */
	case 5:  return RGB( 58, 108,  94); /* Wetland (deep marsh peat-teal) */
	case 6:  return RGB(176, 162, 132); /* Cold Desert (dry steppe sand) */
	case 7:  return RGB(148, 136,  88); /* Shrubland (warm dry olive-sage) */
	case 8:  return RGB( 64, 108,  78); /* Coniferous Forest (montane pine green) */
	case 9:  return RGB( 58,  94,  76); /* Boreal Forest (spruce taiga green-slate) */
	case 10: return RGB(106, 148,  72); /* Temperate Grassland (fresh meadow green) */
	case 11: return RGB( 92, 136,  74); /* Woodland (broadleaf woodland green) */
	case 12: return RGB(116, 134,  68); /* Temperate Seasonal Forest (warm deciduous olive) */
	case 13: return RGB( 52, 112,  68); /* Temperate Rainforest (lush mossy emerald) */
	case 14: return RGB(218, 184, 126); /* Subtropical Desert (golden erg sand) */
	case 15: return RGB(198, 152,  72); /* Tropical Savannah (golden ochre grassland) */
	case 16: return RGB(142, 140,  68); /* Tropical Seasonal Forest (monsoon gold-green) */
	case 17: return RGB( 46, 114,  52); /* Tropical Rainforest (deep equatorial emerald) */
	case 18: return RGB( 54,  48,  52); /* Volcanic (basalt & tephra ash) */
	default: {
		ndw_ground_t g = ndw_biome_style(biome);
		return ndw_ground_base_rgb(&g);
	}
	}
}

#define BORDER_CACHE 64
typedef struct {
	int16_t pos[4];
	uint8_t used;
	ndw_warp_grid_t grid;
} border_ent_t;
static border_ent_t s_border[BORDER_CACHE];
static unsigned s_border_next;

const ndw_warp_grid_t *nd_seven_warp_grid(const int16_t pos[4])
{
	for (int i = 0; i < BORDER_CACHE; i++) {
		if (s_border[i].used && !memcmp(s_border[i].pos, pos, sizeof(s_border[i].pos)))
			return &s_border[i].grid;
	}
	border_ent_t *e = &s_border[s_border_next++ % BORDER_CACHE];
	memcpy(e->pos, pos, sizeof(e->pos));
	e->used = 1;
	ndw_warp_grid_fill(&e->grid, pos);
	return &e->grid;
}

int nd_seven_pos_in_map(const int16_t pos[4], const nd_map_data_t *map)
{
	if (!map || map->width == 0 || map->height == 0)
		return 0;
	if (pos[2] != map->pos[2] || pos[3] != map->pos[3])
		return 0;

	int ti = (int)pos[0] - (int)map->pos[0];
	int tj = (int)map->pos[1] - (int)pos[1];
	int col = (int)map->player_x + ti;
	int row = (int)map->player_y + tj;

	return (col >= 0 && col < (int)map->width && row >= 0 && row < (int)map->height);
}

static float s_camera_zoom = 1.0f;

void nd_seven_set_zoom(float zoom)
{
	if (zoom < ND_SEVEN_ZOOM_MIN) zoom = ND_SEVEN_ZOOM_MIN;
	if (zoom > ND_SEVEN_ZOOM_MAX) zoom = ND_SEVEN_ZOOM_MAX;
	if (s_camera_zoom != zoom) {
		s_camera_zoom = zoom;
	}
}

float nd_seven_get_zoom(void)
{
	return s_camera_zoom;
}

void nd_seven_zoom_in(void)
{
	nd_seven_set_zoom(s_camera_zoom * 1.15f);
}

void nd_seven_zoom_out(void)
{
	nd_seven_set_zoom(s_camera_zoom / 1.15f);
}

static float s_cam_yaw_target = 0.0f;
static float s_cam_yaw_cur = 0.0f;
static float s_cam_pitch_target = 55.0f;
static float s_cam_pitch_cur = 55.0f;
static int   s_cam_pitch_user_override = 0;

static float wrap_deg_180(float deg)
{
	deg = fmodf(deg + 180.0f, 360.0f);
	if (deg < 0.0f)
		deg += 360.0f;
	return deg - 180.0f;
}

static float clamp_pitch_range(float pitch_deg)
{
	if (pitch_deg < ND_SEVEN_CAM_PITCH_MIN) return ND_SEVEN_CAM_PITCH_MIN;
	if (pitch_deg > ND_SEVEN_CAM_PITCH_MAX) return ND_SEVEN_CAM_PITCH_MAX;
	return pitch_deg;
}

void nd_seven_set_cam_angles(float yaw_deg, float pitch_deg)
{
	yaw_deg = wrap_deg_180(yaw_deg);
	pitch_deg = clamp_pitch_range(pitch_deg);
	s_cam_yaw_target = yaw_deg;
	s_cam_yaw_cur = yaw_deg;
	s_cam_pitch_target = pitch_deg;
	s_cam_pitch_cur = pitch_deg;
	s_cam_pitch_user_override = 1;
}

void nd_seven_orbit_cam(float dyaw_deg, float dpitch_deg)
{
	s_cam_yaw_target = wrap_deg_180(s_cam_yaw_target + dyaw_deg);
	s_cam_pitch_target = clamp_pitch_range(s_cam_pitch_target + dpitch_deg);
	if (fabsf(dpitch_deg) > 0.01f)
		s_cam_pitch_user_override = 1;
}

void nd_seven_reset_cam(void)
{
	s_cam_yaw_target = 0.0f;
	s_cam_pitch_target = 55.0f;
	s_cam_pitch_user_override = 0;
}

float nd_seven_get_yaw_deg(void)
{
	return s_cam_yaw_cur;
}

float nd_seven_get_pitch_deg(void)
{
	return s_cam_pitch_cur;
}

int nd_seven_has_pitch_override(void)
{
	return s_cam_pitch_user_override;
}

float nd_seven_eval_effective_pitch(float base_pitch_deg, float eff_cam_dist)
{
	float p = clamp_pitch_range(base_pitch_deg);
	if (s_cam_pitch_user_override || eff_cam_dist >= 280.0f)
		return p;
	/* Close-zoom horizon tilt: zoom > 1.0 (eff_cam_dist < 280) glides smoothly
	 * from 55 deg diorama toward 36 deg low-horizon view unless user overrode pitch. */
	float u = (280.0f - eff_cam_dist) / (280.0f - 95.0f);
	if (u < 0.0f) u = 0.0f;
	if (u > 1.0f) u = 1.0f;
	float t = u * u * (3.0f - 2.0f * u);
	float target = (p > 36.0f) ? 36.0f : p;
	return clamp_pitch_range(p + (target - p) * t);
}

void nd_seven_step_cam(float dt)
{
	if (dt <= 0.0f)
		return;
	float alpha = 1.0f - expf(-16.0f * dt);
	if (alpha < 0.0f) alpha = 0.0f;
	if (alpha > 1.0f) alpha = 1.0f;

	float dyaw = wrap_deg_180(s_cam_yaw_target - s_cam_yaw_cur);
	if (fabsf(dyaw) < 0.02f)
		s_cam_yaw_cur = s_cam_yaw_target;
	else
		s_cam_yaw_cur = wrap_deg_180(s_cam_yaw_cur + dyaw * alpha);

	float dpitch = s_cam_pitch_target - s_cam_pitch_cur;
	if (fabsf(dpitch) < 0.02f)
		s_cam_pitch_cur = s_cam_pitch_target;
	else
		s_cam_pitch_cur = clamp_pitch_range(s_cam_pitch_cur + dpitch * alpha);
}

float nd_seven_clamp_cam_pitch(uint16_t world, float tx, float ty, float tz,
                               float dist, float yaw_deg, float pitch_deg)
{
	float p = clamp_pitch_range(pitch_deg);
	if (dist <= 1.0f || dist >= 8000.0f)
		return p;

	const float deg2rad = 3.14159265358979323846f / 180.0f;
	float yaw_rad = yaw_deg * deg2rad;
	float sin_y = sinf(yaw_rad);
	float cos_y = cosf(yaw_rad);
	float base_wl = (float)nd_field_water_level(world);

	/* Iterate up to 3 times so raising pitch pulls the horizontal sample inward accurately */
	for (int iter = 0; iter < 3; iter++) {
		float p_rad = p * deg2rad;
		float cos_p = cosf(p_rad);
		float sin_p = sinf(p_rad);
		float cam_z = tz + dist * sin_p;
		float req_sin_p = sin_p;
		float samples[3] = { 0.50f, 0.78f, 1.00f };
		for (int s = 0; s < 3; s++) {
			float f = samples[s];
			float sx = tx + dist * cos_p * f * sin_y;
			float sy = ty + dist * cos_p * f * cos_y;
			float gz = nd_seven_ground_elevation_f((int32_t)lroundf(sx), (int32_t)lroundf(sy));
			float floor_z = (gz > base_wl) ? gz : base_wl;
			float min_z = floor_z + ((s == 2) ? 14.0f : 8.0f);
			if (cam_z < min_z) {
				float need = (min_z - tz) / (dist * ((s == 2) ? 1.0f : fmaxf(0.35f, f)));
				if (need > req_sin_p)
					req_sin_p = need;
			}
		}
		if (req_sin_p <= sin_p + 1e-4f)
			break;
		if (req_sin_p > 0.99619f) req_sin_p = 0.99619f; /* sin(85 deg) */
		float new_p = asinf(req_sin_p) / deg2rad;
		new_p = clamp_pitch_range(new_p);
		if (new_p <= p + 0.05f) {
			p = new_p;
			break;
		}
		p = new_p;
	}
	return p;
}

static float s_detail_scale = 1.00f;

void nd_seven_set_detail_scale(float scale)
{
	if (scale < ND_SEVEN_DETAIL_MIN) scale = ND_SEVEN_DETAIL_MIN;
	if (scale > ND_SEVEN_DETAIL_MAX) scale = ND_SEVEN_DETAIL_MAX;
	s_detail_scale = scale;
}

float nd_seven_get_detail_scale(void)
{
	return s_detail_scale;
}

void nd_seven_detail_in(void)
{
	nd_seven_set_detail_scale(s_detail_scale * ND_SEVEN_DETAIL_STEP);
}

void nd_seven_detail_out(void)
{
	nd_seven_set_detail_scale(s_detail_scale / ND_SEVEN_DETAIL_STEP);
}

static float s_res_scale = 1.00f;
static int s_cloud_quality = ND_CLOUD_QUALITY_HIGH;
static int s_msaa_enabled = 1;
static int s_fps_cap = 0;

void nd_seven_set_res_scale(float scale)
{
	if (scale < ND_SEVEN_RES_MIN) scale = ND_SEVEN_RES_MIN;
	if (scale > ND_SEVEN_RES_MAX) scale = ND_SEVEN_RES_MAX;
	s_res_scale = scale;
}

float nd_seven_get_res_scale(void)
{
	return s_res_scale;
}

void nd_seven_res_in(void)
{
	nd_seven_set_res_scale(s_res_scale + ND_SEVEN_RES_STEP);
}

void nd_seven_res_out(void)
{
	nd_seven_set_res_scale(s_res_scale - ND_SEVEN_RES_STEP);
}

void nd_seven_scaled_dims(uint32_t w, uint32_t h, uint32_t *out_w, uint32_t *out_h)
{
	uint32_t rw = w, rh = h;
	if (s_res_scale < 0.999f && w > 0 && h > 0) {
		uint32_t min_w = (w < 160u) ? ((w < 4u) ? w : 4u) : 160u;
		uint32_t min_h = (h < 120u) ? ((h < 4u) ? h : 4u) : 120u;
		rw = (uint32_t)((float)w * s_res_scale + 0.5f);
		rh = (uint32_t)((float)h * s_res_scale + 0.5f);
		if (rw > 4u) rw &= ~1u;
		if (rh > 4u) rh &= ~1u;
		if (rw < min_w) rw = min_w;
		if (rh < min_h) rh = min_h;
	}
	if (out_w) *out_w = rw;
	if (out_h) *out_h = rh;
}

void nd_seven_set_cloud_quality(int quality)
{
	if (quality < ND_CLOUD_QUALITY_OFF) quality = ND_CLOUD_QUALITY_OFF;
	if (quality > ND_CLOUD_QUALITY_HIGH) quality = ND_CLOUD_QUALITY_HIGH;
	s_cloud_quality = quality;
}

int nd_seven_get_cloud_quality(void)
{
	return s_cloud_quality;
}

void nd_seven_cycle_cloud_quality(void)
{
	s_cloud_quality = (s_cloud_quality + 1) % 4;
}

void nd_seven_set_msaa(int enabled)
{
	s_msaa_enabled = enabled ? 1 : 0;
}

int nd_seven_get_msaa(void)
{
	return s_msaa_enabled;
}

void nd_seven_toggle_msaa(void)
{
	s_msaa_enabled = !s_msaa_enabled;
}

void nd_seven_set_fps_cap(int fps)
{
	if (fps <= 0)
		s_fps_cap = 0;
	else if (fps <= 45)
		s_fps_cap = 30;
	else
		s_fps_cap = 60;
}

int nd_seven_get_fps_cap(void)
{
	return s_fps_cap;
}

void nd_seven_cycle_fps_cap(void)
{
	if (s_fps_cap == 30)
		s_fps_cap = 60;
	else if (s_fps_cap == 60)
		s_fps_cap = 0;
	else
		s_fps_cap = 30;
}

void nd_seven_apply_quality_preset(int preset)
{
	switch (preset) {
	case ND_QUALITY_LOW:
		nd_seven_set_res_scale(0.50f);
		nd_seven_set_detail_scale(0.50f);
		nd_seven_set_cloud_quality(ND_CLOUD_QUALITY_LOW);
		nd_seven_set_msaa(0);
		nd_seven_set_fps_cap(30);
		break;
	case ND_QUALITY_HIGH:
		nd_seven_set_res_scale(1.00f);
		nd_seven_set_detail_scale(1.00f);
		nd_seven_set_cloud_quality(ND_CLOUD_QUALITY_HIGH);
		nd_seven_set_msaa(1);
		nd_seven_set_fps_cap(0);
		break;
	case ND_QUALITY_MEDIUM:
	default:
		nd_seven_set_res_scale(0.75f);
		nd_seven_set_detail_scale(0.75f);
		nd_seven_set_cloud_quality(ND_CLOUD_QUALITY_MEDIUM);
		nd_seven_set_msaa(0);
		nd_seven_set_fps_cap(60);
		break;
	}
}

int nd_seven_get_quality_preset(void)
{
	int res_pct = (int)(s_res_scale * 100.0f + 0.5f);
	int det_pct = (int)(s_detail_scale * 100.0f + 0.5f);
	if (res_pct == 100 && det_pct == 100 &&
	    s_cloud_quality == ND_CLOUD_QUALITY_HIGH &&
	    s_msaa_enabled == 1 && s_fps_cap == 0)
		return ND_QUALITY_HIGH;
	if (res_pct == 75 && det_pct == 75 &&
	    s_cloud_quality == ND_CLOUD_QUALITY_MEDIUM &&
	    s_msaa_enabled == 0 && s_fps_cap == 60)
		return ND_QUALITY_MEDIUM;
	if (res_pct == 50 && det_pct == 50 &&
	    s_cloud_quality == ND_CLOUD_QUALITY_LOW &&
	    s_msaa_enabled == 0 && s_fps_cap == 30)
		return ND_QUALITY_LOW;
	return ND_QUALITY_CUSTOM;
}

void nd_seven_cycle_quality_preset(void)
{
	int cur = nd_seven_get_quality_preset();
	if (cur == ND_QUALITY_HIGH)
		nd_seven_apply_quality_preset(ND_QUALITY_LOW);
	else if (cur == ND_QUALITY_LOW)
		nd_seven_apply_quality_preset(ND_QUALITY_MEDIUM);
	else
		nd_seven_apply_quality_preset(ND_QUALITY_HIGH);
}

void nd_seven_ground_cache_forget(void)
{
	memset(s_border, 0, sizeof(s_border));
	ar_system_t *cs = nd_seven_get_chunk_sys();
	if (cs) {
		ar_system_clear(cs);
	}
}

int16_t nd_seven_ground_elevation_q12_4(int32_t wx, int32_t wy)
{
	uint16_t world = (uint16_t)nd_seven_get_world();
	ar_system_t *cs = nd_seven_get_chunk_sys();
	if (cs) {
		ar_sample_t s;
		if (ar_probe_view(cs, 0, world, wx, wy, &s))
			return s.val;
	}

	int32_t cx = morton_fdiv(wx, NDW_TILE_W);
	int32_t cy = morton_fdiv(wy, NDW_TILE_H);
	int32_t ox = cx * NDW_TILE_W;
	int32_t oy = cy * NDW_TILE_H;
	const int gdim = 48;

	float fx = ((float)(wx - ox) / (float)NDW_TILE_W) * (float)gdim;
	float fy = ((float)(wy - oy) / (float)NDW_TILE_H) * (float)gdim;
	int col0 = (int)fx;
	int row0 = (int)fy;
	if (col0 < 0) col0 = 0;
	if (col0 >= gdim) col0 = gdim - 1;
	if (row0 < 0) row0 = 0;
	if (row0 >= gdim) row0 = gdim - 1;
	int col1 = col0 + 1;
	int row1 = row0 + 1;

	float u = fx - (float)col0;
	float v = fy - (float)row0;
	float wu = u * u * (3.0f - 2.0f * u);
	float wv = v * v * (3.0f - 2.0f * v);

	int32_t x0 = ox + (int32_t)(((int64_t)col0 * NDW_TILE_W) / gdim);
	int32_t x1 = ox + (int32_t)(((int64_t)col1 * NDW_TILE_W) / gdim);
	int32_t y0 = oy + (int32_t)(((int64_t)row0 * NDW_TILE_H) / gdim);
	int32_t y1 = oy + (int32_t)(((int64_t)row1 * NDW_TILE_H) / gdim);
	float v00 = (float)nd_field_sample(world, 3, x0, y0).elevation_q12_4;
	float v10 = (float)nd_field_sample(world, 3, x1, y0).elevation_q12_4;
	float v01 = (float)nd_field_sample(world, 3, x0, y1).elevation_q12_4;
	float v11 = (float)nd_field_sample(world, 3, x1, y1).elevation_q12_4;

	float h0 = v00 * (1.0f - wu) + v10 * wu;
	float h1 = v01 * (1.0f - wu) + v11 * wu;
	float h  = h0 * (1.0f - wv) + h1 * wv;
	return (int16_t)(h >= 0.0f ? (h + 0.5f) : (h - 0.5f));
}

int16_t nd_seven_ground_elevation(int32_t wx, int32_t wy)
{
	return nd_field_elevation_to_block(nd_seven_ground_elevation_q12_4(wx, wy));
}

float nd_seven_ground_elevation_f(int32_t wx, int32_t wy)
{
	int16_t q = nd_seven_ground_elevation_q12_4(wx, wy);
	return (float)q / 16.0f;
}

int32_t nd_seven_meadow_lattice_step(float cam_dist, float max_meadow_cull)
{
	if (cam_dist >= max_meadow_cull || max_meadow_cull <= 1.0f)
		return 0;
	if (cam_dist >= max_meadow_cull * 0.65f)
		return 64; /* 4x sub-lattice (16 * 4) */
	if (cam_dist >= max_meadow_cull * 0.32f)
		return 32; /* 2x sub-lattice (16 * 2) */
	return 16;
}

