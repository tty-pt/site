#ifndef ND_SEVEN_SHADER_H
#define ND_SEVEN_SHADER_H

/*
 * nd_seven_shader.h — Passthrough texture blit shader definition.
 * Registered dynamically with liblamina via lam_shader_register().
 * Pure-C software rasterization in nd_seven_ground.c is the authoritative engine.
 */

#include <ttypt/lam.h>
#include <ttypt/chaff_glsl.h>

/* NeverDark 3-octave divergence-free recursive curl-flow, synoptic Rossby flow,
 * and 3D Ekman spiral wind-shear GLSL shader functions. */
#define ND_GLSL_FLOW_FN \
	"vec4 chaff_eval_flow_2d_banded(vec2 worldPos, float anim_time, vec3 band_weights) {\n" \
	"    const mat2 rot  = mat2(0.8, -0.6, 0.6, 0.8);\n" \
	"    const mat2 irot = mat2(0.8,  0.6, -0.6, 0.8);\n" \
	"    const vec2 dir0 = vec2(0.741, -0.671);\n" \
	"    const vec2 dir1 = vec2(0.822, -0.569);\n" \
	"    const vec2 dir2 = vec2(0.640, -0.768);\n" \
	"    float w0_nyq = band_weights.x;\n" \
	"    float w1_nyq = band_weights.y;\n" \
	"    float w2_nyq = band_weights.z;\n" \
	"    if (w0_nyq <= 0.001) return vec4(dir0.x, dir0.y, 0.65, 0.0);\n" \
	"    float t0 = mod(anim_time * 0.05, 1024.0);\n" \
	"    vec2 p0 = worldPos * 0.0055 - dir0 * t0;\n" \
	"    vec3 w0 = chaff_grad_noise2d(p0, 0x57494E44u);\n" \
	"    vec2 baseWind = normalize(dir0 + vec2(-w0.z, w0.y) * (0.28 * w0_nyq));\n" \
	"    vec3 w1 = vec3(0.5, 0.0, 0.0);\n" \
	"    vec2 localWind1 = baseWind;\n" \
	"    if (w1_nyq > 0.001) {\n" \
	"        float t1 = mod(anim_time * 0.12, 1024.0);\n" \
	"        vec2 p1 = rot * (worldPos * 0.028 + w0.yz * 0.65) - dir1 * t1;\n" \
	"        w1 = chaff_grad_noise2d(p1, 0x47555354u);\n" \
	"        vec2 mesoGrad = irot * w1.yz;\n" \
	"        localWind1 = normalize(baseWind + vec2(-mesoGrad.y, mesoGrad.x) * (0.22 * w1_nyq));\n" \
	"    }\n" \
	"    vec3 w2 = vec3(0.5, 0.0, 0.0);\n" \
	"    vec2 windDir = localWind1;\n" \
	"    if (w2_nyq > 0.001) {\n" \
	"        float t2 = mod(anim_time * 0.22, 1024.0);\n" \
	"        vec2 p2 = rot * (rot * (worldPos * 0.120) + w0.yz * 0.45 + w1.yz * 0.55) - dir2 * t2;\n" \
	"        w2 = chaff_grad_noise2d(p2, 0x45444459u);\n" \
	"        vec2 microGrad = (irot * irot) * w2.yz;\n" \
	"        windDir = normalize(localWind1 + vec2(-microGrad.y, microGrad.x) * (0.16 * w2_nyq));\n" \
	"    }\n" \
	"    float gustEnv = clamp(0.65 + 0.45 * (w0.x - 0.5) * w0_nyq + 0.55 * (w1.x - 0.38) * w1_nyq + 0.30 * (w2.x - 0.42) * w2_nyq, 0.25, 1.35);\n" \
	"    float catspaw = smoothstep(0.38, 0.76, w1.x * 0.62 + w2.x * 0.38) * w1_nyq;\n" \
	"    return vec4(windDir.x, windDir.y, gustEnv, catspaw);\n" \
	"}\n" \
	"vec4 chaff_eval_flow_2d(vec2 worldPos, float anim_time) {\n" \
	"    return chaff_eval_flow_2d_banded(worldPos, anim_time, vec3(1.0, 1.0, 1.0));\n" \
	"}\n" \
	"vec4 chaff_eval_synoptic_flow_2d(vec2 worldPos, float anim_time, float fw) {\n" \
	"    const vec2 prevDir = vec2(0.741, -0.671);\n" \
	"    float tFlow = mod(anim_time * 0.005, 1024.0);\n" \
	"    vec3 psi0 = chaff_grad_noise2d(worldPos * 0.00000042 - prevDir * (tFlow * 0.40), 0x4359434Cu);\n" \
	"    vec3 psi1 = chaff_grad_noise2d(worldPos * 0.00000180 + vec2(-psi0.z, psi0.y) * 0.30 - prevDir * tFlow, 0x5354524Du);\n" \
	"    vec2 synWarp = vec2(-psi0.z, psi0.y) * 260000.0 + vec2(-psi1.z, psi1.y) * 68000.0;\n" \
	"    vec2 vSyn = normalize(prevDir + vec2(-psi0.z, psi0.y) * 0.48 + vec2(-psi1.z, psi1.y) * 0.28);\n" \
	"    float convEnv = clamp(0.50 + 0.40 * (psi0.x - 0.50) + 0.30 * (psi1.x - 0.50), 0.0, 1.0);\n" \
	"    return vec4(vSyn.x, vSyn.y, convEnv, synWarp.x);\n" \
	"}\n" \
	"void chaff_eval_wind_3d(vec3 worldPos, float zBase, float zTop, float anim_time, float fw,\n" \
	"                        out vec2 outWindDir, out vec2 outShearDisp, out float outGust, out float outUpdraft) {\n" \
	"    float spanZ = max(16.0, zTop - zBase);\n" \
	"    float h = clamp((worldPos.z - zBase) / spanZ, 0.0, 1.0);\n" \
	"    vec4 synFlow = chaff_eval_synoptic_flow_2d(worldPos.xy, anim_time, fw);\n" \
	"    float w0_sfc = smoothstep(0.45, 0.12, 0.0055 * fw);\n" \
	"    float w1_sfc = smoothstep(0.45, 0.12, 0.0280 * fw);\n" \
	"    float w2_sfc = smoothstep(0.45, 0.12, 0.1200 * fw);\n" \
	"    vec4 sfcFlow = chaff_eval_flow_2d_banded(worldPos.xy, anim_time, vec3(w0_sfc, w1_sfc, w2_sfc));\n" \
	"    float veerAngle = h * 0.488;\n" \
	"    float cosV = cos(veerAngle), sinV = sin(veerAngle);\n" \
	"    vec2 baseDir = normalize(mix(sfcFlow.xy, synFlow.xy, 0.35 + 0.45 * h));\n" \
	"    vec2 atmoDir = vec2(baseDir.x * cosV - baseDir.y * sinV, baseDir.x * sinV + baseDir.y * cosV);\n" \
	"    float atmoSpeed = 1.0 + 0.85 * pow(h, 1.35);\n" \
	"    outWindDir = atmoDir;\n" \
	"    outShearDisp = atmoDir * (pow(h, 1.25) * spanZ * 0.35) * atmoSpeed;\n" \
	"    outGust = clamp(mix(sfcFlow.z, 1.0 + 0.50 * (synFlow.z - 0.5), h), 0.25, 1.75);\n" \
	"    outUpdraft = clamp(0.45 * synFlow.z + 0.35 * sfcFlow.z + 0.20 * sfcFlow.w, 0.10, 1.50);\n" \
	"}\n"

