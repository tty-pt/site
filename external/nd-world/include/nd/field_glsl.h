#ifndef ND_FIELD_GLSL_H
#define ND_FIELD_GLSL_H

#include <ttypt/chaff_glsl.h>

/* ============================================================================
 * NeverDark Canonical World Field GLSL Snippets (<nd/field_glsl.h>)
 *
 * Encapsulates the GPU side of the 16-octave planetary torus elevation &
 * sub-texel scale-space band-pass evaluation so nd-world owns both the CPU
 * (nd/field.h) and GPU (nd/field_glsl.h) representations of the world field.
 *
 * Requires CHAFF_GLSL_HASH_FN and CHAFF_GLSL_BAND_FN to be emitted prior to
 * ND_FIELD_GLSL_CANONICAL_FN, plus uniform int uWorldId and uniform float uSeaLevel.
 * ============================================================================ */

#define ND_FIELD_GLSL_CANONICAL_FN \
	"int chaff_octave_amplitude_ex(int shift, int min_shift, int base_amp) {\n" \
	"    if (shift < min_shift || base_amp <= 0) return 0;\n" \
	"    int k = shift - min_shift;\n" \
	"    int base = base_amp << (k >> 1);\n" \
	"    if ((k & 1) != 0) {\n" \
	"        base += (base >> 1);\n" \
	"    }\n" \
	"    return base;\n" \
	"}\n" \
	"float chaff_wpoint_coarse_2d_f(int x, int y, int shift, uint seed) {\n" \
	"    uint ushift = uint(shift);\n" \
	"    uint cell_count_mask = (1u << (16u - ushift)) - 1u;\n" \
	"    uint mask_frac = (1u << ushift) - 1u;\n" \
	"    uint cshift = (shift >= 9) ? 0u : ushift;\n" \
	"    uint ux = uint(x) & 0xFFFFu;\n" \
	"    uint uy = uint(y) & 0xFFFFu;\n" \
	"    uint kx0 = ux >> ushift;\n" \
	"    uint ky0 = uy >> ushift;\n" \
	"    uint kx1 = (kx0 + 1u) & cell_count_mask;\n" \
	"    uint ky1 = (ky0 + 1u) & cell_count_mask;\n" \
	"    int cx0 = int(kx0 << cshift);\n" \
	"    int cy0 = int(ky0 << cshift);\n" \
	"    int cx1 = int(kx1 << cshift);\n" \
	"    int cy1 = int(ky1 << cshift);\n" \
	"    float inv_step = 1.0 / float(1 << shift);\n" \
	"    float fx = float(ux & mask_frac) * inv_step;\n" \
	"    float fy = float(uy & mask_frac) * inv_step;\n" \
	"    float u = fx * fx * fx * (fx * (fx * 6.0 - 15.0) + 10.0);\n" \
	"    float v = fy * fy * fy * (fy * (fy * 6.0 - 15.0) + 10.0);\n" \
	"    float v00 = float(chaff_hash2(ivec2(cx0, cy0), seed) >> 24u);\n" \
	"    float v10 = float(chaff_hash2(ivec2(cx1, cy0), seed) >> 24u);\n" \
	"    float v01 = float(chaff_hash2(ivec2(cx0, cy1), seed) >> 24u);\n" \
	"    float v11 = float(chaff_hash2(ivec2(cx1, cy1), seed) >> 24u);\n" \
	"    return mix(mix(v00, v10, u), mix(v01, v11, u), v);\n" \
	"}\n" \
	"vec3 chaff_wpoint_2d_grad_f(vec2 p, int shift, uint seed) {\n" \
	"    float step_f = float(1 << shift);\n" \
	"    vec2 q = p / step_f;\n" \
	"    vec2 iq = floor(q);\n" \
	"    vec2 f = q - iq;\n" \
	"    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);\n" \
	"    vec2 du = (30.0 * f * f * (f * (f - 2.0) + 1.0)) / step_f;\n" \
	"    uint step_u = 1u << uint(shift);\n" \
	"    uint sx0 = (uint(int(iq.x)) << uint(shift)) & 0xFFFFu;\n" \
	"    uint sy0 = (uint(int(iq.y)) << uint(shift)) & 0xFFFFu;\n" \
	"    uint sx1 = (sx0 + step_u) & 0xFFFFu;\n" \
	"    uint sy1 = (sy0 + step_u) & 0xFFFFu;\n" \
	"    float v00 = float(chaff_hash2(ivec2(int(sx0), int(sy0)), seed) >> 24u);\n" \
	"    float v10 = float(chaff_hash2(ivec2(int(sx1), int(sy0)), seed) >> 24u);\n" \
	"    float v01 = float(chaff_hash2(ivec2(int(sx0), int(sy1)), seed) >> 24u);\n" \
	"    float v11 = float(chaff_hash2(ivec2(int(sx1), int(sy1)), seed) >> 24u);\n" \
	"    float k0 = v10 - v00;\n" \
	"    float k1 = v01 - v00;\n" \
	"    float k2 = v11 - v01 - k0;\n" \
	"    float val = v00 + k0 * u.x + k1 * u.y + k2 * u.x * u.y;\n" \
	"    vec2 grad = vec2((k0 + k2 * u.y) * du.x, (k1 + k2 * u.x) * du.y);\n" \
	"    return vec3(val, grad.x, grad.y);\n" \
	"}\n" \
	"const float OCTAVE_MIN_PX  = 1.35;\n" \
	"const float OCTAVE_FULL_PX = 4.50;\n" \
	"float nd_octave_weight(float wavelength, float fw) {\n" \
	"    return smoothstep(OCTAVE_MIN_PX, OCTAVE_FULL_PX, wavelength / max(fw, 1e-4));\n" \
	"}\n" \
	"float nd_gpu_eval_coarse_unwrapped(uint seed, int rx, int ry, float fw) {\n" \
	"    int cur_rx = rx;\n" \
	"    int cur_ry = ry;\n" \
	"    float coarse_q12_4 = 0.0;\n" \
	"    for (int s = 15; s >= 2; s--) {\n" \
	"        float cell_w = float(1 << s) * 144.0;\n" \
	"        float w_oct = nd_octave_weight(cell_w, fw);\n" \
	"        if (w_oct <= 0.001) break;\n" \
	"        int amp = chaff_octave_amplitude_ex(s, 2, 1);\n" \
	"        uint oct_seed = seed ^ (uint(s) * 0x85EBCA77u + 1u);\n" \
	"        float raw = chaff_wpoint_coarse_2d_f(cur_rx, cur_ry, s, oct_seed) - 128.0;\n" \
	"        coarse_q12_4 += (raw * float(amp)) * 0.125 * w_oct;\n" \
	"        int nx = (cur_rx * 29304 - cur_ry * 14652) >> 15;\n" \
	"        int ny = (cur_rx * 14652 + cur_ry * 29304) >> 15;\n" \
	"        cur_rx = nx;\n" \
	"        cur_ry = ny;\n" \
	"    }\n" \
	"    return coarse_q12_4;\n" \
	"}\n" \
	"vec3 nd_gpu_eval_canonical_field(vec2 worldPos, float fw, float texelSpan, float texZ, vec2 texGrad, out float out_fine_elev) {\n" \
	"    float coarseH = texZ;\n" \
	"    vec2 coarseGrad = texGrad;\n" \
	"    /* Compute exact active sub-texel octave window [s_min, s_max] in [3..9] before evaluating ruggedness */\n" \
	"    if (fw * OCTAVE_MIN_PX >= 512.0 || texelSpan * 3.2 <= 8.0 || chaff_band_weight(512.0, fw, texelSpan) <= 0.001) {\n" \
	"        out_fine_elev = 0.0;\n" \
	"        return vec3(coarseH, coarseGrad.x, coarseGrad.y);\n" \
	"    }\n" \
	"    uint seed = 0x48454947u ^ (uint(uWorldId) * 0x9E3779B1u);\n" \
	"    uint rug_seed = 0x52554747u ^ (uint(uWorldId) * 0x9E3779B1u);\n" \
	"    const mat2 rotAtan = mat2(0.8942871, 0.44714355, -0.44714355, 0.8942871);\n" \
	"    const mat2 irotAtan = mat2(0.8942871, -0.44714355, 0.44714355, 0.8942871);\n" \
	"    float raw_macro = chaff_wpoint_2d_grad_f(worldPos, 10, rug_seed ^ 0xA531u).x;\n" \
	"    vec2 p_rug_rot = rotAtan * worldPos;\n" \
	"    float raw_meso = chaff_wpoint_2d_grad_f(p_rug_rot, 8, rug_seed ^ 0x5CA9u).x;\n" \
	"    float raw_rug = (raw_macro * 5.0 + raw_meso * 3.0) * 0.125;\n" \
	"    float rug_env = smoothstep(120.0, 168.0, raw_rug);\n" \
	"    vec2 cur_p = worldPos;\n" \
	"    mat2 cur_inv = mat2(1.0, 0.0, 0.0, 1.0);\n" \
	"    float base_fine = 0.0;\n" \
	"    vec2 base_grad = vec2(0.0);\n" \
	"    float meso_ridge = 0.0;\n" \
	"    vec2 meso_grad = vec2(0.0);\n" \
	"    for (int s = 9; s >= 3; s--) {\n" \
	"        float oct_wl = float(1 << s);\n" \
	"        float wNyq = nd_octave_weight(oct_wl, fw);\n" \
	"        if (wNyq <= 0.001) break;\n" \
	"        float wSubTex = smoothstep(texelSpan * 3.2, texelSpan * 1.2, oct_wl);\n" \
	"        if (wSubTex <= 0.001) {\n" \
	"            cur_p = rotAtan * cur_p;\n" \
	"            cur_inv = cur_inv * irotAtan;\n" \
	"            continue;\n" \
	"        }\n" \
	"        wNyq *= wSubTex;\n" \
	"        int amp_b = chaff_octave_amplitude_ex(s, 3, 1);\n" \
	"        uint b_seed = seed ^ (uint(s) * 0x85EBCA77u + 0xBEEFu);\n" \
	"        vec3 gb = chaff_wpoint_2d_grad_f(cur_p, s, b_seed);\n" \
	"        float vb = gb.x - 128.0;\n" \
	"        vec2 db = cur_inv * gb.yz;\n" \
	"        if (s >= 7) {\n" \
	"            float av = abs(vb);\n" \
	"            float r = 64.0 - (av * av) / (av + 16.0);\n" \
	"            float sgn = (vb >= 0.0) ? -1.0 : 1.0;\n" \
	"            float dr = (av * (av + 32.0)) / ((av + 16.0) * (av + 16.0));\n" \
	"            db = db * (sgn * dr);\n" \
	"            vb = r;\n" \
	"        }\n" \
	"        float g2_b = dot(base_grad, base_grad);\n" \
	"        float erosion_b = 1.0 / (1.0 + g2_b * 1.4);\n" \
	"        float scale_b = (float(amp_b) / 128.0) * wNyq * erosion_b;\n" \
	"        base_fine += vb * scale_b;\n" \
	"        base_grad += db * scale_b;\n" \
	"        if (s >= 5 && rug_env > 0.001) {\n" \
	"            int amp_m = chaff_octave_amplitude_ex(s, 5, 10);\n" \
	"            uint m_seed = (seed ^ 0x7F4A7C15u) ^ (uint(s) * 0x85EBCA77u + 0xCAFEu);\n" \
	"            vec3 gm = chaff_wpoint_2d_grad_f(cur_p, s, m_seed);\n" \
	"            float vm = gm.x - 128.0;\n" \
	"            vec2 dm = cur_inv * gm.yz;\n" \
	"            if (s >= 6) {\n" \
	"                float av = abs(vm);\n" \
	"                float r = 64.0 - (av * av) / (av + 16.0);\n" \
	"                float sgn = (vm >= 0.0) ? -1.0 : 1.0;\n" \
	"                float dr = (av * (av + 32.0)) / ((av + 16.0) * (av + 16.0));\n" \
	"                dm = dm * (sgn * dr);\n" \
	"                vm = r;\n" \
	"            }\n" \
	"            float g2_m = dot(meso_grad, meso_grad);\n" \
	"            float erosion_m = 1.0 / (1.0 + g2_m * 1.4);\n" \
	"            float scale_m = (float(amp_m) / 128.0) * wNyq * erosion_m;\n" \
	"            meso_ridge += vm * scale_m;\n" \
	"            meso_grad += dm * scale_m;\n" \
	"        }\n" \
	"        cur_p = rotAtan * cur_p;\n" \
	"        cur_inv = cur_inv * irotAtan;\n" \
	"    }\n" \
	"    float b_scale = 0.5 + 1.5 * rug_env;\n" \
	"    float fineH = base_fine * b_scale + meso_ridge * rug_env;\n" \
	"    vec2 fineGrad = base_grad * b_scale + meso_grad * rug_env;\n" \
	"    float rel_water = coarseH - uSeaLevel;\n" \
	"    float coast_w = 1.0;\n" \
	"    if (rel_water <= -8.0) {\n" \
	"        coast_w = 0.5;\n" \
	"    } else if (rel_water <= 0.0) {\n" \
	"        coast_w = 0.25 + (-rel_water / 8.0) * 0.25;\n" \
	"    } else if (rel_water < 6.0) {\n" \
	"        coast_w = 0.25 + (rel_water / 6.0) * 0.75;\n" \
	"    }\n" \
	"    fineH = clamp(fineH * coast_w, -127.0, 127.0);\n" \
	"    fineGrad *= coast_w;\n" \
	"    out_fine_elev = fineH;\n" \
	"    return vec3(coarseH + fineH, coarseGrad.x + fineGrad.x, coarseGrad.y + fineGrad.y);\n" \
	"}\n"

#endif /* ND_FIELD_GLSL_H */
