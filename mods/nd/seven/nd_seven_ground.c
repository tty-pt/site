#include "nd_seven_ground.h"
#include "nd_seven_atlas_draw.h"
#include "nd_seven_motion.h"
#include <ttypt/areole.h>
#include <nd/field.h>
#include <math.h>
#include <string.h>

#ifdef ND_FIELD_MEMO_EXTERN
_CHAFF_TLS nd_field_room_memo_t g_nd_field_room_memo[4096];
#endif

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

static int s_msaa_samples = 4;

void nd_seven_set_msaa_samples(int samples)
{
	if (samples <= 0) {
		s_msaa_samples = 0;
		s_msaa_enabled = 0;
	} else if (samples <= 2) {
		s_msaa_samples = 2;
		s_msaa_enabled = 1;
	} else {
		s_msaa_samples = 4;
		s_msaa_enabled = 1;
	}
}

int nd_seven_get_msaa_samples(void)
{
	return s_msaa_samples;
}

void nd_seven_set_msaa(int enabled)
{
	if (enabled <= 0)
		nd_seven_set_msaa_samples(0);
	else if (enabled == 2)
		nd_seven_set_msaa_samples(2);
	else
		nd_seven_set_msaa_samples(4);
}

int nd_seven_get_msaa(void)
{
	return s_msaa_enabled;
}

void nd_seven_toggle_msaa(void)
{
	nd_seven_set_msaa_samples(s_msaa_enabled ? 0 : 4);
}

void nd_seven_cycle_msaa(void)
{
	if (s_msaa_samples == 0)
		nd_seven_set_msaa_samples(2);
	else if (s_msaa_samples == 2)
		nd_seven_set_msaa_samples(4);
	else
		nd_seven_set_msaa_samples(0);
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
		nd_seven_set_res_scale(0.67f);
		nd_seven_set_detail_scale(0.72f);
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
	    s_msaa_samples == 4 && s_fps_cap == 0)
		return ND_QUALITY_HIGH;
	if (res_pct == 75 && det_pct == 75 &&
	    s_cloud_quality == ND_CLOUD_QUALITY_MEDIUM &&
	    s_msaa_samples == 0 && s_fps_cap == 60)
		return ND_QUALITY_MEDIUM;
	if (res_pct == 67 && det_pct == 72 &&
	    s_cloud_quality == ND_CLOUD_QUALITY_LOW &&
	    s_msaa_samples == 0 && s_fps_cap == 30)
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

static uint32_t s_ground_forget_seq = 1u;

void nd_seven_ground_cache_forget(void)
{
	s_ground_forget_seq++;
	memset(s_border, 0, sizeof(s_border));
	ar_system_t *cs = nd_seven_get_chunk_sys();
	if (cs) {
		ar_system_clear(cs);
	}
}

uint32_t nd_seven_ground_forget_seq(void)
{
	return s_ground_forget_seq;
}

static int s_ground_perf_enabled = 0;
static uint32_t s_ground_perf_probes = 0;

void nd_seven_ground_perf_set(int enabled)
{
	s_ground_perf_enabled = enabled ? 1 : 0;
	s_ground_perf_probes = 0;
}

uint32_t nd_seven_ground_perf_take_probes(void)
{
	uint32_t n = s_ground_perf_probes;
	s_ground_perf_probes = 0;
	return n;
}