#define CHAFF_GLSL_FLOW_FN ND_GLSL_FLOW_FN

typedef struct {
	float base_dir_x;
	float base_dir_y;
	float freq0;
	float freq1;
	float freq2;
	float speed0;
	float speed1;
	float speed2;
	float curl_amp0;
	float curl_amp1;
	float curl_amp2;
	float gust_base;
	float gust_min;
	float gust_max;
	uint32_t seed0;
	uint32_t seed1;
	uint32_t seed2;
} nd_flow_params_t;

#define ND_FLOW_PARAMS_DEFAULT { \
	0.741f, -0.671f, \
	0.0055f, 0.0280f, 0.1200f, \
	0.05f, 0.12f, 0.22f, \
	0.28f, 0.22f, 0.16f, \
	0.65f, 0.25f, 1.35f, \
	0x57494E44u, 0x47555354u, 0x45444459u \
}

typedef struct {
	float prev_dir_x;
	float prev_dir_y;
	float time_scale;
	float freq0;
	float freq1;
	float curl_amp0;
	float curl_amp1;
	float warp_amp0;
	float warp_amp1;
	uint32_t seed0;
	uint32_t seed1;
} nd_synoptic_params_t;

#define ND_SYNOPTIC_PARAMS_DEFAULT { \
	0.741f, -0.671f, 0.005f, \
	0.00000042f, 0.00000180f, \
	0.48f, 0.28f, \
	260000.0f, 68000.0f, \
	0x4359434Cu, 0x5354524Du \
}

