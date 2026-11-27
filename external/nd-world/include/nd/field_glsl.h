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
	"float nd_gpu_eval_coarse_unwrapped(uint seed, int rx, int ry, float fw, out float out_cont_q12_4) {\n" \
	"    int cur_rx = rx;\n" \
	"    int cur_ry = ry;\n" \
	"    float coarse_q12_4 = 0.0;\n" \
	"    float cont_q12_4 = 0.0;\n" \
	"    for (int s = 15; s >= 2; s--) {\n" \
	"        float cell_w = float(1 << s) * 144.0;\n" \
	"        float w_oct = (s >= 5) ? 1.0 : nd_octave_weight(cell_w, fw);\n" \
	"        if (w_oct <= 0.001 && s < 9) break;\n" \
	"        int amp = chaff_octave_amplitude_ex(s, 2, 1);\n" \
	"        uint oct_seed = seed ^ (uint(s) * 0x85EBCA77u + 1u);\n" \
	"        float raw = chaff_wpoint_coarse_2d_f(cur_rx, cur_ry, s, oct_seed) - 128.0;\n" \
	"        float contrib = (raw * float(amp)) * 0.125;\n" \
	"        if (s >= 9) cont_q12_4 += contrib;\n" \
	"        coarse_q12_4 += contrib * w_oct;\n" \
	"        int nx = (cur_rx * 29304 - cur_ry * 14652) >> 15;\n" \
	"        int ny = (cur_rx * 14652 + cur_ry * 29304) >> 15;\n" \
	"        cur_rx = nx;\n" \
	"        cur_ry = ny;\n" \
	"    }\n" \
	"    out_cont_q12_4 = cont_q12_4;\n" \
	"    return coarse_q12_4;\n" \
	"}\n" \
	"float nd_gpu_bimodal_hypsometry(float elev) {\n" \
	"    if (elev < 0.0) {\n" \
	"        if (elev > -8.0) return elev;\n" \
	"        float d = -elev - 8.0;\n" \
	"        float deep = 8.0 + d * 1.8 + d * d * 0.45;\n" \
	"        return -min(deep, 2000.0);\n" \
	"    } else {\n" \
	"        if (elev <= 8.0) return elev * 1.5;\n" \
	"        if (elev <= 25.0) return 12.0 + (elev - 8.0) * 1.8;\n" \
	"        float m = elev - 25.0;\n" \
	"        float peak = 43.0 + m * 2.2 + m * m * 0.12;\n" \
	"        return min(peak, max(uPlanetMetric.y, 21600.0));\n" \
	"    }\n" \
	"}\n" \
	"float chaff_wpoint_periodic_2d_f(int x, int y, int shift, uint seed) {\n" \
	"    uint ushift = uint(shift);\n" \
	"    uint cell_count_mask = (1u << (16u - ushift)) - 1u;\n" \
	"    uint mask_frac = (1u << ushift) - 1u;\n" \
	"    uint ux = uint(x) & 0xFFFFu;\n" \
	"    uint uy = uint(y) & 0xFFFFu;\n" \
	"    uint kx0 = ux >> ushift;\n" \
	"    uint ky0 = uy >> ushift;\n" \
	"    uint kx1 = (kx0 + 1u) & cell_count_mask;\n" \
	"    uint ky1 = (ky0 + 1u) & cell_count_mask;\n" \
	"    float inv_step = 1.0 / float(1 << shift);\n" \
	"    float fx = float(ux & mask_frac) * inv_step;\n" \
	"    float fy = float(uy & mask_frac) * inv_step;\n" \
	"    float u = fx * fx * fx * (fx * (fx * 6.0 - 15.0) + 10.0);\n" \
	"    float v = fy * fy * fy * (fy * (fy * 6.0 - 15.0) + 10.0);\n" \
	"    float v00 = float(chaff_hash2(ivec2(int(kx0), int(ky0)), seed) >> 24u);\n" \
	"    float v10 = float(chaff_hash2(ivec2(int(kx1), int(ky0)), seed) >> 24u);\n" \
	"    float v01 = float(chaff_hash2(ivec2(int(kx0), int(ky1)), seed) >> 24u);\n" \
	"    float v11 = float(chaff_hash2(ivec2(int(kx1), int(ky1)), seed) >> 24u);\n" \
	"    return mix(mix(v00, v10, u), mix(v01, v11, u), v);\n" \
	"}\n" \
	"uint nd_gpu_xxh32_u32(uint val, uint seed) {\n" \
	"    uint h32 = seed + 0x165667B5u;\n" \
	"    h32 += val * 0xC2B2AE3Du;\n" \
	"    h32 = ((h32 << 17u) | (h32 >> 15u)) * 0x27D4EB2Fu;\n" \
	"    h32 ^= h32 >> 15u;\n" \
	"    h32 *= 0x85EBCA77u;\n" \
	"    h32 ^= h32 >> 13u;\n" \
	"    h32 *= 0xC2B2AE3Du;\n" \
	"    h32 ^= h32 >> 16u;\n" \
	"    return h32;\n" \
	"}\n" \
	"float nd_gpu_ridge01(float n01) {\n" \
	"    return max(0.0, 1.0 - abs(n01 * 2.0 - 1.0));\n" \
	"}\n" \
	"float nd_gpu_terrace3(float x) {\n" \
	"    float xc = clamp(x, 0.0, 1.0);\n" \
	"    if (xc >= 1.0) return 1.0;\n" \
	"    float sc = xc * 3.0;\n" \
	"    float tier = floor(sc);\n" \
	"    float rem = sc - tier;\n" \
	"    float riser = smoothstep(0.05, 0.65, rem) * 0.85;\n" \
	"    float bench = rem * 0.15;\n" \
	"    return clamp((tier + riser + bench) * (1.0 / 3.0), 0.0, 1.0);\n" \
	"}\n" \
	"float nd_gpu_scarp_wave(float proj) {\n" \
	"    float phase = mod(proj, 1024.0);\n" \
	"    return (phase < 280.0) ? smoothstep(0.0, 280.0, phase) : (1.0 - smoothstep(280.0, 1024.0, phase));\n" \
	"}\n" \
	"float nd_gpu_dune_wave(float proj) {\n" \
	"    float phase = mod(proj, 128.0);\n" \
	"    return (phase < 84.0) ? (phase * (1.0 / 84.0)) : ((128.0 - phase) * (1.0 / 44.0));\n" \
	"}\n" \
	"float nd_gpu_volcano_cone(float d) {\n" \
	"    if (d >= 480.0) return 0.0;\n" \
	"    float c = smoothstep(0.0, 480.0, 480.0 - d);\n" \
	"    c = c * (0.4 + 0.6 * c);\n" \
	"    if (d < 64.0) {\n" \
	"        c = max(0.0, c - smoothstep(0.0, 64.0, 64.0 - d) * 0.22);\n" \
	"    }\n" \
	"    return c;\n" \
	"}\n" \
	"float nd_gpu_orogeny_eval_archetype(uint prov, float az_rad, vec2 corner_r, vec2 ur,\n" \
	"                                    float base_hyp, float shore, float foothill, float inland,\n" \
	"                                    float hill, float alpine, float scale_f,\n" \
	"                                    float roll_raw, float hill_raw, float alp_raw,\n" \
	"                                    float n10, float r10, float r9, float r7, float r6) {\n" \
	"    float r = roll_raw * shore;\n" \
	"    vec2 dr = ur - corner_r;\n" \
	"    if (prov == 0u) {\n" \
	"        return r + hill_raw * 0.25 * hill + alp_raw * 0.125 * alpine;\n" \
	"    } else if (prov == 1u) {\n" \
	"        return r + hill_raw * hill + alp_raw * 0.375 * alpine;\n" \
	"    } else if (prov == 2u) {\n" \
	"        return r + hill_raw * 1.25 * hill + alp_raw * alpine;\n" \
	"    } else if (prov == 3u) {\n" \
	"        float m_sig = (n10 * 3.0 + r9 * 2.0) * 0.2;\n" \
	"        float mesa_h = nd_gpu_terrace3(m_sig) * 10500.0 * scale_f;\n" \
	"        return r + mesa_h * inland + hill_raw * 0.5 * hill + alp_raw * 0.125 * alpine;\n" \
	"    } else if (prov == 4u) {\n" \
	"        float proj = dr.x * cos(az_rad) + dr.y * sin(az_rad);\n" \
	"        float scarp_h = nd_gpu_scarp_wave(proj) * 14000.0 * scale_f;\n" \
	"        return r + scarp_h * inland + hill_raw * hill + alp_raw * 0.25 * alpine;\n" \
	"    } else if (prov == 5u) {\n" \
	"        float u_wall = smoothstep(1400.0 / 4096.0, 3100.0 / 4096.0, (r10 * 5.0 + r9 * 3.0) * 0.125);\n" \
	"        return r + (hill_raw * u_wall) * hill + (alp_raw * u_wall) * alpine;\n" \
	"    } else if (prov == 6u) {\n" \
	"        float volc_h = nd_gpu_volcano_cone(length(dr)) * 19500.0 * scale_f;\n" \
	"        return r + volc_h * inland + hill_raw * hill + alp_raw * 0.25 * alpine;\n" \
	"    } else if (prov == 7u) {\n" \
	"        float kb = (r7 * 5.0 + r6 * 3.0) * 0.125;\n" \
	"        float karst_h = (kb * kb) * 3600.0;\n" \
	"        return r + karst_h * foothill + hill_raw * 0.5 * hill + alp_raw * 0.25 * alpine;\n" \
	"    } else if (prov == 8u) {\n" \
	"        float proj = dr.x * cos(az_rad) + dr.y * sin(az_rad);\n" \
	"        float dune_h = nd_gpu_dune_wave(proj) * 720.0;\n" \
	"        return r + dune_h * shore + hill_raw * 0.5 * hill + alp_raw * 0.1875 * alpine;\n" \
	"    } else {\n" \
	"        float cliff_h = smoothstep(6.0, 64.0, base_hyp) * 1100.0;\n" \
	"        return r + cliff_h * shore + hill_raw * hill + alp_raw * 0.625 * alpine;\n" \
	"    }\n" \
	"}\n" \
	"float nd_gpu_orogeny_wu(uint seed, int rx, int ry, float base_hyp, float c_cont_wu, float fw) {\n" \
	"    if (base_hyp <= 1.0) return 0.0;\n" \
	"    float maxElev = max(uPlanetMetric.y, 21600.0);\n" \
	"    float scale_f = maxElev * (1.0 / 21600.0);\n" \
	"    float shore = smoothstep(1.0, 24.0, base_hyp);\n" \
	"    float foothill = smoothstep(12.0, 40.0, c_cont_wu) * smoothstep(1.0, 20.0, base_hyp);\n" \
	"    float inland = smoothstep(25.0, 62.0, c_cont_wu) * smoothstep(1.0, 16.0, base_hyp);\n" \
	"    uint oseed = seed ^ 0xD0D2877Du;\n" \
	"    float wx_raw = chaff_wpoint_periodic_2d_f(rx, ry, 9, oseed ^ 0x11A3u) - 128.0;\n" \
	"    float wy_raw = chaff_wpoint_periodic_2d_f(rx, ry, 9, oseed ^ 0x22B7u) - 128.0;\n" \
	"    float mx_raw = chaff_wpoint_periodic_2d_f(rx, ry, 7, oseed ^ 0x33C9u) - 128.0;\n" \
	"    float my_raw = chaff_wpoint_periodic_2d_f(rx, ry, 7, oseed ^ 0x44DBu) - 128.0;\n" \
	"    int urx = rx + int(floor(wx_raw * 0.5)) + int(floor(mx_raw * 0.125));\n" \
	"    int ury = ry + int(floor(wy_raw * 0.5)) + int(floor(my_raw * 0.125));\n" \
	"    float n10 = chaff_wpoint_periodic_2d_f(urx, ury, 10, oseed ^ 0x55EDu) * (1.0 / 256.0);\n" \
	"    float n11 = chaff_wpoint_periodic_2d_f(urx + 521, ury - 389, 11, oseed ^ 0x661Fu) * (1.0 / 256.0);\n" \
	"    float n9  = chaff_wpoint_periodic_2d_f(urx + 313, ury - 571,  9, oseed ^ 0x4E3Bu) * (1.0 / 256.0);\n" \
	"    float n8  = chaff_wpoint_periodic_2d_f(urx - 419, ury + 263,  8, oseed ^ 0x3D29u) * (1.0 / 256.0);\n" \
	"    float n7  = chaff_wpoint_periodic_2d_f(urx + 191, ury + 547,  7, oseed ^ 0x9955u) * (1.0 / 256.0);\n" \
	"    float n6  = chaff_wpoint_periodic_2d_f(urx - 613, ury + 271,  6, oseed ^ 0xAA67u) * (1.0 / 256.0);\n" \
	"    float n5  = chaff_wpoint_periodic_2d_f(urx + 713, ury - 419,  5, oseed ^ 0x7731u) * (1.0 / 256.0);\n" \
	"    float r11 = nd_gpu_ridge01(n11);\n" \
	"    float r10 = nd_gpu_ridge01(n10);\n" \
	"    float r9  = nd_gpu_ridge01(n9);\n" \
	"    float r8  = nd_gpu_ridge01(n8);\n" \
	"    float r7  = nd_gpu_ridge01(n7);\n" \
	"    float r6  = nd_gpu_ridge01(n6);\n" \
	"    float r5  = nd_gpu_ridge01(n5);\n" \
	"    float roll_raw = n9 * 260.0 + n8 * 170.0 + n7 * 105.0 + n6 * 54.0 + n5 * 24.0;\n" \
	"    float h_sig = (n10 * 3.0 + n9 * 3.0 + r8 * 2.0) * 0.125;\n" \
	"    float hill = smoothstep(850.0 / 4096.0, 3150.0 / 4096.0, h_sig) * foothill;\n" \
	"    float w4_oct = nd_octave_weight(2304.0, fw);\n" \
	"    float n4 = 0.0;\n" \
	"    if (w4_oct > 0.001 && (hill > 0.0001 || inland > 0.0001)) {\n" \
	"        n4 = chaff_wpoint_periodic_2d_f(urx - 317, ury + 823, 4, oseed ^ 0x8843u) * (1.0 / 256.0);\n" \
	"    }\n" \
	"    float hill_raw = 0.0;\n" \
	"    if (hill > 0.0001) {\n" \
	"        hill_raw = n9 * 1400.0 + n8 * 840.0 + n7 * 460.0 + r6 * 220.0 + r5 * 100.0 + n4 * 44.0 * w4_oct;\n" \
	"    }\n" \
	"    float belt = (r11 * 5.0 + r10 * 3.0) * 0.125;\n" \
	"    float a_sm = smoothstep(1200.0 / 4096.0, 3350.0 / 4096.0, belt);\n" \
	"    float alpine = a_sm * (0.375 + 0.625 * a_sm) * inland;\n" \
	"    float alp_raw = 0.0;\n" \
	"    if (alpine > 0.0001) {\n" \
	"        float h11 = r11 * r11;\n" \
	"        float h10 = r10 * (0.375 + 0.625 * r10);\n" \
	"        float h10w = h10 * (0.375 + 0.625 * h11);\n" \
	"        float h9 = r9 * (0.375 + 0.625 * r9);\n" \
	"        float h9w = h9 * (0.328125 + 0.671875 * h10w);\n" \
	"        float h8 = r8 * (0.375 + 0.625 * r8);\n" \
	"        float h8w = h8 * (0.296875 + 0.703125 * h9w);\n" \
	"        float h7 = r7 * (0.375 + 0.625 * r7);\n" \
	"        float h7w = h7 * (0.28125 + 0.71875 * h8w);\n" \
	"        float h6 = r6 * r6;\n" \
	"        float h6w = h6 * (0.25 + 0.75 * h7w);\n" \
	"        float h5 = r5 * (0.375 + 0.625 * r5);\n" \
	"        float h5w = h5 * (0.234375 + 0.765625 * h6w);\n" \
	"        alp_raw = h11 * 11200.0 + h10w * 7200.0 + h9w * 4000.0 +\n" \
	"                  h8w * 2000.0  + h7w  * 960.0  + h6w * 440.0 + h5w * 180.0;\n" \
	"        if (w4_oct > 0.001) {\n" \
	"            float r4 = nd_gpu_ridge01(n4);\n" \
	"            float h4 = r4 * (0.375 + 0.625 * r4);\n" \
	"            float h4w = h4 * (0.21875 + 0.78125 * h5w);\n" \
	"            alp_raw += h4w * 72.0 * w4_oct;\n" \
	"            float w3_oct = nd_octave_weight(1152.0, fw);\n" \
	"            if (w3_oct > 0.001) {\n" \
	"                float n3 = chaff_wpoint_periodic_2d_f(urx - 227, ury - 389, 3, oseed ^ 0xCC8Bu) * (1.0 / 256.0);\n" \
	"                float r3 = nd_gpu_ridge01(n3);\n" \
	"                float h3w = r3 * (0.125 + 0.875 * h4w);\n" \
	"                alp_raw += h3w * 28.0 * w3_oct;\n" \
	"            }\n" \
	"        }\n" \
	"        if (maxElev > 21600.0) {\n" \
	"            float extra_wu = maxElev - 21600.0;\n" \
	"            float n12 = chaff_wpoint_periodic_2d_f(urx - 2113, ury + 1657, 12, oseed ^ 0x6F23u) * (1.0 / 256.0);\n" \
	"            float r12 = nd_gpu_ridge01(n12);\n" \
	"            alp_raw += (r12 * r12) * extra_wu * 1.25;\n" \
	"        }\n" \
	"    }\n" \
	"    int cx0 = urx >> 10;\n" \
	"    int cy0 = ury >> 10;\n" \
	"    float fx = float(urx & 1023) * (1.0 / 1024.0);\n" \
	"    float fy = float(ury & 1023) * (1.0 / 1024.0);\n" \
	"    float u = fx * fx * fx * (fx * (fx * 6.0 - 15.0) + 10.0);\n" \
	"    float v = fy * fy * fy * (fy * (fy * 6.0 - 15.0) + 10.0);\n" \
	"    vec2 ur_v = vec2(float(urx), float(ury));\n" \
	"    float acc = 0.0;\n" \
	"    for (int k = 0; k < 4; k++) {\n" \
	"        int dxk = k & 1;\n" \
	"        int dyk = k >> 1;\n" \
	"        float wk = ((dxk == 0) ? (1.0 - u) : u) * ((dyk == 0) ? (1.0 - v) : v);\n" \
	"        if (wk <= 0.0001) continue;\n" \
	"        int cxk = cx0 + dxk;\n" \
	"        int cyk = cy0 + dyk;\n" \
	"        uint wcx = uint(cxk) & 63u;\n" \
	"        uint wcy = uint(cyk) & 63u;\n" \
	"        uint gh = nd_gpu_xxh32_u32((wcx * 0x9E3779B1u) ^ (wcy * 0x85EBCA77u), oseed ^ 0x51A7BEEFu);\n" \
	"        uint prov = gh % 10u;\n" \
	"        float az_rad = float((gh >> 8u) & 255u) * (6.28318530718 / 256.0);\n" \
	"        float hk = nd_gpu_orogeny_eval_archetype(\n" \
	"            prov, az_rad, vec2(float(cxk << 10), float(cyk << 10)), ur_v,\n" \
	"            base_hyp, shore, foothill, inland, hill, alpine, scale_f,\n" \
	"            roll_raw, hill_raw, alp_raw, n10, r10, r9, r7, r6);\n" \
	"        acc += wk * hk;\n" \
	"    }\n" \
	"    return max(acc, 0.0);\n" \
	"}\n" \
	"float nd_gpu_room_hyp_coarse(uint seed, int rx_unwrapped, int ry_unwrapped, float fw) {\n" \
	"    int rx = ((rx_unwrapped + 32768) & 65535) - 32768;\n" \
	"    int ry = ((ry_unwrapped + 32768) & 65535) - 32768;\n" \
	"    uint ux = (uint(rx) - 32512u) & 0xFFFFu;\n" \
	"    uint uy = (uint(ry) - 32512u) & 0xFFFFu;\n" \
	"    bool wrap_x = (ux < 512u);\n" \
	"    bool wrap_y = (uy < 512u);\n" \
	"    float tx = wrap_x ? (float(ux) * (1.0 / 512.0)) : 0.0;\n" \
	"    float ty = wrap_y ? (float(uy) * (1.0 / 512.0)) : 0.0;\n" \
	"    int rx_base = wrap_x ? (32512 + int(ux)) : rx;\n" \
	"    int ry_base = wrap_y ? (32512 + int(uy)) : ry;\n" \
	"    int k_limit = wrap_y ? 4 : (wrap_x ? 2 : 1);\n" \
	"    float c00 = 0.0, c10 = 0.0, c01 = 0.0, c11 = 0.0;\n" \
	"    float k00 = 0.0, k10 = 0.0, k01 = 0.0, k11 = 0.0;\n" \
	"    for (int k = 0; k < k_limit; k++) {\n" \
	"        if ((k & 1) != 0 && !wrap_x) continue;\n" \
	"        int cur_x = rx_base - (k & 1) * 65536;\n" \
	"        int cur_y = ry_base - (k >> 1) * 65536;\n" \
	"        float kval = 0.0;\n" \
	"        float val = nd_gpu_eval_coarse_unwrapped(seed, cur_x, cur_y, fw, kval);\n" \
	"        if (k == 0) { c00 = val; k00 = kval; }\n" \
	"        else if (k == 1) { c10 = val; k10 = kval; }\n" \
	"        else if (k == 2) { c01 = val; k01 = kval; }\n" \
	"        else { c11 = val; k11 = kval; }\n" \
	"    }\n" \
	"    float c_y0 = wrap_x ? mix(c00, c10, tx) : c00;\n" \
	"    float c_y1 = wrap_x ? mix(c01, c11, tx) : c01;\n" \
	"    float c = wrap_y ? mix(c_y0, c_y1, ty) : c_y0;\n" \
	"    float k_y0 = wrap_x ? mix(k00, k10, tx) : k00;\n" \
	"    float k_y1 = wrap_x ? mix(k01, k11, tx) : k01;\n" \
	"    float c_cont = wrap_y ? mix(k_y0, k_y1, ty) : k_y0;\n" \
	"    c -= float(uSeaOffsetQ12_4);\n" \
	"    c_cont -= float(uSeaOffsetQ12_4);\n" \
	"    float base_hyp = nd_gpu_bimodal_hypsometry(c * 0.0625);\n" \
	"    return min(base_hyp + nd_gpu_orogeny_wu(seed, rx, ry, base_hyp, c_cont * 0.0625, fw), max(uPlanetMetric.y, 21600.0));\n" \
	"}\n" \
	"vec3 nd_gpu_eval_canonical_field(vec2 worldPos, float fw, float texelSpan, float texZ, vec2 texGrad, out float out_fine_elev) {\n" \
	"    uint seed = 0x48454947u ^ (uint(uWorldId) * 0x9E3779B1u);\n" \
	"    float coarseH = texZ;\n" \
	"    vec2 coarseGrad = texGrad;\n" \
	"    if (texelSpan > 320.0 && fw < 180.0) {\n" \
	"        float rx_f = (worldPos.x - 80.0) / 160.0;\n" \
	"        float ry_f = (64.0 - worldPos.y) / 128.0;\n" \
	"        int rx0 = int(floor(rx_f));\n" \
	"        int ry0 = int(floor(ry_f));\n" \
	"        float h00 = 0.0, h10 = 0.0, h01 = 0.0, h11 = 0.0;\n" \
	"        for (int cr = 0; cr < 4; cr++) {\n" \
	"            float hc = nd_gpu_room_hyp_coarse(seed, rx0 + (cr & 1), ry0 + (cr >> 1), fw);\n" \
	"            if (cr == 0) h00 = hc;\n" \
	"            else if (cr == 1) h10 = hc;\n" \
	"            else if (cr == 2) h01 = hc;\n" \
	"            else h11 = hc;\n" \
	"        }\n" \
	"        vec2 f = vec2(rx_f - floor(rx_f), ry_f - floor(ry_f));\n" \
	"        vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);\n" \
	"        vec2 du = 30.0 * f * f * (f * (f - 2.0) + 1.0);\n" \
	"        float k0 = h10 - h00;\n" \
	"        float k1 = h01 - h00;\n" \
	"        float k2 = h11 - h01 - k0;\n" \
	"        float interpH = h00 + k0 * u.x + k1 * u.y + k2 * u.x * u.y;\n" \
	"        vec2 interpGrad = vec2((k0 + k2 * u.y) * du.x / 160.0, -(k1 + k2 * u.x) * du.y / 128.0);\n" \
	"        float roomBlend = (1.0 - smoothstep(90.0, 180.0, fw));\n" \
	"        coarseH = mix(texZ, interpH, roomBlend);\n" \
	"        coarseGrad = mix(texGrad, interpGrad, roomBlend);\n" \
	"    }\n" \
	"    /* Compute exact active sub-texel octave window [s_min, s_max] in [3..9] before evaluating ruggedness */\n" \
	"    float max_sub_wl = (texelSpan <= 320.0) ? min(512.0, texelSpan * 3.2) : 512.0;\n" \
	"    if (fw * OCTAVE_MIN_PX >= max_sub_wl || texelSpan * 3.2 <= 8.0 || nd_octave_weight(max_sub_wl, fw) <= 0.001) {\n" \
	"        out_fine_elev = 0.0;\n" \
	"        return vec3(coarseH, coarseGrad.x, coarseGrad.y);\n" \
	"    }\n" \
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
	"        if (texelSpan <= 320.0) {\n" \
	"            float wSubTex = smoothstep(texelSpan * 3.2, texelSpan * 1.2, oct_wl);\n" \
	"            if (wSubTex <= 0.001) {\n" \
	"                cur_p = rotAtan * cur_p;\n" \
	"                cur_inv = cur_inv * irotAtan;\n" \
	"                continue;\n" \
	"            }\n" \
	"            wNyq *= wSubTex;\n" \
	"        }\n" \
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
	"    if (rel_water > 140.0) {\n" \
	"        coast_w *= 1.0 + 0.75 * smoothstep(140.0, 460.0, rel_water);\n" \
	"    }\n" \
	"    fineH = clamp(fineH * coast_w, -127.0, 127.0);\n" \
	"    fineGrad *= coast_w;\n" \
	"    out_fine_elev = fineH;\n" \
	"    return vec3(coarseH + fineH, coarseGrad.x + fineGrad.x, coarseGrad.y + fineGrad.y);\n" \
	"}\n"

#endif /* ND_FIELD_GLSL_H */