int16_t nd_seven_ground_elevation_q12_4(int32_t wx, int32_t wy)
{
	if (s_ground_perf_enabled)
		s_ground_perf_probes++;
	uint16_t world = (uint16_t)nd_seven_get_world();
	ar_system_t *cs = nd_seven_get_chunk_sys();
	if (cs) {
		int16_t val = 0;
		if (ar_probe_view_val(cs, 0, world, wx, wy, &val))
			return val;
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
	float r = cam_dist / max_meadow_cull;
	if (r >= 0.65f)
		return 56;
	if (r >= 0.40f)
		return 36;
	if (r >= 0.22f)
		return 24;
	return 16;
}

static float alt_smoothstep(float e0, float e1, float x)
{
	float t = (x - e0) / (e1 - e0);
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;
	return t * t * (3.0f - 2.0f * t);
}

void nd_seven_eval_alt_profile(float cam_dist, float detail_scale,
                               float base_res_scale, int cloud_quality,
                               nd_seven_alt_profile_t *out)
{
	if (!out)
		return;

	float d = (cam_dist > 35.0f) ? cam_dist : 35.0f;
	float ld = log2f(d);

	/* Smooth C1 transitions in log2(dist) space so scroll-wheel zooming feels uniform */
	/* log2(600)=9.2288, log2(3500)=11.7731, log2(45000)=15.4576, log2(800000)=19.6096 */
	float t_mid   = alt_smoothstep(9.2288f, 11.7731f, ld);
	float t_orbit = alt_smoothstep(15.4576f, 19.6096f, ld);

	float det = detail_scale;
	if (det < 0.55f) det = 0.55f;
	if (det > 1.00f) det = 1.00f;
	/* Map detail_scale [0.55..1.00] -> mild quality factor [1.42..1.00] */
	float q_mul = 1.0f + (1.0f - det) * 0.9333f;

	/* U-shaped target_quad_px:
	 * Close (d<=600): 1.333 px (crisp 1:1 ground detail)
	 * Mid-altitude (3.5k..45k): 2.80..4.00 px (lean vertex/leaf budget during overlap)
	 * Orbit (800k..28M): tightens back to 2.00..2.80 px (keeps planet at L12-L13, not L14!) */
	float close_px = 2.0f * (32.0f / 48.0f);
	float mid_px   = 2.80f * q_mul;
	float orbit_px = 2.35f * q_mul;
	float mid_or_orbit = mid_px + (orbit_px - mid_px) * t_orbit;
	out->target_quad_px = close_px + (mid_or_orbit - close_px) * t_mid;

	/* Smoothly relax resolution scaling back to 1.00 in orbit (log2(40k)=15.2877 -> log2(400k)=18.6096) */
	float br = base_res_scale;
	if (br < ND_SEVEN_RES_MIN) br = ND_SEVEN_RES_MIN;
	if (br > ND_SEVEN_RES_MAX) br = ND_SEVEN_RES_MAX;
	float t_res = alt_smoothstep(15.2877f, 18.6096f, ld);
	out->eff_res_scale = br + (1.0f - br) * t_res;

	/* Cloud resolution scale relative to NATIVE viewport (never compounded with res_scale) */
	float c_mid, c_orb;
	if (cloud_quality <= ND_CLOUD_QUALITY_LOW) {
		c_mid = 0.35f;
		c_orb = 0.67f;
	} else if (cloud_quality == ND_CLOUD_QUALITY_MEDIUM) {
		c_mid = 0.42f;
		c_orb = 0.75f;
	} else {
		c_mid = 0.50f;
		c_orb = 1.00f;
	}
	out->eff_cloud_scale = c_mid + (c_orb - c_mid) * t_orbit;

	out->grass_max_dist  = 1450.0f;
	out->ssao_max_dist   = 2200.0f;
	out->eco_detail_fade = 1.0f - alt_smoothstep(2500.0f, 6500.0f, d);
	out->min_level       = 0u;
	out->chunk_budget    = 8u;
}

void nd_seven_drs_init(nd_seven_drs_t *drs)
{
	if (!drs)
		return;
	memset(drs, 0, sizeof(*drs));
	drs->enabled    = 0;
	drs->target_ms  = 16.67f;
	drs->hi_ms      = 18.0f;
	drs->lo_ms      = 13.0f;
	drs->min_scale  = 0.75f;
	drs->max_scale  = 1.00f;
	drs->step       = 0.05f;
	drs->cur_scale  = 1.00f;
	drs->ema_ms     = 16.67f;
}

float nd_seven_drs_update(nd_seven_drs_t *drs, float dt_s,
                          float cam_x, float cam_y, float cam_dist,
                          float cam_yaw, float cam_pitch)
{
	if (!drs)
		return 1.00f;
	if (!drs->enabled) {
		drs->cur_scale = (drs->max_scale > 0.0f) ? drs->max_scale : 1.00f;
		drs->in_motion = 0u;
		drs->settle_s = 0.0f;
		drs->cooldown_s = 0.0f;
		return drs->cur_scale;
	}

	if (dt_s < 0.001f) dt_s = 0.001f;
	if (dt_s > 0.250f) dt_s = 0.250f;
	float frame_ms = dt_s * 1000.0f;
	drs->ema_ms += 0.20f * (frame_ms - drs->ema_ms);

	bool fast_motion = false;
	if (drs->has_pose) {
		float dx = cam_x - drs->last_cam_x;
		float dy = cam_y - drs->last_cam_y;
		float d_pos = sqrtf(dx * dx + dy * dy);
		float rel_zoom = fabsf(cam_dist - drs->last_cam_dist) / fmaxf(1.0f, fabsf(drs->last_cam_dist));
		float d_yaw = fabsf(cam_yaw - drs->last_cam_yaw);
		if (d_yaw > 180.0f) d_yaw = fabsf(360.0f - d_yaw);
		float d_ang = d_yaw + fabsf(cam_pitch - drs->last_cam_pitch);
		fast_motion = (d_pos > 1.5f || rel_zoom > 0.02f || d_ang > 0.35f);
	}
	drs->last_cam_x     = cam_x;
	drs->last_cam_y     = cam_y;
	drs->last_cam_dist  = cam_dist;
	drs->last_cam_yaw   = cam_yaw;
	drs->last_cam_pitch = cam_pitch;
	drs->has_pose       = 1u;

	if (fast_motion) {
		drs->in_motion = 1u;
		drs->settle_s = 0.0f;
		if (frame_ms > drs->hi_ms || drs->ema_ms > drs->hi_ms) {
			/* Immediate downscale during fast camera motion when over budget */
			drs->cur_scale -= drs->step;
			drs->cooldown_s = 0.10f;
		}
	} else {
		drs->settle_s += dt_s;
		if (drs->in_motion) {
			/* Rapid recovery back to max_scale within ~250 ms of camera settle */
			if (drs->ema_ms <= drs->hi_ms) {
				float ramp_rate = (drs->max_scale - drs->min_scale) / 0.20f;
				drs->cur_scale += ramp_rate * dt_s;
			}
			if (drs->settle_s >= 0.25f) {
				if (drs->ema_ms <= drs->hi_ms)
					drs->cur_scale = drs->max_scale;
				drs->in_motion = 0u;
				drs->cooldown_s = 0.15f;
			}
		} else {
			if (drs->cooldown_s > 0.0f) {
				drs->cooldown_s -= dt_s;
				if (drs->cooldown_s < 0.0f)
					drs->cooldown_s = 0.0f;
			}
			if (drs->cooldown_s <= 0.0f) {
				if (drs->ema_ms > drs->hi_ms) {
					drs->cur_scale -= drs->step;
					drs->cooldown_s = 0.15f;
				} else if (drs->ema_ms < drs->lo_ms) {
					drs->cur_scale += drs->step;
					drs->cooldown_s = 0.15f;
				} else if (drs->cur_scale < drs->max_scale &&
				           drs->target_ms > 0.0f &&
				           drs->ema_ms <= drs->target_ms + 0.5f &&
				           drs->settle_s >= 0.50f) {
					/* VSync-locked displays (e.g. 60 Hz = 16.67 ms) never drop below lo_ms:
					 * gently probe back up when steadily meeting target_ms with zero drops. */
					drs->cur_scale += drs->step;
					drs->cooldown_s = 0.50f;
				}
			}
		}
	}

	if (drs->cur_scale < drs->min_scale) drs->cur_scale = drs->min_scale;
	if (drs->cur_scale > drs->max_scale) drs->cur_scale = drs->max_scale;
	return drs->cur_scale;
}

static nd_seven_drs_t s_drs;
static int s_drs_inited = 0;

static nd_seven_drs_t *gl_drs_state(void)
{
	if (!s_drs_inited) {
		nd_seven_drs_init(&s_drs);
		s_drs_inited = 1;
	}
	return &s_drs;
}

void nd_seven_set_drs(int enabled)
{
	nd_seven_drs_t *d = gl_drs_state();
	d->enabled = enabled ? 1 : 0;
	if (!d->enabled) {
		d->cur_scale = d->max_scale;
		d->in_motion = 0u;
		d->settle_s = 0.0f;
		d->cooldown_s = 0.0f;
	}
}

int nd_seven_get_drs(void)
{
	return gl_drs_state()->enabled;
}

void nd_seven_toggle_drs(void)
{
	nd_seven_set_drs(!nd_seven_get_drs());
}

float nd_seven_get_drs_scale(void)
{
	nd_seven_drs_t *d = gl_drs_state();
	return d->enabled ? d->cur_scale : 1.00f;
}

void nd_seven_drs_feed_frame(float dt_s, float cam_x, float cam_y,
                             float cam_dist, float cam_yaw, float cam_pitch)
{
	nd_seven_drs_t *d = gl_drs_state();
	if (!d->enabled)
		return;
	int cap = nd_seven_get_fps_cap();
	float want_target = (cap == 30) ? 33.33f : 16.67f;
	if (fabsf(d->target_ms - want_target) > 0.1f) {
		d->target_ms = want_target;
		d->hi_ms     = (cap == 30) ? 36.0f : 18.0f;
		d->lo_ms     = (cap == 30) ? 26.0f : 13.0f;
		d->ema_ms    = want_target;
	}
	(void)nd_seven_drs_update(d, dt_s, cam_x, cam_y, cam_dist, cam_yaw, cam_pitch);
}