typedef struct {
	nd_flow_params_t     surface;
	nd_synoptic_params_t synoptic;
	float                ekman_veer_rad;
	float                shear_factor;
	float                jet_accel;
} nd_wind_3d_params_t;

#define ND_WIND_3D_PARAMS_DEFAULT { \
	ND_FLOW_PARAMS_DEFAULT, \
	ND_SYNOPTIC_PARAMS_DEFAULT, \
	0.488f, 0.35f, 0.85f \
}

static inline void nd_glsl_ref_eval_flow_2d_banded_ex(
	const nd_flow_params_t *params,
	float wx, float wy, float anim_time,
	float w0_nyq, float w1_nyq, float w2_nyq,
	float *out_dir_x, float *out_dir_y,
	float *out_gust, float *out_catspaw)
{
	const nd_flow_params_t def = ND_FLOW_PARAMS_DEFAULT;
	const nd_flow_params_t *p = params ? params : &def;

	if (w0_nyq <= 0.001f) {
		if (out_dir_x)   *out_dir_x   = p->base_dir_x;
		if (out_dir_y)   *out_dir_y   = p->base_dir_y;
		if (out_gust)    *out_gust    = p->gust_base;
		if (out_catspaw) *out_catspaw = 0.0f;
		return;
	}
	float raw_t0 = anim_time * p->speed0;
	float t0 = raw_t0 - 1024.0f * floorf(raw_t0 / 1024.0f);
	float p0x = wx * p->freq0 - p->base_dir_x * t0;
	float p0y = wy * p->freq0 - p->base_dir_y * t0;
	float w0v = 0.5f, w0gx = 0.0f, w0gy = 0.0f;
	chaff_glsl_ref_grad_noise2d(p0x, p0y, p->seed0, &w0v, &w0gx, &w0gy);

	float bwx = p->base_dir_x - w0gy * (p->curl_amp0 * w0_nyq);
	float bwy = p->base_dir_y + w0gx * (p->curl_amp0 * w0_nyq);
	float bw_inv = 1.0f / sqrtf(bwx * bwx + bwy * bwy + 1e-6f);
	bwx *= bw_inv;
	bwy *= bw_inv;

	float w1v = 0.5f, w1gx = 0.0f, w1gy = 0.0f;
	float lwx = bwx, lwy = bwy;
	if (w1_nyq > 0.001f) {
		float raw_t1 = anim_time * p->speed1;
		float t1 = raw_t1 - 1024.0f * floorf(raw_t1 / 1024.0f);
		float u1x = wx * p->freq1 + w0gx * 0.65f;
		float u1y = wy * p->freq1 + w0gy * 0.65f;
		float p1x = ( 0.8f * u1x + 0.6f * u1y) - 0.822f * t1;
		float p1y = (-0.6f * u1x + 0.8f * u1y) + 0.569f * t1;
		chaff_glsl_ref_grad_noise2d(p1x, p1y, p->seed1, &w1v, &w1gx, &w1gy);
		float mgx =  0.8f * w1gx - 0.6f * w1gy;
		float mgy =  0.6f * w1gx + 0.8f * w1gy;
		lwx = bwx - mgy * (p->curl_amp1 * w1_nyq);
		lwy = bwy + mgx * (p->curl_amp1 * w1_nyq);
		float lw_inv = 1.0f / sqrtf(lwx * lwx + lwy * lwy + 1e-6f);
		lwx *= lw_inv;
		lwy *= lw_inv;
	}

	float w2v = 0.5f, w2gx = 0.0f, w2gy = 0.0f;
	float fwx = lwx, fwy = lwy;
	if (w2_nyq > 0.001f) {
		float raw_t2 = anim_time * p->speed2;
		float t2 = raw_t2 - 1024.0f * floorf(raw_t2 / 1024.0f);
		float r1x =  0.8f * (wx * p->freq2) + 0.6f * (wy * p->freq2) + w0gx * 0.45f + w1gx * 0.55f;
		float r1y = -0.6f * (wx * p->freq2) + 0.8f * (wy * p->freq2) + w0gy * 0.45f + w1gy * 0.55f;
		float p2x = ( 0.8f * r1x + 0.6f * r1y) - 0.640f * t2;
		float p2y = (-0.6f * r1x + 0.8f * r1y) + 0.768f * t2;
		chaff_glsl_ref_grad_noise2d(p2x, p2y, p->seed2, &w2v, &w2gx, &w2gy);
		float u2gx =  0.28f * w2gx - 0.96f * w2gy;
		float u2gy =  0.96f * w2gx + 0.28f * w2gy;
		fwx = lwx - u2gy * (p->curl_amp2 * w2_nyq);
		fwy = lwy + u2gx * (p->curl_amp2 * w2_nyq);
		float fw_inv = 1.0f / sqrtf(fwx * fwx + fwy * fwy + 1e-6f);
		fwx *= fw_inv;
		fwy *= fw_inv;
	}

	float gust = p->gust_base + 0.45f * (w0v - 0.5f) * w0_nyq + 0.55f * (w1v - 0.38f) * w1_nyq + 0.30f * (w2v - 0.42f) * w2_nyq;
	if (gust < p->gust_min) gust = p->gust_min;
	if (gust > p->gust_max) gust = p->gust_max;

	float cp_raw = (w1v * 0.62f + w2v * 0.38f - 0.38f) / (0.76f - 0.38f);
	if (cp_raw < 0.0f) cp_raw = 0.0f;
	if (cp_raw > 1.0f) cp_raw = 1.0f;
	float cp = cp_raw * cp_raw * (3.0f - 2.0f * cp_raw) * w1_nyq;

	if (out_dir_x)   *out_dir_x   = fwx;
	if (out_dir_y)   *out_dir_y   = fwy;
	if (out_gust)    *out_gust    = gust;
	if (out_catspaw) *out_catspaw = cp;
}

static inline void nd_glsl_ref_eval_synoptic_flow_2d_ex(
	const nd_synoptic_params_t *params,
	float wx, float wy, float anim_time, float fw,
	float *out_vx, float *out_vy, float *out_conv, float *out_warp_x)
{
	(void)fw;
	const nd_synoptic_params_t def = ND_SYNOPTIC_PARAMS_DEFAULT;
	const nd_synoptic_params_t *p = params ? params : &def;

	float raw_tFlow = anim_time * p->time_scale;
	float tFlow = raw_tFlow - 1024.0f * floorf(raw_tFlow / 1024.0f);
	float psi0_v = 0.5f, psi0_gx = 0.0f, psi0_gy = 0.0f;
	chaff_glsl_ref_grad_noise2d(wx * p->freq0 - p->prev_dir_x * (tFlow * 0.40f),
	                            wy * p->freq0 - p->prev_dir_y * (tFlow * 0.40f),
	                            p->seed0, &psi0_v, &psi0_gx, &psi0_gy);
	float psi1_v = 0.5f, psi1_gx = 0.0f, psi1_gy = 0.0f;
	chaff_glsl_ref_grad_noise2d(wx * p->freq1 + (-psi0_gy) * 0.30f - p->prev_dir_x * tFlow,
	                            wy * p->freq1 + psi0_gx * 0.30f - p->prev_dir_y * tFlow,
	                            p->seed1, &psi1_v, &psi1_gx, &psi1_gy);
	float synWarpX = (-psi0_gy) * p->warp_amp0 + (-psi1_gy) * p->warp_amp1;
	float synVx = p->prev_dir_x + (-psi0_gy) * p->curl_amp0 + (-psi1_gy) * p->curl_amp1;
	float synVy = p->prev_dir_y + psi0_gx * p->curl_amp0 + psi1_gx * p->curl_amp1;
	float synInv = 1.0f / sqrtf(synVx * synVx + synVy * synVy + 1e-6f);
	synVx *= synInv;
	synVy *= synInv;
	float conv = 0.50f + 0.40f * (psi0_v - 0.50f) + 0.30f * (psi1_v - 0.50f);
	if (conv < 0.0f) conv = 0.0f;
	if (conv > 1.0f) conv = 1.0f;
	if (out_vx) *out_vx = synVx;
	if (out_vy) *out_vy = synVy;
	if (out_conv) *out_conv = conv;
	if (out_warp_x) *out_warp_x = synWarpX;
}

static inline void nd_glsl_ref_eval_wind_3d_ex(
	const nd_wind_3d_params_t *params,
	float wx, float wy, float wz,
	float zBase, float zTop, float anim_time, float fw,
	float *out_dir_x, float *out_dir_y,
	float *out_shear_x, float *out_shear_y,
	float *out_gust, float *out_updraft)
{
	const nd_wind_3d_params_t def = ND_WIND_3D_PARAMS_DEFAULT;
	const nd_wind_3d_params_t *p = params ? params : &def;

	float spanZ = (zTop - zBase) > 16.0f ? (zTop - zBase) : 16.0f;
	float h = (wz - zBase) / spanZ;
	if (h < 0.0f) h = 0.0f;
	if (h > 1.0f) h = 1.0f;
	float synVx = p->synoptic.prev_dir_x, synVy = p->synoptic.prev_dir_y, synConv = 0.5f, synWarpX = 0.0f;
	nd_glsl_ref_eval_synoptic_flow_2d_ex(&p->synoptic, wx, wy, anim_time, fw, &synVx, &synVy, &synConv, &synWarpX);
	float w0_sfc = chaff_glsl_ref_smoothstep(0.45f, 0.12f, p->surface.freq0 * fw);
	float w1_sfc = chaff_glsl_ref_smoothstep(0.45f, 0.12f, p->surface.freq1 * fw);
	float w2_sfc = chaff_glsl_ref_smoothstep(0.45f, 0.12f, p->surface.freq2 * fw);
	float sfcVx = p->surface.base_dir_x, sfcVy = p->surface.base_dir_y, sfcGust = p->surface.gust_base, sfcCp = 0.0f;
	nd_glsl_ref_eval_flow_2d_banded_ex(&p->surface, wx, wy, anim_time, w0_sfc, w1_sfc, w2_sfc, &sfcVx, &sfcVy, &sfcGust, &sfcCp);
	float veerAngle = h * p->ekman_veer_rad;
	float cosV = cosf(veerAngle), sinV = sinf(veerAngle);
	float bBlend = 0.35f + 0.45f * h;
	float bvx = (1.0f - bBlend) * sfcVx + bBlend * synVx;
	float bvy = (1.0f - bBlend) * sfcVy + bBlend * synVy;
	float bInv = 1.0f / sqrtf(bvx * bvx + bvy * bvy + 1e-6f);
	bvx *= bInv;
	bvy *= bInv;
	float atmoDirX = bvx * cosV - bvy * sinV;
	float atmoDirY = bvx * sinV + bvy * cosV;
	float atmoSpeed = 1.0f + p->jet_accel * powf(h, 1.35f);
	float shearMag = powf(h, 1.25f) * spanZ * p->shear_factor * atmoSpeed;
	float gust = (1.0f - h) * sfcGust + h * (1.0f + 0.50f * (synConv - 0.5f));
	if (gust < 0.25f) gust = 0.25f;
	if (gust > 1.75f) gust = 1.75f;
	float updraft = 0.45f * synConv + 0.35f * sfcGust + 0.20f * sfcCp;
	if (updraft < 0.10f) updraft = 0.10f;
	if (updraft > 1.50f) updraft = 1.50f;
	if (out_dir_x) *out_dir_x = atmoDirX;
	if (out_dir_y) *out_dir_y = atmoDirY;
	if (out_shear_x) *out_shear_x = atmoDirX * shearMag;
	if (out_shear_y) *out_shear_y = atmoDirY * shearMag;
	if (out_gust) *out_gust = gust;
	if (out_updraft) *out_updraft = updraft;
}

static inline void chaff_glsl_ref_eval_wind_3d(float wx, float wy, float wz,
                                               float zBase, float zTop, float anim_time, float fw,
                                               float *out_dir_x, float *out_dir_y,
                                               float *out_shear_x, float *out_shear_y,
                                               float *out_gust, float *out_updraft) {
	nd_glsl_ref_eval_wind_3d_ex(NULL, wx, wy, wz, zBase, zTop, anim_time, fw,
	                            out_dir_x, out_dir_y, out_shear_x, out_shear_y, out_gust, out_updraft);
}

static const char * const ND_WATER_FRAG_GLSL =
	"precision mediump float;\n"
	"varying vec2 v_uv;\n"
	"uniform sampler2D u_tex;\n"
	"void main() {\n"
	"	gl_FragColor = texture2D(u_tex, v_uv);\n"
	"}\n";

static inline uint32_t nd_water_shader_id(void)
{
	static uint32_t s_id = 0;
#ifdef __wasm__
	if (!s_id) {
		s_id = lam_shader_register("nd_water", ND_WATER_FRAG_GLSL);
	}
#endif
	return s_id;
}

static inline uint32_t nd_chaff_shader_id(void)
{
	static uint32_t s_id = 0;
#ifdef __wasm__
	if (!s_id) {
		s_id = lam_shader_register("chaff_noise", CHAFF_GLSL_FULLSCREEN_FRAG);
	}
#endif
	return s_id;
}

#endif /* ND_SEVEN_SHADER_H */
