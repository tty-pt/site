/*
 * nd_seven_gl.c — Pure-C WebGL2 3D Terrain, Water, and Billboard Renderer.
 * Part of the NeverDark Seven 2.5D diorama platform.
 *
 * Implements the 3D rendering pipeline entirely in pure C over <ttypt/webgl.h>.
 * Dual-compiles for native SSR (safe no-op stubs) and WebAssembly (WebGL2).
 */

#include "nd_seven_gl.h"
#include "nd_seven_shader.h"
#include "nd_seven_atlas.h"
#include "nd_seven_atlas_draw.h"
#include <ttypt/webgl.h>
#include <ttypt/areole.h>
#include <ttypt/chaff_glsl.h>
#include <nd/field.h>
#include <nd/field_glsl.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

#ifndef __wasm__
/* Native GL stub for glReadPixels (not provided by webgl.h for native) */
static inline void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                                GLenum format, GLenum type, void *pixels) {
    (void)x; (void)y; (void)width; (void)height; (void)format; (void)type; (void)pixels;
}

/* Forward declarations for native-only functions */
static void gl_build_occ2d_map(void);
#endif

#define PI_F 3.14159265358979323846f
#define TWO_PI_F 6.28318530717958647692f

/* Shared scene-lighting model (terrain, water, sprites): cloud shadow strength s in [0, 0.95] attenuates direct sun by
 * ND_CLOUD_SHADOW_DIRECT_LOSS and the neutral (untinted) ambient fill by ND_CLOUD_SHADOW_AMBIENT_LOSS. Clouds reduce
 * opacity of the shadow instead of switching the terrain to night. */
#define ND_SL_STR_(x) #x
#define ND_SL_STR(x) ND_SL_STR_(x)
#define ND_GLSL_SCENE_LIGHT \
	"float nd_sun_trans(float s) { return 1.0 - " ND_SL_STR(ND_CLOUD_SHADOW_DIRECT_LOSS) " * s; }\n" \
	"float nd_amb_trans(float s) { return 1.0 - " ND_SL_STR(ND_CLOUD_SHADOW_AMBIENT_LOSS) " * s; }\n"

/* Analytic planetary cloud shadow (per-pixel evaluation): used above 14 km and blended seamlessly across 9..14 km. */
#define ND_GLSL_CLOUD_SHADOW_FN \
	"/* Scale-Adaptive 2D Projection of the 3D Cloud Volume along the Sun Vector L_sun = (-0.577, -0.577, 0.816):\n" \
	" * Projects the 3D wind-sheared cloud decks onto the 2D ground/water surface with orbital shadow offset\n" \
	" * so continental cyclone arms and cumulus towers cast unmistakable 3D ground shadows from ground to orbit! */\n" \
	"float eval_planetary_cloud_shadow(vec2 fragXY, float fragZ, float anim_time, float humidity, float dist, float fw) {\n" \
	"    const float zBase = 420.0;\n" \
	"    const float zTop  = 2820.0;\n" \
	"    float orbitOffset = clamp(dist * 0.045, 0.0, 14000.0);\n" \
	"    vec2 sunProj = (length(uSunDir.xy) > 0.05) ? -normalize(uSunDir.xy) : vec2(-0.7071, -0.7071);\n" \
	"    /* Single representative projection slice at zk = 920 (the 2D synoptic field is identical at every height; the\n" \
	"     * two former slices differed only by a ~640 unit sun-ward shift, far below the 1.1..4.2 km cell size) */\n" \
	"    const float zk = 920.0;\n" \
	"    float distFadeZ = smoothstep(8000.0, 14000.0, dist);\n" \
	"    if (fragZ >= zk + 240.0 && distFadeZ < 0.999) return 0.0;\n" \
	"    float sliceFade = mix(smoothstep(zk + 240.0, zk - 120.0, fragZ), 1.0, distFadeZ);\n" \
	"    float dz = max(0.0, zk - fragZ) + orbitOffset;\n" \
	"    vec2 projXY = fragXY + sunProj * dz;\n" \
	"    vec4 synC; vec2 pD; vec2 pZ; vec2 baseG;\n" \
	"    if (eval_synoptic_carrier(projXY, anim_time, humidity, fw, synC, pD, pZ, baseG)) return 0.0;\n" \
	"    vec2 atmoWindDir = vec2(0.741, -0.671), shearDisp = vec2(21.8, -19.8);\n" \
	"    float atmoGust = 0.65, updraft = 0.50;\n" \
	"    if (fw < 200.0) chaff_eval_wind_3d(vec3(projXY, zk), zBase, zTop, anim_time, fw, atmoWindDir, shearDisp, atmoGust, updraft);\n" \
	"    vec2 dummyG; float dummyC;\n" \
	"    vec4 synCloud = eval_synoptic_macro_cloud_from_carrier(projXY - shearDisp, anim_time, humidity, fw, false, synC, pZ, baseG, dummyG, dummyC);\n" \
	"    return clamp(smoothstep(0.05, 0.66, synCloud.x * sliceFade), 0.0, 0.95);\n" \
	"}\n"

/* Cloud-shadow map (R8, one frame-wide evaluation of the synoptic field on the Sun-Pyramid cloud slice zk = 920):
 * Indexed by world/projected slice position, so terrain, water, sprites, and volumetric cloud sun-shadow probes each do ONE texture fetch.
 * uCsm = (window centre x, window centre y, window half-extent); half < 0 means Sun-Pyramid is 100% clear sky; half == 0 means no map bound. */
#define ND_GLSL_CSM_LOOKUP_FN \
	"uniform sampler2D uCloudShadowMap;\n" \
	"uniform vec3 uCsm;\n" \
	"float nd_csm_density(vec2 sliceXY) {\n" \
	"    if (uCsm.z <= 0.0) return 0.0;\n" \
	"    vec2 uv = (sliceXY - uCsm.xy) / (2.0 * uCsm.z) + 0.5;\n" \
	"    vec2 e = min(uv, 1.0 - uv);\n" \
	"    float edge = smoothstep(0.0, 0.04, min(e.x, e.y));\n" \
	"    return texture(uCloudShadowMap, uv).r * edge;\n" \
	"}\n" \
	"float nd_cloud_shadow_map(vec2 fragXY, float fragZ, float dist) {\n" \
	"    const float zk = 920.0;\n" \
	"    float distFadeZ = smoothstep(8000.0, 14000.0, dist);\n" \
	"    if (uCsm.z <= 0.0 || (fragZ >= zk + 240.0 && distFadeZ < 0.999)) return 0.0;\n" \
	"    float sliceFade = mix(smoothstep(zk + 240.0, zk - 120.0, fragZ), 1.0, distFadeZ);\n" \
	"    float dz = max(0.0, zk - fragZ) + clamp(dist * 0.045, 0.0, 14000.0);\n" \
	"    vec2 sunProj = (length(uSunDir.xy) > 0.05) ? -normalize(uSunDir.xy) : vec2(-0.7071, -0.7071);\n" \
	"    vec2 projXY = fragXY + sunProj * dz;\n" \
	"    return clamp(smoothstep(0.05, 0.66, nd_csm_density(projXY) * sliceFade), 0.0, 0.95);\n" \
	"}\n"

/* Terrain / water entry point: zero cost when Sun-Pyramid is clear sky (uCsm.z < 0.0), one map fetch below 140 km inside the CSM window,
 * smooth crossfade 140..180 km, analytic evaluation above 180 km. */
#define ND_GLSL_CLOUD_SHADOW_ENTRY_FN \
	"float nd_cloud_shadow(vec2 fragXY, float fragZ, float anim_time, float humidity, float dist, float fw) {\n" \
	"    if (uCsm.z < -1.5 || (uCsm.z < 0.0 && dist < 180000.0)) return 0.0;\n" \
	"    if (dist >= 180000.0 || uCsm.z <= 0.0) return eval_planetary_cloud_shadow(fragXY, fragZ, anim_time, humidity, dist, fw);\n" \
	"    const float zk = 920.0;\n" \
	"    float dz = max(0.0, zk - fragZ) + clamp(dist * 0.045, 0.0, 14000.0);\n" \
	"    vec2 sunProj = (length(uSunDir.xy) > 0.05) ? -normalize(uSunDir.xy) : vec2(-0.7071, -0.7071);\n" \
	"    vec2 projXY = fragXY + sunProj * dz;\n" \
	"    vec2 csmRel = abs(projXY - uCsm.xy) / uCsm.z;\n" \
	"    float csmBox = max(csmRel.x, csmRel.y);\n" \
	"    if (dist <= 140000.0 && csmBox <= 0.96) return nd_cloud_shadow_map(fragXY, fragZ, dist);\n" \
	"    if (csmBox >= 0.96) return eval_planetary_cloud_shadow(fragXY, fragZ, anim_time, humidity, dist, fw);\n" \
	"    float csmSh = nd_cloud_shadow_map(fragXY, fragZ, dist);\n" \
	"    float anaSh = eval_planetary_cloud_shadow(fragXY, fragZ, anim_time, humidity, dist, fw);\n" \
	"    float blend = max(smoothstep(140000.0, 180000.0, dist), smoothstep(0.88, 0.96, csmBox));\n" \
	"    return mix(csmSh, anaSh, blend);\n" \
	"}\n"

/* Toroidal geometric self-eclipse raymarch:
 * Tests if the ray towards the sun traverses across the central throat and penetrates the opposite ring rim.
 * In the torus frame (Z = axis of revolution), core circle has radius R in Z=0, tube radius r0.
 * Euclidean SDF to the torus tube is: length(vec2(length(Q.xy) - R, Q.z)) - r0. */
#define ND_GLSL_TORUS_ECLIPSE_FN \
	"float eval_torus_self_eclipse(vec3 P, vec3 sDir, float R, float r0) {\n" \
	"    float t = r0 * 0.45;\n" \
	"    float maxT = 2.0 * (R + r0);\n" \
	"    float occ = 1.0;\n" \
	"    for (int i = 0; i < 4; i++) {\n" \
	"        vec3 Q = P + t * sDir;\n" \
	"        float rho = length(Q.xy);\n" \
	"        float d = length(vec2(rho - R, Q.z)) - r0;\n" \
	"        if (d < 0.0) return 0.0;\n" \
	"        occ = min(occ, clamp(d / (0.07 * r0), 0.0, 1.0));\n" \
	"        t += max(d, r0 * 0.35);\n" \
	"        if (t > maxT) break;\n" \
	"    }\n" \
	"    return occ;\n" \
	"}\n"

/* Canonical 2D Synoptic & Mesoscale Cellular Weather Envelope Function:
 * Pure function of (worldXY, anim_time, humidity, fw). Zero camera-distance morphing.
 * Driven by the evolved synoptic wind streamfunction and divergence-free curl flow:
 * 1. Synoptic Scale (600 km - 2,800 km): Planetary Rossby waves psi0/psi1 produce crisp cyclonic
 *    frontal bands and spiral rainbands, while keeping ~65-75% of the planet in sunny anticyclonic
 *    subsidence basins so continents, mountains, and oceans are clearly visible from space.
 * 2. Regional Frontal Squall Scale (18 km - 120 km): Frontal rainbands and wind-shift squall lines.
 * 3. Mesoscale Cellular Cumulus & Cloud Streets (1.1 km - 4.2 km): Wind-aligned roll vortices and
 *    convective cumulus cells (FEW / SCT / BKN) whose analytical area-fraction expectation matches
 *    the band-limited orbital envelope so zooming never triggers a sudden cloud-cover jump. */
#define ND_GLSL_SYNOPTIC_CLOUD_FN \
	"bool eval_synoptic_carrier(vec2 worldXY, float anim_time, float humidity, float fw,\n" \
	"                           out vec4 outSyn, out vec2 outPDisp, out vec2 outPZonal, out vec2 outBaseGrad) {\n" \
	"    const vec2 prevDir = vec2(0.741, -0.671);\n" \
	"    const vec2 prevPerp = vec2(0.671, 0.741);\n" \
	"    float tFlow = mod(anim_time * 0.005, 1024.0);\n" \
	"    vec3 psi0 = chaff_grad_noise2d(worldXY * 0.00000042 - prevDir * (tFlow * 0.40), 0x4359434Cu);\n" \
	"    float wPsi1 = smoothstep(0.32, 0.08, 0.00000180 * fw);\n" \
	"    vec3 psi1 = (wPsi1 > 0.001) ? chaff_grad_noise2d(worldXY * 0.00000180 + vec2(-psi0.z, psi0.y) * 0.30 - prevDir * tFlow, 0x5354524Du) : vec3(0.5, 0.0, 0.0);\n" \
	"    vec2 synWarp = vec2(-psi0.z, psi0.y) * 260000.0 + vec2(-psi1.z, psi1.y) * (68000.0 * wPsi1);\n" \
	"    vec2 pDisp = worldXY + synWarp;\n" \
	"    float pAlong = dot(pDisp, prevDir);\n" \
	"    float pCross = dot(pDisp, prevPerp);\n" \
	"    vec2 pZonal = prevDir * (pAlong * 0.65) + prevPerp * (pCross * 1.55);\n" \
	"    float phase0 = mod(anim_time * 0.004, 1024.0);\n" \
	"    float phase1 = mod(anim_time * 0.008, 1024.0);\n" \
	"    vec3 oct0 = chaff_grad_noise2d(pZonal * 0.00000050 - prevDir * phase0, 0x434C4430u);\n" \
	"    const mat2 rot = mat2(0.8, -0.6, 0.6, 0.8);\n" \
	"    const mat2 irot = mat2(0.8, 0.6, -0.6, 0.8);\n" \
	"    float wOct1 = smoothstep(0.32, 0.08, 0.00000240 * fw);\n" \
	"    vec3 oct1 = (wOct1 > 0.001) ? chaff_grad_noise2d((rot * pZonal) * 0.00000240 - prevDir * phase1, 0x434C4431u) : vec3(0.5, 0.0, 0.0);\n" \
	"    float r0 = 1.0 - 2.0 * abs(oct0.x - 0.5);\n" \
	"    float r1 = 1.0 - 2.0 * abs(oct1.x - 0.5);\n" \
	"    float dR0 = 4.0 * (0.5 - oct0.x);\n" \
	"    float dR1 = 4.0 * (0.5 - oct1.x);\n" \
	"    float front0 = oct0.x + (r0 - 0.68) * 0.28;\n" \
	"    float front1 = oct1.x + (r1 - 0.68) * 0.24;\n" \
	"    vec2 g0 = oct0.yz * (1.0 + 0.28 * dR0) * 0.72;\n" \
	"    vec2 g1 = (irot * oct1.yz) * (1.0 + 0.24 * dR1) * (0.54 * 1.65 * wOct1);\n" \
	"    outBaseGrad = g0 + g1;\n" \
	"    float synFront = 0.50 + (front0 - 0.50) * 0.72 + (front1 - 0.50) * (0.54 * wOct1) + (psi0.x - 0.50) * 0.34;\n" \
	"    outSyn = vec4(synFront, r0, synWarp);\n" \
	"    outPDisp = pDisp;\n" \
	"    outPZonal = pZonal;\n" \
	"    float wReg2 = smoothstep(0.26, 0.07, 0.00001150 * fw);\n" \
	"    float wReg3 = smoothstep(0.24, 0.06, 0.00005500 * fw);\n" \
	"    float maxFront = synFront + 0.116 * wReg2 + 0.070 * wReg3 + 0.055;\n" \
	"    return (maxFront <= (0.555 - 0.105 * humidity)) && ((synFront + 0.173) <= (0.56 - 0.08 * humidity));\n" \
	"}\n" \
	"vec4 eval_synoptic_macro_cloud_from_carrier(vec2 worldXY, float anim_time, float humidity, float fw, bool wantCirrus,\n" \
	"                                            vec4 synCarrier, vec2 pZonal, vec2 baseGrad,\n" \
	"                                            out vec2 outGrad, out float outHighCirrus) {\n" \
	"    const vec2 prevDir = vec2(0.741, -0.671);\n" \
	"    const vec2 prevPerp = vec2(0.671, 0.741);\n" \
	"    const mat2 rot = mat2(0.8, -0.6, 0.6, 0.8);\n" \
	"    const mat2 irot = mat2(0.8, 0.6, -0.6, 0.8);\n" \
	"    float synFront = synCarrier.x;\n" \
	"    float r0 = synCarrier.y;\n" \
	"    vec2 synWarp = synCarrier.zw;\n" \
	"    vec2 macroGrad = baseGrad;\n" \
	"    float wReg2 = smoothstep(0.26, 0.07, 0.00001150 * fw);\n" \
	"    float wReg3 = smoothstep(0.24, 0.06, 0.00005500 * fw);\n" \
	"    /* Exact analytical upper bound of regional squalls (0.116*wReg2 + 0.070*wReg3) and cirrus (+0.118):\n" \
	"     * bails out immediately in anticyclonic clear-sky basins before evaluating 7 cirrus/squall/meso noises! */\n" \
	"    float maxFront = synFront + 0.116 * wReg2 + 0.070 * wReg3;\n" \
	"    if (maxFront <= (0.555 - 0.105 * humidity) && (!wantCirrus || (synFront + 0.118) <= (0.56 - 0.08 * humidity))) {\n" \
	"        outGrad = vec2(0.0);\n" \
	"        outHighCirrus = 0.0;\n" \
	"        return vec4(0.0, synFront, synWarp);\n" \
	"    }\n" \
	"    outHighCirrus = 0.0;\n" \
	"    float wCir0 = smoothstep(0.32, 0.08, 0.00000115 * fw);\n" \
	"    if (wantCirrus && wCir0 > 0.001) {\n" \
	"        float phase1 = mod(anim_time * 0.008, 1024.0);\n" \
	"        /* High-troposphere jet-stream cirrus & frontal outflow shield (confined to baroclinic fronts) */\n" \
	"        vec2 pJet = irot * pZonal + vec2(38000.0, -52000.0);\n" \
	"        vec3 cir0 = chaff_grad_noise2d(pJet * 0.00000115 - prevDir * (phase1 * 1.35), 0x43495230u);\n" \
	"        float wCir = smoothstep(0.32, 0.08, 0.00000380 * fw);\n" \
	"        vec3 cirN = (wCir > 0.001) ? chaff_grad_noise2d((irot * pJet) * 0.00000380 - prevDir * (phase1 * 1.45), 0x43495252u) : vec3(0.5, 0.0, 0.0);\n" \
	"        float cirStreak0 = 1.0 - 2.0 * abs(cir0.x - 0.5);\n" \
	"        float cirStreak1 = 1.0 - 2.0 * abs(cirN.x - 0.5);\n" \
	"        float cirStreak  = mix(cirStreak0, 0.55 * cirStreak0 + 0.45 * cirStreak1, wCir);\n" \
	"        outHighCirrus = wCir0 * smoothstep(0.56 - 0.08 * humidity, 0.78 - 0.06 * humidity, synFront + (cirStreak - 0.56) * 0.26)\n" \
	"                      * smoothstep(0.42, 0.84, cirStreak);\n" \
	"    }\n" \
	"    /* Regional squall bands (87 km & 18 km) band-limited strictly by screen-space Nyquist fw (>= 4..14 px per wavelength) */\n" \
	"    if (wReg2 > 0.001) {\n" \
	"        float phase2 = mod(anim_time * 0.015, 1024.0);\n" \
	"        vec2 p2 = rot * (rot * worldXY) + synWarp * 0.14;\n" \
	"        vec3 oct2 = chaff_grad_noise2d(p2 * 0.00001150 - prevDir * phase2, 0x434C4432u);\n" \
	"        float r2 = 1.0 - 2.0 * abs(oct2.x - 0.5);\n" \
	"        float dR2 = 4.0 * (0.5 - oct2.x);\n" \
	"        float squall2 = (oct2.x - 0.50) + (r2 - 0.68) * 0.25;\n" \
	"        synFront += squall2 * 0.20 * wReg2;\n" \
	"        macroGrad += (irot * (irot * oct2.yz)) * (1.0 + 0.25 * dR2) * (0.52 * wReg2);\n" \
	"        if (wReg3 > 0.001) {\n" \
	"            float phase3 = mod(anim_time * 0.028, 1024.0);\n" \
	"            vec2 p3 = rot * p2 + synWarp * 0.04;\n" \
	"            vec3 oct3 = chaff_grad_noise2d(p3 * 0.00005500 - prevDir * phase3, 0x434C4433u);\n" \
	"            synFront += (oct3.x - 0.50) * 0.14 * wReg3;\n" \
	"            macroGrad += (irot * oct3.yz) * (0.42 * wReg3);\n" \
	"        }\n" \
	"    }\n" \
	"    float synRegime = smoothstep(0.555 - 0.105 * humidity, 0.750 - 0.075 * humidity, synFront);\n" \
	"    float overcastShield = smoothstep(0.66, 0.90, synRegime);\n" \
	"    /* Analytical band-limited synoptic envelope (orbitCov) matches the spatial expectation of cumulusCells\n" \
	"     * so zooming across the mesoscale Nyquist threshold (wMeso0: 1 -> 0) never causes a coverage jump! */\n" \
	"    float orbitArm = smoothstep(0.32, 0.72, r0);\n" \
	"    float cellExpect = synRegime * (0.42 + 0.44 * synRegime) * (0.78 + 0.22 * orbitArm);\n" \
	"    float orbitCov = mix(cellExpect, synRegime * (0.80 + 0.20 * orbitArm), overcastShield);\n" \
	"    float macroCoverage = orbitCov;\n" \
	"    float wMeso0 = smoothstep(0.22, 0.06, 0.00024000 * fw);\n" \
	"    if (wMeso0 > 0.001 && synRegime > 0.005) {\n" \
	"        float phaseM0 = mod(anim_time * 0.048, 1024.0);\n" \
	"        vec2 pStreet = vec2(dot(worldXY, prevDir) * 0.72, dot(worldXY, prevPerp) * 1.32) + synWarp * 0.012;\n" \
	"        vec3 m0 = chaff_grad_noise2d(pStreet * 0.00024000 - prevDir * phaseM0, 0x4D455330u);\n" \
	"        float wMeso0b = smoothstep(0.22, 0.06, 0.00038000 * fw);\n" \
	"        vec3 m1 = (wMeso0b > 0.001) ? chaff_grad_noise2d((rot * pStreet) * 0.00038000 - prevDir * (phaseM0 * 1.25), 0x4D455331u) : vec3(0.5, 0.0, 0.0);\n" \
	"        float b0 = 1.0 - 2.0 * abs(m0.x - 0.5);\n" \
	"        float b1 = 1.0 - 2.0 * abs(m1.x - 0.5);\n" \
	"        float cellField = 0.42 * m0.x + 0.34 * (b0 * b0) + 0.24 * mix(0.25, b1 * b1, wMeso0b);\n" \
	"        macroGrad += (prevDir * m0.y * 0.72 + prevPerp * m0.z * 1.32) * (0.65 * wMeso0);\n" \
	"        float wMeso1 = smoothstep(0.20, 0.05, 0.00090000 * fw);\n" \
	"        if (wMeso1 > 0.001) {\n" \
	"            float phaseM1 = mod(anim_time * 0.082, 1024.0);\n" \
	"            vec3 m2 = chaff_grad_noise2d((rot * (rot * worldXY)) * 0.00090000 - prevDir * phaseM1, 0x4D455332u);\n" \
	"            float b2 = 1.0 - 2.0 * abs(m2.x - 0.5);\n" \
	"            cellField = mix(cellField, 0.55 * cellField + 0.45 * (0.45 * m2.x + 0.55 * b2 * b2), 0.55 * wMeso1);\n" \
	"            macroGrad += (irot * (irot * m2.yz)) * (0.55 * wMeso1);\n" \
	"        }\n" \
	"        float threshLo = mix(0.56, 0.20, synRegime);\n" \
	"        float threshHi = mix(0.76, 0.50, synRegime);\n" \
	"        float cumulusCells = smoothstep(threshLo, threshHi, cellField) * smoothstep(0.02, 0.18, synRegime);\n" \
	"        /* In continental overcast shields (overcastShield > 0), maintain dense continuous cloud deck\n" \
	"         * while carving billowing top relief, rather than punching empty holes to the ground! */\n" \
	"        float structuredCov = mix(cumulusCells, max(cumulusCells, synRegime * 0.88 + 0.12 * cellField), overcastShield);\n" \
	"        macroCoverage = mix(orbitCov, structuredCov, wMeso0);\n" \
	"    }\n" \
	"    outGrad = macroGrad;\n" \
	"    return vec4(macroCoverage, synFront, synWarp);\n" \
	"}\n" \
	"vec4 eval_synoptic_macro_cloud_core(vec2 worldXY, float anim_time, float humidity, float fw, bool wantCirrus, out vec2 outGrad, out float outHighCirrus) {\n" \
	"    vec4 synC; vec2 pD; vec2 pZ; vec2 baseG;\n" \
	"    eval_synoptic_carrier(worldXY, anim_time, humidity, fw, synC, pD, pZ, baseG);\n" \
	"    return eval_synoptic_macro_cloud_from_carrier(worldXY, anim_time, humidity, fw, wantCirrus, synC, pZ, baseG, outGrad, outHighCirrus);\n" \
	"}\n" \
	"vec4 eval_synoptic_macro_cloud_grad(vec2 worldXY, float anim_time, float humidity, float fw, out vec2 outGrad, out float outHighCirrus) {\n" \
	"    return eval_synoptic_macro_cloud_core(worldXY, anim_time, humidity, fw, true, outGrad, outHighCirrus);\n" \
	"}\n" \
	"vec4 eval_synoptic_macro_cloud_fw(vec2 worldXY, float anim_time, float humidity, float fw) {\n" \
	"    vec2 dummyG; float dummyC;\n" \
	"    return eval_synoptic_macro_cloud_core(worldXY, anim_time, humidity, fw, false, dummyG, dummyC);\n" \
	"}\n" \
	"vec4 eval_synoptic_macro_cloud(vec2 worldXY, float anim_time, float humidity) {\n" \
	"    return eval_synoptic_macro_cloud_fw(worldXY, anim_time, humidity, 12000.0);\n" \
	"}\n" \
	"bool eval_synoptic_is_clear_sky(vec2 worldXY, float anim_time, float humidity, float fw) {\n" \
	"    vec4 synC; vec2 pD; vec2 pZ; vec2 baseG;\n" \
	"    return eval_synoptic_carrier(worldXY, anim_time, humidity, fw, synC, pD, pZ, baseG);\n" \
	"}\n"

/* Macro low-zoom grid: 16x16 quads = 17x17 vertices = 289 vertices (512 triangles, 4x lighter than 32x32) */
#define GL_GRID_QUADS_MACRO 16
#define GL_GRID_VERTS_MACRO (GL_GRID_QUADS_MACRO + 1)
#define GL_TOTAL_VERTS_MACRO (GL_GRID_VERTS_MACRO * GL_GRID_VERTS_MACRO)
#define GL_TOTAL_INDICES_MACRO (GL_GRID_QUADS_MACRO * GL_GRID_QUADS_MACRO * 6)

/* Level 1..2 standard grid: 32x32 quads = 33x33 vertices = 1,089 vertices (2,048 triangles) */
#define GL_GRID_QUADS_LO 32
#define GL_GRID_QUADS GL_GRID_QUADS_LO
#define GL_GRID_VERTS (GL_GRID_QUADS + 1)
#define GL_TOTAL_VERTS (GL_GRID_VERTS * GL_GRID_VERTS)
#define GL_TOTAL_INDICES (GL_GRID_QUADS * GL_GRID_QUADS * 6)

/* Level 0 normal-zoom high-detail grid: 48x48 quads = 49x49 vertices = 2,401 vertices (4,608 triangles, 2.25x density) */
#define GL_GRID_QUADS_HI 48
#define GL_GRID_VERTS_HI (GL_GRID_QUADS_HI + 1)
#define GL_TOTAL_VERTS_HI (GL_GRID_VERTS_HI * GL_GRID_VERTS_HI)
#define GL_TOTAL_INDICES_HI (GL_GRID_QUADS_HI * GL_GRID_QUADS_HI * 6)

#define MAX_CHUNK_TEXTURES 192
#define MAX_WATER_QUEUE_BASE 192
#define MAX_BB_VERTS 65536
#define MAX_BB_OTHER_VERTS 32768

/* Dynamic queue sizes based on detail scale (Low preset = smaller queues) */
static inline int nd_seven_gl_max_water_queue(void) {
    float scale = nd_seven_get_detail_scale();
    if (scale < 0.75f) return 64;   /* Low preset */
    if (scale < 0.9f) return 128;   /* Medium preset */
    return MAX_WATER_QUEUE_BASE;    /* High/Ultra preset */
}

/* --- GLSL 300 es Shaders --- */

static const char *s_vs_terrain =
	"#version 300 es\n"
	"precision highp float;\n"
	"precision highp int;\n"
	"precision highp isampler2D;\n"
	"precision highp sampler2D;\n"
	"layout (location = 0) in vec2 a_grid_uv;\n"
	"uniform mat4 uMVP;\n"
	"uniform isampler2D uHeightTex;\n"
	"uniform vec2 uChunkOrigin;\n"
	"uniform vec2 uChunkRelOrigin;\n"
	"uniform vec2 uChunkSpan;\n"
	"uniform vec4 uRefTrig;\n"
	"uniform float uHeightScale;\n"
	"uniform float uSeaLevel;\n"
	"uniform vec3 uCameraPos;\n"
	"uniform vec3 uPlanetCenter;\n"
	"uniform mat4 uBodyRot;\n"
	"uniform vec2 uTorusRadii;\n"
	"uniform vec2 uWorldDim;\n"
	"out vec2 vUV;\n"
	"out float vZ;\n"
	"out float vLighting;\n"
	"out float vDistance;\n"
	"out vec2 vWorldPos;\n"
	"out vec3 vViewDir;\n"
	"out float vWaterDepth;\n"
	"out vec3 vNormal;\n"
	"out vec3 vTorusNormal;\n"
	"out vec3 vTangentU;\n"
	"out vec3 vTangentV;\n"
	"float getH(ivec2 p, ivec2 maxC) {\n"
	"    ivec2 c = clamp(p, ivec2(0), maxC);\n"
	"    float hc = float(texelFetch(uHeightTex, c, 0).r);\n"
	"    if (c == p) return hc;\n"
	"    ivec2 cIn = clamp(c + sign(c - p), ivec2(0), maxC);\n"
	"    float hIn = float(texelFetch(uHeightTex, cIn, 0).r);\n"
	"    return 2.0 * hc - hIn;\n"
	"}\n"
	CHAFF_GLSL_HASH_FN
	CHAFF_GLSL_GRAD_NOISE_FN
	"/* 4x4 Catmull-Rom bicubic spline: smooth non-stair-stepped C1 height field & exact analytic gradients */\n"
	"float sampleHeightSmooth(vec2 uv, out vec2 out_grad) {\n"
	"    vec2 texSize = vec2(textureSize(uHeightTex, 0));\n"
	"    vec2 maxCoordF = max(vec2(1.0), texSize - 1.0);\n"
	"    vec2 uvc = clamp(uv, 0.0, 1.0);\n"
	"    vec2 pixelCoord = uvc * maxCoordF;\n"
	"    ivec2 iCoord = ivec2(floor(pixelCoord));\n"
	"    vec2 f = pixelCoord - vec2(iCoord);\n"
	"    ivec2 maxCoord = ivec2(maxCoordF);\n"
	"    if (uChunkSpan.x > 5120.0) {\n"
	"        float h00 = getH(iCoord, maxCoord);\n"
	"        float h10 = getH(iCoord + ivec2(1, 0), maxCoord);\n"
	"        float h01 = getH(iCoord + ivec2(0, 1), maxCoord);\n"
	"        float h11 = getH(iCoord + ivec2(1, 1), maxCoord);\n"
	"        vec2 u5 = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);\n"
	"        vec2 du5 = 30.0 * f * f * (f * (f - 2.0) + 1.0);\n"
	"        float hQuint = mix(mix(h00, h10, u5.x), mix(h01, h11, u5.x), u5.y);\n"
	"        vec2 gradQuint = vec2(mix(h10 - h00, h11 - h01, u5.y) * du5.x, mix(h01 - h00, h11 - h10, u5.x) * du5.y);\n"
	"        out_grad.x = gradQuint.x * (maxCoordF.x / max(1.0, uChunkSpan.x));\n"
	"        out_grad.y = gradQuint.y * (maxCoordF.y / max(1.0, uChunkSpan.y));\n"
	"        return hQuint;\n"
	"    }\n"
	"    vec4 wx = vec4(\n"
	"        f.x * (-0.5 + f.x * (1.0 - 0.5 * f.x)),\n"
	"        1.0 + f.x * f.x * (-2.5 + 1.5 * f.x),\n"
	"        f.x * (0.5 + f.x * (2.0 - 1.5 * f.x)),\n"
	"        f.x * f.x * (-0.5 + 0.5 * f.x)\n"
	"    );\n"
	"    vec4 wy = vec4(\n"
	"        f.y * (-0.5 + f.y * (1.0 - 0.5 * f.y)),\n"
	"        1.0 + f.y * f.y * (-2.5 + 1.5 * f.y),\n"
	"        f.y * (0.5 + f.y * (2.0 - 1.5 * f.y)),\n"
	"        f.y * f.y * (-0.5 + 0.5 * f.y)\n"
	"    );\n"
	"    vec4 dwx = vec4(\n"
	"        -0.5 + f.x * (2.0 - 1.5 * f.x),\n"
	"        f.x * (-5.0 + 4.5 * f.x),\n"
	"        0.5 + f.x * (4.0 - 4.5 * f.x),\n"
	"        f.x * (-1.0 + 1.5 * f.x)\n"
	"    );\n"
	"    vec4 dwy = vec4(\n"
	"        -0.5 + f.y * (2.0 - 1.5 * f.y),\n"
	"        f.y * (-5.0 + 4.5 * f.y),\n"
	"        0.5 + f.y * (4.0 - 4.5 * f.y),\n"
	"        f.y * (-1.0 + 1.5 * f.y)\n"
	"    );\n"
	"    float hBicubic = 0.0;\n"
	"    vec2 gradBicubic = vec2(0.0);\n"
	"    for (int j = 0; j < 4; j++) {\n"
	"        float rowH = 0.0;\n"
	"        float rowDx = 0.0;\n"
	"        int py = iCoord.y + j - 1;\n"
	"        for (int i = 0; i < 4; i++) {\n"
	"            int px = iCoord.x + i - 1;\n"
	"            float val = getH(ivec2(px, py), maxCoord);\n"
	"            rowH += val * wx[i];\n"
	"            rowDx += val * dwx[i];\n"
	"        }\n"
	"        hBicubic += rowH * wy[j];\n"
	"        gradBicubic.x += rowDx * wy[j];\n"
	"        gradBicubic.y += rowH * dwy[j];\n"
	"    }\n"
	"    /* Watertight chunk seam: use smooth quintic weights on the outer 1.25-texel perimeter */\n"
	"    float borderDist = min(min(pixelCoord.x, maxCoordF.x - pixelCoord.x), min(pixelCoord.y, maxCoordF.y - pixelCoord.y));\n"
	"    float borderBlend = smoothstep(0.0, 1.25, borderDist);\n"
	"    float finalH = hBicubic;\n"
	"    vec2 finalGrad = gradBicubic;\n"
	"    if (borderBlend < 1.0) {\n"
	"        float h00 = getH(iCoord, maxCoord);\n"
	"        float h10 = getH(iCoord + ivec2(1, 0), maxCoord);\n"
	"        float h01 = getH(iCoord + ivec2(0, 1), maxCoord);\n"
	"        float h11 = getH(iCoord + ivec2(1, 1), maxCoord);\n"
	"        vec2 u5 = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);\n"
	"        vec2 du5 = 30.0 * f * f * (f * (f - 2.0) + 1.0);\n"
	"        float hQuint = mix(mix(h00, h10, u5.x), mix(h01, h11, u5.x), u5.y);\n"
	"        vec2 gradQuint = vec2(mix(h10 - h00, h11 - h01, u5.y) * du5.x, mix(h01 - h00, h11 - h10, u5.x) * du5.y);\n"
	"        finalH = mix(hQuint, hBicubic, borderBlend);\n"
	"        finalGrad = mix(gradQuint, gradBicubic, borderBlend);\n"
	"    }\n"
	"    out_grad.x = finalGrad.x * (maxCoordF.x / max(1.0, uChunkSpan.x));\n"
	"    out_grad.y = finalGrad.y * (maxCoordF.y / max(1.0, uChunkSpan.y));\n"
	"    return finalH;\n"
	"}\n"
	"void main() {\n"
	"    vUV = a_grid_uv;\n"
	"    float scale = (uHeightScale != 0.0) ? uHeightScale : 1.0;\n"
	"    float effectiveScale = scale * 0.0625;\n"
	"    vec2 grad;\n"
	"    float raw_h = sampleHeightSmooth(a_grid_uv, grad);\n"
	"    float wz = raw_h * effectiveScale;\n"
	"    vec2 worldXY = vec2(uChunkOrigin.x + a_grid_uv.x * uChunkSpan.x, uChunkOrigin.y + a_grid_uv.y * uChunkSpan.y);\n"
	"    vec2 relXY = vec2(uChunkRelOrigin.x + a_grid_uv.x * uChunkSpan.x, uChunkRelOrigin.y + a_grid_uv.y * uChunkSpan.y);\n"
	"    const float TWO_PI = 6.28318530718;\n"
	"    float R = (uTorusRadii.x > 0.0) ? uTorusRadii.x : 2000000.0;\n"
	"    float r0 = (uTorusRadii.y > 0.0) ? uTorusRadii.y : 1335000.0;\n"
	"    vec2 wdim = (uWorldDim.x > 0.0 && uWorldDim.y > 0.0) ? uWorldDim : vec2(10485760.0, 8388608.0);\n"
	"    float du = TWO_PI * (relXY.x / wdim.x);\n"
	"    float dv = TWO_PI * (relXY.y / wdim.y);\n"
	"    float sdu = sin(du), shdu = sin(0.5 * du);\n"
	"    float sdv = sin(dv), shdv = sin(0.5 * dv);\n"
	"    float cu0 = uRefTrig.x, su0 = uRefTrig.y, cv0 = uRefTrig.z, sv0 = uRefTrig.w;\n"
	"    float dcu = -2.0 * shdu * shdu * cu0 - sdu * su0;\n"
	"    float dsu = -2.0 * shdu * shdu * su0 + sdu * cu0;\n"
	"    float dcv = -2.0 * shdv * shdv * cv0 - sdv * sv0;\n"
	"    float dsv = -2.0 * shdv * shdv * sv0 + sdv * cv0;\n"
	"    float cu = cu0 + dcu, su = su0 + dsu;\n"
	"    float cv = cv0 + dcv, sv = sv0 + dsv;\n"
	"    float Rv0 = R + r0 * cv0;\n"
	"    mat3 bodyR = mat3(uBodyRot);\n"
	"    vec3 baseTorusPos = uPlanetCenter + bodyR * vec3(Rv0 * dcu + (r0 * dcv) * cu, Rv0 * dsu + (r0 * dcv) * su, r0 * dsv);\n"
	"    float baseCamDist = length(baseTorusPos - uCameraPos);\n"
	"    /* At orbital/planetary altitude, smoothly suppress minor vertex noise displacement unless a feature is very prominent\n"
	"     * (e.g. major mountain ranges or deep abyssal trenches), keeping coarse polygon silhouettes clean and jitter-free */\n"
	"    float planetSmooth = smoothstep(32000.0, 320000.0, baseCamDist);\n"
	"    if (planetSmooth > 0.001) {\n"
	"        float seaRef = (uSeaLevel > -900.0) ? uSeaLevel : 0.0;\n"
	"        float relZ = wz - seaRef;\n"
	"        float prominence = smoothstep(65.0, 520.0, abs(relZ));\n"
	"        float vertKeep = mix(1.0, prominence, planetSmooth);\n"
	"        wz = seaRef + relZ * vertKeep;\n"
	"        grad *= vertKeep;\n"
	"    }\n"
	"    vZ = wz;\n"
	"    float dRv = r0 * dcv + wz * cv;\n"
	"    vec3 worldPos = uPlanetCenter + bodyR * vec3(Rv0 * dcu + dRv * cu, Rv0 * dsu + dRv * su, r0 * dsv + wz * sv);\n"
	"    vec3 torusN = bodyR * vec3(cv * cu, cv * su, sv);\n"
	"    vec3 tangentU = bodyR * vec3(-su, cu, 0.0);\n"
	"    vec3 tangentV = bodyR * vec3(-sv * cu, -sv * su, cv);\n"
	"    vec3 N = normalize(torusN - grad.x * effectiveScale * tangentU - grad.y * effectiveScale * tangentV);\n"
	"    vec3 L = normalize(-0.577 * tangentU - 0.577 * tangentV + 0.816 * torusN);\n"
	"    float diff = max(0.0, dot(N, L));\n"
	"    vLighting = 0.45 + 0.55 * diff;\n"
	"    vWorldPos = worldXY;\n"
	"    vWaterDepth = (uSeaLevel > -900.0) ? (uSeaLevel - wz) : -999.0;\n"
	"    vDistance = length(worldPos - uCameraPos);\n"
	"    vViewDir = uCameraPos - worldPos;\n"
	"    vNormal = N;\n"
	"    vTorusNormal = torusN;\n"
	"    vTangentU = tangentU;\n"
	"    vTangentV = tangentV;\n"
	"    gl_Position = uMVP * vec4(worldPos, 1.0);\n"
	"}\n";

static const char *s_fs_terrain =
	"#version 300 es\n"
	"precision highp float;\n"
	"precision highp int;\n"
	"precision highp isampler2D;\n"
	"precision highp sampler2D;\n"
	"in vec2 vUV;\n"
	"in float vZ;\n"
	"in float vLighting;\n"
	"in float vDistance;\n"
	"in vec2 vWorldPos;\n"
	"in vec3 vViewDir;\n"
	"in float vWaterDepth;\n"
	"in vec3 vNormal;\n"
	"in vec3 vTorusNormal;\n"
	"in vec3 vTangentU;\n"
	"in vec3 vTangentV;\n"
	"uniform isampler2D uHeightTex;\n"
	"uniform sampler2D uSubstrateTex;\n"
	"uniform vec2 uChunkSpan;\n"
	"uniform float uHeightScale;\n"
	"uniform float uSeaLevel;\n"
	"uniform float uFogStart;\n"
	"uniform float uFogEnd;\n"
	"uniform float uAnimTime;\n"
	"uniform vec3 uSkyColor;\n"
	"uniform float uAtmoIntensity;\n"
	"uniform vec4 uLambLight;\n"
	"uniform float uBaseSeaLevel;\n"
	"uniform vec3 uSunDir;\n"
	"uniform vec3 uMoonDir;\n"
	"uniform vec4 uLambWakeHead;\n"
	"uniform mat4 uLambWakeXYT0;\n"
	"uniform mat4 uLambWakeXYT1;\n"
	"uniform mat4 uLambWakeVel0;\n"
	"uniform mat4 uLambWakeVel1;\n"
	"uniform int uWorldId;\n"
	"uniform int uSeaOffsetQ12_4;\n"
	"uniform vec3 uCameraPos;\n"
	"uniform vec3 uPlanetCenter;\n"
	"uniform vec2 uTorusRadii;\n"
	"out vec4 FragColor;\n"
	"float getH(ivec2 p, ivec2 maxC) {\n"
	"    ivec2 c = clamp(p, ivec2(0), maxC);\n"
	"    float hc = float(texelFetch(uHeightTex, c, 0).r);\n"
	"    if (c == p) return hc;\n"
	"    ivec2 cIn = clamp(c + sign(c - p), ivec2(0), maxC);\n"
	"    float hIn = float(texelFetch(uHeightTex, cIn, 0).r);\n"
	"    return 2.0 * hc - hIn;\n"
	"}\n"
		"/* 4x4 Catmull-Rom bicubic spline: smooth non-stair-stepped C1 height field, exact analytic gradients & Laplacian curvature */\n"
	"float g_bicubicCurv = 0.0;\n"
	"float sampleHeightSmooth(vec2 uv, out vec2 out_grad) {\n"
	"    vec2 texSize = vec2(textureSize(uHeightTex, 0));\n"
	"    vec2 maxCoordF = max(vec2(1.0), texSize - 1.0);\n"
	"    vec2 uvc = clamp(uv, 0.0, 1.0);\n"
	"    vec2 pixelCoord = uvc * maxCoordF;\n"
	"    ivec2 iCoord = ivec2(floor(pixelCoord));\n"
	"    vec2 f = pixelCoord - vec2(iCoord);\n"
	"    ivec2 maxCoord = ivec2(maxCoordF);\n"
	"    vec2 texToWorld = maxCoordF / max(vec2(1.0), uChunkSpan);\n"
	"    vec2 u5 = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);\n"
	"    if (vDistance > 8400.0 || uChunkSpan.x > 5120.0) {\n"
	"        float h00 = getH(iCoord,                 maxCoord);\n"
	"        float h10 = getH(iCoord + ivec2(1, 0),   maxCoord);\n"
	"        float h01 = getH(iCoord + ivec2(0, 1),   maxCoord);\n"
	"        float h11 = getH(iCoord + ivec2(1, 1),   maxCoord);\n"
	"        vec2 du5 = 30.0 * f * f * (f * (f - 2.0) + 1.0);\n"
	"        float hQuint = mix(mix(h00, h10, u5.x), mix(h01, h11, u5.x), u5.y);\n"
	"        vec2 gradQuint = vec2(mix(h10 - h00, h11 - h01, u5.y) * du5.x, mix(h01 - h00, h11 - h10, u5.x) * du5.y);\n"
	"        out_grad = gradQuint * texToWorld;\n"
	"        g_bicubicCurv = 0.0;\n"
	"        return hQuint;\n"
	"    }\n"
	"    vec4 wx = vec4(\n"
	"        f.x * (-0.5 + f.x * (1.0 - 0.5 * f.x)),\n"
	"        1.0 + f.x * f.x * (-2.5 + 1.5 * f.x),\n"
	"        f.x * (0.5 + f.x * (2.0 - 1.5 * f.x)),\n"
	"        f.x * f.x * (-0.5 + 0.5 * f.x)\n"
	"    );\n"
	"    vec4 wy = vec4(\n"
	"        f.y * (-0.5 + f.y * (1.0 - 0.5 * f.y)),\n"
	"        1.0 + f.y * f.y * (-2.5 + 1.5 * f.y),\n"
	"        f.y * (0.5 + f.y * (2.0 - 1.5 * f.y)),\n"
	"        f.y * f.y * (-0.5 + 0.5 * f.y)\n"
	"    );\n"
	"    vec4 dwx = vec4(\n"
	"        -0.5 + f.x * (2.0 - 1.5 * f.x),\n"
	"        f.x * (-5.0 + 4.5 * f.x),\n"
	"        0.5 + f.x * (4.0 - 4.5 * f.x),\n"
	"        f.x * (-1.0 + 1.5 * f.x)\n"
	"    );\n"
	"    vec4 dwy = vec4(\n"
	"        -0.5 + f.y * (2.0 - 1.5 * f.y),\n"
	"        f.y * (-5.0 + 4.5 * f.y),\n"
	"        0.5 + f.y * (4.0 - 4.5 * f.y),\n"
	"        f.y * (-1.0 + 1.5 * f.y)\n"
	"    );\n"
	"    float hBicubic = 0.0;\n"
	"    vec2 gradBicubic = vec2(0.0);\n"
	"    float gridH[16];\n"
	"    for (int j = 0; j < 4; j++) {\n"
	"        float rowH = 0.0;\n"
	"        float rowDx = 0.0;\n"
	"        int py = iCoord.y + j - 1;\n"
	"        for (int i = 0; i < 4; i++) {\n"
	"            int px = iCoord.x + i - 1;\n"
	"            float val = getH(ivec2(px, py), maxCoord);\n"
	"            gridH[j * 4 + i] = val;\n"
	"            rowH += val * wx[i];\n"
	"            rowDx += val * dwx[i];\n"
	"        }\n"
	"        hBicubic += rowH * wy[j];\n"
	"        gradBicubic.x += rowDx * wy[j];\n"
	"        gradBicubic.y += rowH * dwy[j];\n"
	"    }\n"
	"    /* Smooth bilinear central-difference Laplacian across the 2x2 cell corners (eliminates Catmull-Rom ddwx/ddwy texel-grid ridges) */\n"
	"    float lap00 = (gridH[4] + gridH[6] + gridH[1] + gridH[9]  - 4.0 * gridH[5]);\n"
	"    float lap10 = (gridH[5] + gridH[7] + gridH[2] + gridH[10] - 4.0 * gridH[6]);\n"
	"    float lap01 = (gridH[8] + gridH[10] + gridH[5] + gridH[13] - 4.0 * gridH[9]);\n"
	"    float lap11 = (gridH[9] + gridH[11] + gridH[6] + gridH[14] - 4.0 * gridH[10]);\n"
	"    float lapSmooth = mix(mix(lap00, lap10, u5.x), mix(lap01, lap11, u5.x), u5.y);\n"
	"    /* Watertight chunk seam: use smooth quintic weights on the outer 1.25-texel perimeter */\n"
	"    float borderDist = min(min(pixelCoord.x, maxCoordF.x - pixelCoord.x), min(pixelCoord.y, maxCoordF.y - pixelCoord.y));\n"
	"    float borderBlend = smoothstep(0.0, 1.25, borderDist);\n"
	"    float finalH = hBicubic;\n"
	"    vec2 finalGrad = gradBicubic;\n"
	"    if (borderBlend < 1.0) {\n"
	"        float h00 = gridH[5];\n"
	"        float h10 = gridH[6];\n"
	"        float h01 = gridH[9];\n"
	"        float h11 = gridH[10];\n"
	"        vec2 du5 = 30.0 * f * f * (f * (f - 2.0) + 1.0);\n"
	"        float hQuint = mix(mix(h00, h10, u5.x), mix(h01, h11, u5.x), u5.y);\n"
	"        vec2 gradQuint = vec2(mix(h10 - h00, h11 - h01, u5.y) * du5.x, mix(h01 - h00, h11 - h10, u5.x) * du5.y);\n"
	"        finalH = mix(hQuint, hBicubic, borderBlend);\n"
	"        finalGrad = mix(gradQuint, gradBicubic, borderBlend);\n"
	"    }\n"
	"    out_grad.x = finalGrad.x * texToWorld.x;\n"
	"    out_grad.y = finalGrad.y * texToWorld.y;\n"
	"    g_bicubicCurv = clamp(lapSmooth * 0.5 * (texToWorld.x * texToWorld.x + texToWorld.y * texToWorld.y) * smoothstep(1.5, 3.0, borderDist), -0.025, 0.025);\n"
	"    return finalH;\n"
	"}\n"
	CHAFF_GLSL_HASH_FN
	CHAFF_GLSL_GRAD_NOISE_FN
	CHAFF_GLSL_BAND_FN
	CHAFF_GLSL_FLOW_FN
	ND_GLSL_SYNOPTIC_CLOUD_FN
	ND_GLSL_TORUS_ECLIPSE_FN
	ND_FIELD_GLSL_CANONICAL_FN
	"/* 3D Value Noise for dancing water caustics: dual-octave 3D interference reproduces the crossing web of sunlight */\n"
	"float chaff_sample_3d(vec3 pos, uint shift, uint seed) {\n"
	"    float cell_size = float(1u << shift);\n"
	"    vec3 p = pos / cell_size;\n"
	"    ivec3 i0 = ivec3(floor(p));\n"
	"    vec3 f = fract(p);\n"
	"    vec2 u = f.xy * f.xy * (3.0 - 2.0 * f.xy);\n"
	"    float uz = f.z * f.z * (3.0 - 2.0 * f.z);\n"
	"    int step_px = int(1u << shift);\n"
	"    ivec3 cell = i0 * step_px;\n"
	"    uint v000 = chaff_hash3(cell + ivec3(0, 0, 0), seed) >> 24u;\n"
	"    uint v100 = chaff_hash3(cell + ivec3(step_px, 0, 0), seed) >> 24u;\n"
	"    uint v010 = chaff_hash3(cell + ivec3(0, step_px, 0), seed) >> 24u;\n"
	"    uint v110 = chaff_hash3(cell + ivec3(step_px, step_px, 0), seed) >> 24u;\n"
	"    uint v001 = chaff_hash3(cell + ivec3(0, 0, step_px), seed) >> 24u;\n"
	"    uint v101 = chaff_hash3(cell + ivec3(step_px, 0, step_px), seed) >> 24u;\n"
	"    uint v011 = chaff_hash3(cell + ivec3(0, step_px, step_px), seed) >> 24u;\n"
	"    uint v111 = chaff_hash3(cell + ivec3(step_px, step_px, step_px), seed) >> 24u;\n"
	"    float b00 = mix(float(v000), float(v100), u.x);\n"
	"    float b10 = mix(float(v010), float(v110), u.x);\n"
	"    float b01 = mix(float(v001), float(v101), u.x);\n"
	"    float b11 = mix(float(v011), float(v111), u.x);\n"
	"    float b0 = mix(b00, b10, u.y);\n"
	"    float b1 = mix(b01, b11, u.y);\n"
	"    return mix(b0, b1, uz) / 255.0;\n"
	"}\n"
	"float nd_caustic_web(vec2 cUV, float tC, float closeZoom) {\n"
	"    float c1 = chaff_sample_3d(vec3(cUV * 1.85, tC * 2.4), 3u, 0x1337u);\n"
	"    float c2 = chaff_sample_3d(vec3((cUV + vec2(37.0, -23.0)) * 1.85, tC * 1.9), 3u, 0xBEEFu);\n"
	"    float caustMeso = pow(clamp(1.0 - abs(c1 - c2) * 5.6, 0.0, 1.0), 3.0);\n"
	"    float caustFine = 0.0;\n"
	"    if (closeZoom > 0.01) {\n"
	"        float c3 = chaff_sample_3d(vec3(cUV * 4.8, tC * 3.6), 2u, 0xCA57u);\n"
	"        float c4 = chaff_sample_3d(vec3((cUV + vec2(-19.0, 41.0)) * 4.8, tC * 3.0), 2u, 0xCA58u);\n"
	"        caustFine = pow(clamp(1.0 - abs(c3 - c4) * 5.6, 0.0, 1.0), 3.0) * closeZoom;\n"
	"    }\n"
	"    return caustMeso + caustFine * 0.70;\n"
	"}\n"
	"vec3 eval_shore_micro_relief(vec2 worldPos, float depth, float fw) {\n"
	"    float env = smoothstep(3.2, 0.25, abs(depth));\n"
	"    if (env <= 0.001 || fw > 16.0) return vec3(0.0);\n"
	"    float wNyq1 = nd_octave_weight(1.0 / 0.18, fw);\n"
	"    float wNyq2 = nd_octave_weight(1.0 / 0.52, fw);\n"
	"    vec3 g1 = (wNyq1 > 0.001) ? chaff_grad_noise2d(worldPos * 0.18, 0xA341316Cu) : vec3(0.5, 0.0, 0.0);\n"
	"    vec3 g2 = (wNyq2 > 0.001) ? chaff_grad_noise2d(worldPos * 0.52 + vec2(17.3, -29.1), 0xC8013EA4u) : vec3(0.5, 0.0, 0.0);\n"
	"    float disp = ((g1.x - 0.5) * 0.38 * wNyq1 + (g2.x - 0.5) * 0.16 * wNyq2) * env;\n"
	"    vec2 gTotal = (g1.yz * (0.18 * 0.38 * wNyq1) + g2.yz * (0.52 * 0.16 * wNyq2)) * env;\n"
	"    return vec3(disp, gTotal.x, gTotal.y);\n"
	"}\n"
	"vec4 eval_zoom_terrain_detail(vec2 pos, float dist, float fw, float texelSpan, out vec4 out_octaves) {\n"
	"    if (dist >= 9500.0 || fw * OCTAVE_MIN_PX >= 6.25) {\n"
	"        out_octaves = vec4(0.5);\n"
	"        return vec4(0.0);\n"
	"    }\n"
	"    const float freqs[4] = float[4](0.16, 0.68, 1.85, 6.20);\n"
	"    const float amps[4]  = float[4](0.32, 0.28, 0.24, 0.18);\n"
	"    const float dFar[4]  = float[4](9500.0, 4200.0, 1850.0, 780.0);\n"
	"    const float dNear[4] = float[4](2400.0, 1050.0,  420.0, 180.0);\n"
	"    const uint  seeds[4] = uint[4](0xA341316Cu, 0xC8013EA4u, 0x3891053Bu, 0x85EBCA6Bu);\n"
	"    const mat2  rot  = mat2(0.8, -0.6, 0.6, 0.8);\n"
	"    const mat2  irot = mat2(0.8,  0.6, -0.6, 0.8);\n"
	"    float h = 0.0;\n"
	"    vec2 grad = vec2(0.0);\n"
	"    float cavity = 0.0;\n"
	"    vec2 pRot = pos;\n"
	"    mat2 curInvRot = mat2(1.0, 0.0, 0.0, 1.0);\n"
	"    float oct[4] = float[4](0.5, 0.5, 0.5, 0.5);\n"
	"    /* All 4 octaves active, governed by continuous distance and sub-texel band-pass weighting (down to 0.16 wu grit) */\n"
	"    int maxDetailOct = (dist < 780.0) ? 4 : ((dist < 1850.0) ? 3 : 2);\n"
	"    for (int i = 0; i < 4; i++) {\n"
	"        if (i >= maxDetailOct) break;\n"
	"        float wDist = clamp((dFar[i] - dist) / max(1.0, dFar[i] - dNear[i]), 0.0, 1.0);\n"
	"        float wBand = chaff_band_weight(1.0 / freqs[i], fw, texelSpan);\n"
	"        float w = wDist * wBand;\n"
	"        if (w <= 0.001) break;\n"
	"        vec3 g = chaff_grad_noise2d(pRot * freqs[i], seeds[i]);\n"
	"        oct[i] = mix(0.5, g.x, w);\n"
	"        float signedVal = g.x - 0.5;\n"
	"        vec2 worldG = curInvRot * g.yz;\n"
	"        float erodeDamp = 1.0 / (1.0 + dot(grad, grad) * 0.85);\n"
	"        h += signedVal * amps[i] * w * erodeDamp;\n"
	"        grad += worldG * (freqs[i] * amps[i] * w * erodeDamp);\n"
	"        if (i >= 1) {\n"
	"            cavity += max(0.0, 0.48 - g.x) * (amps[i] * 2.6 * w);\n"
	"        }\n"
	"        pRot -= worldG * (0.09 * w);\n"
	"        pRot = rot * pRot;\n"
	"        curInvRot = curInvRot * irot;\n"
	"    }\n"
	"    out_octaves = vec4(oct[0], oct[1], oct[2], oct[3]);\n"
	"    return vec4(h, grad.x, grad.y, clamp(cavity, 0.0, 0.48));\n"
	"}\n"
	"/* High-Definition Macro & Orbital Procedural Noise Image (atmosphere -> low orbit -> full planet):\n"
	" * Governed by the same continuous chaff_band_weight(wavelength, fw, texelSpan) scale law across 6 octaves\n"
	" * so only sub-texel octaves above the Nyquist limit are evaluated per pixel. */\n"
	"vec4 eval_macro_planetary_detail(vec2 pos, float dist, float fw, float texelSpan, out vec4 out_macro_oct) {\n"
	"    float highAltWeight = smoothstep(450.0, 2600.0, dist);\n"
	"    if (highAltWeight <= 0.001 || texelSpan * 4.5 <= 100.0) {\n"
	"        out_macro_oct = vec4(0.5);\n"
	"        return vec4(0.0);\n"
	"    }\n"
	"    const float freqs[6] = float[6](0.000005, 0.000022, 0.00010, 0.00048, 0.00220, 0.01000);\n"
	"    const float amps[6]  = float[6](46.00, 27.00, 15.00, 8.00, 4.00, 1.80);\n"
	"    const uint  seeds[6] = uint[6](0x54454354u, 0x43524154u, 0x504C414Eu, 0x4F524231u, 0x41544D4Fu, 0x4D45534Fu);\n"
	"    const mat2  rot  = mat2(0.8, -0.6, 0.6, 0.8);\n"
	"    const mat2  irot = mat2(0.8,  0.6, -0.6, 0.8);\n"
	"    float h = 0.0;\n"
	"    vec2 grad = vec2(0.0);\n"
	"    float ridgeAcc = 0.0;\n"
	"    vec2 pRot = pos;\n"
	"    mat2 curInvRot = mat2(1.0, 0.0, 0.0, 1.0);\n"
	"    vec4 oct = vec4(0.5);\n"
	"    for (int i = 0; i < 6; i++) {\n"
	"        float wl = 1.0 / freqs[i];\n"
	"        float wNyq = nd_octave_weight(wl, fw) * highAltWeight;\n"
	"        if (wNyq <= 0.001) break;\n"
	"        float wSubTex = smoothstep(texelSpan * 3.2, texelSpan * 1.2, wl);\n"
	"        if (wSubTex <= 0.001) {\n"
	"            pRot = rot * pRot;\n"
	"            curInvRot = curInvRot * irot;\n"
	"            continue;\n"
	"        }\n"
	"        wNyq *= wSubTex;\n"
	"        vec3 g = chaff_grad_noise2d(pRot * freqs[i], seeds[i]);\n"
	"        if (i <= 2) oct.x = mix(oct.x, g.x, 0.5 * wNyq);\n"
	"        else if (i == 3) oct.y = mix(0.5, g.x, wNyq);\n"
	"        else if (i == 4) oct.z = mix(0.5, g.x, wNyq);\n"
	"        else oct.w = mix(0.5, g.x, wNyq);\n"
	"        float signedVal = g.x - 0.5;\n"
	"        float ridge = 0.5 - abs(signedVal);\n"
	"        float shaped = mix(signedVal, ridge - 0.16, 0.48);\n"
	"        vec2 worldG = curInvRot * g.yz;\n"
	"        float erodeDamp = 1.0 / (1.0 + dot(grad, grad) * 0.9);\n"
	"        h += shaped * amps[i] * wNyq * erodeDamp;\n"
	"        grad += worldG * (freqs[i] * amps[i] * wNyq * erodeDamp);\n"
	"        ridgeAcc += max(0.0, ridge - 0.16) * (0.55 * wNyq * erodeDamp);\n"
	"        pRot -= worldG * (0.14 * wNyq);\n"
	"        pRot = rot * pRot;\n"
	"        curInvRot = curInvRot * irot;\n"
	"    }\n"
	"    out_macro_oct = oct;\n"
	"    return vec4(h, grad.x, grad.y, clamp(ridgeAcc, 0.0, 0.75));\n"
	"}\n"
	"/* Multi-octave moving recursive wind field: synoptic wind -> meso gusts (cat's paws) -> micro eddies.\n"
	" * Uses spatial domain warping + constant-direction phase advection so temporal velocity is invariant over time. */\n"
	"vec4 eval_recursive_wind(vec2 worldPos, float anim_time, float fw, float dist) {\n"
	"    const vec2 dir0 = vec2(0.741, -0.671);\n"
	"    float w0_nyq = smoothstep(0.65, 0.18, 0.0055 * fw) * smoothstep(95000.0, 24000.0, dist);\n"
	"    float w1_nyq = smoothstep(0.65, 0.18, 0.0280 * fw) * smoothstep(32000.0, 8000.0, dist);\n"
	"    float w2_nyq = smoothstep(0.65, 0.18, 0.1200 * fw) * smoothstep(10000.0, 2400.0, dist);\n"
	"    if (w0_nyq <= 0.001) return vec4(dir0.x, dir0.y, 0.65, 0.0);\n"
	"    return chaff_eval_flow_2d_banded(worldPos, anim_time, vec3(w0_nyq, w1_nyq, w2_nyq));\n"
	"}\n"
	"/* Height-model basin & upwind fetch classifier: distinguishes quiet lakes/sheltered coves from deep open sea\n"
	" * using purely local continuous depth and shoreline slope (zero fixed chunk-UV probes, zero chunk seams) */\n"
	"vec2 eval_water_fetch_and_basin(vec2 uv, vec2 windDir, vec2 shoreGrad, float fragWaterDepth, float effectiveScale) {\n"
	"    float slopeMag = length(shoreGrad);\n"
	"    float windAlign = (slopeMag > 1e-4) ? dot(-shoreGrad / slopeMag, windDir) : 0.0;\n"
	"    float d_up1 = max(0.0, fragWaterDepth - max(0.0, windAlign) * slopeMag * 12.0);\n"
	"    float d_up2 = max(0.0, fragWaterDepth - max(0.0, windAlign) * slopeMag * 28.0);\n"
	"    float d_off = fragWaterDepth + slopeMag * 18.0;\n"
	"    float openSeaFactor = smoothstep(8.0, 22.0, d_off) * smoothstep(2.5, 12.0, fragWaterDepth);\n"
	"    float windFetch = clamp(0.25 + 0.40 * smoothstep(0.4, 5.0, d_up1) + 0.35 * smoothstep(1.2, 12.0, d_up2), 0.25, 1.0);\n"
	"    return vec2(openSeaFactor, windFetch);\n"
	"}\n"
	"vec4 eval_zoom_water_waves(vec2 pos, float anim_time, float dist, float fw, float openSeaFactor) {\n"
	"    if (dist >= 65000.0) return vec4(0.0);\n"
	"    vec4 wind = eval_recursive_wind(pos, anim_time, fw, dist);\n"
	"    vec2 wDir = wind.xy;\n"
	"    vec2 wPerp = vec2(-wDir.y, wDir.x);\n"
	"    float gust = wind.z;\n"
	"    const float freqs[6] = float[6](0.024, 0.072, 0.220, 0.680, 2.150, 6.800);\n"
	"    const float amps[6]  = float[6](1.450, 0.680, 0.360, 0.160, 0.055, 0.020);\n"
	"    const float spds[6]  = float[6](0.180, 0.340, 0.620, 1.050, 1.750, 2.800);\n"
	"    const float dFar[6]  = float[6](65000.0, 18000.0, 4800.0, 1600.0, 520.0, 200.0);\n"
	"    const float dNear[6] = float[6](14000.0,  3600.0, 1100.0,  360.0, 130.0,  42.0);\n"
	"    const uint  seeds[6] = uint[6](0x4F434530u, 0x5357454Cu, 0x57415645u, 0x52495050u, 0x46494E45u, 0x53505241u);\n"
	"    const vec2  dirs[6]  = vec2[6](\n"
	"        vec2( 0.8575, -0.5145),\n"
	"        vec2( 0.6402, -0.7682),\n"
	"        vec2( 0.9285, -0.3714),\n"
	"        vec2( 0.5145, -0.8575),\n"
	"        vec2( 0.7809, -0.6247),\n"
	"        vec2( 0.3714, -0.9285)\n"
	"    );\n"
	"    const mat2  rot  = mat2(0.8, -0.6, 0.6, 0.8);\n"
	"    const mat2  irot = mat2(0.8,  0.6, -0.6, 0.8);\n"
	"    float h = 0.0;\n"
	"    vec2 grad = vec2(0.0);\n"
	"    float crest = 0.0;\n"
	"    vec2 pRot = pos;\n"
	"    mat2 curInvRot = mat2(1.0, 0.0, 0.0, 1.0);\n"
	"    for (int i = 0; i < 6; i++) {\n"
	"        float wDist = clamp((dFar[i] - dist) / max(1.0, dFar[i] - dNear[i]), 0.0, 1.0);\n"
	"        float wNyq  = smoothstep(0.55, 0.16, freqs[i] * fw);\n"
	"        float gustScale = (i >= 3) ? (0.75 + 0.45 * gust) : (0.85 + 0.20 * gust);\n"
	"        float basinGate = (i < 2) ? smoothstep(0.12, 0.55, openSeaFactor) : 1.0;\n"
	"        float w = wDist * wNyq * gustScale * basinGate;\n"
	"        if (w <= 0.001) { if (i < 2) continue; break; }\n"
	"        float phase = mod(anim_time * spds[i], 1024.0);\n"
	"        vec2 windWarp = (wDir * (gust - 0.65) + wPerp * (wind.w - 0.5)) * 0.45;\n"
	"        vec2 q = pRot * freqs[i] + windWarp - dirs[i] * phase;\n"
	"        vec3 g = chaff_grad_noise2d(q, seeds[i]);\n"
	"        float signedVal = g.x - 0.5;\n"
	"        float ridge = 0.5 - abs(signedVal);\n"
	"        float shaped = mix(signedVal, ridge - 0.18, 0.35);\n"
	"        float dShape = 0.85 + 0.45 * ridge;\n"
	"        vec2 worldG = curInvRot * g.yz;\n"
	"        h += shaped * amps[i] * w;\n"
	"        grad += worldG * (dShape * freqs[i] * amps[i] * w);\n"
	"        crest += max(0.0, ridge - 0.22) * (w * ((i <= 2) ? 1.4 : 0.8));\n"
	"        pRot += g.yz * (0.11 * w);\n"
	"        pRot = rot * pRot;\n"
	"        curInvRot = curInvRot * irot;\n"
	"    }\n"
	"    return vec4(h, grad.x, grad.y, clamp(crest, 0.0, 1.0));\n"
	"}\n"	"vec2 eval_wind_capillary_facets(vec2 pos, float anim_time, vec4 wind, float dist, float fw) {\n"
	"    float distVis = clamp(1.0 - (dist - 550.0) / 2200.0, 0.0, 1.0);\n"
	"    if (distVis <= 0.001) return vec2(0.0);\n"
	"    vec2 wDir = wind.xy;\n"
	"    vec2 wPerp = vec2(-wDir.y, wDir.x);\n"
	"    const vec2 prevW = vec2(0.741, -0.671);\n"
	"    const vec2 prevP = vec2(0.671,  0.741);\n"
	"    float rippleEnv = (0.45 + 0.50 * wind.w) * distVis;\n"
	"    float f1 = 0.55;\n"
	"    float f2 = 1.25;\n"
	"    float nyq1 = smoothstep(0.55, 0.14, f1 * fw);\n"
	"    float nyq2 = smoothstep(0.55, 0.14, f2 * fw);\n"
	"    float t1 = mod(anim_time * 1.15, 512.0);\n"
	"    float t2 = mod(anim_time * 1.65, 512.0);\n"
	"    vec2 gustShift = wDir * (wind.z * 0.55) + wPerp * (wind.w * 0.35);\n"
	"    vec2 wCoord1 = vec2(dot(pos, prevW) * 1.45, dot(pos, prevP) * 0.78) * f1 + gustShift - vec2(t1, 0.18 * t1);\n"
	"    vec2 wCoord2 = vec2(dot(pos, prevW) * 1.35 + dot(pos, prevP) * 0.35, dot(pos, prevP) * 0.85 - dot(pos, prevW) * 0.25) * f2 + gustShift * 1.4 - vec2(t2, -0.22 * t2);\n"
	"    vec3 c1 = (nyq1 > 0.001) ? chaff_grad_noise2d(wCoord1, 0xCA9111A1u) : vec3(0.5, 0.0, 0.0);\n"
	"    vec3 c2 = (nyq2 > 0.001) ? chaff_grad_noise2d(wCoord2 + c1.yz * 0.25, 0xCA9111A2u) : vec3(0.5, 0.0, 0.0);\n"
	"    vec2 localG = c1.yz * (0.09 * nyq1) + c2.yz * (0.06 * nyq2);\n"
	"    vec2 worldCapGrad = (wDir * localG.x * 1.25 + wPerp * localG.y * 0.75) * rippleEnv;\n"
	"    return worldCapGrad;\n"
	"}\n"
	"/* Laplace / Equilibrium Astronomical Ocean Tide on the Torus:\n"
	" * Evaluates the second-order Legendre tidal potential P_2(cos gamma) = 0.5 * (3 * (N . D)^2 - 1)\n"
	" * from the Moon (uMoonDir) and Sun (uSunDir) with the physical 1 : 0.46 Earth-Moon-Sun amplitude ratio.\n"
	" * Gated to 0 on perched mountain lakes (uSeaLevel > uBaseSeaLevel + 0.5). */\n"
	"float eval_astronomical_tide(vec3 torusN, float openSeaFactor, float localSeaLevel) {\n"
	"    if (localSeaLevel <= -900.0) return 0.0;\n"
	"    float isOcean = smoothstep(0.20, 0.65, openSeaFactor) * (1.0 - smoothstep(0.25, 0.75, abs(localSeaLevel - uBaseSeaLevel)));\n"
	"    if (isOcean <= 0.001) return 0.0;\n"
	"    vec3 mDir = (length(uMoonDir) > 0.001) ? normalize(uMoonDir) : vec3(1.0, 0.0, 0.0);\n"
	"    vec3 sDir = (length(uSunDir)  > 0.001) ? normalize(uSunDir)  : vec3(0.0, 1.0, 0.0);\n"
	"    float cosMoon = dot(torusN, mDir);\n"
	"    float cosSun  = dot(torusN, sDir);\n"
	"    float p2Moon  = 0.5 * (3.0 * cosMoon * cosMoon - 1.0);\n"
	"    float p2Sun   = 0.5 * (3.0 * cosSun  * cosSun  - 1.0);\n"
	"    return isOcean * (0.68 * p2Moon + 0.31 * p2Sun);\n"
	"}\n"
	"/* Low-frequency shoreline wave run-up sampler (octaves 0..2 only, wavelengths 4.5..42 units):\n"
	" * Matches the visible travelling water wave pattern on both lakes and oceans while omitting\n"
	" * high-frequency octaves 3..5 (wavelengths 0.14..1.47 units) that would jitter the shoreline edge. */\n"
	"vec2 eval_shore_wave_swell(vec2 pos, float anim_time, float dist, float fw) {\n"
	"    vec4 wind = eval_recursive_wind(pos, anim_time, fw, dist);\n"
	"    vec2 wDir = wind.xy;\n"
	"    vec2 wPerp = vec2(-wDir.y, wDir.x);\n"
	"    float gust = wind.z;\n"
	"    const float freqs[3] = float[3](0.024, 0.072, 0.220);\n"
	"    const float amps[3]  = float[3](1.450, 0.680, 0.280);\n"
	"    const float spds[3]  = float[3](0.180, 0.340, 0.620);\n"
	"    const float dFar[3]  = float[3](65000.0, 18000.0, 4800.0);\n"
	"    const float dNear[3] = float[3](14000.0,  3600.0, 1100.0);\n"
	"    const uint  seeds[3] = uint[3](0x4F434530u, 0x5357454Cu, 0x57415645u);\n"
	"    const vec2  dirs[3]  = vec2[3](\n"
	"        vec2( 0.8575, -0.5145),\n"
	"        vec2( 0.6402, -0.7682),\n"
	"        vec2( 0.9285, -0.3714)\n"
	"    );\n"
	"    const mat2 rot = mat2(0.8, -0.6, 0.6, 0.8);\n"
	"    float h = 0.0;\n"
	"    float crest = 0.0;\n"
	"    vec2 pRot = pos;\n"
	"    for (int i = 0; i < 3; i++) {\n"
	"        float wDist = clamp((dFar[i] - dist) / max(1.0, dFar[i] - dNear[i]), 0.0, 1.0);\n"
	"        float wNyq  = smoothstep(0.55, 0.16, freqs[i] * fw);\n"
	"        float w = wDist * wNyq * (0.85 + 0.20 * gust);\n"
	"        if (w > 0.001) {\n"
	"            float phase = mod(anim_time * spds[i], 1024.0);\n"
	"            vec2 windWarp = (wDir * (gust - 0.65) + wPerp * (wind.w - 0.5)) * 0.45;\n"
	"            vec2 q = pRot * freqs[i] + windWarp - dirs[i] * phase;\n"
	"            vec3 g = chaff_grad_noise2d(q, seeds[i]);\n"
	"            float signedVal = g.x - 0.5;\n"
	"            float ridge = 0.5 - abs(signedVal);\n"
	"            float shaped = mix(signedVal, ridge - 0.18, 0.35);\n"
	"            h += shaped * amps[i] * w;\n"
	"            crest += max(0.0, ridge - 0.22) * (w * 1.4);\n"
	"            pRot += g.yz * (0.11 * w);\n"
	"        }\n"
	"        pRot = rot * pRot;\n"
	"    }\n"
	"    return vec2(h, clamp(crest, 0.0, 1.0));\n"
	"}\n"
	"/* Asymmetric Shoreline Wave Run-Up & Backwash (Swash) + Intertidal Wet-Sand Hysteresis:\n"
	" * Driven by the smooth travelling wave octaves (eval_shore_wave_swell) + gentle shoreward phase\n"
	" * on both lakes and oceans, with zero high-frequency shoreline static.\n"
	" * Returns vec4(swashSurfZ, highWaterZ, swashFoam, swashFilmMask). */\n"
	"vec4 eval_shore_swash(vec2 pos, float baseTideZ, float terrainZ, vec2 slopeGrad, float anim_time, float dist, float fw, float openSeaFactor, float localSeaLevel) {\n"
	"    float isOcean = smoothstep(0.20, 0.65, openSeaFactor) * (1.0 - smoothstep(0.25, 0.75, abs(localSeaLevel - uBaseSeaLevel)));\n"
	"    float slopeMag = length(slopeGrad);\n"
	"    float relZ = terrainZ - baseTideZ;\n"
	"    float nearShore = smoothstep(2.4, 0.0, max(0.0, relZ)) * smoothstep(4.5, 0.0, max(0.0, -relZ));\n"
	"    if (nearShore <= 0.001 || dist > 3800.0) {\n"
	"        float tideHigh = localSeaLevel + (0.15 + 1.05 * isOcean);\n"
	"        return vec4(baseTideZ, max(baseTideZ, tideHigh), 0.0, 0.0);\n"
	"    }\n"
	"    float distFade = smoothstep(3800.0, 450.0, dist);\n"
	"    float slopeResponse = (0.52 + 0.48 * smoothstep(0.005, 0.12, slopeMag)) * distFade;\n"
	"    float oceanSwell = 0.0;\n"
	"    float lakeLapping = 0.0;\n"
	"    if (isOcean > 0.01) {\n"
	"        vec3 nAlong = chaff_grad_noise2d(pos * 0.014 + vec2(13.7, -9.2), 0x53574153u);\n"
	"        float groupEnv = 0.76 + 0.24 * sin(mod(anim_time * 0.24, 628.3185) + (nAlong.x - 0.5) * 2.2);\n"
	"        float waveArg = mod(anim_time * 0.92, 628.3185) - relZ * 0.32 + (nAlong.x - 0.5) * 1.8;\n"
	"        float swellWave = sin(waveArg) + 0.22 * sin(2.0 * waveArg - 0.35);\n"
	"        vec2 shoreWaves = eval_shore_wave_swell(pos, anim_time, dist, fw);\n"
	"        float waveletRunup = shoreWaves.x * 0.86 * slopeResponse;\n"
	"        float swellRunup = swellWave * 0.58 * groupEnv * slopeResponse;\n"
	"        oceanSwell = (waveletRunup + swellRunup) * isOcean;\n"
	"    }\n"
	"    float lapPhase = sin(mod(anim_time * 2.1, 628.3185) + pos.x * 0.45 - pos.y * 0.38);\n"
	"    lakeLapping = lapPhase * 0.055 * slopeResponse * (1.0 - isOcean);\n"
	"    float swashDisp = (oceanSwell + lakeLapping) * nearShore;\n"
	"    float swashSurfZ = baseTideZ + swashDisp;\n"
	"    float recentSwashTop = baseTideZ + (0.12 + 1.04 * isOcean) * slopeResponse;\n"
	"    float highWaterZ = mix(max(baseTideZ, localSeaLevel + 0.15 + 1.05 * isOcean), max(swashSurfZ, recentSwashTop), nearShore);\n"
	"    float edgeLip = smoothstep(0.35, 0.0, abs(terrainZ - swashSurfZ));\n"
	"    float bodyFoamScale = 0.15 + 0.85 * isOcean;\n"
	"    float swashFoam = clamp(edgeLip * 0.16 * bodyFoamScale * distFade, 0.0, 0.24);\n"
	"    float swashFilm = smoothstep(highWaterZ + 0.12, swashSurfZ - 0.04, terrainZ) * step(swashSurfZ, terrainZ) * distFade;\n"
	"    return vec4(swashSurfZ, highWaterZ, swashFoam, swashFilm);\n"
	"}\n"	"vec4 eval_lamb_wake_ribbon(vec2 pos, float anim_time, float dist, float fw) {\n"
	"    float headEnergy = uLambWakeHead.w;\n"
	"    if (headEnergy <= 0.001 || dist > 2600.0) return vec4(0.0);\n"
	"    vec2 dHead = pos - uLambWakeHead.xy;\n"
	"    if (dot(dHead, dHead) > 140.0 * 140.0) return vec4(0.0);\n"
	"    float distFade = smoothstep(2600.0, 450.0, dist) * nd_octave_weight(3.5, fw) * headEnergy;\n"
	"    if (distFade <= 0.001) return vec4(0.0);\n"
	"    vec4 headVel0 = uLambWakeVel0[0];\n"
	"    float headSpd = length(headVel0.xy);\n"
	"    vec2 headFwd = (headSpd > 0.5) ? (headVel0.xy / headSpd) : vec2(0.0, 1.0);\n"
	"    float totalH = 0.0;\n"
	"    vec2 totalGrad = vec2(0.0);\n"
	"    float totalFoam = 0.0;\n"
	"    vec3 wakeTurb = chaff_grad_noise2d(pos * 0.38 + vec2(mod(anim_time * 0.75, 256.0), -mod(anim_time * 0.55, 256.0)), 0x57414B45u);\n"
	"    vec2 warpOffset = wakeTurb.yz * 0.85;\n"
	"    float pathArc = 0.0;\n"
	"    for (int i = 0; i < 7; i++) {\n"
	"        vec4 nodeA = (i < 4) ? uLambWakeXYT0[i] : uLambWakeXYT1[i - 4];\n"
	"        vec4 velA  = (i < 4) ? uLambWakeVel0[i] : uLambWakeVel1[i - 4];\n"
	"        if (velA.z <= 0.002) continue;\n"
	"        int j = i + 1;\n"
	"        vec4 nodeB = (j < 4) ? uLambWakeXYT0[j] : uLambWakeXYT1[j - 4];\n"
	"        vec4 velB  = (j < 4) ? uLambWakeVel0[j] : uLambWakeVel1[j - 4];\n"
	"        float spdA = length(velA.xy);\n"
	"        vec2 fwdA = (spdA > 0.5) ? (velA.xy / spdA) : headFwd;\n"
	"        vec2 pA = nodeA.xy;\n"
	"        vec2 pB = (velB.z > 0.002) ? nodeB.xy : (pA - fwdA * 3.5);\n"
	"        float tB = (velB.z > 0.002) ? nodeB.z : (nodeA.z - 0.18);\n"
	"        vec2 seg = pA - pB;\n"
	"        float segLen = length(seg);\n"
	"        vec2 tDir = (segLen > 0.15) ? (seg / segLen) : fwdA;\n"
	"        vec2 nDir = vec2(-tDir.y, tDir.x);\n"
	"        float span = max(segLen, 0.25);\n"
	"        vec2 relB = pos - pB;\n"
	"        float proj = dot(relB, tDir);\n"
	"        float tSeg = clamp(proj / span, 0.0, 1.0);\n"
	"        vec2 closest = pB + tDir * (tSeg * segLen);\n"
	"        float segTime = mix(tB, nodeA.z, tSeg);\n"
	"        float age = clamp(anim_time - segTime, 0.0, 4.2);\n"
	"        if (age >= 4.15) {\n"
	"            pathArc += segLen;\n"
	"            continue;\n"
	"        }\n"
	"        float segEnergy = mix((velB.z > 0.002) ? velB.z : velA.z * 0.5, velA.z, tSeg);\n"
	"        float ageDecay = exp(-0.80 * age) * smoothstep(4.0, 0.15, age) * segEnergy;\n"
	"        /* Smooth natural longitudinal window around segment [pB, pA] with rounded capsule ends (no planar clip) */\n"
	"        float sOverA = max(0.0, proj - segLen);\n"
	"        float sOverB = max(0.0, -proj);\n"
	"        float headCapSigma = (i == 0) ? 1.85 : 2.60;\n"
	"        float tailCapSigma = 3.20 + age * 2.80;\n"
	"        float longWin = exp(-(sOverA * sOverA) / (headCapSigma * headCapSigma))\n"
	"                      * exp(-(sOverB * sOverB) / (tailCapSigma * tailCapSigma));\n"
	"        /* Neighboring segments share interior nodes: weight smoothly so interior joints don't double-count */\n"
	"        float jointWeight = (i == 0) ? smoothstep(-tailCapSigma, 0.0, proj)\n"
	"                                     : (smoothstep(-tailCapSigma, 0.0, proj) * smoothstep(segLen + headCapSigma, segLen, proj + 0.5 * headCapSigma));\n"
	"        float envBase = ageDecay * longWin * (0.45 + 0.55 * jointWeight);\n"
	"        float curArc = pathArc + (1.0 - tSeg) * segLen + sOverA;\n"
	"        pathArc += segLen;\n"
	"        if (envBase <= 0.002) continue;\n"
	"        vec2 delta = (pos + warpOffset * (0.16 + 0.14 * age)) - closest;\n"
	"        float rawU = dot(delta, nDir);\n"
	"        float u = abs(rawU);\n"
	"        float signU = (rawU >= 0.0) ? 1.0 : -1.0;\n"
	"        float wakeHalfW = 1.35 + curArc * 0.16 + age * 2.10;\n"
	"        float edgeSigma = 1.15 + age * 0.85;\n"
	"        float dEdge = (u - wakeHalfW) / edgeSigma;\n"
	"        /* Smooth Kelvin V-shear rim along the wake arms */\n"
	"        float rimEnv = exp(-(dEdge * dEdge)) * exp(-curArc * 0.028) * envBase;\n"
	"        float spdScale = clamp(spdA / 36.0, 0.30, 1.15);\n"
	"        if (rimEnv > 0.002) {\n"
	"            float hRim = 0.20 * rimEnv * spdScale;\n"
	"            totalH += hRim;\n"
	"            float dRimDu = (-2.2 * dEdge / edgeSigma) * hRim;\n"
	"            totalGrad += (nDir * signU - tDir * 0.20) * dRimDu;\n"
	"            totalFoam += rimEnv * spdScale * exp(-age * 1.15) * (0.14 + 0.20 * wakeTurb.x);\n"
	"        }\n"
	"        /* Turbulent wake trough & subtle aerated trail inside the V-cone (softened near body so it doesn't white-out the lamb) */\n"
	"        float coreEnv = smoothstep(wakeHalfW + edgeSigma * 0.80, wakeHalfW * 0.15, u) * exp(-curArc * 0.035) * envBase;\n"
	"        if (coreEnv > 0.003) {\n"
	"            float eddy = coreEnv * spdScale;\n"
	"            float bodyFoamRamp = smoothstep(0.5, 4.5, curArc);\n"
	"            totalH -= 0.11 * eddy;\n"
	"            totalGrad += wakeTurb.yz * (0.42 * eddy) + nDir * (signU * 0.20 * eddy);\n"
	"            float bubbleMask = smoothstep(0.36, 0.74, wakeTurb.x) * exp(-age * 1.25);\n"
	"            totalFoam += eddy * (0.10 + bubbleMask * 0.34) * (0.35 + 0.65 * bodyFoamRamp);\n"
	"        }\n"
	"    }\n"
	"    return vec4(clamp(totalH, -0.42, 0.42) * distFade, totalGrad.x * distFade, totalGrad.y * distFade, clamp(totalFoam * distFade, 0.0, 0.55));\n"
	"}\n"
	"vec4 sampleSubstrateSmooth(vec2 uv) {\n"
	"    vec2 texSize = vec2(textureSize(uSubstrateTex, 0));\n"
	"    vec2 maxCoordF = max(vec2(1.0), texSize - 1.0);\n"
	"    vec2 p = clamp(uv, 0.0, 1.0) * maxCoordF;\n"
	"    vec2 ip = floor(p);\n"
	"    vec2 fp = p - ip;\n"
	"    vec2 u = fp * fp * fp * (fp * (fp * 6.0 - 15.0) + 10.0);\n"
	"    return texture(uSubstrateTex, (ip + u + 0.5) / texSize);\n"
	"}\n"
	"/* Reconstructs the true horizontal river water table W_river from active river texels in the 3x3 neighborhood,\n"
	" * uncontaminated by dry bank texels (where alpha == 1.0 and riverCarve == 0.0), eliminating riverbank water-walls. */\n"
	"float eval_river_surface(vec2 uv, float baseSeaLevel, float effectiveScale, out float outRiverProx, out float outRiverCarve) {\n"
	"    outRiverCarve = (1.0 - sampleSubstrateSmooth(uv).a) * 15.9375;\n"
	"    if (vDistance > 10000.0 || uChunkSpan.x > 40960.0) {\n"
	"        outRiverProx = smoothstep(0.02, 1.25, outRiverCarve);\n"
	"        return baseSeaLevel;\n"
	"    }\n"
	"    vec2 texSize = vec2(textureSize(uSubstrateTex, 0));\n"
	"    vec2 maxCoordF = max(vec2(1.0), texSize - 1.0);\n"
	"    vec2 uvc = clamp(uv, 0.0, 1.0);\n"
	"    vec2 p = uvc * maxCoordF;\n"
	"    ivec2 ip = ivec2(floor(p));\n"
	"    vec2 fp = p - vec2(ip);\n"
	"    ivec2 maxC = ivec2(maxCoordF);\n"
	"    if (outRiverCarve < 0.01) {\n"
	"        float a00 = texelFetch(uSubstrateTex, clamp(ip + ivec2(-1, -1), ivec2(0), maxC), 0).a;\n"
	"        float a10 = texelFetch(uSubstrateTex, clamp(ip + ivec2( 2, -1), ivec2(0), maxC), 0).a;\n"
	"        float a01 = texelFetch(uSubstrateTex, clamp(ip + ivec2(-1,  2), ivec2(0), maxC), 0).a;\n"
	"        float a11 = texelFetch(uSubstrateTex, clamp(ip + ivec2( 2,  2), ivec2(0), maxC), 0).a;\n"
	"        if (min(min(a00, a10), min(a01, a11)) > 0.995) {\n"
	"            outRiverProx = 0.0;\n"
	"            return baseSeaLevel;\n"
	"        }\n"
	"    }\n"
	"    float sumW = 0.0;\n"
	"    float sumZ = 0.0;\n"
	"    float maxC_nb = 0.0;\n"
	"    for (int j = -1; j <= 2; j++) {\n"
	"        float wy = max(0.0, 1.5 - abs(float(j) - fp.y));\n"
	"        for (int i = -1; i <= 2; i++) {\n"
	"            ivec2 tc = clamp(ip + ivec2(i, j), ivec2(0), maxC);\n"
	"            float a_i = texelFetch(uSubstrateTex, tc, 0).a;\n"
	"            float c_i = (1.0 - a_i) * 15.9375;\n"
	"            if (c_i > 0.05) {\n"
	"                float wx = max(0.0, 1.5 - abs(float(i) - fp.x));\n"
	"                float w = wx * wy * c_i;\n"
	"                float h_i = float(texelFetch(uHeightTex, tc, 0).r) * effectiveScale;\n"
	"                float w_surf_i = max(baseSeaLevel, h_i + c_i - 3.0);\n"
	"                sumW += w;\n"
	"                sumZ += w * w_surf_i;\n"
	"                maxC_nb = max(maxC_nb, c_i * clamp(wx * wy * 0.75, 0.0, 1.0));\n"
	"            }\n"
	"        }\n"
	"    }\n"
	"    outRiverProx = smoothstep(0.02, 1.25, max(outRiverCarve, maxC_nb));\n"
	"    if (sumW > 1e-4) {\n"
	"        return mix(baseSeaLevel, sumZ / sumW, smoothstep(0.0, 0.35, outRiverProx));\n"
	"    }\n"
	"    return baseSeaLevel;\n"
	"}\n"
	ND_GLSL_SCENE_LIGHT
	ND_GLSL_CLOUD_SHADOW_FN
	ND_GLSL_CSM_LOOKUP_FN
	ND_GLSL_CLOUD_SHADOW_ENTRY_FN
	"void main() {\n"
	"    float scale = (uHeightScale != 0.0) ? uHeightScale : 1.0;\n"
	"    float effectiveScale = scale * 0.0625;\n"
	"    vec2 fragGrad;\n"
	"    float raw_h = sampleHeightSmooth(vUV, fragGrad);\n"
	"    float fragZ = raw_h * effectiveScale;\n"
	"    vec2 worldSlopeGrad = fragGrad * effectiveScale;\n"
	"    float fw = clamp(max(length(dFdx(vWorldPos)), length(dFdy(vWorldPos))), 1e-4, max(uChunkSpan.x, uChunkSpan.y) * 0.25);\n"
	"    /* Option A: Per-pixel canonical GPU terrain field evaluation with continuous cross-LOD fading.\n"
	"     * Blending by both continuous vDistance and fw/texelSpan ensures adjacent chunks of different\n"
	"     * quadtree levels fade seamlessly with zero LOD boundary seam or pop. */\n"
	"    float texelSpan = uChunkSpan.x / 48.0;\n"
	"    float spanBlend = smoothstep(12.0, 28.0, texelSpan);\n"
	"    float distBlend = smoothstep(3200.0, 8400.0, vDistance);\n"
	"    float gpuFieldBlend = clamp(max(spanBlend, distBlend), 0.0, 1.0);\n"
	"    float texZ = fragZ;\n"
	"    float riverProx = 0.0;\n"
	"    float riverCarve = 0.0;\n"
	"    float effSeaLevel = uSeaLevel;\n"
	"    if (uSeaLevel > -900.0) {\n"
	"        effSeaLevel = eval_river_surface(vUV, uSeaLevel, effectiveScale, riverProx, riverCarve);\n"
	"        gpuFieldBlend *= (1.0 - riverProx);\n"
	"    }\n"
	"    float gpuFineElev = 0.0;\n"
	"    if (gpuFieldBlend > 0.001) {\n"
	"        vec3 gpuField = nd_gpu_eval_canonical_field(vWorldPos, fw, texelSpan, texZ, worldSlopeGrad, gpuFineElev);\n"
	"        /* Preserve carved river valleys, highland lake basins, and biome-damped plains where CPU texZ differs from uncarved gpuField.x */\n"
	"        if (abs(gpuField.x - texZ) > 2.5) {\n"
	"            float diffPreserve = smoothstep(2.5, 8.0, abs(gpuField.x - texZ));\n"
	"            gpuField.x = mix(gpuField.x, texZ, diffPreserve * 0.90);\n"
	"            gpuField.yz = mix(gpuField.yz, worldSlopeGrad, diffPreserve * 0.90);\n"
	"        }\n"
	"        fragZ = mix(fragZ, gpuField.x, gpuFieldBlend);\n"
	"        /* Never let Nyquist octave band-limiting in gpuField submerge dry land or flatten mountain peaks */\n"
	"        if (texZ > effSeaLevel) fragZ = max(fragZ, texZ);\n"
	"        worldSlopeGrad = mix(worldSlopeGrad, gpuField.yz, gpuFieldBlend);\n"
	"    }\n"
	"    vec3 torusN = normalize(vTorusNormal);\n"
	"    vec3 tangentU = normalize(vTangentU);\n"
	"    vec3 tangentV = normalize(vTangentV);\n"
	"    vec3 fragNormal = normalize(torusN - worldSlopeGrad.x * tangentU - worldSlopeGrad.y * tangentV);\n"
	"    vec4 macroOct = vec4(0.5);\n"
	"    vec4 macroDetail = eval_macro_planetary_detail(vWorldPos, vDistance, fw, texelSpan, macroOct);\n"
	"    vec4 wind = eval_recursive_wind(vWorldPos, uAnimTime, fw, vDistance);\n"
	"    float preDepth = (effSeaLevel > -900.0) ? (effSeaLevel - fragZ) : -999.0;\n"
	"    vec2 basin = (effSeaLevel > -900.0) ? eval_water_fetch_and_basin(vUV, wind.xy, worldSlopeGrad, max(0.0, preDepth), effectiveScale) : vec2(0.0);\n"
	"    float openSeaFactor = basin.x * (1.0 - riverProx);\n"
	"    float windFetch = basin.y;\n"
	"    /* Astronomical ocean tide (open ocean only; zero on lakes and rivers) + shoreline wave swash excursion */\n"
	"    float astroTide = eval_astronomical_tide(torusN, openSeaFactor, effSeaLevel);\n"
	"    float baseTideSeaLevel = (effSeaLevel > -900.0) ? (effSeaLevel + astroTide) : -999.0;\n"
	"    /* Refine coarse macro heightmap & normals with high-definition procedural noise image at altitude */\n"
	"    float rawMacroDepth = (effSeaLevel > -900.0) ? (baseTideSeaLevel - fragZ) : -999.0;\n"
	"    float coastDetailEnv = (effSeaLevel > -900.0) ? (0.35 + 0.65 * smoothstep(0.0, 12.0, abs(rawMacroDepth))) : 1.0;\n"
	"    coastDetailEnv *= (1.0 - 0.70 * riverProx);\n"
	"    fragZ += macroDetail.x * coastDetailEnv * (1.0 - 0.65 * gpuFieldBlend);\n"
	"    fragNormal = normalize(fragNormal - macroDetail.y * 0.65 * tangentU - macroDetail.z * 0.65 * tangentV);\n"
	"    float rawDepth = (effSeaLevel > -900.0) ? (baseTideSeaLevel - fragZ) : -999.0;\n"
	"    vec3 shoreRelief = (effSeaLevel > -900.0) ? eval_shore_micro_relief(vWorldPos, rawDepth, fw) * (1.0 - 0.65 * riverProx) : vec3(0.0);\n"
	"    fragZ += shoreRelief.x;\n"
	"    /* Prevent sub-texel micro-relief from artificially submerging dry riverbanks where texZ >= effSeaLevel */\n"
	"    if (riverProx > 0.01 && texZ >= effSeaLevel + 0.05) {\n"
	"        fragZ = max(fragZ, baseTideSeaLevel + 0.08);\n"
	"    }\n"
	"    worldSlopeGrad += shoreRelief.yz;\n"
	"    fragNormal = normalize(fragNormal - shoreRelief.y * tangentU - shoreRelief.z * tangentV);\n"
	"    vec4 swashState = (effSeaLevel > -900.0) ? eval_shore_swash(vWorldPos, baseTideSeaLevel, fragZ, worldSlopeGrad, uAnimTime, vDistance, fw, openSeaFactor, effSeaLevel) : vec4(-999.0, -999.0, 0.0, 0.0);\n"
	"    vec4 lambWake = (effSeaLevel > -900.0) ? eval_lamb_wake_ribbon(vWorldPos, uAnimTime, vDistance, fw) : vec4(0.0);\n"
	"    float liveSeaLevel = swashState.x + lambWake.x;\n"
	"    float highWaterZ   = swashState.y;\n"
	"    float fragWaterDepth = (effSeaLevel > -900.0) ? (liveSeaLevel - fragZ) : -999.0;\n"
	"    vec4 octaves = vec4(0.5);\n"
	"    vec4 detail = (fragWaterDepth <= 18.0 && vDistance < 9500.0) ? eval_zoom_terrain_detail(vWorldPos, vDistance, fw, texelSpan, octaves) : vec4(0.0);\n"
	"    vec2 edgeMask = 4.0 * vUV * (1.0 - vUV);\n"
	"    vec2 worldToUV = 1.0 / max(uChunkSpan, vec2(160.0));\n"
	"    vec2 macroUVWarp = vec2(macroOct.z - 0.5, macroOct.w - 0.5) * (2.8 * worldToUV * edgeMask * smoothstep(2000.0, 8000.0, vDistance) * smoothstep(90000.0, 28000.0, vDistance));\n"
	"    vec2 sampleUV = clamp(vUV + detail.yz * (1.6 * worldToUV * edgeMask) + macroUVWarp, 0.0, 1.0);\n"
	"    vec2 waveRefGrad = vec2(0.0);\n"
	"    if (fragWaterDepth > 0.0) {\n"
	"        float refFade = smoothstep(3200.0, 600.0, vDistance) * smoothstep(0.65, 0.18, 0.22 * fw);\n"
	"        if (refFade > 0.001) {\n"
	"            float depthFactor = smoothstep(0.0, 0.8, fragWaterDepth) * (0.45 + 0.55 * clamp(fragWaterDepth / 6.0, 0.0, 1.0)) * refFade;\n"
	"            float tRef = mod(uAnimTime, 256.0);\n"
	"            vec3 wRef1 = chaff_grad_noise2d(vWorldPos * 0.22 + vec2(tRef * 0.55, -tRef * 0.45), 0x57415631u);\n"
	"            vec3 wRef2 = chaff_grad_noise2d(vWorldPos * 0.52 + vec2(-tRef * 0.80, tRef * 0.65), 0x57415632u);\n"
	"            waveRefGrad = (wRef1.yz * 0.65 + wRef2.yz * 0.35 + lambWake.yz * 1.65) * refFade;\n"
	"            vec2 refractOffset = waveRefGrad * (4.2 * worldToUV * depthFactor * edgeMask);\n"
	"            sampleUV = clamp(vUV + refractOffset, 0.0, 1.0);\n"
	"        }\n"
	"    }\n"
	"    vec4 baseCol = sampleSubstrateSmooth(sampleUV);\n"
	"    vec3 rawBiome = baseCol.rgb; /* sampleUV differs from vUV by < 1 texel: reuse the sample */\n"
	"    /* Continuous 12-regime biome weights derived from chromaticity-invariant uSubstrateTex (populated via ndw_biome_base_rgb) */\n"
	"    float maxBio = max(rawBiome.r, max(rawBiome.g, rawBiome.b));\n"
	"    float minBio = min(rawBiome.r, min(rawBiome.g, rawBiome.b));\n"
	"    float wVolcanic  = smoothstep(0.32, 0.18, maxBio);\n"
	"    float wPolar     = smoothstep(0.65, 0.85, minBio) * smoothstep(0.14, 0.04, max(rawBiome.r, rawBiome.g) - rawBiome.b);\n"
	"    float wDesert    = smoothstep(0.62, 0.72, rawBiome.r) * smoothstep(0.58, 0.68, rawBiome.g) * smoothstep(0.42, 0.50, rawBiome.b) * (1.0 - wPolar);\n"
	"    float wMediter   = smoothstep(0.60, 0.68, rawBiome.r) * smoothstep(0.45, 0.52, rawBiome.g) * smoothstep(0.58, 0.53, rawBiome.g) * smoothstep(0.28, 0.33, rawBiome.b) * smoothstep(0.42, 0.35, rawBiome.b) * smoothstep(0.10, 0.16, rawBiome.r - rawBiome.g);\n"
	"    float wSavannah  = smoothstep(0.52, 0.68, rawBiome.r) * smoothstep(0.48, 0.58, rawBiome.g) * smoothstep(0.38, 0.30, rawBiome.b) * smoothstep(0.03, 0.12, rawBiome.r - rawBiome.g) * (1.0 - wPolar) * (1.0 - wDesert) * (1.0 - wMediter * 0.7);\n"
	"    float wWetland   = smoothstep(0.28, 0.23, rawBiome.r) * smoothstep(0.38, 0.42, rawBiome.g) * smoothstep(0.33, 0.36, rawBiome.b) * smoothstep(0.10, 0.13, rawBiome.b - rawBiome.r) * (1.0 - wPolar) * (1.0 - wVolcanic);\n"
	"    float wBoreal    = smoothstep(0.30, 0.25, rawBiome.r) * smoothstep(0.32, 0.37, rawBiome.g) * smoothstep(0.26, 0.30, rawBiome.b) * smoothstep(0.03, 0.06, rawBiome.b - rawBiome.r) * (1.0 - wWetland * 0.85) * (1.0 - wPolar) * (1.0 - wVolcanic);\n"
	"    float wTempSeas  = smoothstep(0.40, 0.45, rawBiome.r) * smoothstep(0.52, 0.47, rawBiome.r) * smoothstep(0.48, 0.52, rawBiome.g) * smoothstep(0.04, 0.07, rawBiome.g - rawBiome.r) * smoothstep(0.32, 0.28, rawBiome.b) * (1.0 - wMediter);\n"
	"    float wTropSeas  = smoothstep(0.50, 0.55, rawBiome.r) * smoothstep(0.50, 0.54, rawBiome.g) * smoothstep(0.06, 0.02, abs(rawBiome.r - rawBiome.g)) * smoothstep(0.34, 0.28, rawBiome.b) * (1.0 - wMediter) * (1.0 - wSavannah);\n"
	"    float wTropical  = smoothstep(0.26, 0.21, rawBiome.r) * smoothstep(0.38, 0.44, rawBiome.g) * smoothstep(0.16, 0.23, rawBiome.g - rawBiome.r) * smoothstep(0.30, 0.26, rawBiome.b) * (1.0 - wWetland) * (1.0 - wBoreal * 0.6);\n"
	"    float wGrassland = smoothstep(0.30, 0.38, rawBiome.r) * smoothstep(0.48, 0.55, rawBiome.g) * smoothstep(0.10, 0.16, rawBiome.g - rawBiome.r) * smoothstep(0.36, 0.30, rawBiome.b) * (1.0 - wPolar) * (1.0 - wDesert);\n"
	"    float wTemperate = clamp(1.0 - (wVolcanic + wPolar + wDesert + wSavannah * 0.5 + wBoreal * 0.5 + wTropical), 0.0, 1.0);\n"
	"    float texBioHumidity = clamp(0.55 * wTemperate + 0.84 * wTropical + 0.88 * wWetland + 0.58 * wBoreal + 0.48 * wGrassland + 0.28 * wSavannah + 0.34 * wMediter + 0.45 * wPolar + 0.15 * wVolcanic + 0.10 * wDesert + 0.25 * smoothstep(-4.0, 8.0, fragWaterDepth), 0.08, 0.95);\n"
	"    /* Toroidal atmospheric circulation profile (matching <nd/field.h>):\n"
	"     * latPhi = 2*pi*(64 - wy)/8388608: 0 at outer equator (y=0), +-pi/2 at polar crowns, +-pi at inner equator (y=+-32768).\n"
	"     * Solar evaporation dries the scorched equators (latDeg < 24 deg), while moisture condenses in mid/subpolar latitudes. */\n"
	"    float latPhi = 6.28318530718 * ((64.0 - vWorldPos.y) / 8388608.0);\n"
	"    float latDeg = asin(clamp(abs(sin(latPhi)), 0.0, 1.0)) * 57.2957795;\n"
	"    float innerHole = max(0.0, -cos(latPhi));\n"
	"    float eqEvapDry = smoothstep(24.0, 0.0, latDeg) * 0.18;\n"
	"    float midLatWet = smoothstep(18.0, 0.0, abs(latDeg - 38.0)) * 0.12;\n"
	"    float subpolarWet = smoothstep(10.0, 0.0, abs(latDeg - 58.0)) * 0.08;\n"
	"    float worldBioHumidity = clamp(texBioHumidity - eqEvapDry + midLatWet + subpolarWet, 0.06, 0.92);\n"
	"    /* Orographic rain shadow & windward lift: ascending maritime wind adds moisture,\n"
	"     * while descending air on the leeward mountain flanks warms and dries adiabatically */\n"
	"    float orographicLift = dot(wind.xy, -worldSlopeGrad);\n"
	"    float oroMoistureBias = clamp(orographicLift * 1.8, -0.22, 0.18);\n"
	"    float bioHumidity = clamp(worldBioHumidity + oroMoistureBias, 0.04, 0.96);\n"
	"    float cloudFw = mix(fw, max(0.18, vDistance * 0.00055), gpuFieldBlend);\n"
	"    float rawCloudShadow = nd_cloud_shadow(vWorldPos, fragZ, uAnimTime, bioHumidity, vDistance, cloudFw);\n"
	"    float cloudShadow = rawCloudShadow;\n"
	"    float sunTrans = nd_sun_trans(cloudShadow);\n"
	"    vec3 sDir = (length(uSunDir) > 0.001) ? normalize(uSunDir) : vec3(1.0, 0.0, 0.0);\n"
	"    float orbitLight = smoothstep(24000.0, 220000.0, vDistance);\n"
	"    vec3 L_local = normalize(-0.577 * tangentU - 0.577 * tangentV + 0.816 * torusN);\n"
	"    vec3 sunLocal = vec3(dot(sDir, tangentU), dot(sDir, tangentV), dot(sDir, torusN));\n"
	"    vec3 L_day = (sunLocal.z > -0.08) ? normalize(vec3(sunLocal.xy, max(0.12, sunLocal.z))) : L_local;\n"
	"    vec3 L_frag = normalize(mix(L_day, normalize(sDir + 0.35 * torusN), orbitLight));\n"
	"    float sunDot = dot(torusN, sDir);\n"
	"    vec3 P_torus = (uCameraPos - vViewDir) - uPlanetCenter;\n"
	"    float R_torus = (uTorusRadii.x > 0.0) ? uTorusRadii.x : 2000000.0;\n"
	"    float r0_torus = (uTorusRadii.y > 0.0) ? uTorusRadii.y : 1335000.0;\n"
	"    float innerEclipse = (sunDot > 0.0 && innerHole > 0.001) ? eval_torus_self_eclipse(P_torus, sDir, R_torus, r0_torus) : 1.0;\n"
	"    float effSun = sunDot * innerEclipse;\n"
	"    float dayFactor = clamp(0.12 + 0.88 * smoothstep(-0.16, 0.22, effSun), 0.12, 1.0);\n"
	"    float directSun = smoothstep(-0.06, 0.18, effSun);\n"
	"    float twilight = smoothstep(0.32, -0.06, effSun) * smoothstep(-0.16, 0.12, effSun);\n"
	"    vec3 sunColor = mix(vec3(1.0, 0.98, 0.92), mix(vec3(1.0, 0.62, 0.28), vec3(0.96, 0.38, 0.18), smoothstep(0.18, -0.06, effSun)), twilight);\n"
	"    vec3 sunLitCol = mix(vec3(1.0), sunColor, twilight * 0.80);\n"
	"    /* Torus-Shine: indirect illumination from the sunlit opposite ring arch onto the night hemisphere */\n"
	"    float torusShineVisibility = clamp(innerHole * 1.4 + 0.18 * max(0.0, dot(torusN, vec3(0.0, 0.0, 1.0))), 0.0, 1.0);\n"
	"    float oppSunlit = clamp(0.30 + 0.70 * smoothstep(-0.25, 0.45, -sDir.z + 0.5 * innerHole), 0.20, 1.0);\n"
	"    float nightMask = smoothstep(0.06, -0.16, effSun);\n"
	"    float torusShine = nightMask * torusShineVisibility * oppSunlit * 0.16;\n"
	"    vec3 torusShineCol = vec3(0.56, 0.68, 0.86);\n"
	"    float baseDiff = max(0.0, dot(fragNormal, L_frag)) * sunTrans * directSun;\n"
	"    float effLighting = (0.45 * nd_amb_trans(cloudShadow) * (0.20 + 0.80 * dayFactor)) + 0.55 * baseDiff + torusShine;\n"
	"    vec3 dryCol = baseCol.rgb;\n"
	"    float dryLighting = effLighting;\n"
	"    float slope = 1.0 - max(0.0, dot(fragNormal, torusN));\n"
	"    float elevAboveWater = (effSeaLevel > -900.0) ? max(0.0, fragZ - liveSeaLevel) : max(0.0, fragZ);\n"
	"    float elevAboveHighTide = (effSeaLevel > -900.0) ? max(0.0, fragZ - highWaterZ) : max(0.0, fragZ);\n"
	"    float h0 = detail.x;\n"
	"    vec2 microGrad = detail.yz;\n"
	"    float microCavity = detail.w;\n"
	"    float nMeso  = octaves.x;\n"
	"    float nStd   = octaves.y;\n"
	"    float nClose = octaves.z;\n"
	"    float nUltra = octaves.w;\n"
	"    /* Analytical Laplacian curvature (kappa = d2x + d2y) from Catmull-Rom bicubic height + micro-detail:\n"
	"     * convexity > 0 on ridges/spurs (soil erodes away, exposing bedrock/crust),\n"
	"     * concavity > 0 in gullies/hollows/cliff toes (scree, loam, moss, peat, and snowdrifts accumulate). */\n"
	"    float rawCurv = g_bicubicCurv * effectiveScale * (1.0 - 0.75 * gpuFieldBlend);\n"
	"    float convexity = clamp(-rawCurv * 28.0 + max(0.0, h0) * 0.65, 0.0, 1.0);\n"
	"    float concavity = clamp(rawCurv * 28.0 + microCavity * 1.15 - min(0.0, h0) * 0.45, 0.0, 1.0);\n"
	"    /* Full per-pixel hypsometric & fine-relief shading (moved from CPU substrate to preserve pure RGB biome chromaticity) */\n"
	"    float hypsoShade = clamp((elevAboveWater - 18.0) * 0.0012, -0.06, 0.14) * (1.0 - wPolar * 0.7);\n"
	"    dryCol *= clamp(1.0 + hypsoShade + (convexity - concavity) * 0.05, 0.84, 1.18);\n"
	"    float macroFade = clamp((44000.0 - vDistance) / 38000.0, 0.0, 1.0);\n"
	"    float distFade  = clamp(1.0 - (vDistance - 1800.0) / 26200.0, 0.0, 1.0);\n"
	"    float closeZoom = clamp((820.0 - vDistance) / 480.0, 0.0, 1.0);\n"
	"    float ultraZoom = clamp((480.0 - vDistance) / 260.0, 0.0, 1.0);\n"
	"    /* 1. Terrestrial & shallow-submerged substrate shading (evaluated down to fragWaterDepth < 6.5 so shallow water preserves exact shore detail) */\n"
	"    if (fragWaterDepth < 6.5 && macroFade > 0.001) {\n"
	"        /* Exposed bedrock on steep cliffs & prominent ridge crests; talus/scree in steep concave gullies */\n"
	"        float rockFactor  = clamp(smoothstep(0.11, 0.28, slope + convexity * 0.035) + convexity * smoothstep(0.08, 0.18, slope) * 0.24, 0.0, 1.0);\n"
	"        float talusFactor = clamp(smoothstep(0.065, 0.15, slope) * (1.0 - smoothstep(0.17, 0.28, slope)) * clamp(0.18 + 0.65 * concavity, 0.0, 0.70), 0.0, 1.0);\n"
	"        float rockCrevice = 0.0;\n"
	"        if (rockFactor > 0.0 || talusFactor > 0.0) {\n"
	"            /* Dipping stepped rock strata + broad band-limited stone facets (zero zero-crossing spiderweb creases) */\n"
	"            float dipCoord = (fragZ + dot(vWorldPos, vec2(0.10, -0.07)) + h0 * 1.4 + (nMeso - 0.5) * 1.6) * 0.26;\n"
	"            float dipFract = fract(dipCoord);\n"
	"            float steppedLedge = smoothstep(0.15, 0.35, dipFract) - smoothstep(0.72, 0.92, dipFract) * 0.55;\n"
	"            float strata = (steppedLedge - 0.32) * 0.18 + (nMeso - 0.5) * 0.12;\n"
	"            vec3 fracG1 = (distFade > 0.01) ? chaff_grad_noise2d(vec2(vWorldPos.x * 0.18 + fragZ * 0.12, vWorldPos.y * 0.18 - fragZ * 0.10), 0x524F434Bu) : vec3(0.5, 0.0, 0.0);\n"
	"            vec3 fracG2 = (closeZoom > 0.01) ? chaff_grad_noise2d(vec2(vWorldPos.x * 0.36 - vWorldPos.y * 0.24, vWorldPos.x * 0.24 + vWorldPos.y * 0.36) + fragZ * 0.10, 0x4A4F494Eu) : vec3(0.5, 0.0, 0.0);\n"
	"            rockCrevice = smoothstep(0.28, 0.10, fracG1.x) * 0.14 * closeZoom;\n"
	"            float facetStep = (fracG1.x - 0.5) * 0.10 * distFade;\n"
	"            float fineGrain = (nStd - 0.5) * 0.08 * distFade + (fracG2.x - 0.5) * 0.06 * closeZoom + facetStep;\n"
	"            vec3 slateWarm = vec3(0.50, 0.47, 0.42);\n"
	"            vec3 graniteCool = vec3(0.43, 0.45, 0.47);\n"
	"            vec3 rockCol = mix(slateWarm, graniteCool, clamp(fracG1.x, 0.0, 1.0)) * (0.92 + strata + fineGrain);\n"
	"            rockCol *= (1.0 - rockCrevice);\n"
	"            /* Geological strata: red sandstone/limestone terraces in deserts, pale karst in Mediterranean, columnar basalt in volcanic */\n"
	"            float sedPhase = texZ * 0.22 + dot(vWorldPos, vec2(0.035, 0.025)) + (nMeso - 0.5) * 1.0;\n"
	"            float sedStep = smoothstep(0.22, 0.78, fract(sedPhase)) * 0.18 - 0.09;\n"
	"            vec3 terracottaSandstone = vec3(0.70, 0.42, 0.28) * (0.94 + sedStep);\n"
	"            vec3 buffLimestone = vec3(0.78, 0.68, 0.50) * (0.96 - sedStep * 0.5);\n"
	"            vec3 desertCliffCol = mix(terracottaSandstone, buffLimestone, smoothstep(0.30, 0.70, fract(sedPhase * 0.45)));\n"
	"            vec3 medKarstCol = mix(vec3(0.68, 0.43, 0.29), vec3(0.82, 0.78, 0.69), smoothstep(0.26, 0.66, nMeso * 0.60 + nStd * 0.40 + convexity * 0.20)) * (0.95 + sedStep * 0.45);\n"
	"            vec3 basaltCol = vec3(0.22, 0.21, 0.24) * (0.90 + 0.12 * nStd);\n"
	"            rockCol = mix(rockCol, desertCliffCol, clamp(wDesert + wSavannah * 0.55, 0.0, 1.0) * 0.85 * (1.0 - wPolar));\n"
	"            rockCol = mix(rockCol, medKarstCol, wMediter * 0.88);\n"
	"            rockCol = mix(rockCol, basaltCol, wVolcanic * 0.90);\n"
	"            float mossLedge = smoothstep(0.22, 0.10, slope) * (0.45 + 0.55 * concavity) * smoothstep(0.42, 0.72, nMeso * 0.6 + nStd * 0.4) * distFade;\n"
	"            vec3 ledgeFloraCol = mix(vec3(0.25, 0.37, 0.18), vec3(0.38, 0.44, 0.30), wBoreal);\n"
	"            rockCol = mix(rockCol, ledgeFloraCol, mossLedge * 0.50 * clamp(1.0 - wDesert - wMediter * 0.85 - wSavannah * 0.75, 0.0, 1.0));\n"
	"            if (talusFactor > 0.0) {\n"
	"                /* Band-limited scree & talus in concave gullies and cliff toes (cohesive stone clasts without high-frequency speckle) */\n"
	"                float stoneBody = smoothstep(0.30, 0.70, nMeso * 0.58 + nStd * 0.42);\n"
	"                vec3 baseScree = vec3(0.47, 0.44, 0.39);\n"
	"                baseScree = mix(baseScree, vec3(0.66, 0.47, 0.32), clamp(wDesert + wSavannah * 0.50, 0.0, 1.0) * 0.80 * (1.0 - wPolar));\n"
	"                baseScree = mix(baseScree, vec3(0.76, 0.69, 0.58), wMediter * 0.82);\n"
	"                baseScree = mix(baseScree, vec3(0.25, 0.24, 0.27), wVolcanic * 0.80);\n"
	"                vec3 screeCol = baseScree * (0.88 + 0.18 * stoneBody);\n"
	"                dryCol = mix(dryCol, screeCol, talusFactor * 0.58 * distFade);\n"
	"                microGrad += fracG1.yz * (0.14 * talusFactor * closeZoom);\n"
	"            }\n"
	"            dryCol = mix(dryCol, rockCol, rockFactor);\n"
	"            microGrad += fracG1.yz * (0.18 * rockFactor * closeZoom);\n"
	"        }\n"
	"        if (macroFade > 0.0) {\n"
	"            /* Alluvial soil & loam collects in concave hollows/swales or exposed slope cuts (not uniform hillside rings) */\n"
	"            float soilFactor = clamp((smoothstep(0.030, 0.105, slope) * (0.30 + 0.55 * concavity) + concavity * 0.26) * (1.0 - rockFactor), 0.0, 1.0);\n"
	"            if (soilFactor > 0.0 && distFade > 0.0) {\n"
	"                float soilMoisture = clamp(1.0 - elevAboveWater / 14.0 + concavity * 0.30 + bioHumidity * 0.20, 0.0, 1.0);\n"
	"                float pebble = smoothstep(0.68, 0.86, nMeso * 0.55 + nStd * 0.45) * closeZoom;\n"
	"                vec3 soilBase = vec3(0.40, 0.31, 0.21);\n"
	"                soilBase = mix(soilBase, vec3(0.56, 0.33, 0.20), clamp(wTropical + wSavannah * 0.75 + wMediter * 0.85, 0.0, 1.0) * 0.85);\n"
	"                soilBase = mix(soilBase, vec3(0.29, 0.23, 0.17), wBoreal * 0.70);\n"
	"                vec3 loamCol = soilBase * (0.88 + 0.18 * nMeso + 0.08 * (nStd - 0.5) * closeZoom);\n"
	"                loamCol = mix(loamCol, vec3(0.52, 0.49, 0.44), pebble * 0.32);\n"
	"                loamCol *= (1.0 - soilMoisture * 0.22);\n"
	"                dryCol = mix(dryCol, loamCol, soilFactor * distFade);\n"
	"            }\n"
	"            float dryScrub = smoothstep(0.42, 0.18, bioHumidity);\n"
	"            float sandAridity = clamp((dryCol.r - dryCol.b * 1.10) * 2.8 + dryScrub * 0.40, 0.0, 1.0) * smoothstep(0.38, 0.62, dryCol.r) * (1.0 - wSavannah * 0.65) * (1.0 - wMediter * 0.75);\n"
	"            float shoreFactor = smoothstep(6.0, 0.4, min(elevAboveWater, elevAboveHighTide)) * (1.0 - rockFactor);\n"
	"            float sandFactor = clamp(max(sandAridity, shoreFactor), 0.0, 1.0);\n"
	"            float sandSparkle = 0.0;\n"
	"            float wetSand = 0.0;\n"
	"            if (sandFactor > 0.001 && distFade > 0.0) {\n"
	"                float duneWave = nMeso * 0.45 + nStd * 0.55;\n"
	"                /* Asymmetric wind-driven aeolian sand ripples (sharper windward crest, gentler leeward trough) */\n"
	"                float rippleCoord = dot(vWorldPos.xy, vec2(wind.y, -wind.x)) * 0.55 + (nStd - 0.5) * 1.2;\n"
	"                float rippleSaw = fract(rippleCoord);\n"
	"                float asymRipple = (smoothstep(0.0, 0.35, rippleSaw) - smoothstep(0.35, 1.0, rippleSaw)) * (0.60 * nStd + 0.40 * nClose) * 0.14 * sandFactor;\n"
	"                /* Intertidal wet-sand hysteresis: sand between liveSeaLevel and highWaterZ retains moisture & specular sheen */\n"
	"                wetSand = max(smoothstep(2.6, 0.0, elevAboveWater), smoothstep(1.8, -0.2, elevAboveHighTide) * 0.85);\n"
	"                float warmSandMix = clamp(max(wDesert, shoreFactor * (1.0 - riverProx)), 0.0, 1.0);\n"
	"                vec3 drySandBase = mix(vec3(0.64, 0.60, 0.51), vec3(0.80, 0.69, 0.49), warmSandMix);\n"
	"                vec3 wetSandBase = mix(vec3(0.46, 0.44, 0.38), vec3(0.58, 0.49, 0.36), warmSandMix);\n"
	"                vec3 drySandCol = drySandBase * (0.90 + 0.18 * duneWave + asymRipple + 0.10 * (nClose - 0.5) * closeZoom);\n"
	"                vec3 wetSandCol = wetSandBase * (0.92 + 0.14 * nStd + 0.08 * (nClose - 0.5) * closeZoom);\n"
	"                vec3 sandCol = mix(drySandCol, wetSandCol, wetSand);\n"
	"                /* Alluvial river cobble & shingle point bars along river margins (band-limited smooth gravel) */\n"
	"                if (riverProx > 0.02 && wDesert < 0.5) {\n"
	"                    float stoneShingle = smoothstep(0.26, 0.74, nMeso * 0.48 + nStd * 0.52);\n"
	"                    vec3 gravelDry = mix(vec3(0.46, 0.44, 0.41), vec3(0.58, 0.55, 0.49), nStd) * (0.86 + 0.22 * stoneShingle + 0.08 * (nClose - 0.5) * closeZoom);\n"
	"                    vec3 gravelWet = gravelDry * 0.72;\n"
	"                    vec3 riverBarCol = mix(gravelDry, gravelWet, wetSand);\n"
	"                    /* Riparian mossy sedge margin at the top of the riverbank (elevAboveWater 1.2..4.2 wu) */\n"
	"                    float riparianSedge = smoothstep(0.6, 2.4, elevAboveWater) * smoothstep(5.2, 2.2, elevAboveWater) * (1.0 - wDesert);\n"
	"                    riverBarCol = mix(riverBarCol, dryCol * vec3(0.82, 1.04, 0.76), riparianSedge * 0.70);\n"
	"                    sandCol = mix(sandCol, riverBarCol, riverProx * (1.0 - wDesert * 0.8));\n"
	"                }\n"
	"                dryCol = mix(dryCol, sandCol, sandFactor * distFade);\n"
	"                sandSparkle = smoothstep(0.72, 0.92, nStd * 0.50 + nClose * 0.50) * closeZoom * 0.65;\n"
	"            }\n"
	"            /* 12-Regime Vegetated & Ecological Ground Floor:\n"
	"             *   - Grassland/Meadow: anisotropic wind-combed turf fibers & clover patches\n"
	"             *   - Savannah/Dry Scrub: exposed ochre-laterite crust with discrete clumped tussocks & root shadow rings\n"
	"             *   - Mediterranean Garrigue: terra rossa crimson-ochre soil, pale karst limestone clasts & aromatic scrub\n"
	"             *   - Wetland/Fen: dark saturated peat moss hummocks, sedge tussocks & specular shallow marsh pools in hollows\n"
	"             *   - Temperate Seasonal Forest: warm russet-copper deciduous leaf litter in hollows\n"
	"             *   - Tropical Seasonal Forest: golden-amber dry-season underbrush on convex rises vs lush green in hollows\n"
	"             *   - Boreal/Taiga/Tundra: dark spruce-needle duff, fallen bark, peat hummocks, and pale cladonia lichen */\n"
	"            float vegFactor = clamp(1.0 - sandFactor * 1.35 - rockFactor * 1.4, 0.0, 1.0) * (1.0 - wDesert * 0.95) * smoothstep(-0.18, 0.10, dryCol.g - dryCol.r * 0.84 + wSavannah * 0.28 + wBoreal * 0.18 + wMediter * 0.28 + wWetland * 0.22 + wTropSeas * 0.20) * smoothstep(0.12, 0.34, bioHumidity);\n"
	"            float canopyApronAO = 0.0;\n"
	"            float marshPoolMask = 0.0;\n"
	"            if (vegFactor > 0.001 && fragWaterDepth < 0.35) {\n"
	"                vec4 meadowWind = wind;\n"
	"                vec2 wDir = meadowWind.xy;\n"
	"                vec2 wPerp = vec2(-wDir.y, wDir.x);\n"
	"                /* Anisotropic wind-combed grass fiber coordinates (stretched 3.2x along wind, compressed across wind) */\n"
	"                vec2 fiberUV = vec2(dot(vWorldPos, wDir) * 0.65, dot(vWorldPos, wPerp) * 2.15);\n"
	"                vec3 fiberNoise = (closeZoom > 0.01) ? chaff_grad_noise2d(fiberUV + microGrad * 0.15, 0x47524153u) : vec3(nClose, 0.0, 0.0);\n"
	"                float turfFiber = (fiberNoise.x - 0.5) * closeZoom;\n"
	"                vec3 deepMoss   = dryCol * vec3(0.76, 0.96, 0.74);\n"
	"                vec3 lushTurf   = dryCol * vec3(0.92, 1.10, 0.86);\n"
	"                vec3 warmClover = dryCol * vec3(1.10, 1.04, 0.74);\n"
	"                vec3 vegCol = mix(deepMoss, lushTurf, smoothstep(0.24, 0.72, nMeso - concavity * 0.18));\n"
	"                vegCol = mix(vegCol, warmClover, smoothstep(0.40, 0.78, nStd) * 0.62 * (1.0 - concavity * 0.5));\n"
	"                /* Savannah / Steppe clumped tussock grass over exposed ochre-laterite crust */\n"
	"                float tussockRegime = clamp(wSavannah + dryScrub * 0.65, 0.0, 1.0);\n"
	"                if (tussockRegime > 0.01) {\n"
	"                    float clumpField = nStd * 0.55 + nClose * 0.45;\n"
	"                    float tussockMask = smoothstep(0.44, 0.64, clumpField);\n"
	"                    float tussockRim  = smoothstep(0.36, 0.46, clumpField) * (1.0 - tussockMask);\n"
	"                    vec3 lateriteCrust = mix(vec3(0.54, 0.34, 0.20), vec3(0.62, 0.46, 0.28), nMeso) * (0.88 + 0.22 * nUltra);\n"
	"                    vec3 dryTussockCol = dryCol * vec3(1.16, 1.06, 0.66) * (0.90 + 0.24 * turfFiber);\n"
	"                    vec3 savSurface = mix(lateriteCrust, dryTussockCol, tussockMask) * (1.0 - tussockRim * 0.22 * distFade);\n"
	"                    vegCol = mix(vegCol, savSurface, tussockRegime * 0.82);\n"
	"                }\n"
	"                /* Mediterranean Maquis & Garrigue: terracotta terra-rossa soil, pale limestone clasts & olive-sage scrub */\n"
	"                if (wMediter > 0.01) {\n"
	"                    vec3 terraRossa = vec3(0.56, 0.33, 0.21) * (0.86 + 0.24 * nStd);\n"
	"                    vec3 karstClast = vec3(0.74, 0.71, 0.64) * (0.90 + 0.18 * nUltra);\n"
	"                    vec3 garrigueScrub = vec3(0.34, 0.43, 0.24) * (0.88 + 0.22 * turfFiber);\n"
	"                    float scrubMask = smoothstep(0.38, 0.62, nMeso * 0.5 + nStd * 0.5 - convexity * 0.18);\n"
	"                    float clastMask = smoothstep(0.68, 0.84, nClose * 0.6 + nUltra * 0.4) * (1.0 - scrubMask);\n"
	"                    vec3 medFloor = mix(terraRossa, karstClast, clastMask * distFade);\n"
	"                    medFloor = mix(medFloor, garrigueScrub, scrubMask);\n"
	"                    vegCol = mix(vegCol, medFloor, wMediter * 0.78);\n"
	"                }\n"
	"                /* Wetland / Fen / Floodplain Marsh: saturated dark peat moss hummocks & specular shallow pools in hollows */\n"
	"                if (wWetland > 0.01) {\n"
	"                    vec3 darkPeat = vec3(0.16, 0.22, 0.15) * (0.85 + 0.25 * nClose);\n"
	"                    vec3 fenSedge = vec3(0.24, 0.44, 0.26) * (0.90 + 0.22 * turfFiber);\n"
	"                    float hummock = smoothstep(0.34, 0.66, nMeso * 0.55 + nStd * 0.45 + convexity * 0.25 - concavity * 0.35);\n"
	"                    marshPoolMask = wWetland * smoothstep(0.42, 0.18, hummock) * smoothstep(6.0, 0.2, elevAboveWater) * distFade;\n"
	"                    vec3 marshCol = mix(darkPeat, fenSedge, hummock);\n"
	"                    vegCol = mix(vegCol, marshCol, wWetland * 0.82);\n"
	"                }\n"
	"                /* Temperate Seasonal Deciduous Forest: warm russet-copper leaf litter in hollows & emerald moss on rises */\n"
	"                if (wTempSeas > 0.01) {\n"
	"                    vec3 russetLeafLitter = mix(vec3(0.46, 0.29, 0.16), vec3(0.54, 0.38, 0.19), nClose) * (0.86 + 0.24 * nUltra);\n"
	"                    float litterPatch = smoothstep(0.36, 0.68, nMeso * 0.5 + nStd * 0.5 + concavity * 0.28);\n"
	"                    vegCol = mix(vegCol, russetLeafLitter, wTempSeas * litterPatch * 0.58 * distFade);\n"
	"                }\n"
	"                /* Tropical Seasonal Monsoon Forest: golden-amber dry grass on convex interfluves vs lush green gallery hollows */\n"
	"                if (wTropSeas > 0.01) {\n"
	"                    vec3 monsoonGold = vec3(0.52, 0.50, 0.24) * (0.88 + 0.22 * turfFiber);\n"
	"                    vec3 galleryGreen = vec3(0.20, 0.46, 0.18) * (0.90 + 0.20 * nStd);\n"
	"                    float galleryMask = smoothstep(0.32, 0.66, concavity * 0.6 + (1.0 - nMeso) * 0.4);\n"
	"                    vec3 tropSeasFloor = mix(monsoonGold, galleryGreen, galleryMask);\n"
	"                    vegCol = mix(vegCol, tropSeasFloor, wTropSeas * 0.65);\n"
	"                }\n"
	"                /* Boreal Taiga / Periglacial Tundra: dark spruce needle litter, peat hummocks & pale reindeer lichen */\n"
	"                if (wBoreal > 0.01) {\n"
	"                    vec3 needleDuff = vec3(0.24, 0.21, 0.16) * (0.85 + 0.28 * nClose);\n"
	"                    vec3 sphagnumPeat = vec3(0.28, 0.35, 0.20) * (0.88 + 0.24 * nStd);\n"
	"                    vec3 cladoniaLichen = vec3(0.56, 0.60, 0.46);\n"
	"                    float lichenPatch = smoothstep(0.64, 0.82, nStd * 0.5 + nClose * 0.5) * (0.5 + 0.5 * convexity);\n"
	"                    vec3 borealFloor = mix(needleDuff, sphagnumPeat, smoothstep(0.35, 0.68, nMeso + concavity * 0.25));\n"
	"                    borealFloor = mix(borealFloor, cladoniaLichen, lichenPatch * 0.55 * distFade);\n"
	"                    vegCol = mix(vegCol, borealFloor, wBoreal * 0.78);\n"
	"                }\n"
	"                /* Continuous organic forest-floor humus & moss variation in concave hollows (zero grid-cell circular blotches) */\n"
	"                if (distFade > 0.01) {\n"
	"                    float humusEnv = clamp((wBoreal * 0.45 + wTropical * 0.40 + wTempSeas * 0.40 + wTropSeas * 0.35) * (0.35 + 0.65 * concavity), 0.0, 0.75) * distFade;\n"
	"                    if (humusEnv > 0.001) {\n"
	"                        vec3 humusCol = mix(vec3(0.28, 0.23, 0.16), vec3(0.21, 0.31, 0.17), smoothstep(0.30, 0.70, nMeso));\n"
	"                        vegCol = mix(vegCol, humusCol * (0.88 + 0.24 * nClose), humusEnv * 0.32);\n"
	"                    }\n"
	"                }\n"
	"                float bladeRelief = (nStd - 0.5) * 0.15 * distFade + (turfFiber * 0.65 + (nClose - 0.5) * 0.35) * 0.22 * closeZoom + (nUltra - 0.5) * 0.15 * ultraZoom;\n"
	"                float meadowGustWave = clamp((meadowWind.z - 0.55) * 0.55 + meadowWind.w * 0.45, 0.0, 1.0);\n"
	"                vec3 windSheenCol = vegCol * vec3(1.16, 1.14, 0.86);\n"
	"                vegCol = mix(vegCol, windSheenCol, meadowGustWave * 0.28 * distFade);\n"
	"                vegCol *= clamp(1.0 + bladeRelief, 0.74, 1.28);\n"
	"                microGrad += fiberNoise.yz * (0.28 * vegFactor * closeZoom);\n"
	"                dryCol = mix(dryCol, vegCol, vegFactor * macroFade * smoothstep(0.35, -0.10, fragWaterDepth));\n"
	"            }\n"
	"            float normStrength = 0.26 + 0.24 * distFade + 0.18 * closeZoom;\n"
	"            vec3 N_frag = normalize(fragNormal - microGrad.x * normStrength * tangentU - microGrad.y * normStrength * tangentV);\n"
	"            vec3 viewDir = normalize(vViewDir);\n"
	"            vec3 H_half = normalize(L_frag + viewDir);\n"
	"            float NdotL = max(0.0, dot(N_frag, L_frag));\n"
	"            float NdotV = max(0.001, dot(N_frag, viewDir));\n"
	"            /* Oren-Nayar rough diffuse BRDF for matte rock, sand, soil, and foliage */\n"
	"            float surfRoughness = clamp(0.62 + 0.26 * max(rockFactor, sandFactor) - 0.18 * vegFactor, 0.30, 0.92);\n"
	"            float sigma2 = surfRoughness * surfRoughness;\n"
	"            float onA = 1.0 - 0.5 * (sigma2 / (sigma2 + 0.33));\n"
	"            float onB = 0.45 * (sigma2 / (sigma2 + 0.09));\n"
	"            vec3 L_proj = normalize(L_frag - N_frag * NdotL + vec3(1e-5));\n"
	"            vec3 V_proj = normalize(viewDir - N_frag * NdotV + vec3(1e-5));\n"
	"            float cosPhi = max(0.0, dot(L_proj, V_proj));\n"
	"            float sinAlpha = sqrt(max(0.0, 1.0 - min(NdotL, NdotV) * min(NdotL, NdotV)));\n"
	"            float tanBeta  = sqrt(max(0.0, 1.0 - max(NdotL, NdotV) * max(NdotL, NdotV))) / max(0.05, max(NdotL, NdotV));\n"
	"            float orenNayarDiff = NdotL * (onA + onB * cosPhi * sinAlpha * min(1.8, tanBeta));\n"
	"            /* Grazing-angle Schlick vegetative velvet sheen on grass & turf */\n"
	"            float vegGrazingSheen = pow(clamp(1.0 - NdotV, 0.0, 1.0), 4.0) * NdotL * vegFactor * 0.16 * distFade;\n"
	"            float cavityAO = clamp(1.0 - slope * 0.22 + h0 * 0.24 - microCavity * 0.42 - concavity * 0.14 - rockCrevice * 0.38 - canopyApronAO, 0.48, 1.06);\n"
	"            float fragLit = ((0.40 * nd_amb_trans(cloudShadow)) + 0.60 * (orenNayarDiff + vegGrazingSheen) * sunTrans) * cavityAO;\n"
	"            dryLighting = mix(dryLighting, fragLit, macroFade);\n"
	"            float microShade = clamp(1.0 + h0 * (0.30 + 0.16 * closeZoom) * macroFade, 0.72, 1.28);\n"
	"            dryCol *= microShade;\n"
	"            /* Capillary wet-shore darkening and sub-aerial wet specular sheen across intertidal, marsh pool & swash memory zone */\n"
	"            float shoreWetMoisture = max(max(smoothstep(-0.38, 0.15, fragWaterDepth), swashState.w * 0.88), marshPoolMask * 0.85);\n"
	"            dryCol *= (1.0 - shoreWetMoisture * 0.22);\n"
	"            float subAerialMask = smoothstep(0.25, -0.02, fragWaterDepth);\n"
	"            float wetSpecAngle = max(0.0, dot(N_frag, H_half));\n"
	"            float wetFilmSpec = pow(wetSpecAngle, 36.0) * (0.32 * wetSand + 0.34 * shoreWetMoisture + 0.42 * marshPoolMask) * subAerialMask;\n"
	"            float quartzGlint = pow(wetSpecAngle, 64.0) * sandSparkle * 0.42 * subAerialMask;\n"
	"            dryCol += vec3((wetFilmSpec + quartzGlint) * (0.35 + 0.65 * max(sandFactor, marshPoolMask)) * distFade);\n"
	"            if (rockFactor > 0.25 && distFade > 0.0) {\n"
	"                float specAngle = max(0.0, dot(N_frag, H_half));\n"
	"                float spec = pow(specAngle, 24.0) * (rockFactor * (0.20 + 0.12 * closeZoom) * (1.0 - rockCrevice) * distFade);\n"
	"                dryCol += vec3(spec);\n"
	"            }\n"
	"        }\n"
	"    }\n"
	"    /* 2. Submerged benthic seabed substrate (gradually blends in across deeper shelf) */\n"
	"    vec3 subCol = baseCol.rgb;\n"
	"    float subLighting = effLighting;\n"
	"    if (fragWaterDepth > 0.15) {\n"
	"        float shallowSandMask = smoothstep(14.0, 2.0, fragWaterDepth) * (1.0 - smoothstep(0.08, 0.22, slope));\n"
	"        float sandRipple = (nStd - 0.5) * 0.22 + (nClose - 0.5) * 0.18 * closeZoom + (nUltra - 0.5) * 0.12 * ultraZoom;\n"
	"        /* Biome-aware seabed: quartz/coral sand, temperate riverbed, glacial silt, volcanic basalt */\n"
	"        vec3 bioSand = vec3(0.72, 0.64, 0.47) * wTropical\n"
	"                     + vec3(0.48, 0.45, 0.37) * wTemperate\n"
	"                     + vec3(0.56, 0.60, 0.64) * wPolar\n"
	"                     + vec3(0.22, 0.20, 0.22) * wVolcanic;\n"
	"        vec3 littoralSand = bioSand * clamp(0.92 + sandRipple, 0.72, 1.22);\n"
	"        float pebble = smoothstep(0.64, 0.85, nClose * 0.5 + nUltra * 0.5) * closeZoom;\n"
	"        littoralSand = mix(littoralSand, vec3(0.45, 0.43, 0.38), pebble * 0.42);\n"
	"        vec3 benthicSubstrate = mix(baseCol.rgb, bioSand * 0.78, 0.42);\n"
	"        subCol = mix(benthicSubstrate, littoralSand, shallowSandMask * (0.45 + 0.55 * distFade));\n"
	"        float littoralSilt = smoothstep(18.0, 1.0, fragWaterDepth) * clamp(bioHumidity * 1.5 - 0.30, 0.0, 1.0) * (1.0 - wDesert * 0.88);\n"
	"        vec3 C_SILT_BED = mix(vec3(0.20, 0.56, 0.48) * (1.0 - wPolar) + vec3(0.36, 0.62, 0.68) * wPolar, vec3(0.14, 0.30, 0.32), wVolcanic * 0.70);\n"
	"        subCol = mix(subCol, C_SILT_BED * 0.85, littoralSilt * 0.35);\n"
	"        if (distFade > 0.01 && fragWaterDepth < 18.0) {\n"
	"            vec3 N_sub = normalize(fragNormal - detail.y * (0.30 * distFade) * tangentU - detail.z * (0.30 * distFade) * tangentV);\n"
	"            float subDiff = max(0.0, dot(N_sub, L_frag));\n"
	"            float subCavity = clamp(1.0 + detail.x * 0.28 - detail.w * 0.38, 0.62, 1.18);\n"
	"            subLighting = mix(subLighting, ((0.45 * nd_amb_trans(cloudShadow)) + 0.55 * subDiff * sunTrans) * subCavity, distFade);\n"
	"        }\n"
	"    }\n"
	"    /* Gradual transition from detailed shore substrate into deeper benthic sediment (0.35 .. 5.5 units) */\n"
	"    float submergedBlend = smoothstep(0.35, 5.5, fragWaterDepth);\n"
	"    /* Alpine progression on mountain summits: subalpine granite scree and permanent snowcaps across all zooms */\n"
	"    float peakElevAboveWater = (effSeaLevel > -900.0) ? max(elevAboveWater, texZ - liveSeaLevel) : max(elevAboveWater, texZ);\n"
	"    float effElev = peakElevAboveWater + max(0.0, macroDetail.x * 3.0) + concavity * 28.0 - convexity * 18.0;\n"
	"    float equatorAspect = dot(fragNormal, tangentV) * sign(sin(latPhi));\n"
	"    float sunAspect = dot(fragNormal, L_frag) - 0.35;\n"
	"    float aspectShift = (equatorAspect * 65.0 + clamp(sunAspect, -0.4, 0.4) * 45.0) * (1.0 - wPolar);\n"
	"    float snowBaseZ = 280.0 - 220.0 * wPolar - 70.0 * wBoreal + 140.0 * wTropical + 120.0 * wDesert + aspectShift;\n"
	"    /* Snow sheds from steep convex ridges and drifts deeper into concave cirques and gullies */\n"
	"    float snowShed = smoothstep(0.54, 0.14, slope + convexity * 0.08 - concavity * 0.10);\n"
	"    float alpineSnow = smoothstep(snowBaseZ, snowBaseZ + 180.0, effElev) * snowShed;\n"
	"    float alpineRock = smoothstep(snowBaseZ - 120.0, snowBaseZ + 40.0, effElev) * (1.0 - wPolar);\n"
	"    /* Glacial ice tongue & lateral moraines in high alpine valleys */\n"
	"    float glacialZone = smoothstep(snowBaseZ - 60.0, snowBaseZ + 80.0, effElev) * smoothstep(0.24, 0.05, slope) * (0.45 + 0.55 * concavity) * (1.0 - wDesert * 0.75);\n"
	"    float glacierCrevasse = smoothstep(0.47, 0.53, abs(nStd - 0.5)) * distFade;\n"
	"    vec3 glacierBlueIce = mix(vec3(0.48, 0.76, 0.90), vec3(0.68, 0.88, 0.96), nClose) * (1.0 - glacierCrevasse * 0.38);\n"
	"    float moraineMask = smoothstep(0.24, 0.32, slope) * smoothstep(0.08, 0.24, slope) * glacialZone;\n"
	"    vec3 moraineCol = vec3(0.30, 0.28, 0.26) * (0.80 + 0.40 * nClose);\n"
	"    /* Alpenglow on alpine summits facing away from twilight sunset */\n"
	"    float alpenglowMask = twilight * smoothstep(-0.05, -0.70, dot(fragNormal, sDir)) * smoothstep(snowBaseZ - 60.0, snowBaseZ + 80.0, effElev);\n"
	"    vec3 C_ALPENGLOW = vec3(0.96, 0.46, 0.58);\n"
	"    if (alpineRock > 0.001 || wPolar > 0.01) {\n"
	"        vec3 graniteRock = vec3(0.62, 0.60, 0.57);\n"
	"        dryCol = mix(dryCol, graniteRock, alpineRock * 0.55);\n"
	"        if (moraineMask > 0.001) {\n"
	"            dryCol = mix(dryCol, moraineCol, moraineMask * 0.75 * distFade);\n"
	"        }\n"
	"        float snowMask = max(alpineSnow, wPolar * smoothstep(-1.0, 2.5, peakElevAboveWater) * snowShed);\n"
	"        if (snowMask > 0.001) {\n"
	"            /* Wind-carved sastrugi snow ripples along prevailing wind */\n"
	"            float sastrugi = sin(dot(vWorldPos, wind.xy) * 0.45 + (nStd - 0.5) * 2.0) * 0.04 * distFade;\n"
	"            float snowNoise = (nStd - 0.5) * 0.10 * distFade + (nClose - 0.5) * 0.08 * closeZoom + sastrugi;\n"
	"            vec3 snowCol = vec3(0.97, 0.99, 1.00) * (0.98 + snowNoise);\n"
	"            snowCol = mix(snowCol, glacierBlueIce, glacialZone * 0.55 * (1.0 - alpineSnow));\n"
	"            snowCol = mix(snowCol, C_ALPENGLOW, alpenglowMask * 0.72);\n"
	"            dryCol = mix(dryCol, snowCol, snowMask * 0.97);\n"
	"        }\n"
	"    }\n"
	"    /* High-definition orbital/atmospheric procedural albedo modulation on dry land & ocean basins:\n"
	"     *   - Polar ice caps & arctic tundra: crisp white snow & ice from sea level up (wPolar)\n"
	"     *   - Subalpine granite scree emergence: 200..420 wu\n"
	"     *   - Climate-modulated permanent alpine snow & glacier caps on mountain summits: 280..600+ wu\n"
	"     *   - Golden desert erg sand modulation visible across space */\n"
	"    float orbitDetailWeight = smoothstep(1800.0, 6500.0, vDistance);\n"
	"    if (orbitDetailWeight > 0.001) {\n"
	"        float contTex = (macroOct.x - 0.5) * 0.16 + (macroOct.y - 0.5) * 0.18 + (macroOct.z - 0.5) * 0.14 + (macroOct.w - 0.5) * 0.10;\n"
	"        float ridgeCrest = macroDetail.w * orbitDetailWeight;\n"
	"        float subalpineRock = smoothstep(200.0, 420.0, effElev) * (1.0 - wPolar);\n"
	"        vec3 ridgeTint = mix(dryCol * (1.0 + contTex), vec3(0.62, 0.60, 0.57), clamp(ridgeCrest * 0.35 + subalpineRock * 0.30, 0.0, 0.48));\n"
	"        /* 1. Polar ice caps & frozen summits visible from orbit: brilliant white where biome is polar/snow (wPolar) */\n"
	"        float polarIce = wPolar * smoothstep(-1.0, 2.5, peakElevAboveWater);\n"
	"        /* 2. Alpine mountain snowcaps visible from orbit: high summits and cordillera ridges */\n"
	"        float alpineSnowline = 280.0 - 220.0 * wPolar - 70.0 * wBoreal + 140.0 * wTropical + 120.0 * wDesert + aspectShift;\n"
	"        float mountainSnow = smoothstep(alpineSnowline, alpineSnowline + 180.0, effElev) * smoothstep(0.55, 0.15, slope);\n"
	"        float spaceSnow = max(polarIce, mountainSnow);\n"
	"        float spaceGlacier = smoothstep(alpineSnowline - 40.0, alpineSnowline + 60.0, effElev) * smoothstep(0.24, 0.06, slope) * (1.0 - wDesert * 0.75);\n"
	"        float orbitAlpenglow = twilight * smoothstep(-0.05, -0.70, dot(torusN, sDir)) * spaceSnow;\n"
	"        vec3 snowOrbitCol = vec3(0.97, 0.99, 1.00);\n"
	"        snowOrbitCol = mix(snowOrbitCol, vec3(0.60, 0.82, 0.92), spaceGlacier * 0.45 * (1.0 - mountainSnow));\n"
	"        snowOrbitCol = mix(snowOrbitCol, C_ALPENGLOW, orbitAlpenglow * 0.75);\n"
	"        ridgeTint = mix(ridgeTint, snowOrbitCol, spaceSnow * 0.98);\n"
	"        /* 3. Golden desert erg sand visible from orbit (never overriding alpine snowcaps) */\n"
	"        vec3 desertOrbitCol = vec3(0.86, 0.74, 0.52);\n"
	"        ridgeTint = mix(ridgeTint, desertOrbitCol, wDesert * (1.0 - spaceSnow) * 0.75);\n"
	"        dryCol = mix(dryCol, ridgeTint, orbitDetailWeight * 0.88);\n"
	"        dryCol = mix(dryCol, snowOrbitCol, spaceSnow * 0.96);\n"
	"        subCol *= clamp(1.0 + contTex * 0.75 * orbitDetailWeight, 0.78, 1.22);\n"
	"        float seaIce = clamp(max(wPolar, smoothstep(54.0, 68.0, latDeg)), 0.0, 1.0);\n"
	"        subCol = mix(subCol, vec3(0.96, 0.98, 1.00), seaIce * 0.96);\n"
	"    }\n"
	"    vec3 baseAlbedo = mix(dryCol, subCol, submergedBlend);\n"
	"    vec3 shadowCool = mix(vec3(1.0), vec3(0.85, 0.92, 1.05), cloudShadow * 0.45);\n"
	"    vec3 lit = mix(dryCol * dryLighting * sunLitCol * shadowCool, subCol * subLighting, submergedBlend);\n"
	"    if (uLambLight.w > 0.001) {\n"
	"        vec3 toLamb = vec3((uLambLight.x - vWorldPos.x) * tangentU + (uLambLight.y - vWorldPos.y) * tangentV + (uLambLight.z + 10.0 - fragZ) * torusN);\n"
	"        float d2 = dot(toLamb, toLamb);\n"
	"        float atten = (1.0 / (1.0 + d2 * 0.00055)) * smoothstep(220.0 * 220.0, 28.0 * 28.0, d2);\n"
	"        float nDotL = (d2 > 1e-4) ? max(0.25, dot(fragNormal, toLamb * inversesqrt(d2))) : 1.0;\n"
	"        vec3 warmGlow = vec3(1.06, 0.99, 0.84) * (uLambLight.w * 0.24 * atten * nDotL);\n"
	"        lit += (baseAlbedo * 0.72 + vec3(0.28)) * warmGlow;\n"
	"    }\n"
	"    /* 3. Physical Volumetric Water Extinction & Biome-Aware Optics (Crystal-Clear Shallows to -2,000 wu Abyssal Trenches) */\n"
	"    if (fragWaterDepth > 0.0) {\n"
	"        float d = clamp(fragWaterDepth, 0.0, 2000.0);\n"
	"        float wetBlend = smoothstep(0.0, 0.28, fragWaterDepth);\n"
	"        float distFade = clamp(1.0 - (vDistance - 900.0) / 2500.0, 0.0, 1.0);\n"
	"        float closeZoom = clamp((420.0 - vDistance) / 300.0, 0.0, 1.0);\n"
	"        float latPolar = smoothstep(54.0, 68.0, latDeg);\n"
	"        float alpineFreeze = smoothstep(280.0, 480.0, fragZ) * (1.0 - wTropical);\n"
	"        float freezeFactor = clamp(max(wPolar, max(latPolar, alpineFreeze)), 0.0, 1.0);\n"
	"        /* Subsurface wave-refracted caustic light web projected onto seabed before Beer-Lambert absorption */\n"
	"        float caustic = 0.0;\n"
	"        if (distFade > 0.01 && d < 16.0) {\n"
	"            float tC = mod(uAnimTime, 256.0);\n"
	"            vec2 cUV = vWorldPos + waveRefGrad * 1.6;\n"
	"            caustic = nd_caustic_web(cUV, tC, closeZoom) * distFade * (1.0 - freezeFactor);\n"
	"        }\n"
	"        float causticFocus = smoothstep(0.04, 0.85, d) * exp(-0.11 * d);\n"
	"        lit *= (1.0 + caustic * causticFocus * 0.52);\n"
	"        vec3 viewDir = normalize(vViewDir);\n"
	"        float cosView = max(0.35, dot(torusN, viewDir));\n"
	"        float cosSun = max(0.28, dot(torusN, L_frag));\n"
	"        float opticalDepth = d * (1.0 / cosView + 0.55 / cosSun);\n"
	"        /* Non-linear shallow optical path: crystal-clear wading shallows (d < 4), smooth shelf ramp (1.2..14) */\n"
	"        float shallowFactor = smoothstep(1.2, 14.0, d);\n"
	"        float effectiveOpticalDepth = opticalDepth * (0.08 + 0.92 * shallowFactor);\n"
	"        /* Natural lacustrine & oceanic water body palette (no neon cyan wash in shallows) */\n"
	"        vec3 C_SHALLOW = vec3(0.16, 0.56, 0.62) * wTropical\n"
	"                       + vec3(0.11, 0.42, 0.46) * wTemperate\n"
	"                       + vec3(0.17, 0.50, 0.62) * wPolar\n"
	"                       + vec3(0.09, 0.34, 0.38) * wVolcanic;\n"
	"        vec3 C_SHELF   = vec3(0.11, 0.43, 0.58) * wTropical\n"
	"                       + vec3(0.08, 0.32, 0.44) * wTemperate\n"
	"                       + vec3(0.11, 0.36, 0.54) * wPolar\n"
	"                       + vec3(0.07, 0.25, 0.34) * wVolcanic;\n"
	"        vec3 C_OUTER   = vec3(0.07, 0.28, 0.48) * wTropical\n"
	"                       + vec3(0.05, 0.21, 0.36) * wTemperate\n"
	"                       + vec3(0.06, 0.21, 0.40) * wPolar\n"
	"                       + vec3(0.04, 0.16, 0.27) * wVolcanic;\n"
	"        vec3 C_ABYSS   = vec3(0.030, 0.120, 0.30) * wTropical\n"
	"                       + vec3(0.024, 0.100, 0.24) * wTemperate\n"
	"                       + vec3(0.022, 0.090, 0.24) * wPolar\n"
	"                       + vec3(0.018, 0.072, 0.18) * wVolcanic;\n"
	"        /* Coastal sediment plumes & estuary runoff in humid / high-rainfall basins */\n"
	"        float runoffMoisture = clamp(bioHumidity * 1.5 - 0.30, 0.0, 1.0) * (1.0 - wDesert * 0.88);\n"
	"        float coastalZone = smoothstep(28.0, 1.5, d);\n"
	"        vec3 siltTurb = (distFade > 0.01) ? chaff_grad_noise2d(vWorldPos * 0.024 + vec2(11.2, -7.5), 0x53494C54u) : vec3(0.5, 0.0, 0.0);\n"
	"        float siltSwirl = (siltTurb.x - 0.5) * 0.25;\n"
	"        float siltDensity = coastalZone * runoffMoisture * clamp(0.75 + siltSwirl, 0.0, 1.25);\n"
	"        vec3 C_SILT = mix(vec3(0.20, 0.56, 0.48) * (1.0 - wPolar) + vec3(0.36, 0.62, 0.68) * wPolar, vec3(0.14, 0.30, 0.32), wVolcanic * 0.70);\n"
	"        /* Two-stream wavelength-dependent Beer-Lambert spectral absorption (Jerlov clear pelagic vs turbid coastal) */\n"
	"        vec3 kClear  = vec3(0.075, 0.016, 0.0048);\n"
	"        vec3 kTurbid = vec3(0.135, 0.048, 0.0220);\n"
	"        float turbidity = clamp(siltDensity * 0.65 + (1.0 - openSeaFactor) * 0.25, 0.0, 1.0);\n"
	"        vec3 seabedAbsorption = exp(-mix(kClear, kTurbid, turbidity) * effectiveOpticalDepth);\n"
	"        vec3 transmittedBed = lit * seabedAbsorption;\n"
	"        /* Multi-zone hypsometric water column progression across littoral (2.5..18), shelf (18..85), bathyal (85..340), and hadal (340..1400) */\n"
	"        float tShelf  = smoothstep(2.5, 18.0, d);\n"
	"        float tOuter  = smoothstep(18.0, 85.0, d);\n"
	"        float tAbyss  = smoothstep(85.0, 340.0, d);\n"
	"        float tTrench = smoothstep(340.0, 1400.0, d);\n"
	"        vec3 inscatterCol = mix(C_SHALLOW, C_SHELF, tShelf);\n"
	"        inscatterCol = mix(inscatterCol, C_OUTER, tOuter);\n"
	"        inscatterCol = mix(inscatterCol, C_ABYSS, tAbyss);\n"
	"        inscatterCol = mix(inscatterCol, C_ABYSS * 0.38, tTrench);\n"
	"        inscatterCol = mix(inscatterCol, C_SILT, siltDensity * 0.65);\n"
	"        vec3 bodyScatterWeight = clamp((vec3(1.0) - seabedAbsorption) * (0.20 + 0.80 * shallowFactor) + vec3(siltDensity * 0.35), 0.0, 1.0);\n"
	"        vec3 waterBody = mix(transmittedBed, inscatterCol, bodyScatterWeight);\n"
	"        /* Project camera ray from seabed fragment (fragZ) to horizontal sea-level plane (Z = liveSeaLevel)\n"
	"         * so surface waves, capillary facets, and cloud reflections sit on a true horizontal water plane */\n"
	"        vec2 viewHoriz = vec2(dot(viewDir, tangentU), dot(viewDir, tangentV));\n"
	"        vec2 surfWorldPos = vWorldPos + viewHoriz * (fragWaterDepth / cosView);\n"
	"        float shoreWaveDamp = smoothstep(0.0, 1.8, fragWaterDepth);\n"
	"        float basinWaveScale = (0.38 + 0.62 * openSeaFactor) * windFetch * (0.30 + 0.70 * shoreWaveDamp) * (1.0 - freezeFactor * 0.95);\n"
	"        vec4 waves = eval_zoom_water_waves(surfWorldPos, uAnimTime, vDistance, fw, openSeaFactor) * (1.0 - freezeFactor * 0.95);\n"
	"        vec2 capGrad = eval_wind_capillary_facets(surfWorldPos, uAnimTime, wind, vDistance, fw) * smoothstep(0.4, 2.8, fragWaterDepth) * (1.0 - freezeFactor * 0.95);\n"
	"        vec2 waveGrad = waves.yz * basinWaveScale + capGrad * 0.20 + lambWake.yz * 1.85 * (1.0 - freezeFactor);\n"
	"        vec2 specGrad = waves.yz * (basinWaveScale * 1.15 + 0.22) + capGrad * 0.25 + lambWake.yz * 2.25 * (1.0 - freezeFactor);\n"
	"        vec3 waveNormal = normalize(torusN - waveGrad.x * tangentU - waveGrad.y * tangentV);\n"
	"        vec3 specNormal = normalize(torusN - specGrad.x * tangentU - specGrad.y * tangentV);\n"
	"        float NdotV = max(0.0, dot(waveNormal, viewDir));\n"
	"        vec3 flatRefl = reflect(-viewDir, torusN);\n"
	"        vec3 glintSunDir = normalize(mix(L_frag, flatRefl, 0.35) + 0.25 * torusN);\n"
	"        vec3 halfVec = normalize(glintSunDir + viewDir);\n"
	"        float NdotH_cap  = max(0.0, dot(specNormal, halfVec));\n"
	"        float NdotH_wave = max(0.0, dot(waveNormal, halfVec));\n"
	"        float closeWeight = clamp(1.0 - (vDistance - 350.0) / 1400.0, 0.0, 1.0);\n"
	"        float specMicro   = pow(smoothstep(0.9965, 0.9998, NdotH_cap), 3.0) * (0.28 + 0.34 * wind.w) * (0.50 + 0.50 * closeWeight);\n"
	"        float specWavelet = pow(smoothstep(0.9880, 0.9988, NdotH_wave), 2.2) * (0.16 + 0.14 * wind.z);\n"
	"        float specBroad   = smoothstep(0.9680, 0.9945, NdotH_wave) * (0.06 + 0.04 * (1.0 - closeWeight));\n"
	"        float shoreSpecDamp = smoothstep(0.3, 3.2, fragWaterDepth);\n"
	"        float specSun     = (specMicro * 0.55 + specWavelet * 0.70 + specBroad * 0.45) * shoreSpecDamp * sunTrans;\n"
	"        float iceGlint    = pow(max(0.0, dot(torusN, halfVec)), 22.0) * 0.42;\n"
	"        specSun           = mix(specSun * directSun, iceGlint * sunTrans * directSun, freezeFactor);\n"
	"        float flatNdotV = max(0.0, dot(torusN, viewDir));\n"
	"        float facetTilt = clamp((flatNdotV - dot(specNormal, viewDir)) * 1.8, -0.14, 0.24);\n"
	"        float fresnel = clamp(0.02 + 0.98 * pow(clamp(1.0 - NdotV, 0.0, 1.0), 5.0) + max(0.0, facetTilt) * 0.16, 0.015, 0.62);\n"
	"        vec3 skySurf = ((length(uSkyColor) > 0.001) ? uSkyColor : vec3(0.55, 0.72, 0.88)) * (0.16 + 0.84 * dayFactor);\n"
	"        float waveSlopeDiff = dot(specNormal, L_frag) - dot(torusN, L_frag);\n"
	"        float hillshadeLit = max(0.0, waveSlopeDiff) * sunTrans * directSun;\n"
	"        float hillshadeShd = max(0.0, -waveSlopeDiff);\n"
	"        vec3 waveShade = C_SHELF * (1.0 + hillshadeLit * 0.55 - hillshadeShd * 0.40) * nd_amb_trans(cloudShadow);\n"
	"        float deepSssMask = smoothstep(2.5, 10.0, fragWaterDepth);\n"
	"        float crestGlow = clamp(waves.w * 0.60 + hillshadeLit * 1.40, 0.0, 1.0) * deepSssMask;\n"
	"        vec3 sssTint = mix(C_SHALLOW, waveShade, 0.55) * (crestGlow * 0.22);\n"
	"        float shoreSlope = max(0.22, length(worldSlopeGrad));\n"
	"        float shoreDist = fragWaterDepth / shoreSlope;\n"
	"        float foamDistFade = smoothstep(950.0, 220.0, vDistance);\n"
	"        float openSeaSurfEnergy = openSeaFactor * windFetch * smoothstep(0.55, 1.15, wind.z);\n"
	"        float crestLap = smoothstep(1.6, 0.20, shoreDist) * smoothstep(0.48, 0.82, waves.w) * openSeaSurfEnergy * 0.22;\n"
	"        float whitecap = smoothstep(0.68, 0.94, waves.w) * smoothstep(6.0, 16.0, fragWaterDepth) * openSeaSurfEnergy * 0.24;\n"
	"        /* Upland river riffle whitewater over shallow cobble shoals & rapids */\n"
	"        float riverSlopeEnergy = smoothstep(1.0, 25.0, effSeaLevel - uSeaLevel) * riverProx;\n"
	"        float riffleMask = riverSlopeEnergy * smoothstep(2.6, 0.25, fragWaterDepth) * smoothstep(0.05, 0.45, fragWaterDepth);\n"
	"        float riffleTurb = (riffleMask > 0.001) ? smoothstep(0.52, 0.82, chaff_grad_noise2d(surfWorldPos * 0.42 + vec2(mod(uAnimTime * 1.8, 256.0), -mod(uAnimTime * 1.4, 256.0)), 0x52494646u).x) : 0.0;\n"
	"        float riverRiffleFoam = riffleMask * riffleTurb * 0.34;\n"
	"        float foam = clamp(((crestLap + whitecap + swashState.z + riverRiffleFoam) * foamDistFade + lambWake.w) * (1.0 - freezeFactor), 0.0, 0.55);\n"
	"        vec3 foamColor = vec3(0.92, 0.96, 1.0);\n"
	"        float shallowGlassMask = smoothstep(0.5, 4.0, fragWaterDepth);\n"
	"        float skyReflWeight = fresnel * (0.15 + 0.85 * shallowGlassMask);\n"
	"        float sssWeight     = crestGlow * 0.16;\n"
	"        float specWeight    = clamp(specSun, 0.0, 0.92);\n"
	"        float foamWeight    = foam;\n"
	"        float totalWeight   = skyReflWeight + sssWeight + specWeight + foamWeight;\n"
	"        vec3 reflSkyCol = mix(skySurf * 0.92, skySurf * 1.08, clamp(0.5 + facetTilt * 2.2, 0.0, 1.0));\n"
	"        /* Reflected clouds: intersect the wave-perturbed reflection ray with the cloud slice and read the shared cloud-shadow map there\n"
	"         * (one fetch); without a map (orbit) fall back to the column density above the pixel */\n"
	"        float reflCloudDens = rawCloudShadow;\n"
	"        if (uCsm.z > 0.0 && vDistance < 14000.0) {\n"
	"            vec3 waveRefl = reflect(-viewDir, waveNormal);\n"
	"            float reflUp = max(0.30, dot(waveRefl, torusN));\n"
	"            vec2 reflHoriz = vec2(dot(waveRefl, tangentU), dot(waveRefl, tangentV));\n"
	"            float csmRefl = smoothstep(0.05, 0.66, nd_csm_density(surfWorldPos + reflHoriz * ((920.0 - liveSeaLevel) / reflUp)));\n"
	"            reflCloudDens = mix(csmRefl, rawCloudShadow, smoothstep(9000.0, 14000.0, vDistance));\n"
	"        }\n"
	"        vec3 reflCloudCol = mix(vec3(0.76, 0.82, 0.90), vec3(0.98, 0.97, 0.95), clamp(0.55 + facetTilt * 1.4, 0.0, 1.0));\n"
	"        reflSkyCol = mix(reflSkyCol * nd_amb_trans(cloudShadow), reflCloudCol, clamp(reflCloudDens * 0.85, 0.0, 0.88));\n"
	"        vec3 surfaceNum = reflSkyCol * skyReflWeight + sssTint + sunColor * specSun + foamColor * foamWeight;\n"
	"        vec3 surfaceRgb = (totalWeight > 1e-4) ? clamp(surfaceNum / max(totalWeight, 1e-4), 0.0, 1.0) : reflSkyCol;\n"
	"        if (uLambLight.w > 0.001) {\n"
	"            vec2 dXY = uLambLight.xy - surfWorldPos;\n"
	"            float d2 = dot(dXY, dXY);\n"
	"            float atten = (1.0 / (1.0 + d2 * 0.00055)) * smoothstep(220.0 * 220.0, 28.0 * 28.0, d2);\n"
	"            surfaceRgb += vec3(1.06, 0.99, 0.84) * (uLambLight.w * 0.16 * atten);\n"
	"        }\n"
	"        /* Procedural frozen ice sheet structure (crystals, micro-fractures, pack-ice crevasses) */\n"
	"        if (freezeFactor > 0.001) {\n"
	"            vec3 iceNoise = chaff_grad_noise2d(vWorldPos * 0.05, 0x1CE54E5Eu);\n"
	"            vec3 iceDetail = chaff_grad_noise2d(vWorldPos * 0.28, 0x1CE07E17u);\n"
	"            float icePebble = (iceNoise.x - 0.5) * 0.16 + (iceDetail.x - 0.5) * 0.08;\n"
	"            float iceCracks = smoothstep(0.47, 0.52, abs(iceNoise.x - 0.5)) * 0.22;\n"
	"            vec3 iceAlbedo = mix(vec3(0.68, 0.86, 0.96), vec3(0.96, 0.98, 1.00), clamp(0.52 + icePebble + iceCracks, 0.0, 1.0));\n"
	"            vec3 iceLit = iceAlbedo * effLighting;\n"
	"            waterBody = mix(waterBody, iceLit, freezeFactor * 0.95);\n"
	"            surfaceRgb = mix(surfaceRgb, iceLit, freezeFactor * 0.96);\n"
	"        }\n"
	"        float surfEdgeFade = smoothstep(0.0, 0.18, fragWaterDepth);\n"
	"        float liquidAlpha = clamp(1.0 - (1.0 - skyReflWeight) * (1.0 - sssWeight) * (1.0 - specWeight) * (1.0 - foamWeight), 0.0, 0.94) * surfEdgeFade;\n"
	"        float iceAlpha = clamp(0.88 + 0.10 * smoothstep(0.2, 3.0, fragWaterDepth), 0.0, 0.98);\n"
	"        float surfAlpha = mix(liquidAlpha, iceAlpha, freezeFactor);\n"
	"        vec3 finalWater = mix(waterBody, surfaceRgb, surfAlpha) * (0.16 + 0.84 * dayFactor);\n"
	"        lit = mix(lit, finalWater, wetBlend);\n"
	"    }\n"
	"    float fogFactor = clamp((vDistance - uFogStart) / max(1.0, uFogEnd - uFogStart), 0.0, 1.0);\n"
	"    vec3 sky = ((length(uSkyColor) > 0.001) ? uSkyColor : vec3(0.55, 0.70, 0.85)) * (0.18 + 0.82 * dayFactor);\n"
	"    vec3 dustHaze = vec3(0.84, 0.76, 0.60) * (0.28 + 0.72 * dayFactor);\n"
	"    vec3 atmoSky = mix(sky, dustHaze, wDesert * 0.42 * (1.0 - wPolar));\n"
	"    vec3 viewDirNorm = normalize(vViewDir);\n"
	"    float sunAlign = max(0.0, dot(viewDirNorm, -sDir));\n"
	"    float antiSunAlign = max(0.0, dot(viewDirNorm, sDir));\n"
	"    vec3 twilightGlow = mix(vec3(0.98, 0.54, 0.28), vec3(0.82, 0.36, 0.48), smoothstep(0.12, -0.08, effSun));\n"
	"    vec3 twilightSky = mix(twilightGlow * 0.78, twilightGlow * 1.30, pow(sunAlign, 3.0));\n"
	"    /* Ozone Chappuis band absorption: pink Belt of Venus arch above dark blue earth shadow on antisolar horizon */\n"
	"    vec3 beltOfVenus = mix(vec3(0.24, 0.32, 0.52), vec3(0.92, 0.46, 0.62), smoothstep(0.02, 0.35, max(0.0, dot(torusN, viewDirNorm))));\n"
	"    twilightSky = mix(twilightSky, beltOfVenus, pow(antiSunAlign, 2.5) * twilight);\n"
	"    atmoSky = mix(atmoSky, twilightSky, twilight * 0.76 * (1.0 - wPolar * 0.35));\n"
	"    float limb = pow(clamp(1.0 - max(0.0, dot(torusN, viewDirNorm)), 0.0, 1.0), 3.0);\n"
	"    vec3 limbColor = mix(vec3(0.35, 0.65, 0.95), vec3(0.96, 0.54, 0.30), twilight * 0.85);\n"
	"    vec3 surfaceLit = mix(lit, limbColor * (0.15 + 0.85 * dayFactor), limb * 0.45 * uAtmoIntensity);\n"
	"    FragColor = vec4(mix(surfaceLit, atmoSky, fogFactor * uAtmoIntensity), 1.0);\n"
	"}\n";

static const char *s_vs_water =
	"#version 300 es\n"
	"precision highp float;\n"
	"precision highp int;\n"
	"precision highp isampler2D;\n"
	"precision highp sampler2D;\n"
	"layout (location = 0) in vec2 a_grid_uv;\n"
	"uniform mat4 uMVP;\n"
	"uniform isampler2D uHeightTex;\n"
	"uniform sampler2D uSubstrateTex;\n"
	"uniform vec2 uChunkOrigin;\n"
	"uniform vec2 uChunkRelOrigin;\n"
	"uniform vec2 uChunkSpan;\n"
	"uniform vec4 uRefTrig;\n"
	"uniform float uHeightScale;\n"
	"uniform float uSeaLevel;\n"
	"uniform vec3 uCameraPos;\n"
	"uniform vec3 uPlanetCenter;\n"
	"uniform mat4 uBodyRot;\n"
	"uniform vec2 uTorusRadii;\n"
	"uniform vec2 uWorldDim;\n"
	"out vec2 vUV;\n"
	"out float vWaterDepth;\n"
	"out float vDistance;\n"
	"out vec2 vWorldPos;\n"
	"out vec3 vViewDir;\n"
	"out vec3 vTorusNormal;\n"
	"out vec3 vTangentU;\n"
	"out vec3 vTangentV;\n"
	"float getH(ivec2 p, ivec2 maxC) {\n"
	"    ivec2 c = clamp(p, ivec2(0), maxC);\n"
	"    float hc = float(texelFetch(uHeightTex, c, 0).r);\n"
	"    if (c == p) return hc;\n"
	"    ivec2 cIn = clamp(c + sign(c - p), ivec2(0), maxC);\n"
	"    float hIn = float(texelFetch(uHeightTex, cIn, 0).r);\n"
	"    return 2.0 * hc - hIn;\n"
	"}\n"
	"float sampleHeightSmooth(vec2 uv, out float minNeighborH) {\n"
	"    vec2 texSize = vec2(textureSize(uHeightTex, 0));\n"
	"    vec2 maxCoordF = max(vec2(1.0), texSize - 1.0);\n"
	"    vec2 uvc = clamp(uv, 0.0, 1.0);\n"
	"    vec2 pixelCoord = uvc * maxCoordF;\n"
	"    ivec2 iCoord = ivec2(floor(pixelCoord));\n"
	"    vec2 f = pixelCoord - vec2(iCoord);\n"
	"    ivec2 maxCoord = ivec2(maxCoordF);\n"
	"    vec4 wx = vec4(\n"
	"        f.x * (-0.5 + f.x * (1.0 - 0.5 * f.x)),\n"
	"        1.0 + f.x * f.x * (-2.5 + 1.5 * f.x),\n"
	"        f.x * (0.5 + f.x * (2.0 - 1.5 * f.x)),\n"
	"        f.x * f.x * (-0.5 + 0.5 * f.x)\n"
	"    );\n"
	"    vec4 wy = vec4(\n"
	"        f.y * (-0.5 + f.y * (1.0 - 0.5 * f.y)),\n"
	"        1.0 + f.y * f.y * (-2.5 + 1.5 * f.y),\n"
	"        f.y * (0.5 + f.y * (2.0 - 1.5 * f.y)),\n"
	"        f.y * f.y * (-0.5 + 0.5 * f.y)\n"
	"    );\n"
	"    float hBicubic = 0.0;\n"
	"    float minH = 32767.0;\n"
	"    for (int j = 0; j < 4; j++) {\n"
	"        float rowH = 0.0;\n"
	"        int py = iCoord.y + j - 1;\n"
	"        for (int i = 0; i < 4; i++) {\n"
	"            int px = iCoord.x + i - 1;\n"
	"            float val = getH(ivec2(px, py), maxCoord);\n"
	"            rowH += val * wx[i];\n"
	"            minH = min(minH, val);\n"
	"        }\n"
	"        hBicubic += rowH * wy[j];\n"
	"    }\n"
	"    float borderDist = min(min(pixelCoord.x, maxCoordF.x - pixelCoord.x), min(pixelCoord.y, maxCoordF.y - pixelCoord.y));\n"
	"    float borderBlend = clamp(borderDist, 0.0, 1.0);\n"
	"    float h00 = getH(iCoord, maxCoord);\n"
	"    float h10 = getH(iCoord + ivec2(1, 0), maxCoord);\n"
	"    float h01 = getH(iCoord + ivec2(0, 1), maxCoord);\n"
	"    float h11 = getH(iCoord + ivec2(1, 1), maxCoord);\n"
	"    float hLinear = mix(mix(h00, h10, f.x), mix(h01, h11, f.x), f.y);\n"
	"    minNeighborH = minH;\n"
	"    return mix(hLinear, hBicubic, borderBlend);\n"
	"}\n"
	"float eval_river_surface_vs(vec2 uv, float baseSeaLevel, float effectiveScale) {\n"
	"    vec2 texSize = vec2(textureSize(uSubstrateTex, 0));\n"
	"    vec2 maxCoordF = max(vec2(1.0), texSize - 1.0);\n"
	"    vec2 p = clamp(uv, 0.0, 1.0) * maxCoordF;\n"
	"    ivec2 ip = ivec2(floor(p + 0.5));\n"
	"    ivec2 maxC = ivec2(maxCoordF);\n"
	"    float sumW = 0.0;\n"
	"    float sumZ = 0.0;\n"
	"    for (int j = -1; j <= 1; j++) {\n"
	"        for (int i = -1; i <= 1; i++) {\n"
	"            ivec2 tc = clamp(ip + ivec2(i, j), ivec2(0), maxC);\n"
	"            float c_i = (1.0 - texelFetch(uSubstrateTex, tc, 0).a) * 15.9375;\n"
	"            if (c_i > 0.05) {\n"
	"                float h_i = float(texelFetch(uHeightTex, tc, 0).r) * effectiveScale;\n"
	"                float w = (i == 0 && j == 0) ? (c_i * 2.0) : c_i;\n"
	"                sumW += w;\n"
	"                sumZ += w * max(baseSeaLevel, h_i + c_i - 3.0);\n"
	"            }\n"
	"        }\n"
	"    }\n"
	"    return (sumW > 1e-4) ? (sumZ / sumW) : baseSeaLevel;\n"
	"}\n"
	"void main() {\n"
	"    vUV = a_grid_uv;\n"
	"    float scale = (uHeightScale != 0.0) ? uHeightScale : 1.0;\n"
	"    float effectiveScale = scale * 0.0625;\n"
	"    float minNeighborRaw = 0.0;\n"
	"    float raw_h = sampleHeightSmooth(a_grid_uv, minNeighborRaw);\n"
	"    float wz = raw_h * effectiveScale;\n"
	"    float minNeighborWz = minNeighborRaw * effectiveScale;\n"
	"    float riverCarve = (1.0 - texture(uSubstrateTex, a_grid_uv).a) * 15.9375;\n"
	"    float effSeaLevel = eval_river_surface_vs(a_grid_uv, uSeaLevel, effectiveScale);\n"
	"    float depth = effSeaLevel - wz;\n"
	"    vWaterDepth = depth;\n"
	"    vec2 worldXY = vec2(uChunkOrigin.x + a_grid_uv.x * uChunkSpan.x, uChunkOrigin.y + a_grid_uv.y * uChunkSpan.y);\n"
	"    vec2 relXY = vec2(uChunkRelOrigin.x + a_grid_uv.x * uChunkSpan.x, uChunkRelOrigin.y + a_grid_uv.y * uChunkSpan.y);\n"
	"    const float TWO_PI = 6.28318530718;\n"
	"    float R = (uTorusRadii.x > 0.0) ? uTorusRadii.x : 2000000.0;\n"
	"    float r0 = (uTorusRadii.y > 0.0) ? uTorusRadii.y : 1335000.0;\n"
	"    vec2 wdim = (uWorldDim.x > 0.0 && uWorldDim.y > 0.0) ? uWorldDim : vec2(10485760.0, 8388608.0);\n"
	"    float du = TWO_PI * (relXY.x / wdim.x);\n"
	"    float dv = TWO_PI * (relXY.y / wdim.y);\n"
	"    float sdu = sin(du), shdu = sin(0.5 * du);\n"
	"    float sdv = sin(dv), shdv = sin(0.5 * dv);\n"
	"    float cu0 = uRefTrig.x, su0 = uRefTrig.y, cv0 = uRefTrig.z, sv0 = uRefTrig.w;\n"
	"    float dcu = -2.0 * shdu * shdu * cu0 - sdu * su0;\n"
	"    float dsu = -2.0 * shdu * shdu * su0 + sdu * cu0;\n"
	"    float dcv = -2.0 * shdv * shdv * cv0 - sdv * sv0;\n"
	"    float dsv = -2.0 * shdv * shdv * sv0 + sdv * cv0;\n"
	"    float cu = cu0 + dcu, su = su0 + dsu;\n"
	"    float cv = cv0 + dcv, sv = sv0 + dsv;\n"
	"    /* Raise effWz above max tidal/swash water level near shore to prevent linearly interpolated terrain triangles from depth-clipping per-fragment water */\n"
	"    /* Exact sea-level plane: the water pass depth-tests the true surface against sprite depth */\n"
	"    float effWz = effSeaLevel;\n"
	"    float Rv0 = R + r0 * cv0;\n"
	"    float dRv = r0 * dcv + effWz * cv;\n"
	"    mat3 bodyR = mat3(uBodyRot);\n"
	"    vec3 worldPos = uPlanetCenter + bodyR * vec3(Rv0 * dcu + dRv * cu, Rv0 * dsu + dRv * su, r0 * dsv + effWz * sv);\n"
	"    vTorusNormal = bodyR * vec3(cv * cu, cv * su, sv);\n"
	"    vTangentU = bodyR * vec3(-su, cu, 0.0);\n"
	"    vTangentV = bodyR * vec3(-sv * cu, -sv * su, cv);\n"
	"    vWorldPos = worldXY;\n"
	"    vDistance = length(worldPos - uCameraPos);\n"
	"    vViewDir = uCameraPos - worldPos;\n"
	"    gl_Position = uMVP * vec4(worldPos, 1.0);\n"
	"}\n";

static const char *s_fs_water =
	"#version 300 es\n"
	"precision highp float;\n"
	"precision highp int;\n"
	"precision highp isampler2D;\n"
	"precision highp sampler2D;\n"
	"in vec2 vUV;\n"
	"in float vWaterDepth;\n"
	"in float vDistance;\n"
	"in vec2 vWorldPos;\n"
	"in vec3 vViewDir;\n"
	"in vec3 vTorusNormal;\n"
	"in vec3 vTangentU;\n"
	"in vec3 vTangentV;\n"
	"uniform isampler2D uHeightTex;\n"
	"uniform sampler2D uSubstrateTex;\n"
	"uniform vec2 uChunkSpan;\n"
	"uniform float uHeightScale;\n"
	"uniform float uSeaLevel;\n"
	"uniform float uBaseSeaLevel;\n"
	"uniform float uFogStart;\n"
	"uniform float uFogEnd;\n"
	"uniform float uAnimTime;\n"
	"uniform vec3 uSkyColor;\n"
	"uniform float uAtmoIntensity;\n"
	"uniform vec4 uLambLight;\n"
	"uniform vec3 uSunDir;\n"
	"uniform vec3 uMoonDir;\n"
	"uniform vec4 uLambWakeHead;\n"
	"uniform mat4 uLambWakeXYT0;\n"
	"uniform mat4 uLambWakeXYT1;\n"
	"uniform mat4 uLambWakeVel0;\n"
	"uniform mat4 uLambWakeVel1;\n"
	"uniform sampler2D uSceneColor;\n"
	"uniform sampler2D uSceneDepth;\n"
	"uniform vec2 uViewport;\n"
	"uniform vec2 uNearFar;\n"
	"uniform vec2 uProjScale;\n"
	"uniform vec3 uCameraPos;\n"
	"uniform vec3 uPlanetCenter;\n"
	"uniform vec2 uTorusRadii;\n"
	"out vec4 FragColor;\n"
	"float getH(ivec2 p, ivec2 maxC) {\n"
	"    ivec2 c = clamp(p, ivec2(0), maxC);\n"
	"    float hc = float(texelFetch(uHeightTex, c, 0).r);\n"
	"    if (c == p) return hc;\n"
	"    ivec2 cIn = clamp(c + sign(c - p), ivec2(0), maxC);\n"
	"    float hIn = float(texelFetch(uHeightTex, cIn, 0).r);\n"
	"    return 2.0 * hc - hIn;\n"
	"}\n"
	"/* 4x4 Catmull-Rom bicubic spline: smooth non-stair-stepped C1 height field & exact analytic gradients */\n"
	"float sampleHeightSmooth(vec2 uv, out vec2 out_grad) {\n"
	"    vec2 texSize = vec2(textureSize(uHeightTex, 0));\n"
	"    vec2 maxCoordF = max(vec2(1.0), texSize - 1.0);\n"
	"    vec2 uvc = clamp(uv, 0.0, 1.0);\n"
	"    vec2 pixelCoord = uvc * maxCoordF;\n"
	"    ivec2 iCoord = ivec2(floor(pixelCoord));\n"
	"    vec2 f = pixelCoord - vec2(iCoord);\n"
	"    ivec2 maxCoord = ivec2(maxCoordF);\n"
	"    vec4 wx = vec4(\n"
	"        f.x * (-0.5 + f.x * (1.0 - 0.5 * f.x)),\n"
	"        1.0 + f.x * f.x * (-2.5 + 1.5 * f.x),\n"
	"        f.x * (0.5 + f.x * (2.0 - 1.5 * f.x)),\n"
	"        f.x * f.x * (-0.5 + 0.5 * f.x)\n"
	"    );\n"
	"    vec4 wy = vec4(\n"
	"        f.y * (-0.5 + f.y * (1.0 - 0.5 * f.y)),\n"
	"        1.0 + f.y * f.y * (-2.5 + 1.5 * f.y),\n"
	"        f.y * (0.5 + f.y * (2.0 - 1.5 * f.y)),\n"
	"        f.y * f.y * (-0.5 + 0.5 * f.y)\n"
	"    );\n"
	"    vec4 dwx = vec4(\n"
	"        -0.5 + f.x * (2.0 - 1.5 * f.x),\n"
	"        f.x * (-5.0 + 4.5 * f.x),\n"
	"        0.5 + f.x * (4.0 - 4.5 * f.x),\n"
	"        f.x * (-1.0 + 1.5 * f.x)\n"
	"    );\n"
	"    vec4 dwy = vec4(\n"
	"        -0.5 + f.y * (2.0 - 1.5 * f.y),\n"
	"        f.y * (-5.0 + 4.5 * f.y),\n"
	"        0.5 + f.y * (4.0 - 4.5 * f.y),\n"
	"        f.y * (-1.0 + 1.5 * f.y)\n"
	"    );\n"
	"    float hBicubic = 0.0;\n"
	"    vec2 gradBicubic = vec2(0.0);\n"
	"    for (int j = 0; j < 4; j++) {\n"
	"        float rowH = 0.0;\n"
	"        float rowDx = 0.0;\n"
	"        int py = iCoord.y + j - 1;\n"
	"        for (int i = 0; i < 4; i++) {\n"
	"            int px = iCoord.x + i - 1;\n"
	"            float val = getH(ivec2(px, py), maxCoord);\n"
	"            rowH += val * wx[i];\n"
	"            rowDx += val * dwx[i];\n"
	"        }\n"
	"        hBicubic += rowH * wy[j];\n"
	"        gradBicubic.x += rowDx * wy[j];\n"
	"        gradBicubic.y += rowH * dwy[j];\n"
	"    }\n"
	"    float borderDist = min(min(pixelCoord.x, maxCoordF.x - pixelCoord.x), min(pixelCoord.y, maxCoordF.y - pixelCoord.y));\n"
	"    float borderBlend = clamp(borderDist, 0.0, 1.0);\n"
	"    float h00 = getH(iCoord, maxCoord);\n"
	"    float h10 = getH(iCoord + ivec2(1, 0), maxCoord);\n"
	"    float h01 = getH(iCoord + ivec2(0, 1), maxCoord);\n"
	"    float h11 = getH(iCoord + ivec2(1, 1), maxCoord);\n"
	"    float hLinear = mix(mix(h00, h10, f.x), mix(h01, h11, f.x), f.y);\n"
	"    vec2 gradLinear = vec2(mix(h10 - h00, h11 - h01, f.y), mix(h01 - h00, h11 - h10, f.x));\n"
	"    float finalH = mix(hLinear, hBicubic, borderBlend);\n"
	"    vec2 finalGrad = mix(gradLinear, gradBicubic, borderBlend);\n"
	"    out_grad.x = finalGrad.x * (maxCoordF.x / max(1.0, uChunkSpan.x));\n"
	"    out_grad.y = finalGrad.y * (maxCoordF.y / max(1.0, uChunkSpan.y));\n"
	"    return finalH;\n"
	"}\n"
	CHAFF_GLSL_HASH_FN
	CHAFF_GLSL_GRAD_NOISE_FN
	CHAFF_GLSL_FLOW_FN
	ND_GLSL_SYNOPTIC_CLOUD_FN
	ND_GLSL_TORUS_ECLIPSE_FN
	"vec4 eval_recursive_wind(vec2 worldPos, float anim_time) {\n"
	"    return chaff_eval_flow_2d(worldPos, anim_time);\n"
	"}\n"
	ND_GLSL_SCENE_LIGHT
	ND_GLSL_CLOUD_SHADOW_FN
	ND_GLSL_CSM_LOOKUP_FN
	ND_GLSL_CLOUD_SHADOW_ENTRY_FN
	"vec3 eval_shore_micro_relief(vec2 worldPos, float depth, float fw) {\n"
	"    float env = smoothstep(3.2, 0.25, abs(depth));\n"
	"    if (env <= 0.001 || fw > 16.0) return vec3(0.0);\n"
	"    float wNyq1 = smoothstep(0.55, 0.18, 0.18 * fw);\n"
	"    float wNyq2 = smoothstep(0.55, 0.18, 0.52 * fw);\n"
	"    vec3 g1 = (wNyq1 > 0.001) ? chaff_grad_noise2d(worldPos * 0.18, 0xA341316Cu) : vec3(0.5, 0.0, 0.0);\n"
	"    vec3 g2 = (wNyq2 > 0.001) ? chaff_grad_noise2d(worldPos * 0.52 + vec2(17.3, -29.1), 0xC8013EA4u) : vec3(0.5, 0.0, 0.0);\n"
	"    float disp = ((g1.x - 0.5) * 0.38 * wNyq1 + (g2.x - 0.5) * 0.16 * wNyq2) * env;\n"
	"    vec2 gTotal = (g1.yz * (0.18 * 0.38 * wNyq1) + g2.yz * (0.52 * 0.16 * wNyq2)) * env;\n"
	"    return vec3(disp, gTotal.x, gTotal.y);\n"
	"}\n"

	"/* Height-model basin & upwind fetch classifier: distinguishes quiet lakes/sheltered coves from deep open sea\n"
	" * using purely local continuous depth and shoreline slope (zero fixed chunk-UV probes, zero chunk seams) */\n"
	"vec2 eval_water_fetch_and_basin(vec2 uv, vec2 windDir, vec2 shoreGrad, float fragWaterDepth, float effectiveScale) {\n"
	"    float slopeMag = length(shoreGrad);\n"
	"    float windAlign = (slopeMag > 1e-4) ? dot(-shoreGrad / slopeMag, windDir) : 0.0;\n"
	"    float d_up1 = max(0.0, fragWaterDepth - max(0.0, windAlign) * slopeMag * 12.0);\n"
	"    float d_up2 = max(0.0, fragWaterDepth - max(0.0, windAlign) * slopeMag * 28.0);\n"
	"    float d_off = fragWaterDepth + slopeMag * 18.0;\n"
	"    float openSeaFactor = smoothstep(8.0, 22.0, d_off) * smoothstep(2.5, 12.0, fragWaterDepth);\n"
	"    float windFetch = clamp(0.25 + 0.40 * smoothstep(0.4, 5.0, d_up1) + 0.35 * smoothstep(1.2, 12.0, d_up2), 0.25, 1.0);\n"
	"    return vec2(openSeaFactor, windFetch);\n"
	"}\n"
	"vec4 eval_zoom_water_waves(vec2 pos, float anim_time, float dist, float fw, float openSeaFactor) {\n"
	"    if (dist >= 65000.0) return vec4(0.0);\n"
	"    vec4 wind = eval_recursive_wind(pos, anim_time);\n"
	"    vec2 wDir = wind.xy;\n"
	"    vec2 wPerp = vec2(-wDir.y, wDir.x);\n"
	"    float gust = wind.z;\n"
	"    const float freqs[6] = float[6](0.024, 0.072, 0.220, 0.680, 2.150, 6.800);\n"
	"    const float amps[6]  = float[6](1.450, 0.680, 0.360, 0.160, 0.055, 0.020);\n"
	"    const float spds[6]  = float[6](0.180, 0.340, 0.620, 1.050, 1.750, 2.800);\n"
	"    const float dFar[6]  = float[6](65000.0, 18000.0, 4800.0, 1600.0, 520.0, 200.0);\n"
	"    const float dNear[6] = float[6](14000.0,  3600.0, 1100.0,  360.0, 130.0,  42.0);\n"
	"    const uint  seeds[6] = uint[6](0x4F434530u, 0x5357454Cu, 0x57415645u, 0x52495050u, 0x46494E45u, 0x53505241u);\n"
	"    const vec2  dirs[6]  = vec2[6](\n"
	"        vec2( 0.8575, -0.5145),\n"
	"        vec2( 0.6402, -0.7682),\n"
	"        vec2( 0.9285, -0.3714),\n"
	"        vec2( 0.5145, -0.8575),\n"
	"        vec2( 0.7809, -0.6247),\n"
	"        vec2( 0.3714, -0.9285)\n"
	"    );\n"
	"    const mat2  rot  = mat2(0.8, -0.6, 0.6, 0.8);\n"
	"    const mat2  irot = mat2(0.8,  0.6, -0.6, 0.8);\n"
	"    float h = 0.0;\n"
	"    vec2 grad = vec2(0.0);\n"
	"    float crest = 0.0;\n"
	"    vec2 pRot = pos;\n"
	"    mat2 curInvRot = mat2(1.0, 0.0, 0.0, 1.0);\n"
	"    for (int i = 0; i < 6; i++) {\n"
	"        float wDist = clamp((dFar[i] - dist) / max(1.0, dFar[i] - dNear[i]), 0.0, 1.0);\n"
	"        float wNyq  = smoothstep(0.55, 0.16, freqs[i] * fw);\n"
	"        float gustScale = (i >= 3) ? (0.75 + 0.45 * gust) : (0.85 + 0.20 * gust);\n"
	"        float basinGate = (i < 2) ? smoothstep(0.12, 0.55, openSeaFactor) : 1.0;\n"
	"        float w = wDist * wNyq * gustScale * basinGate;\n"
	"        if (w <= 0.001) { if (i < 2) continue; break; }\n"
	"        float phase = mod(anim_time * spds[i], 1024.0);\n"
	"        vec2 windWarp = (wDir * (gust - 0.65) + wPerp * (wind.w - 0.5)) * 0.45;\n"
	"        vec2 q = pRot * freqs[i] + windWarp - dirs[i] * phase;\n"
	"        vec3 g = chaff_grad_noise2d(q, seeds[i]);\n"
	"        float signedVal = g.x - 0.5;\n"
	"        float ridge = 0.5 - abs(signedVal);\n"
	"        float shaped = mix(signedVal, ridge - 0.18, 0.35);\n"
	"        float dShape = 0.85 + 0.45 * ridge;\n"
	"        vec2 worldG = curInvRot * g.yz;\n"
	"        h += shaped * amps[i] * w;\n"
	"        grad += worldG * (dShape * freqs[i] * amps[i] * w);\n"
	"        crest += max(0.0, ridge - 0.22) * (w * ((i <= 2) ? 1.4 : 0.8));\n"
	"        pRot += g.yz * (0.11 * w);\n"
	"        pRot = rot * pRot;\n"
	"        curInvRot = curInvRot * irot;\n"
	"    }\n"
	"    return vec4(h, grad.x, grad.y, clamp(crest, 0.0, 1.0));\n"
	"}\n"	"vec2 eval_wind_capillary_facets(vec2 pos, float anim_time, vec4 wind, float dist, float fw) {\n"
	"    float distVis = clamp(1.0 - (dist - 550.0) / 2200.0, 0.0, 1.0);\n"
	"    if (distVis <= 0.001) return vec2(0.0);\n"
	"    vec2 wDir = wind.xy;\n"
	"    vec2 wPerp = vec2(-wDir.y, wDir.x);\n"
	"    const vec2 prevW = vec2(0.741, -0.671);\n"
	"    const vec2 prevP = vec2(0.671,  0.741);\n"
	"    float rippleEnv = (0.45 + 0.50 * wind.w) * distVis;\n"
	"    float f1 = 0.55;\n"
	"    float f2 = 1.25;\n"
	"    float nyq1 = smoothstep(0.55, 0.14, f1 * fw);\n"
	"    float nyq2 = smoothstep(0.55, 0.14, f2 * fw);\n"
	"    float t1 = mod(anim_time * 1.15, 512.0);\n"
	"    float t2 = mod(anim_time * 1.65, 512.0);\n"
	"    /* Steady coordinate projection + bounded spatial wind-gust warp so velocity never grows with anim_time */\n"
	"    vec2 gustShift = wDir * (wind.z * 0.55) + wPerp * (wind.w * 0.35);\n"
	"    vec2 wCoord1 = vec2(dot(pos, prevW) * 1.45, dot(pos, prevP) * 0.78) * f1 + gustShift - vec2(t1, 0.18 * t1);\n"
	"    vec2 wCoord2 = vec2(dot(pos, prevW) * 1.35 + dot(pos, prevP) * 0.35, dot(pos, prevP) * 0.85 - dot(pos, prevW) * 0.25) * f2 + gustShift * 1.4 - vec2(t2, -0.22 * t2);\n"
	"    vec3 c1 = (nyq1 > 0.001) ? chaff_grad_noise2d(wCoord1, 0xCA9111A1u) : vec3(0.5, 0.0, 0.0);\n"
	"    vec3 c2 = (nyq2 > 0.001) ? chaff_grad_noise2d(wCoord2 + c1.yz * 0.25, 0xCA9111A2u) : vec3(0.5, 0.0, 0.0);\n"
	"    vec2 localG = c1.yz * (0.09 * nyq1) + c2.yz * (0.06 * nyq2);\n"
	"    vec2 worldCapGrad = (wDir * localG.x * 1.25 + wPerp * localG.y * 0.75) * rippleEnv;\n"
	"    return worldCapGrad;\n"
	"}\n"
	"float nd_octave_weight(float wavelength, float fw) {\n"
	"    return smoothstep(0.92, 0.28, fw / max(1e-4, wavelength));\n"
	"}\n"
	"float eval_astronomical_tide(vec3 torusN, float openSeaFactor, float localSeaLevel) {\n"
	"    if (localSeaLevel <= -900.0) return 0.0;\n"
	"    float isOcean = smoothstep(0.20, 0.65, openSeaFactor) * (1.0 - smoothstep(0.25, 0.75, abs(localSeaLevel - uBaseSeaLevel)));\n"
	"    if (isOcean <= 0.001) return 0.0;\n"
	"    vec3 mDir = (length(uMoonDir) > 0.001) ? normalize(uMoonDir) : vec3(1.0, 0.0, 0.0);\n"
	"    vec3 sDir = (length(uSunDir)  > 0.001) ? normalize(uSunDir)  : vec3(0.0, 1.0, 0.0);\n"
	"    float cosMoon = dot(torusN, mDir);\n"
	"    float cosSun  = dot(torusN, sDir);\n"
	"    float p2Moon  = 0.5 * (3.0 * cosMoon * cosMoon - 1.0);\n"
	"    float p2Sun   = 0.5 * (3.0 * cosSun  * cosSun  - 1.0);\n"
	"    return isOcean * (0.68 * p2Moon + 0.31 * p2Sun);\n"
	"}\n"
	"vec2 eval_shore_wave_swell(vec2 pos, float anim_time, float dist, float fw) {\n"
	"    vec4 wind = eval_recursive_wind(pos, anim_time);\n"
	"    vec2 wDir = wind.xy;\n"
	"    vec2 wPerp = vec2(-wDir.y, wDir.x);\n"
	"    float gust = wind.z;\n"
	"    const float freqs[3] = float[3](0.024, 0.072, 0.220);\n"
	"    const float amps[3]  = float[3](1.450, 0.680, 0.280);\n"
	"    const float spds[3]  = float[3](0.180, 0.340, 0.620);\n"
	"    const float dFar[3]  = float[3](65000.0, 18000.0, 4800.0);\n"
	"    const float dNear[3] = float[3](14000.0,  3600.0, 1100.0);\n"
	"    const uint  seeds[3] = uint[3](0x4F434530u, 0x5357454Cu, 0x57415645u);\n"
	"    const vec2  dirs[3]  = vec2[3](\n"
	"        vec2( 0.8575, -0.5145),\n"
	"        vec2( 0.6402, -0.7682),\n"
	"        vec2( 0.9285, -0.3714)\n"
	"    );\n"
	"    const mat2 rot = mat2(0.8, -0.6, 0.6, 0.8);\n"
	"    float h = 0.0;\n"
	"    float crest = 0.0;\n"
	"    vec2 pRot = pos;\n"
	"    for (int i = 0; i < 3; i++) {\n"
	"        float wDist = clamp((dFar[i] - dist) / max(1.0, dFar[i] - dNear[i]), (i == 0) ? 0.35 : 0.0, 1.0);\n"
	"        float wNyq  = smoothstep(0.55, 0.16, freqs[i] * fw);\n"
	"        float w = wDist * wNyq * (0.85 + 0.20 * gust);\n"
	"        if (w > 0.001) {\n"
	"            float phase = mod(anim_time * spds[i], 1024.0);\n"
	"            vec2 windWarp = (wDir * (gust - 0.65) + wPerp * (wind.w - 0.5)) * 0.45;\n"
	"            vec2 q = pRot * freqs[i] + windWarp - dirs[i] * phase;\n"
	"            vec3 g = chaff_grad_noise2d(q, seeds[i]);\n"
	"            float signedVal = g.x - 0.5;\n"
	"            float ridge = 0.5 - abs(signedVal);\n"
	"            float shaped = mix(signedVal, ridge - 0.18, 0.35);\n"
	"            h += shaped * amps[i] * w;\n"
	"            crest += max(0.0, ridge - 0.22) * (w * 1.4);\n"
	"            pRot += g.yz * (0.11 * w);\n"
	"        }\n"
	"        pRot = rot * pRot;\n"
	"    }\n"
	"    return vec2(h, clamp(crest, 0.0, 1.0));\n"
	"}\n"
	"vec4 eval_shore_swash(vec2 pos, float baseTideZ, float terrainZ, vec2 slopeGrad, float anim_time, float dist, float fw, float openSeaFactor, float localSeaLevel) {\n"
	"    float isOcean = smoothstep(0.20, 0.65, openSeaFactor) * (1.0 - smoothstep(0.25, 0.75, abs(localSeaLevel - uBaseSeaLevel)));\n"
	"    float slopeMag = length(slopeGrad);\n"
	"    float relZ = terrainZ - baseTideZ;\n"
	"    float nearShore = smoothstep(2.4, 0.0, max(0.0, relZ)) * smoothstep(4.5, 0.0, max(0.0, -relZ));\n"
	"    if (nearShore <= 0.001 || dist > 3800.0) {\n"
	"        float tideHigh = localSeaLevel + (0.15 + 1.05 * isOcean);\n"
	"        return vec4(baseTideZ, max(baseTideZ, tideHigh), 0.0, 0.0);\n"
	"    }\n"
	"    float distFade = smoothstep(3800.0, 450.0, dist);\n"
	"    float slopeResponse = (0.52 + 0.48 * smoothstep(0.005, 0.12, slopeMag)) * distFade;\n"
	"    float oceanSwell = 0.0;\n"
	"    float lakeLapping = 0.0;\n"
	"    if (isOcean > 0.01) {\n"
	"        vec3 nAlong = chaff_grad_noise2d(pos * 0.014 + vec2(13.7, -9.2), 0x53574153u);\n"
	"        float groupEnv = 0.76 + 0.24 * sin(mod(anim_time * 0.24, 628.3185) + (nAlong.x - 0.5) * 2.2);\n"
	"        float waveArg = mod(anim_time * 0.92, 628.3185) - relZ * 0.32 + (nAlong.x - 0.5) * 1.8;\n"
	"        float swellWave = sin(waveArg) + 0.22 * sin(2.0 * waveArg - 0.35);\n"
	"        vec2 shoreWaves = eval_shore_wave_swell(pos, anim_time, dist, fw);\n"
	"        float waveletRunup = shoreWaves.x * 0.86 * slopeResponse;\n"
	"        float swellRunup = swellWave * 0.58 * groupEnv * slopeResponse;\n"
	"        oceanSwell = (waveletRunup + swellRunup) * isOcean;\n"
	"    }\n"
	"    float lapPhase = sin(mod(anim_time * 2.1, 628.3185) + pos.x * 0.45 - pos.y * 0.38);\n"
	"    lakeLapping = lapPhase * 0.055 * slopeResponse * (1.0 - isOcean);\n"
	"    float swashDisp = (oceanSwell + lakeLapping) * nearShore;\n"
	"    float swashSurfZ = baseTideZ + swashDisp;\n"
	"    float recentSwashTop = baseTideZ + (0.12 + 1.04 * isOcean) * slopeResponse;\n"
	"    float highWaterZ = mix(max(baseTideZ, localSeaLevel + 0.15 + 1.05 * isOcean), max(swashSurfZ, recentSwashTop), nearShore);\n"
	"    float edgeLip = smoothstep(0.35, 0.0, abs(terrainZ - swashSurfZ));\n"
	"    float bodyFoamScale = 0.15 + 0.85 * isOcean;\n"
	"    float swashFoam = clamp(edgeLip * 0.16 * bodyFoamScale * distFade, 0.0, 0.24);\n"
	"    float swashFilm = smoothstep(highWaterZ + 0.12, swashSurfZ - 0.04, terrainZ) * step(swashSurfZ, terrainZ) * distFade;\n"
	"    return vec4(swashSurfZ, highWaterZ, swashFoam, swashFilm);\n"
	"}\n"	"vec4 eval_lamb_wake_ribbon(vec2 pos, float anim_time, float dist, float fw) {\n"
	"    float headEnergy = uLambWakeHead.w;\n"
	"    if (headEnergy <= 0.001 || dist > 2600.0) return vec4(0.0);\n"
	"    vec2 dHead = pos - uLambWakeHead.xy;\n"
	"    if (dot(dHead, dHead) > 140.0 * 140.0) return vec4(0.0);\n"
	"    float distFade = smoothstep(2600.0, 450.0, dist) * nd_octave_weight(3.5, fw) * headEnergy;\n"
	"    if (distFade <= 0.001) return vec4(0.0);\n"
	"    vec4 headVel0 = uLambWakeVel0[0];\n"
	"    float headSpd = length(headVel0.xy);\n"
	"    vec2 headFwd = (headSpd > 0.5) ? (headVel0.xy / headSpd) : vec2(0.0, 1.0);\n"
	"    float totalH = 0.0;\n"
	"    vec2 totalGrad = vec2(0.0);\n"
	"    float totalFoam = 0.0;\n"
	"    vec3 wakeTurb = chaff_grad_noise2d(pos * 0.38 + vec2(mod(anim_time * 0.75, 256.0), -mod(anim_time * 0.55, 256.0)), 0x57414B45u);\n"
	"    vec2 warpOffset = wakeTurb.yz * 0.85;\n"
	"    float pathArc = 0.0;\n"
	"    for (int i = 0; i < 7; i++) {\n"
	"        vec4 nodeA = (i < 4) ? uLambWakeXYT0[i] : uLambWakeXYT1[i - 4];\n"
	"        vec4 velA  = (i < 4) ? uLambWakeVel0[i] : uLambWakeVel1[i - 4];\n"
	"        if (velA.z <= 0.002) continue;\n"
	"        int j = i + 1;\n"
	"        vec4 nodeB = (j < 4) ? uLambWakeXYT0[j] : uLambWakeXYT1[j - 4];\n"
	"        vec4 velB  = (j < 4) ? uLambWakeVel0[j] : uLambWakeVel1[j - 4];\n"
	"        float spdA = length(velA.xy);\n"
	"        vec2 fwdA = (spdA > 0.5) ? (velA.xy / spdA) : headFwd;\n"
	"        vec2 pA = nodeA.xy;\n"
	"        vec2 pB = (velB.z > 0.002) ? nodeB.xy : (pA - fwdA * 3.5);\n"
	"        float tB = (velB.z > 0.002) ? nodeB.z : (nodeA.z - 0.18);\n"
	"        vec2 seg = pA - pB;\n"
	"        float segLen = length(seg);\n"
	"        vec2 tDir = (segLen > 0.15) ? (seg / segLen) : fwdA;\n"
	"        vec2 nDir = vec2(-tDir.y, tDir.x);\n"
	"        float span = max(segLen, 0.25);\n"
	"        vec2 relB = pos - pB;\n"
	"        float proj = dot(relB, tDir);\n"
	"        float tSeg = clamp(proj / span, 0.0, 1.0);\n"
	"        vec2 closest = pB + tDir * (tSeg * segLen);\n"
	"        float segTime = mix(tB, nodeA.z, tSeg);\n"
	"        float age = clamp(anim_time - segTime, 0.0, 4.2);\n"
	"        if (age >= 4.15) {\n"
	"            pathArc += segLen;\n"
	"            continue;\n"
	"        }\n"
	"        float segEnergy = mix((velB.z > 0.002) ? velB.z : velA.z * 0.5, velA.z, tSeg);\n"
	"        float ageDecay = exp(-0.80 * age) * smoothstep(4.0, 0.15, age) * segEnergy;\n"
	"        float sOverA = max(0.0, proj - segLen);\n"
	"        float sOverB = max(0.0, -proj);\n"
	"        float headCapSigma = (i == 0) ? 1.85 : 2.60;\n"
	"        float tailCapSigma = 3.20 + age * 2.80;\n"
	"        float longWin = exp(-(sOverA * sOverA) / (headCapSigma * headCapSigma))\n"
	"                      * exp(-(sOverB * sOverB) / (tailCapSigma * tailCapSigma));\n"
	"        float jointWeight = (i == 0) ? smoothstep(-tailCapSigma, 0.0, proj)\n"
	"                                     : (smoothstep(-tailCapSigma, 0.0, proj) * smoothstep(segLen + headCapSigma, segLen, proj + 0.5 * headCapSigma));\n"
	"        float envBase = ageDecay * longWin * (0.45 + 0.55 * jointWeight);\n"
	"        float curArc = pathArc + (1.0 - tSeg) * segLen + sOverA;\n"
	"        pathArc += segLen;\n"
	"        if (envBase <= 0.002) continue;\n"
	"        vec2 delta = (pos + warpOffset * (0.16 + 0.14 * age)) - closest;\n"
	"        float rawU = dot(delta, nDir);\n"
	"        float u = abs(rawU);\n"
	"        float signU = (rawU >= 0.0) ? 1.0 : -1.0;\n"
	"        float wakeHalfW = 1.35 + curArc * 0.16 + age * 2.10;\n"
	"        float edgeSigma = 1.15 + age * 0.85;\n"
	"        float dEdge = (u - wakeHalfW) / edgeSigma;\n"
	"        float rimEnv = exp(-(dEdge * dEdge)) * exp(-curArc * 0.028) * envBase;\n"
	"        float spdScale = clamp(spdA / 36.0, 0.30, 1.15);\n"
	"        if (rimEnv > 0.002) {\n"
	"            float hRim = 0.20 * rimEnv * spdScale;\n"
	"            totalH += hRim;\n"
	"            float dRimDu = (-2.2 * dEdge / edgeSigma) * hRim;\n"
	"            totalGrad += (nDir * signU - tDir * 0.20) * dRimDu;\n"
	"            totalFoam += rimEnv * spdScale * exp(-age * 1.15) * (0.14 + 0.20 * wakeTurb.x);\n"
	"        }\n"
	"        float coreEnv = smoothstep(wakeHalfW + edgeSigma * 0.80, wakeHalfW * 0.15, u) * exp(-curArc * 0.035) * envBase;\n"
	"        if (coreEnv > 0.003) {\n"
	"            float eddy = coreEnv * spdScale;\n"
	"            float bodyFoamRamp = smoothstep(0.5, 4.5, curArc);\n"
	"            totalH -= 0.11 * eddy;\n"
	"            totalGrad += wakeTurb.yz * (0.42 * eddy) + nDir * (signU * 0.20 * eddy);\n"
	"            float bubbleMask = smoothstep(0.36, 0.74, wakeTurb.x) * exp(-age * 1.25);\n"
	"            totalFoam += eddy * (0.10 + bubbleMask * 0.34) * (0.35 + 0.65 * bodyFoamRamp);\n"
	"        }\n"
	"    }\n"
	"    return vec4(clamp(totalH, -0.42, 0.42) * distFade, totalGrad.x * distFade, totalGrad.y * distFade, clamp(totalFoam * distFade, 0.0, 0.55));\n"
	"}\n"
	"/* Linear eye depth of a window-space depth value, written to stay accurate for a 8e7 far plane */\n"
	"float nd_eye_depth(float d) {\n"
	"    return (uNearFar.x * uNearFar.y) / (uNearFar.y * (1.0 - d) + d * uNearFar.x);\n"
	"}\n"
	"/* True camera distance of the nearest opaque scene point under a screen UV. Sprite quads are pushed toward the camera by\n"
	" * min(24, 0.08 * dist) in s_vs_billboard to avoid terrain clipping: undo it so thickness through the water is exact. */\n"
	"float nd_object_dist(vec2 uv) {\n"
	"    float e = nd_eye_depth(texture(uSceneDepth, uv).r);\n"
	"    vec2 n = uv * 2.0 - 1.0;\n"
	"    float dist = e * length(vec3(n * uProjScale, 1.0));\n"
	"    return (dist < 276.0) ? dist / 0.92 : dist + 24.0;\n"
	"}\n"
	"float eval_river_surface_fs(vec2 uv, float baseSeaLevel, float effectiveScale, out float outRiverProx) {\n"
	"    vec2 texSize = vec2(textureSize(uSubstrateTex, 0));\n"
	"    vec2 maxCoordF = max(vec2(1.0), texSize - 1.0);\n"
	"    vec2 p = clamp(uv, 0.0, 1.0) * maxCoordF;\n"
	"    ivec2 ip = ivec2(floor(p));\n"
	"    vec2 fp = p - vec2(ip);\n"
	"    ivec2 maxC = ivec2(maxCoordF);\n"
	"    float sumW = 0.0;\n"
	"    float sumZ = 0.0;\n"
	"    float maxC_nb = 0.0;\n"
	"    for (int j = -1; j <= 2; j++) {\n"
	"        float wy = max(0.0, 1.5 - abs(float(j) - fp.y));\n"
	"        for (int i = -1; i <= 2; i++) {\n"
	"            ivec2 tc = clamp(ip + ivec2(i, j), ivec2(0), maxC);\n"
	"            float c_i = (1.0 - texelFetch(uSubstrateTex, tc, 0).a) * 15.9375;\n"
	"            if (c_i > 0.05) {\n"
	"                float wx = max(0.0, 1.5 - abs(float(i) - fp.x));\n"
	"                float w = wx * wy * c_i;\n"
	"                float h_i = float(texelFetch(uHeightTex, tc, 0).r) * effectiveScale;\n"
	"                sumW += w;\n"
	"                sumZ += w * max(baseSeaLevel, h_i + c_i - 3.0);\n"
	"                maxC_nb = max(maxC_nb, c_i * clamp(wx * wy * 0.75, 0.0, 1.0));\n"
	"            }\n"
	"        }\n"
	"    }\n"
	"    outRiverProx = smoothstep(0.02, 1.25, maxC_nb);\n"
	"    return (sumW > 1e-4) ? mix(baseSeaLevel, sumZ / sumW, smoothstep(0.0, 0.35, outRiverProx)) : baseSeaLevel;\n"
	"}\n"
	"void main() {\n"
	"    /* Only sprite pixels (stencil-marked) that lie BELOW the water surface reach this point */\n"
	"    vec2 sUV = gl_FragCoord.xy / uViewport;\n"
	"    float objPath1 = nd_object_dist(sUV) - vDistance;\n"
	"    if (objPath1 <= 0.02) discard;\n"
	"    float scale = (uHeightScale != 0.0) ? uHeightScale : 1.0;\n"
	"    float effectiveScale = scale * 0.0625;\n"
	"    vec2 shoreGrad = vec2(0.0);\n"
	"    float raw_h = sampleHeightSmooth(vUV, shoreGrad);\n"
	"    float fragZ = raw_h * effectiveScale;\n"
	"    shoreGrad *= effectiveScale;\n"
	"    float fw = clamp(max(length(dFdx(vWorldPos)), length(dFdy(vWorldPos))), 1e-4, max(uChunkSpan.x, uChunkSpan.y) * 0.25);\n"
	"    vec3 viewDir = normalize(vViewDir);\n"
	"    vec3 torusN = normalize(vTorusNormal);\n"
	"    vec3 tangentU = normalize(vTangentU);\n"
	"    vec3 tangentV = normalize(vTangentV);\n"
	"    vec4 subTex = texture(uSubstrateTex, vUV);\n"
	"    float riverCarve = (1.0 - subTex.a) * 15.9375;\n"
	"    float riverProx = 0.0;\n"
	"    float effSeaLevel = eval_river_surface_fs(vUV, uSeaLevel, effectiveScale, riverProx);\n"
	"    vec4 windPre = eval_recursive_wind(vWorldPos, uAnimTime);\n"
	"    vec2 basinPre = eval_water_fetch_and_basin(vUV, windPre.xy, shoreGrad, max(0.0, effSeaLevel - fragZ), effectiveScale);\n"
	"    float openSeaFactor = basinPre.x * (1.0 - riverProx);\n"
	"    float windFetch = basinPre.y;\n"
	"    float astroTide = eval_astronomical_tide(torusN, openSeaFactor, effSeaLevel);\n"
	"    float baseTideSeaLevel = effSeaLevel + astroTide;\n"
	"    float rawDepth = baseTideSeaLevel - fragZ;\n"
	"    vec3 shoreRelief = eval_shore_micro_relief(vWorldPos, rawDepth, fw);\n"
	"    fragZ += shoreRelief.x;\n"
	"    shoreGrad += shoreRelief.yz;\n"
	"    vec4 swashState = eval_shore_swash(vWorldPos, baseTideSeaLevel, fragZ, shoreGrad, uAnimTime, vDistance, fw, openSeaFactor, effSeaLevel);\n"
	"    vec4 lambWake = eval_lamb_wake_ribbon(vWorldPos, uAnimTime, vDistance, fw);\n"
	"    float liveSeaLevel = swashState.x + lambWake.x;\n"
	"    float fragWaterDepth = liveSeaLevel - fragZ;\n"
	"    if (fragWaterDepth <= 0.0) {\n"
	"        discard;\n"
	"    }\n"
	"    vec3 sDir = (length(uSunDir) > 0.001) ? normalize(uSunDir) : vec3(1.0, 0.0, 0.0);\n"
	"    float orbitLight = smoothstep(24000.0, 220000.0, vDistance);\n"
	"    vec3 L_local = normalize(-0.577 * tangentU - 0.577 * tangentV + 0.816 * torusN);\n"
	"    vec3 sunLocal = vec3(dot(sDir, tangentU), dot(sDir, tangentV), dot(sDir, torusN));\n"
	"    vec3 L_day = (sunLocal.z > -0.08) ? normalize(vec3(sunLocal.xy, max(0.12, sunLocal.z))) : L_local;\n"
	"    vec3 sunDir = normalize(mix(L_day, normalize(sDir + 0.35 * torusN), orbitLight));\n"
	"    /* Biome-aware water optics derived from uSubstrateTex (populated via ndw_biome_base_rgb) */\n"
	"    vec3 rawBiome = subTex.rgb;\n"
	"    float maxBio = max(rawBiome.r, max(rawBiome.g, rawBiome.b));\n"
	"    float minBio = min(rawBiome.r, min(rawBiome.g, rawBiome.b));\n"
	"    float wVolcanic = smoothstep(0.32, 0.14, maxBio);\n"
	"    float wPolar = smoothstep(0.52, 0.72, minBio) * smoothstep(0.18, 0.04, max(rawBiome.r, rawBiome.g) - rawBiome.b);\n"
	"    float wTropical = smoothstep(0.46, 0.68, rawBiome.r) * smoothstep(0.38, 0.58, rawBiome.g) * (1.0 - wPolar);\n"
	"    float wTemperate = clamp(1.0 - (wVolcanic + wPolar + wTropical), 0.0, 1.0);\n"
	"    float latPhi = 6.28318530718 * ((64.0 - vWorldPos.y) / 8388608.0);\n"
	"    float latDeg = asin(clamp(abs(sin(latPhi)), 0.0, 1.0)) * 57.2957795;\n"
	"    float innerHole = max(0.0, -cos(latPhi));\n"
	"    float sunDot = dot(torusN, sDir);\n"
	"    vec3 P_torus = (uCameraPos - vViewDir) - uPlanetCenter;\n"
	"    float R_torus = (uTorusRadii.x > 0.0) ? uTorusRadii.x : 2000000.0;\n"
	"    float r0_torus = (uTorusRadii.y > 0.0) ? uTorusRadii.y : 1335000.0;\n"
	"    float innerEclipse = (sunDot > 0.0 && innerHole > 0.001) ? eval_torus_self_eclipse(P_torus, sDir, R_torus, r0_torus) : 1.0;\n"
	"    float effSun = sunDot * innerEclipse;\n"
	"    float dayFactor = clamp(0.12 + 0.88 * smoothstep(-0.16, 0.22, effSun), 0.12, 1.0);\n"
	"    float directSun = smoothstep(-0.06, 0.18, effSun);\n"
	"    float twilight = smoothstep(0.32, -0.06, effSun) * smoothstep(-0.16, 0.12, effSun);\n"
	"    vec3 sunColor = mix(vec3(1.0, 0.98, 0.92), mix(vec3(1.0, 0.62, 0.28), vec3(0.96, 0.38, 0.18), smoothstep(0.18, -0.06, effSun)), twilight);\n"
	"    /* Torus-Shine: indirect illumination from the sunlit opposite ring arch onto the night ocean */\n"
	"    float torusShineVisibility = clamp(innerHole * 1.4 + 0.18 * max(0.0, dot(torusN, vec3(0.0, 0.0, 1.0))), 0.0, 1.0);\n"
	"    float oppSunlit = clamp(0.30 + 0.70 * smoothstep(-0.25, 0.45, -sDir.z + 0.5 * innerHole), 0.20, 1.0);\n"
	"    float nightMask = smoothstep(0.06, -0.16, effSun);\n"
	"    float torusShine = nightMask * torusShineVisibility * oppSunlit * 0.16;\n"
	"    vec3 torusShineCol = vec3(0.56, 0.68, 0.86);\n"
	"    float latPolar = smoothstep(54.0, 68.0, latDeg);\n"
	"    float alpineFreeze = smoothstep(280.0, 480.0, fragZ) * (1.0 - wTropical);\n"
	"    float freezeFactor = clamp(max(wPolar, max(latPolar, alpineFreeze)), 0.0, 1.0);\n"
	"    float eqEvapDry = smoothstep(24.0, 0.0, latDeg) * 0.18;\n"
	"    float midLatWet = smoothstep(18.0, 0.0, abs(latDeg - 38.0)) * 0.12;\n"
	"    float subpolarWet = smoothstep(10.0, 0.0, abs(latDeg - 58.0)) * 0.08;\n"
	"    float bioHumidity = clamp(0.65 * wTemperate + 0.88 * wTropical + 0.52 * wPolar + 0.25 * wVolcanic + 0.15 * smoothstep(0.0, 16.0, fragWaterDepth) - eqEvapDry + midLatWet + subpolarWet, 0.15, 0.95);\n"
	"    float cloudShadow = nd_cloud_shadow(vWorldPos, effSeaLevel, uAnimTime, bioHumidity, vDistance, fw);\n"
	"    float sunTrans = nd_sun_trans(cloudShadow);\n"
	"    vec3 C_SHALLOW = vec3(0.16, 0.56, 0.62) * wTropical\n"
	"                   + vec3(0.11, 0.42, 0.46) * wTemperate\n"
	"                   + vec3(0.17, 0.50, 0.62) * wPolar\n"
	"                   + vec3(0.09, 0.34, 0.38) * wVolcanic;\n"
	"    vec3 C_SHELF   = vec3(0.11, 0.43, 0.58) * wTropical\n"
	"                   + vec3(0.08, 0.32, 0.44) * wTemperate\n"
	"                   + vec3(0.11, 0.36, 0.54) * wPolar\n"
	"                   + vec3(0.07, 0.25, 0.34) * wVolcanic;\n"
	"    vec3 C_OUTER   = vec3(0.07, 0.28, 0.48) * wTropical\n"
	"                   + vec3(0.05, 0.21, 0.36) * wTemperate\n"
	"                   + vec3(0.06, 0.21, 0.40) * wPolar\n"
	"                   + vec3(0.04, 0.16, 0.27) * wVolcanic;\n"
	"    vec3 C_ABYSS   = vec3(0.030, 0.120, 0.30) * wTropical\n"
	"                   + vec3(0.024, 0.100, 0.24) * wTemperate\n"
	"                   + vec3(0.022, 0.090, 0.24) * wPolar\n"
	"                   + vec3(0.018, 0.072, 0.18) * wVolcanic;\n"
	"    /* Evaluate recursive moving wind field & height-model basin/fetch profile */\n"
	"    vec4 wind = eval_recursive_wind(vWorldPos, uAnimTime);\n"
	"    float shoreWaveDamp = smoothstep(0.0, 1.8, fragWaterDepth);\n"
	"    float basinWaveScale = (0.38 + 0.62 * openSeaFactor) * windFetch * (0.30 + 0.70 * shoreWaveDamp) * (1.0 - freezeFactor * 0.95);\n"
	"    vec4 waves = eval_zoom_water_waves(vWorldPos, uAnimTime, vDistance, fw, openSeaFactor) * (1.0 - freezeFactor * 0.95);\n"
	"    vec2 capGrad = eval_wind_capillary_facets(vWorldPos, uAnimTime, wind, vDistance, fw) * smoothstep(0.4, 2.8, fragWaterDepth) * (1.0 - freezeFactor * 0.95);\n"
	"    vec2 waveGrad = waves.yz * basinWaveScale + capGrad * 0.20 + lambWake.yz * 1.85 * (1.0 - freezeFactor);\n"
	"    vec2 specGrad = waves.yz * (basinWaveScale * 1.15 + 0.22) + capGrad * 0.25 + lambWake.yz * 2.25 * (1.0 - freezeFactor);\n"
	"    vec3 waveNormal = normalize(torusN - waveGrad.x * tangentU - waveGrad.y * tangentV);\n"
	"    vec3 specNormal = normalize(torusN - specGrad.x * tangentU - specGrad.y * tangentV);\n"
	"    float NdotV = max(0.0, dot(waveNormal, viewDir));\n"
	"    float NdotL = max(0.0, dot(waveNormal, sunDir));\n"
	"    /* Directional solar/sky glitter axis in the local torus frame */\n"
	"    vec3 flatRefl = reflect(-viewDir, torusN);\n"
	"    vec3 glintSunDir = normalize(mix(sunDir, flatRefl, 0.35) + 0.25 * torusN);\n"
	"    vec3 halfVec = normalize(glintSunDir + viewDir);\n"
	"    float NdotH_cap  = max(0.0, dot(specNormal, halfVec));\n"
	"    float NdotH_wave = max(0.0, dot(waveNormal, halfVec));\n"
	"    /* Three-tier specular hierarchy across the entire water surface:\n"
	"     * 1. Crisp pinpoint capillary sun glints (modulated by recursive wind cat's-paws)\n"
	"     * 2. Travelling wavelet slope highlights\n"
	"     * 3. Broad sky-sheen facet modulation */\n"
	"    float closeWeight = clamp(1.0 - (vDistance - 350.0) / 1400.0, 0.0, 1.0);\n"
	"    float specMicro   = pow(smoothstep(0.9965, 0.9998, NdotH_cap), 3.0) * (0.28 + 0.34 * wind.w) * (0.50 + 0.50 * closeWeight);\n"
	"    float specWavelet = pow(smoothstep(0.9880, 0.9988, NdotH_wave), 2.2) * (0.16 + 0.14 * wind.z);\n"
	"    float specBroad   = smoothstep(0.9680, 0.9945, NdotH_wave) * (0.06 + 0.04 * (1.0 - closeWeight));\n"
	"    float shoreSpecDamp = smoothstep(0.3, 3.2, fragWaterDepth);\n"
	"    float specSun     = (specMicro * 0.55 + specWavelet * 0.70 + specBroad * 0.45) * shoreSpecDamp * sunTrans;\n"
	"    float iceGlint    = pow(max(0.0, dot(torusN, halfVec)), 22.0) * 0.42;\n"
	"    specSun           = mix(specSun * directSun, iceGlint * sunTrans * directSun, freezeFactor);\n"
	"    /* Schlick Fresnel for air-water interface (R0 = 0.02) + per-wavelet facet tilt contrast */\n"
	"    float flatNdotV = max(0.0, dot(torusN, viewDir));\n"
	"    float facetTilt = clamp((flatNdotV - dot(specNormal, viewDir)) * 1.8, -0.14, 0.24);\n"
	"    float fresnel = clamp(0.02 + 0.98 * pow(clamp(1.0 - NdotV, 0.0, 1.0), 5.0) + max(0.0, facetTilt) * 0.16, 0.015, 0.62);\n"
	"    vec3 sky = ((length(uSkyColor) > 0.001) ? uSkyColor : vec3(0.55, 0.72, 0.88)) * (0.16 + 0.84 * dayFactor);\n"
	"    /* 3D wave slope hillshading: sunward wave faces tilt luminous, leeward troughs deepen */\n"
	"    float waveSlopeDiff = dot(specNormal, sunDir) - dot(torusN, sunDir);\n"
	"    float hillshadeLit = max(0.0, waveSlopeDiff) * sunTrans * directSun;\n"
	"    float hillshadeShd = max(0.0, -waveSlopeDiff);\n"
	"    vec3 waveShade = C_SHELF * (1.0 + hillshadeLit * 0.55 - hillshadeShd * 0.40) * nd_amb_trans(cloudShadow);\n"
	"    /* Subsurface wave-crest back-scattering only in deeper water (suppressed in crystal shallows) */\n"
	"    float deepSssMask = smoothstep(2.5, 10.0, fragWaterDepth);\n"
	"    float crestGlow = clamp(waves.w * 0.60 + hillshadeLit * 1.40, 0.0, 1.0) * deepSssMask;\n"
	"    vec3 sssTint = mix(C_SHALLOW, waveShade, 0.55) * (crestGlow * 0.22);\n"
	"    /* Height-model gated shoreline surf: ZERO crashing waves or shore vapor on quiet lakes (openSeaFactor == 0)! */\n"
	"    float shoreSlope = max(0.22, length(shoreGrad));\n"
	"    float shoreDist = fragWaterDepth / shoreSlope;\n"
	"    float foamDistFade = smoothstep(950.0, 220.0, vDistance);\n"
	"    float openSeaSurfEnergy = openSeaFactor * windFetch * smoothstep(0.55, 1.15, wind.z);\n"
	"    float crestLap = smoothstep(1.6, 0.20, shoreDist) * smoothstep(0.48, 0.82, waves.w) * openSeaSurfEnergy * 0.22;\n"
	"    float whitecap = smoothstep(0.68, 0.94, waves.w) * smoothstep(6.0, 16.0, fragWaterDepth) * openSeaSurfEnergy * 0.24;\n"
	"    float riverSlopeEnergy = smoothstep(1.0, 25.0, effSeaLevel - uSeaLevel) * riverProx;\n"
	"    float riffleMask = riverSlopeEnergy * smoothstep(2.6, 0.25, fragWaterDepth) * smoothstep(0.05, 0.45, fragWaterDepth);\n"
	"    float riffleTurb = (riffleMask > 0.001) ? smoothstep(0.52, 0.82, chaff_grad_noise2d(vWorldPos * 0.42 + vec2(mod(uAnimTime * 1.8, 256.0), -mod(uAnimTime * 1.4, 256.0)), 0x52494646u).x) : 0.0;\n"
	"    float riverRiffleFoam = riffleMask * riffleTurb * 0.34;\n"
	"    float foam = clamp(((crestLap + whitecap + swashState.z + riverRiffleFoam) * foamDistFade + lambWake.w) * (1.0 - freezeFactor), 0.0, 0.55);\n"
	"    vec3 foamColor = vec3(0.92, 0.96, 1.0);\n"
	"    /* Pure air-water surface compositing: shallow water stays glass-clear except for wavelet specularity & Fresnel */\n"
	"    float shallowGlassMask = smoothstep(0.5, 4.0, fragWaterDepth);\n"
	"    float skyReflWeight = fresnel * (0.15 + 0.85 * shallowGlassMask);\n"
	"    float sssWeight     = crestGlow * 0.16;\n"
	"    float specWeight    = clamp(specSun, 0.0, 0.92);\n"
	"    float foamWeight    = foam;\n"
	"    float totalWeight   = skyReflWeight + sssWeight + specWeight + foamWeight;\n"
	"    vec3 reflSkyCol = mix(sky * 0.92, sky * 1.08, clamp(0.5 + facetTilt * 2.2, 0.0, 1.0));\n"
	"    /* Reflected clouds: wave-perturbed reflection ray intersected with the cloud slice, read from the shared cloud-shadow map */\n"
	"    float reflCloudDens = cloudShadow;\n"
	"    if (uCsm.z > 0.0 && vDistance < 14000.0) {\n"
	"        vec3 waveRefl = reflect(-viewDir, waveNormal);\n"
	"        float reflUp = max(0.30, dot(waveRefl, torusN));\n"
	"        vec2 reflHoriz = vec2(dot(waveRefl, tangentU), dot(waveRefl, tangentV));\n"
	"        float csmRefl = smoothstep(0.05, 0.66, nd_csm_density(vWorldPos + reflHoriz * ((920.0 - effSeaLevel) / reflUp)));\n"
	"        reflCloudDens = mix(csmRefl, cloudShadow, smoothstep(9000.0, 14000.0, vDistance));\n"
	"    }\n"
	"    vec3 reflCloudCol = mix(vec3(0.76, 0.82, 0.90), vec3(0.98, 0.97, 0.95), clamp(0.55 + facetTilt * 1.4, 0.0, 1.0));\n"
	"    reflSkyCol = mix(reflSkyCol * nd_amb_trans(cloudShadow), reflCloudCol, clamp(reflCloudDens * 0.85, 0.0, 0.88));\n"
	"    float torusSpec = pow(NdotH_wave, 12.0) * (torusShine * 0.45);\n"
	"    vec3 surfaceNum = reflSkyCol * skyReflWeight + sssTint + sunColor * specSun + foamColor * foamWeight + torusShineCol * torusSpec;\n"
	"    vec3 surfaceRgb = (totalWeight > 1e-4) ? clamp(surfaceNum / max(totalWeight, 1e-4), 0.0, 1.0) : reflSkyCol;\n"
	"    surfaceRgb += torusShineCol * torusShine;\n"
	"    if (uLambLight.w > 0.001) {\n"
	"        vec2 dXY = uLambLight.xy - vWorldPos;\n"
	"        float d2 = dot(dXY, dXY);\n"
	"        float atten = (1.0 / (1.0 + d2 * 0.00055)) * smoothstep(220.0 * 220.0, 28.0 * 28.0, d2);\n"
	"        surfaceRgb += vec3(1.06, 0.99, 0.84) * (uLambLight.w * 0.16 * atten);\n"
	"    }\n"
	"    /* Procedural frozen ice sheet structure (crystals, micro-fractures, pack-ice crevasses) */\n"
	"    vec3 iceNoise = chaff_grad_noise2d(vWorldPos * 0.05, 0x1CE54E5Eu);\n"
	"    vec3 iceDetail = chaff_grad_noise2d(vWorldPos * 0.28, 0x1CE07E17u);\n"
	"    float icePebble = (iceNoise.x - 0.5) * 0.16 + (iceDetail.x - 0.5) * 0.08;\n"
	"    float iceCracks = smoothstep(0.47, 0.52, abs(iceNoise.x - 0.5)) * 0.22;\n"
	"    vec3 iceAlbedo = mix(vec3(0.68, 0.86, 0.96), vec3(0.96, 0.98, 1.00), clamp(0.52 + icePebble + iceCracks, 0.0, 1.0));\n"
	"    vec3 iceLit = iceAlbedo * ((0.45 * nd_amb_trans(cloudShadow) * (0.20 + 0.80 * dayFactor)) + 0.55 * max(0.0, dot(torusN, sunDir)) * sunTrans * directSun);\n"
	"    surfaceRgb = mix(surfaceRgb * (0.16 + 0.84 * dayFactor), iceLit, freezeFactor * 0.96);\n"
	"    float limb = pow(clamp(1.0 - max(0.0, dot(torusN, viewDir)), 0.0, 1.0), 3.0);\n"
	"    vec3 limbColor = mix(vec3(0.35, 0.70, 1.0), vec3(0.96, 0.54, 0.30), twilight * 0.85);\n"
	"    surfaceRgb = mix(surfaceRgb, limbColor * (0.15 + 0.85 * dayFactor), limb * 0.55 * uAtmoIntensity);\n"
	"    float fogFactor = clamp((vDistance - uFogStart) / max(1.0, uFogEnd - uFogStart), 0.0, 1.0);\n"
	"    float fogW = fogFactor * uAtmoIntensity;\n"
	"    /* --- Submerged sprite: refracted lookup of the resolved scene + true view-ray absorption --- */\n"
	"    float cosV = max(0.35, dot(torusN, viewDir));\n"
	"    float subDepthV = objPath1 * cosV;\n"
	"    float refrFade = smoothstep(2000.0, 500.0, vDistance);\n"
	"    float refrDepth = min(subDepthV, 8.0) * refrFade * (1.0 - freezeFactor * 0.90);\n"
	"    /* Snell-style lateral shift from the local wave slope (world units -> screen UV) plus the apparent lift of submerged things */\n"
	"    vec2 refrWorld = waveGrad * (0.30 * refrDepth);\n"
	"    vec2 refrUV = 0.5 * vec2(-refrWorld.x / (vDistance * uProjScale.x),\n"
	"                              (refrWorld.y * 0.82 + 0.25 * refrDepth * cosV) / (vDistance * uProjScale.y));\n"
	"    vec2 sUVr = clamp(sUV + refrUV, vec2(0.002), vec2(0.998));\n"
	"    float objPath2 = nd_object_dist(sUVr) - vDistance;\n"
	"    bool refrOk = objPath2 > 0.02;\n"
	"    vec2 sUVf = refrOk ? sUVr : sUV;\n"
	"    float pathF = refrOk ? objPath2 : objPath1;\n"
	"    vec4 sceneS = texture(uSceneColor, sUVf);\n"
	"    float objW = 1.0 - sceneS.a; /* sprites wrote alpha 0, the terrain (water already shaded over the seabed) alpha 1 */\n"
	"    float dObj = clamp(pathF * cosV, 0.0, 2000.0);\n"
	"    float shallowFactor = smoothstep(1.2, 14.0, dObj);\n"
	"    float effOptical = pathF * (0.08 + 0.92 * shallowFactor);\n"
	"    vec3 seabedAbsorption = exp(-vec3(0.075, 0.016, 0.0048) * effOptical);\n"
	"    vec3 absorbed = sceneS.rgb * seabedAbsorption;\n"
	"    float dBed = clamp(fragWaterDepth, 0.0, 2000.0);\n"
	"    float tShelf  = smoothstep(2.5, 18.0, dBed);\n"
	"    float tOuter  = smoothstep(18.0, 85.0, dBed);\n"
	"    float tAbyss  = smoothstep(85.0, 340.0, dBed);\n"
	"    float tTrench = smoothstep(340.0, 1400.0, dBed);\n"
	"    vec3 inscatterCol = mix(C_SHALLOW, C_SHELF, tShelf);\n"
	"    inscatterCol = mix(inscatterCol, C_OUTER, tOuter);\n"
	"    inscatterCol = mix(inscatterCol, C_ABYSS, tAbyss);\n"
	"    inscatterCol = mix(inscatterCol, C_ABYSS * 0.38, tTrench);\n"
	"    vec3 bodyScatterWeight = clamp((vec3(1.0) - seabedAbsorption) * (0.20 + 0.80 * shallowFactor), 0.0, 1.0);\n"
	"    vec3 objCol = mix(absorbed, mix(inscatterCol, sky, fogW), bodyScatterWeight);\n"
	"    vec3 sceneOut = mix(sceneS.rgb, objCol, objW);\n"
	"    /* Air-water surface layer over the submerged sprite, identical terms to the terrain pass */\n"
	"    float surfEdgeFade = smoothstep(0.0, 0.18, fragWaterDepth);\n"
	"    float liquidAlpha = clamp(1.0 - (1.0 - skyReflWeight) * (1.0 - sssWeight) * (1.0 - specWeight) * (1.0 - foamWeight), 0.0, 0.94) * surfEdgeFade;\n"
	"    float iceAlpha = clamp(0.88 + 0.10 * smoothstep(0.2, 3.0, fragWaterDepth), 0.0, 0.98);\n"
	"    float surfAlpha = mix(liquidAlpha, iceAlpha, freezeFactor);\n"
	"    vec3 comp = mix(sceneOut, mix(surfaceRgb, sky, fogW), surfAlpha);\n"
	"    /* The waterline fades in over the first quarter unit below the surface */\n"
	"    float wetBlend = smoothstep(0.0, 0.25, subDepthV);\n"
	"    vec3 dryRgb = texture(uSceneColor, sUV).rgb;\n"
	"    FragColor = vec4(mix(dryRgb, comp, wetBlend), 1.0);\n"
	"}\n";

static const char *s_vs_billboard =
	"#version 300 es\n"
	"precision highp float;\n"
	"layout (location = 0) in vec4 a_pos0;\n"
	"layout (location = 1) in vec4 a_pos1;\n"
	"layout (location = 2) in vec4 a_pos2;\n"
	"layout (location = 3) in vec4 a_pos3;\n"
	"layout (location = 4) in vec4 a_uv;\n"
	"layout (location = 5) in vec4 a_tint;\n"
	"layout (location = 6) in vec4 a_param;\n"
	"uniform mat4 uMVP;\n"
	"uniform vec3 uCameraPos;\n"
	"out vec2 vUV;\n"
	"out vec4 vTint;\n"
	"out vec3 vWorldPos;\n"
	"out vec3 vLocalPos;\n"
	"out vec4 vQuadUV;\n"
	"out vec4 vSpriteBounds;\n"
	"void main() {\n"
	"    int vid = gl_VertexID & 3;\n"
	"    vec4 p = (vid == 0) ? a_pos0 : ((vid == 1) ? a_pos1 : ((vid == 2) ? a_pos2 : a_pos3));\n"
	"    vec2 q_uv = vec2((vid & 1) != 0 ? 1.0 : -1.0, (vid >= 2) ? 1.0 : ((a_param.w < -0.5) ? -1.0 : 0.0));\n"
	"    vec2 uv_coord = vec2((vid & 1) != 0 ? a_uv.z : a_uv.x, (vid >= 2) ? a_uv.y : a_uv.w);\n"
	"    vUV = uv_coord;\n"
	"    vTint = a_tint;\n"
	"    vWorldPos = p.xyz;\n"
	"    vec3 a_local_pos = vec3(a_param.x + p.w, a_param.y + ((q_uv.y > 0.0) ? (p.y - a_pos0.y) : 0.0), a_param.z);\n"
	"    vec4 a_quad_uv = vec4(q_uv.x, q_uv.y, a_param.z, a_param.w);\n"
	"    vec4 a_sprite_bounds = vec4(min(a_uv.x, a_uv.z), min(a_uv.y, a_uv.w), max(a_uv.x, a_uv.z), max(a_uv.y, a_uv.w));\n"
	"    vLocalPos = a_local_pos;\n"
	"    vQuadUV = a_quad_uv;\n"
	"    vSpriteBounds = a_sprite_bounds;\n"
	"    vec3 toCam = uCameraPos - p.xyz;\n"
	"    float dCam = length(toCam);\n"
	"    vec3 dirToCam = (dCam > 0.001) ? (toCam / dCam) : vec3(0.0, 0.0, 1.0);\n"
	"    float bias = (a_quad_uv.w < -0.5) ? min(10.0, dCam * 0.035) : min(24.0, dCam * 0.08);\n"
	"    gl_Position = uMVP * vec4(p.xyz + dirToCam * bias, 1.0);\n"
	"}\n";

static const char *s_fs_billboard =
	"#version 300 es\n"
	"precision highp float;\n"
	"precision highp int;\n"
	"precision highp sampler2D;\n"
	"in vec2 vUV;\n"
	"in vec4 vTint;\n"
	"in vec3 vWorldPos;\n"
	"in vec3 vLocalPos;\n"
	"in vec4 vQuadUV;\n"
	"in vec4 vSpriteBounds;\n"
	"uniform sampler2D uAtlasTex;\n"
	"uniform sampler2D uAtlasNormTex;\n"
	"uniform float uSeaLevel;\n"
	"uniform float uAnimTime;\n"
	"uniform vec3 uCameraPos;\n"
	"uniform float uFogStart;\n"
	"uniform float uFogEnd;\n"
	"uniform vec3 uSkyColor;\n"
	"uniform float uAtmoIntensity;\n"
	"uniform vec4 uLambLight;\n"
	"uniform int uPass;\n"
	"uniform vec3 uSunDir;\n"
	"out vec4 FragColor;\n"
	ND_GLSL_SCENE_LIGHT
	ND_GLSL_CSM_LOOKUP_FN
	CHAFF_GLSL_HASH_FN
	CHAFF_GLSL_GRAD_NOISE_FN
	CHAFF_GLSL_FLOW_FN
	"float chaff_sample_3d(vec3 pos, uint shift, uint seed) {\n"
	"    float cell_size = float(1u << shift);\n"
	"    vec3 p = pos / cell_size;\n"
	"    ivec3 i0 = ivec3(floor(p));\n"
	"    vec3 f = fract(p);\n"
	"    vec2 u = f.xy * f.xy * (3.0 - 2.0 * f.xy);\n"
	"    float uz = f.z * f.z * (3.0 - 2.0 * f.z);\n"
	"    int step_px = int(1u << shift);\n"
	"    ivec3 cell = i0 * step_px;\n"
	"    uint v000 = chaff_hash3(cell + ivec3(0, 0, 0), seed) >> 24u;\n"
	"    uint v100 = chaff_hash3(cell + ivec3(step_px, 0, 0), seed) >> 24u;\n"
	"    uint v010 = chaff_hash3(cell + ivec3(0, step_px, 0), seed) >> 24u;\n"
	"    uint v110 = chaff_hash3(cell + ivec3(step_px, step_px, 0), seed) >> 24u;\n"
	"    uint v001 = chaff_hash3(cell + ivec3(0, 0, step_px), seed) >> 24u;\n"
	"    uint v101 = chaff_hash3(cell + ivec3(step_px, 0, step_px), seed) >> 24u;\n"
	"    uint v011 = chaff_hash3(cell + ivec3(0, step_px, step_px), seed) >> 24u;\n"
	"    uint v111 = chaff_hash3(cell + ivec3(step_px, step_px, step_px), seed) >> 24u;\n"
	"    float b00 = mix(float(v000), float(v100), u.x);\n"
	"    float b10 = mix(float(v010), float(v110), u.x);\n"
	"    float b01 = mix(float(v001), float(v101), u.x);\n"
	"    float b11 = mix(float(v011), float(v111), u.x);\n"
	"    float b0 = mix(b00, b10, u.y);\n"
	"    float b1 = mix(b01, b11, u.y);\n"
	"    return mix(b0, b1, uz) / 255.0;\n"
	"}\n"
	"float nd_caustic_web(vec2 cUV, float tC, float closeZoom) {\n"
	"    float c1 = chaff_sample_3d(vec3(cUV * 1.85, tC * 2.4), 3u, 0x1337u);\n"
	"    float c2 = chaff_sample_3d(vec3((cUV + vec2(37.0, -23.0)) * 1.85, tC * 1.9), 3u, 0xBEEFu);\n"
	"    float caustMeso = pow(clamp(1.0 - abs(c1 - c2) * 5.6, 0.0, 1.0), 3.0);\n"
	"    float caustFine = 0.0;\n"
	"    if (closeZoom > 0.01) {\n"
	"        float c3 = chaff_sample_3d(vec3(cUV * 4.8, tC * 3.6), 2u, 0xCA57u);\n"
	"        float c4 = chaff_sample_3d(vec3((cUV + vec2(-19.0, 41.0)) * 4.8, tC * 3.0), 2u, 0xCA58u);\n"
	"        caustFine = pow(clamp(1.0 - abs(c3 - c4) * 5.6, 0.0, 1.0), 3.0) * closeZoom;\n"
	"    }\n"
	"    return caustMeso + caustFine * 0.70;\n"
	"}\n"	"vec4 fetchBoundedTexel(ivec2 tc, ivec4 iBounds, ivec2 maxTex) {\n"
	"    if (tc.x < iBounds.x || tc.x > iBounds.z || tc.y < iBounds.y || tc.y > iBounds.w) {\n"
	"        return vec4(0.0);\n"
	"    }\n"
	"    return texelFetch(uAtlasTex, clamp(tc, ivec2(0), maxTex), 0);\n"
	"}\n"
	"vec4 fetchBoundedNormTexel(ivec2 tc, ivec4 iBounds, ivec2 maxTex) {\n"
	"    if (tc.x < iBounds.x || tc.x > iBounds.z || tc.y < iBounds.y || tc.y > iBounds.w) {\n"
	"        return vec4(0.502, 0.502, 1.0, 0.0);\n"
	"    }\n"
	"    return texelFetch(uAtlasNormTex, clamp(tc, ivec2(0), maxTex), 0);\n"
	"}\n"
	"vec4 sampleAtlasNorm(vec2 uv, vec4 bounds) {\n"
	"    vec2 texSize = vec2(textureSize(uAtlasNormTex, 0));\n"
	"    ivec2 maxTex = ivec2(texSize) - ivec2(1);\n"
	"    vec4 effB = (bounds.z > bounds.x && bounds.w > bounds.y) ? bounds : vec4(0.0, 0.0, 1.0, 1.0);\n"
	"    ivec4 iBounds = ivec4(\n"
	"        ivec2(floor(effB.xy * texSize + 1e-3)),\n"
	"        ivec2(ceil(effB.zw * texSize - 1e-3)) - ivec2(1)\n"
	"    );\n"
	"    ivec2 tc = ivec2(floor(uv * texSize));\n"
	"    return fetchBoundedNormTexel(tc, iBounds, maxTex);\n"
	"}\n"
	"vec4 sampleAtlasSmooth(vec2 uv, vec4 bounds, float style) {\n"
	"    vec2 texSize = vec2(textureSize(uAtlasTex, 0));\n"
	"    ivec2 maxTex = ivec2(texSize) - ivec2(1);\n"
	"    vec4 effB = (bounds.z > bounds.x && bounds.w > bounds.y) ? bounds : vec4(0.0, 0.0, 1.0, 1.0);\n"
	"    ivec4 iBounds = ivec4(\n"
	"        ivec2(floor(effB.xy * texSize + 1e-3)),\n"
	"        ivec2(ceil(effB.zw * texSize - 1e-3)) - ivec2(1)\n"
	"    );\n"
	"    vec2 p = uv * texSize - 0.5;\n"
	"    ivec2 i0 = ivec2(floor(p));\n"
	"    vec2 f = p - vec2(i0);\n"
	"    vec2 fw = max(fwidth(p), vec2(1e-4));\n"
	"    /* Crisp sub-texel antialiased pixel-art sampling for all characters, flora, and objects */\n"
	"    vec2 filterW = clamp(fw * 0.65, vec2(0.08), vec2(0.45));\n"
	"    vec2 u = smoothstep(0.5 - filterW, 0.5 + filterW, f);\n"
	"    vec4 c00 = fetchBoundedTexel(i0,                iBounds, maxTex);\n"
	"    vec4 c10 = fetchBoundedTexel(i0 + ivec2(1, 0),  iBounds, maxTex);\n"
	"    vec4 c01 = fetchBoundedTexel(i0 + ivec2(0, 1),  iBounds, maxTex);\n"
	"    vec4 c11 = fetchBoundedTexel(i0 + ivec2(1, 1),  iBounds, maxTex);\n"
	"    float b00 = (1.0 - u.x) * (1.0 - u.y);\n"
	"    float b10 = u.x * (1.0 - u.y);\n"
	"    float b01 = (1.0 - u.x) * u.y;\n"
	"    float b11 = u.x * u.y;\n"
	"    float a = c00.a * b00 + c10.a * b10 + c01.a * b01 + c11.a * b11;\n"
	"    float w00 = c00.a * b00;\n"
	"    float w10 = c10.a * b10;\n"
	"    float w01 = c01.a * b01;\n"
	"    float w11 = c11.a * b11;\n"
	"    float wSum = w00 + w10 + w01 + w11;\n"
	"    vec3 rgb = (wSum > 1e-5) ? ((c00.rgb * w00 + c10.rgb * w10 + c01.rgb * w01 + c11.rgb * w11) / wSum) : vec3(0.0);\n"
	"    return vec4(rgb, a);\n"
	"}\n"
	"vec4 eval_recursive_wind(vec2 worldPos, float anim_time) {\n"
	"    return chaff_eval_flow_2d(worldPos, anim_time);\n"
	"}\n"
	"vec4 eval_zoom_water_waves(vec2 pos, vec4 wind, float anim_time, float dist, float fw) {\n"
	"    float distVis = clamp(1.0 - (dist - 800.0) / 3600.0, 0.0, 1.0);\n"
	"    if (distVis <= 0.001) return vec4(0.0);\n"
	"    vec2 wDir = wind.xy;\n"
	"    vec2 wPerp = vec2(-wDir.y, wDir.x);\n"
	"    const vec2 prevW = vec2(0.741, -0.671);\n"
	"    const vec2 prevP = vec2(0.671,  0.741);\n"
	"    float speed = (0.75 + 0.35 * wind.z) * 1.6;\n"
	"    float tBase = mod(anim_time * speed, 1024.0);\n"
	"    float f1 = 0.16;\n"
	"    float f2 = 0.48;\n"
	"    float nyq1 = smoothstep(0.60, 0.15, f1 * fw);\n"
	"    float nyq2 = smoothstep(0.60, 0.15, f2 * fw);\n"
	"    float h = 0.0;\n"
	"    vec2 grad = vec2(0.0);\n"
	"    float crest = 0.0;\n"
	"    if (nyq1 > 0.001) {\n"
	"        float t1 = tBase * 0.90;\n"
	"        vec2 p1 = vec2(dot(pos, prevW) * 1.25, dot(pos, prevP) * 0.85) * f1 - vec2(t1, 0.15 * t1);\n"
	"        vec3 c1 = chaff_grad_noise2d(p1, 0x57415641u);\n"
	"        float a1 = 0.28 * nyq1 * distVis;\n"
	"        h += (c1.x - 0.5) * a1;\n"
	"        grad += (prevW * c1.y * 1.25 + prevP * c1.z * 0.85) * (f1 * a1);\n"
	"        crest += smoothstep(0.55, 0.88, c1.x) * nyq1;\n"
	"    }\n"
	"    if (nyq2 > 0.001) {\n"
	"        float t2 = tBase * 1.35;\n"
	"        vec2 p2 = vec2(dot(pos, prevW) * 1.10 - dot(pos, prevP) * 0.40, dot(pos, prevP) * 1.10 + dot(pos, prevW) * 0.30) * f2 - vec2(t2, -0.22 * t2);\n"
	"        vec3 c2 = chaff_grad_noise2d(p2, 0x57415642u);\n"
	"        float a2 = 0.16 * nyq2 * distVis;\n"
	"        h += (c2.x - 0.5) * a2;\n"
	"        grad += (prevW * (c2.y * 1.10 + c2.z * 0.30) + prevP * (c2.z * 1.10 - c2.y * 0.40)) * (f2 * a2);\n"
	"        crest += smoothstep(0.60, 0.92, c2.x) * nyq2 * 0.65;\n"
	"    }\n"
	"    return vec4(h, grad.x, grad.y, clamp(crest, 0.0, 1.0));\n"
	"}\n"
	"vec2 eval_wind_capillary_facets(vec2 pos, float anim_time, vec4 wind, float dist, float fw) {\n"
	"    float distVis = clamp(1.0 - (dist - 550.0) / 2200.0, 0.0, 1.0);\n"
	"    if (distVis <= 0.001) return vec2(0.0);\n"
	"    vec2 wDir = wind.xy;\n"
	"    vec2 wPerp = vec2(-wDir.y, wDir.x);\n"
	"    const vec2 prevW = vec2(0.741, -0.671);\n"
	"    const vec2 prevP = vec2(0.671,  0.741);\n"
	"    float rippleEnv = (0.45 + 0.50 * wind.w) * distVis;\n"
	"    float f1 = 0.55;\n"
	"    float f2 = 1.25;\n"
	"    float nyq1 = smoothstep(0.55, 0.14, f1 * fw);\n"
	"    float nyq2 = smoothstep(0.55, 0.14, f2 * fw);\n"
	"    float t1 = mod(anim_time * 1.15, 512.0);\n"
	"    float t2 = mod(anim_time * 1.65, 512.0);\n"
	"    vec2 gustShift = wDir * (wind.z * 0.55) + wPerp * (wind.w * 0.35);\n"
	"    vec2 wCoord1 = vec2(dot(pos, prevW) * 1.45, dot(pos, prevP) * 0.78) * f1 + gustShift - vec2(t1, 0.18 * t1);\n"
	"    vec2 wCoord2 = vec2(dot(pos, prevW) * 1.35 + dot(pos, prevP) * 0.35, dot(pos, prevP) * 0.85 - dot(pos, prevW) * 0.25) * f2 + gustShift * 1.4 - vec2(-0.25 * t2, t2);\n"
	"    vec3 c1 = (nyq1 > 0.001) ? chaff_grad_noise2d(wCoord1, 0xCA9111A1u) : vec3(0.5, 0.0, 0.0);\n"
	"    vec3 c2 = (nyq2 > 0.001) ? chaff_grad_noise2d(wCoord2 + c1.yz * 0.25, 0xCA9111A2u) : vec3(0.5, 0.0, 0.0);\n"
	"    vec2 localG = c1.yz * (0.09 * nyq1) + c2.yz * (0.06 * nyq2);\n"
	"    vec2 worldCapGrad = (wDir * localG.x * 1.25 + wPerp * localG.y * 0.75) * rippleEnv;\n"
	"    return worldCapGrad;\n"
	"}\n"	"void main() {\n"
	"    float vDist = length(uCameraPos - vWorldPos);\n"
	"    float fogFactor = clamp((vDist - uFogStart) / max(1.0, uFogEnd - uFogStart), 0.0, 1.0);\n"
	"    float latPhi = 6.28318530718 * ((64.0 - vLocalPos.y) / 8388608.0);\n"
	"    float lonTheta = 6.28318530718 * (vLocalPos.x / 8388608.0);\n"
	"    vec3 torusN = vec3(cos(lonTheta) * cos(latPhi), sin(lonTheta) * cos(latPhi), sin(latPhi));\n"
	"    vec3 sDir = (length(uSunDir) > 0.001) ? normalize(uSunDir) : vec3(1.0, 0.0, 0.0);\n"
	"    float sunDot = dot(torusN, sDir);\n"
	"    float dayFactor = clamp(0.12 + 0.88 * smoothstep(-0.16, 0.22, sunDot), 0.12, 1.0);\n"
	"    float directSun = smoothstep(-0.06, 0.16, sunDot);\n"
	"    vec3 sky = ((length(uSkyColor) > 0.001) ? uSkyColor : vec3(0.55, 0.70, 0.85)) * (0.16 + 0.84 * dayFactor);\n"
	"    /* Same cloud shadow as terrain and water: one fetch from the shared cloud-shadow map at the sprite's ground position */\n"
	"    float cloudSh = nd_cloud_shadow_map(vLocalPos.xy, vQuadUV.z, vDist);\n"
	"    /* Exact horizontal water-plane (Z = uSeaLevel) intersection of the camera ray through this billboard pixel */\n"
	"    vec2 waterSurfPos = vec2(vLocalPos.x, vLocalPos.y - (uSeaLevel - vQuadUV.z) * 0.7002075);\n"
	"    float waterFw = max(1e-4, max(length(dFdx(waterSurfPos)), length(dFdy(waterSurfPos))));\n"
	"    float d = 0.0;\n"
	"    bool isSubmerged = false;\n"
	"    vec4 wind = vec4(0.7071, -0.7071, 1.0, 0.5);\n"
	"    vec4 waves = vec4(0.0);\n"
	"    vec2 capGrad = vec2(0.0);\n"
	"    vec2 waveRefGrad = vec2(0.0);\n"
	"    float waveSurfZ = uSeaLevel;\n"
	"    float effSpriteZ = vQuadUV.z + (vLocalPos.z - vQuadUV.z) * 0.35;\n"
	"    bool canSubmerge = (vQuadUV.w < 0.5 || vQuadUV.w > 2.5);\n"
	"    if (uSeaLevel > -900.0 && canSubmerge && vQuadUV.z < uSeaLevel + 0.85) {\n"
	"        wind = eval_recursive_wind(waterSurfPos, uAnimTime);\n"
	"        waves = eval_zoom_water_waves(waterSurfPos, wind, uAnimTime, vDist, waterFw);\n"
	"        waveSurfZ = uSeaLevel + waves.x * 0.35 * smoothstep(2600.0, 400.0, vDist);\n"
	"        if (vQuadUV.z < waveSurfZ + 0.35 && effSpriteZ < waveSurfZ) {\n"
	"            d = waveSurfZ - effSpriteZ;\n"
	"            isSubmerged = true;\n"
	"            capGrad = eval_wind_capillary_facets(waterSurfPos, uAnimTime, wind, vDist, waterFw);\n"
	"            waveRefGrad = waves.yz + capGrad * 0.35;\n"
	"        }\n"
	"    }\n"
	"    /* 1. Universal 3D Sun-Projected & Secondary Lamb Point-Light Silhouette Shadow Pass (vQuadUV.w < -0.5) */\n"
	"    if (vQuadUV.w < -0.5) {\n"
	"        if (isSubmerged && d > 4.0) discard;\n"
	"        bool isLambShadow = (vQuadUV.w < -1.5);\n"
	"        float hFrac = clamp((vQuadUV.y + 1.0) * 0.5, 0.0, 1.0);\n"
	"        float silAlpha = 0.0;\n"
	"        if (vSpriteBounds.z > vSpriteBounds.x && vSpriteBounds.w > vSpriteBounds.y) {\n"
	"            float aC = sampleAtlasSmooth(vUV, vSpriteBounds, 0.0).a;\n"
	"            if (vDist < 1200.0) {\n"
	"                vec2 atlasSz = max(vec2(1.0), vec2(textureSize(uAtlasTex, 0)));\n"
	"                vec2 blurStep = ((isLambShadow ? 0.55 : 0.35) + (isLambShadow ? 1.95 : 1.45) * hFrac) / atlasSz;\n"
	"                float aL = sampleAtlasSmooth(vUV + vec2(-blurStep.x, 0.0), vSpriteBounds, 0.0).a;\n"
	"                float aR = sampleAtlasSmooth(vUV + vec2( blurStep.x, 0.0), vSpriteBounds, 0.0).a;\n"
	"                float aD = sampleAtlasSmooth(vUV + vec2(0.0, -blurStep.y), vSpriteBounds, 0.0).a;\n"
	"                float aU = sampleAtlasSmooth(vUV + vec2(0.0,  blurStep.y), vSpriteBounds, 0.0).a;\n"
	"                silAlpha = 0.40 * aC + 0.15 * (aL + aR + aD + aU);\n"
	"            } else {\n"
	"                silAlpha = aC;\n"
	"            }\n"
	"        }\n"
	"        /* Ground-contact elliptical umbra at the base/trunk/feet + radial fallback when bounds are unset */\n"
	"        float r2 = dot(vQuadUV.xy, vQuadUV.xy);\n"
	"        vec2 baseDelta = vec2(vQuadUV.x * 1.45, (hFrac - 0.14) * 3.20);\n"
	"        float contactUmbra = exp(-dot(baseDelta, baseDelta) * 2.6) * (isLambShadow ? 0.24 : 0.34);\n"
	"        float radialPenumbra = (r2 < 1.0) ? pow(clamp(1.0 - r2, 0.0, 1.0), 1.65) : 0.0;\n"
	"        float heightFade = 1.0 - (isLambShadow ? 0.45 : 0.32) * hFrac * hFrac;\n"
	"        float castMask = (vSpriteBounds.z > vSpriteBounds.x) ? max(silAlpha * heightFade * 0.44, contactUmbra) : (radialPenumbra * 0.36 + contactUmbra * 0.45);\n"
	"        if (castMask <= 0.008) discard;\n"
	"        vec3 shNoise = chaff_grad_noise2d(vLocalPos.xy * 0.28, 0x53484457u);\n"
	"        float dapple = 0.88 + 0.24 * shNoise.x;\n"
	"        float waterAtten = isSubmerged ? clamp(1.0 - d * 0.25, 0.0, 1.0) : 1.0;\n"
	"        float distAtten = isLambShadow ? clamp((3200.0 - vDist) / 1200.0, 0.0, 1.0) : clamp((6500.0 - vDist) / 2500.0, 0.0, 1.0);\n"
	"        float lightTrans = isLambShadow ? 1.0 : nd_sun_trans(cloudSh);\n"
	"        float shadowAlpha = clamp(castMask * dapple * lightTrans * vTint.a * waterAtten * distAtten * (1.0 - fogFactor * uAtmoIntensity), 0.0, isLambShadow ? 0.38 : 0.50);\n"
	"        if (shadowAlpha <= 0.005) discard;\n"
	"        vec3 shadowRgb = mix(vec3(0.05, 0.07, 0.10), sky * 0.16, 0.45);\n"
	"        FragColor = vec4(shadowRgb, shadowAlpha);\n"
	"        return;\n"
	"    }\n"
	"    /* 2. Crisp Sub-Texel Anti-Aliased & Snell-Refracted Sprite Sampling */\n"
	"    float style = vQuadUV.w;\n"
	"    if (style > 5.5) {\n"
	"        /* ND_BB_STYLE_PARTICLE (6.0): Subtle, fine sunlit water droplet / mist bead with per-droplet color variation */\n"
	"        vec2 qc = vec2(vQuadUV.x, (vQuadUV.y - 0.5) * 2.0) * 1.18;\n"
	"        qc.x *= (1.0 + 0.14 * max(0.0, qc.y));\n"
	"        float r2 = dot(qc, qc);\n"
	"        if (r2 >= 1.0) discard;\n"
	"        float radial = clamp(1.0 - r2, 0.0, 1.0);\n"
	"        float nz = sqrt(radial);\n"
	"        vec3 dropN = normalize(vec3(qc.x * 0.72, qc.y * 0.72, nz));\n"
	"        vec3 sunDir = normalize(vec3(-0.55, 0.62, 0.56));\n"
	"        float specGlint = pow(max(0.0, dot(dropN, sunDir)), 5.0);\n"
	"        float fresnelRim = pow(1.0 - nz, 2.0) * radial;\n"
	"        float coreMask = radial * radial * (0.65 + 0.35 * radial);\n"
	"        float pAlpha = clamp(coreMask * vTint.a * 0.78 * (1.0 - fogFactor * uAtmoIntensity), 0.0, 0.78);\n"
	"        if (pAlpha <= 0.015) discard;\n"
	"        vec3 dropCol = clamp(mix(vTint.rgb, vec3(1.0, 1.0, 1.0), specGlint * 0.58 + fresnelRim * 0.22), 0.0, 1.0);\n"
	"        FragColor = vec4(dropCol, pAlpha);\n"
	"        return;\n"
	"    }\n"
	"    bool isLambGlow = (style > 3.5 && style < 4.5);\n"
	"    bool isGrass = (style > 4.5 && style < 5.5);\n"
	"    bool isFloraObj = (style > 0.5 && style < 2.5);\n"
	"    vec4 baseCol = sampleAtlasSmooth(vUV, vSpriteBounds, style);\n"
	"    vec2 sampleUV = vUV;\n"
	"    if (isSubmerged) {\n"
	"        vec2 atlasSize = max(vec2(1.0), vec2(textureSize(uAtlasTex, 0)));\n"
	"        float depthRamp = smoothstep(0.0, 0.85, d) * (0.55 + 0.45 * smoothstep(0.5, 4.5, d));\n"
	"        float refractDistFade = clamp(1.0 - (vDist - 800.0) / 2400.0, 0.0, 1.0);\n"
	"        /* Physical Snell's law air->water wave-slope ray deflection + ripple shear + vertical foreshortening */\n"
	"        float rippleShear = sin(d * 3.4 - uAnimTime * 3.2 + vLocalPos.x * 0.85) * 0.75;\n"
	"        vec2 snellTexelShift = vec2(\n"
	"            (-waveRefGrad.x * 9.50 + rippleShear) * depthRamp,\n"
	"            -waveRefGrad.y * (6.50 * depthRamp) - clamp(d * 0.55, 0.0, 2.20) * smoothstep(0.0, 0.6, d)\n"
	"        ) * refractDistFade;\n"
	"        sampleUV = vUV + snellTexelShift / atlasSize;\n"
	"        if (vSpriteBounds.z > vSpriteBounds.x && vSpriteBounds.w > vSpriteBounds.y) {\n"
	"            vec2 halfTexel = 0.5 / atlasSize;\n"
	"            sampleUV = clamp(sampleUV, vSpriteBounds.xy + halfTexel, vSpriteBounds.zw - halfTexel);\n"
	"        }\n"
	"    }\n"
	"    vec4 subSample = isSubmerged ? sampleAtlasSmooth(sampleUV, vSpriteBounds, style) : baseCol;\n"
	"    float effSampleAlpha = isSubmerged ? mix(baseCol.a, subSample.a, smoothstep(0.0, 0.65, d)) : baseCol.a;\n"
	"    float edgeAlpha = isGrass\n"
	"        ? (smoothstep(0.08, 0.48, effSampleAlpha) * smoothstep(0.0, 0.22, clamp(vQuadUV.y, 0.0, 1.0) + 0.06))\n"
	"        : smoothstep(0.18, 0.52, effSampleAlpha);\n"
	"    float auraAlpha = 0.0;\n"
	"    if (isLambGlow && edgeAlpha < 0.99) {\n"
	"        vec2 qc = vec2(vQuadUV.x, (vQuadUV.y - 0.46) * 2.0);\n"
	"        float r2 = dot(qc, qc);\n"
	"        auraAlpha = exp(-r2 * 2.8) * 0.16 * (1.0 - edgeAlpha);\n"
	"    }\n"
	"    if (edgeAlpha <= 0.01 && auraAlpha <= 0.005) discard;\n"
	"    if (subSample.a < 0.12) subSample = baseCol;\n"
	"    /* 3. Leaf-Selective Plant Tinting & Natural Palette Harmonization */\n"
	"    vec3 albedo = subSample.rgb;\n"
	"    if (style > 0.5 && style < 1.5) {\n"
	"        float leafDom = subSample.g - max(subSample.r, subSample.b);\n"
	"        float leafMask = smoothstep(0.015, 0.065, leafDom);\n"
	"        vec3 plantScale = vTint.rgb * (255.0 / 128.0);\n"
	"        albedo = clamp(albedo * mix(vec3(1.0), plantScale, leafMask), 0.0, 1.0);\n"
	"    } else {\n"
	"        albedo *= vTint.rgb;\n"
	"    }\n"
	"    float outAlpha = clamp(edgeAlpha + auraAlpha, 0.0, 1.0) * vTint.a;\n"
	"    if (outAlpha <= 0.001) discard;\n"
	"    /* Pass 0 draws only the opaque core (depth + stencil), pass 1 only the anti-aliased fringe, pass 2 everything else */\n"
	"    if (uPass == 0 && outAlpha < 0.5) discard;\n"
	"    if (uPass == 1 && outAlpha >= 0.5) discard;\n"
	"    float rawLuma = dot(albedo, vec3(0.299, 0.587, 0.114));\n"
	"    /* 4. Gentle Scene Lighting & Biome-Root Grass Harmonization (preserves crisp painted pixel-art contrast) */\n"
	"    float qx = clamp(vQuadUV.x, -1.0, 1.0);\n"
	"    float qy = clamp(vQuadUV.y, 0.0, 1.0);\n"
	"    float distFade = clamp(1.0 - (vDist - 1800.0) / 36000.0, 0.0, 1.0);\n"
	"    float closeZoom = clamp((820.0 - vDist) / 480.0, 0.0, 1.0);\n"
	"    vec2 surfPos = vec2(vLocalPos.x, vLocalPos.z);\n"
	"    vec3 g1 = chaff_grad_noise2d(surfPos * 0.24, 0xA341316Cu);\n"
	"    vec3 g2 = (distFade > 0.01 && !isGrass) ? chaff_grad_noise2d(surfPos * 0.78 + vec2(17.3, -29.1), 0xC8013EA4u) : vec3(0.5, 0.0, 0.0);\n"
	"    float microH = (g1.x - 0.5) * 0.14;\n"
	"    vec2 microGrad = g1.yz * 0.12;\n"
	"    bool isFoliage = !isLambGlow && (isGrass || isFloraObj || (albedo.g > max(albedo.r, albedo.b) * 1.06));\n"
	"    bool isMineral = !isLambGlow && !isGrass && (style > 2.5);\n"
	"    if (isFoliage && distFade > 0.01) {\n"
	"        if (isGrass) {\n"
	"            /* Cheap travelling gust phase wave replaces a per-pixel 3-noise wind evaluation on every grass fragment */\n"
	"            float gustWave = 0.5 + 0.5 * sin(dot(vLocalPos.xy, vec2(0.052, -0.047)) + mod(uAnimTime, 628.3185) * " ND_SL_STR(ND_GRASS_SHEEN_FREQ_RAD) " + (g1.x - 0.5) * 3.0);\n"
	"            float tipSheen = smoothstep(0.28, 0.96, qy) * clamp(gustWave * 0.80, 0.0, 1.0);\n"
	"            vec3 groundTurf = albedo * mix(0.66, 0.80, smoothstep(0.25, 0.75, g1.x));\n"
	"            vec3 bladeBody  = mix(albedo * vec3(0.92, 0.96, 0.88), albedo * vec3(1.06, 1.04, 0.86), smoothstep(0.22, 0.95, qy + tipSheen * 0.24));\n"
	"            albedo = mix(groundTurf, bladeBody, smoothstep(0.0, 0.42, qy));\n"
	"        } else {\n"
	"            /* Subtle macro patch warmth across trees without washing out pixel-art clusters */\n"
	"            vec3 warmFoliage = albedo * vec3(1.04, 1.01, 0.94);\n"
	"            vec3 deepLeaf    = albedo * vec3(0.94, 0.99, 0.94);\n"
	"            albedo = mix(deepLeaf, warmFoliage, smoothstep(0.28, 0.72, g1.x));\n"
	"        }\n"
	"    } else if (isMineral && distFade > 0.01) {\n"
	"        /* Harmonize mineral sprites with terrain slate/granite strata */\n"
	"        vec3 slateWarm   = vec3(0.45, 0.42, 0.38);\n"
	"        vec3 graniteCool = vec3(0.37, 0.39, 0.41);\n"
	"        vec3 rockBase    = mix(slateWarm, graniteCool, clamp(g1.x, 0.0, 1.0)) * (0.78 + 0.60 * rawLuma);\n"
	"        albedo = mix(albedo, rockBase, 0.28 * distFade);\n"
	"    }\n"
	"    vec3 proxyN = isGrass\n"
	"        ? normalize(mix(vec3(0.0, 0.14, 0.99), vec3(qx * 0.25, 0.32, 0.91), smoothstep(0.0, 0.45, qy)))\n"
	"        : normalize(vec3(qx * 0.28, 0.38 - 0.10 * qy, 0.88));\n"
	"    vec4 normSample = sampleAtlasNorm(sampleUV, vSpriteBounds);\n"
	"    float hasNormMap = step(0.5, normSample.a);\n"
	"    float flipSign = (dFdx(vUV.x) < 0.0) ? -1.0 : 1.0;\n"
	"    vec3 tNorm = vec3((normSample.r * 2.0 - 1.0) * flipSign, normSample.g * 2.0 - 1.0, max(0.20, normSample.b * 2.0 - 1.0));\n"
	"    vec3 mapN = normalize(vec3(tNorm.x, -tNorm.y * 0.5736 + tNorm.z * 0.50, tNorm.y * 0.8192 + tNorm.z * 0.866));\n"
	"    vec3 baseN = mix(proxyN, mapN, hasNormMap);\n"
	"    vec3 N_bb = normalize(baseN - vec3(microGrad.x * 0.12, 0.0, microGrad.y * 0.12));\n"
	"    vec3 L_sun = normalize(vec3(-0.577, -0.577, 0.816));\n"
	"    float sunDiff = max(0.0, dot(N_bb, L_sun));\n"
	"    float cavityAO = clamp(1.0 + microH * 0.10, 0.92, 1.06);\n"
	"    float hAboveGround = max(0.0, vLocalPos.z - vQuadUV.z);\n"
	"    float contactAO = isLambGlow ? 1.0 : mix(0.90, 1.0, smoothstep(0.0, 3.5, hAboveGround));\n"
	"    float hemiSky = 0.5 + 0.5 * N_bb.z;\n"
	"    vec3 ambCol = mix(vec3(0.58, 0.62, 0.68), vec3(0.76, 0.80, 0.86), hemiSky);\n"
	"    vec3 sunCol = vec3(1.05, 1.02, 0.96);\n"
	"    vec3 lightTerm = (ambCol * (0.55 * nd_amb_trans(cloudSh) * (0.20 + 0.80 * dayFactor)) + sunCol * ((0.52 + 0.24 * hasNormMap) * sunDiff * nd_sun_trans(cloudSh) * directSun)) * (cavityAO * contactAO);\n"
	"    vec3 lit = albedo * mix(vec3(1.0), lightTerm, mix(isLambGlow ? 0.28 : 0.42, 0.78, hasNormMap));\n"
	"    if (uSeaLevel > -900.0 && canSubmerge && vQuadUV.z < waveSurfZ + 0.35 && !isSubmerged) {\n"
	"        /* Capillary wet-rim darkening just above the waterline (including on the universe-owner lamb) */\n"
	"        float heightAboveWater = effSpriteZ - waveSurfZ;\n"
	"        float wetRim = smoothstep(0.55, 0.0, heightAboveWater);\n"
	"        lit *= (1.0 - wetRim * 0.28);\n"
	"    }\n"
	"    if (isLambGlow) {\n"
	"        /* Subtle self-luminance: slightly brighter than the environment with a soft warm rim */\n"
	"        float rimGlow = smoothstep(0.85, 0.25, baseCol.a) * edgeAlpha;\n"
	"        float dryGlowScale = isSubmerged ? 0.96 : 1.16;\n"
	"        vec3 bodyLit = clamp(lit * dryGlowScale + vec3(0.11, 0.10, 0.07) * (isSubmerged ? 0.35 : 1.0) + vec3(0.14, 0.12, 0.08) * rimGlow, 0.0, 1.0);\n"
	"        lit = mix(vec3(1.0, 0.96, 0.84), bodyLit, edgeAlpha / max(1e-4, edgeAlpha + auraAlpha));\n"
	"    } else {\n"
	"        /* Upward terrain bounce light near ground contact line */\n"
	"        float bounceMask = (1.0 - smoothstep(0.0, 4.5, hAboveGround)) * (1.0 - qy) * distFade;\n"
	"        lit = mix(lit, lit * vec3(0.86, 0.94, 0.78), bounceMask * 0.35);\n"
	"        /* Subsurface foliage rim scattering & mineral/leaf micro-specular sheen */\n"
	"        vec3 H_half = normalize(L_sun + vec3(0.0, 0.57, 0.82));\n"
	"        float specAngle = max(0.0, dot(N_bb, H_half));\n"
	"        if (isFoliage) {\n"
	"            float rim = clamp(qx * (-0.55) + qy * 0.45, 0.0, 1.0) * smoothstep(0.95, 0.35, baseCol.a);\n"
	"            lit += albedo * vec3(0.32, 0.42, 0.12) * (rim * 0.45 + pow(specAngle, 20.0) * 0.14) * distFade;\n"
	"        } else if (isMineral) {\n"
	"            lit += vec3(pow(specAngle, 28.0) * 0.16 * distFade);\n"
	"        }\n"
	"        /* Subtle point-light illumination from the glowing lamb onto nearby billboards */\n"
	"        if (uLambLight.w > 0.001) {\n"
	"            vec3 toLamb = vec3(uLambLight.x - vLocalPos.x, uLambLight.y - vLocalPos.y, uLambLight.z + 10.0 - vLocalPos.z);\n"
	"            float d2 = dot(toLamb, toLamb);\n"
	"            float atten = (1.0 / (1.0 + d2 * 0.00055)) * smoothstep(220.0 * 220.0, 28.0 * 28.0, d2);\n"
	"            float dirFactor = (d2 > 1e-4) ? clamp(0.45 + 0.55 * dot(N_bb, toLamb * inversesqrt(d2)), 0.25, 1.0) : 1.0;\n"
	"            vec3 warmGlow = vec3(1.06, 0.99, 0.84) * (uLambLight.w * 0.24 * atten * dirFactor);\n"
	"            lit += (albedo * 0.72 + vec3(0.28)) * warmGlow;\n"
	"        }\n"
	"    }\n"
	"    /* Submerged billboard composite: caustics, Beer-Lambert extinction, and radiant waterline meniscus */\n"
	"    if (isSubmerged) {\n"
	"        float dClamped = clamp(d, 0.0, 2000.0);\n"
	"        float baseWaterDepth = max(dClamped, uSeaLevel - vQuadUV.z);\n"
	"        float baseSubFade = clamp((waveSurfZ + 0.35 - vQuadUV.z) / 0.50, 0.0, 1.0);\n"
	"        float caustDistFade = clamp(1.0 - (vDist - 900.0) / 2500.0, 0.0, 1.0);\n"
	"        float caustCloseZoom = clamp((420.0 - vDist) / 300.0, 0.0, 1.0);\n"
	"        float caustic = 0.0;\n"
	"        if (caustDistFade > 0.01 && dClamped < 24.0) {\n"
	"            float tC = mod(uAnimTime, 256.0);\n"
	"            vec2 cUV = waterSurfPos + waveRefGrad * 1.6;\n"
	"            caustic = nd_caustic_web(cUV, tC, caustCloseZoom) * caustDistFade;\n"
	"        }\n"
	"        float causticFocus = smoothstep(0.04, 0.85, dClamped) * exp(-0.085 * dClamped);\n"
	"        vec3 caustLit = lit * (1.0 + caustic * causticFocus * 0.45);\n"
	"        float cosView = 0.819152;\n"
	"        float opticalDepth = dClamped / cosView;\n"
	"        float subRamp = smoothstep(0.0, 0.35, dClamped);\n"
	"        float effectiveOpticalDepth = (0.55 + opticalDepth * 1.15) * subRamp;\n"
	"        vec3 seabedAbsorption = exp(-vec3(0.075, 0.018, 0.0055) * effectiveOpticalDepth);\n"
	"        vec3 transmittedSpr = caustLit * seabedAbsorption;\n"
	"        vec3 C_SHALLOW = vec3(0.11, 0.42, 0.46);\n"
	"        vec3 C_SHELF   = vec3(0.08, 0.32, 0.44);\n"
	"        vec3 C_OUTER   = vec3(0.05, 0.21, 0.36);\n"
	"        vec3 C_ABYSS   = vec3(0.024, 0.10, 0.24);\n"
	"        vec3 inscatterCol = mix(C_SHALLOW, C_SHELF, smoothstep(2.5, 18.0, baseWaterDepth));\n"
	"        inscatterCol = mix(inscatterCol, C_OUTER, smoothstep(18.0, 85.0, baseWaterDepth));\n"
	"        inscatterCol = mix(inscatterCol, C_ABYSS, smoothstep(85.0, 340.0, baseWaterDepth));\n"
	"        inscatterCol = mix(inscatterCol, C_ABYSS * 0.38, smoothstep(340.0, 1400.0, baseWaterDepth));\n"
	"        vec3 bodyScatterWeight = subRamp * (0.20 + 0.80 * (vec3(1.0) - seabedAbsorption));\n"
	"        vec3 waterColumn = mix(transmittedSpr, inscatterCol, bodyScatterWeight);\n"
	"        float shoreWaveDamp = smoothstep(0.0, 1.8, baseWaterDepth);\n"
	"        float openSeaFactor = smoothstep(2.5, 12.0, baseWaterDepth);\n"
	"        float windFetch     = clamp(0.25 + 0.75 * smoothstep(0.4, 8.0, baseWaterDepth), 0.25, 1.0);\n"
	"        float basinWaveScale = (0.38 + 0.62 * openSeaFactor) * windFetch * (0.30 + 0.70 * shoreWaveDamp);\n"
	"        vec2 waveGrad = waves.yz * basinWaveScale + capGrad * 0.45;\n"
	"        vec2 specGrad = waves.yz * (basinWaveScale * 1.45 + 0.35) + capGrad * 1.35;\n"
	"        vec3 surfViewDir = vec3(0.0, 0.573576, 0.819152);\n"
	"        vec3 waveNormal  = normalize(vec3(-waveGrad.x, -waveGrad.y, 1.0));\n"
	"        vec3 specNormal  = normalize(vec3(-specGrad.x, -specGrad.y, 1.0));\n"
	"        float NdotV = max(0.0, dot(waveNormal, surfViewDir));\n"
	"        float flatNdotV = surfViewDir.z;\n"
	"        float facetTilt = clamp((flatNdotV - dot(specNormal, surfViewDir)) * 2.8, -0.18, 0.32);\n"
	"        float fresnel = clamp(0.14 + 0.86 * pow(clamp(1.0 - NdotV, 0.0, 1.0), 4.0) + max(0.0, facetTilt) * 0.24, 0.12, 0.68);\n"
	"        vec3 waveShade = C_SHELF * (1.0 + max(0.0, dot(specNormal, L_sun) - L_sun.z) * 0.55) * nd_amb_trans(cloudSh);\n"
	"        float deepSssMask = smoothstep(2.5, 10.0, baseWaterDepth);\n"
	"        float crestGlow = waves.w * 0.60 * deepSssMask;\n"
	"        vec3 sssTint = mix(C_SHALLOW, waveShade, 0.55) * (crestGlow * 0.22);\n"
	"        float shallowGlassMask = smoothstep(0.2, 2.5, baseWaterDepth);\n"
	"        float skyReflWeight = fresnel * (0.45 + 0.55 * shallowGlassMask);\n"
	"        float sssWeight     = crestGlow * 0.16;\n"
	"        float shoreSpecDamp = smoothstep(0.3, 3.2, baseWaterDepth);\n"
	"        float specWeight    = clamp(smoothstep(0.9840, 0.9982, dot(waveNormal, normalize(reflect(-surfViewDir, vec3(0,0,1)) + surfViewDir))) * 0.30 * shoreSpecDamp * nd_sun_trans(cloudSh), 0.0, 0.92);\n"
	"        float totalWeight   = skyReflWeight + sssWeight + specWeight;\n"
	"        vec3 reflSkyCol = mix(sky * 0.92, sky * 1.08, clamp(0.5 + facetTilt * 2.2, 0.0, 1.0));\n"
	"        vec3 surfaceNum = reflSkyCol * skyReflWeight + sssTint + vec3(1.0, 0.98, 0.92) * specWeight;\n"
	"        vec3 surfaceRgb = (totalWeight > 1e-4) ? clamp(surfaceNum / max(totalWeight, 1e-4), 0.0, 1.0) : reflSkyCol;\n"
	"        float spriteMask = isLambGlow ? edgeAlpha : 1.0;\n"
	"        float surfAlpha = clamp(1.0 - (1.0 - skyReflWeight) * (1.0 - sssWeight) * (1.0 - specWeight), 0.0, 0.94) * smoothstep(0.0, 0.12, dClamped);\n"
	"        vec3 submergedComposite = mix(waterColumn, surfaceRgb, surfAlpha);\n"
	"        float wetBlend = smoothstep(0.0, 0.14, dClamped) * spriteMask;\n"
	"        lit = mix(lit, submergedComposite, wetBlend);\n"
	"        /* Radiant air-water meniscus highlight right along the live wave intersection */\n"
	"        float meniscus = smoothstep(0.0, 0.07, dClamped) * smoothstep(0.42, 0.07, dClamped) * clamp(1.0 - (vDist - 600.0) / 1800.0, 0.0, 1.0) * baseSubFade * spriteMask;\n"
	"        vec3 meniscusColor = mix(sky * 0.95, vec3(0.94, 0.98, 1.0), 0.78);\n"
	"        lit = mix(lit, meniscusColor, meniscus * 0.78);\n"
	"    }\n"
	"    /* Atmospheric Distance Fog & Planetary Limb Scattering Parity with Terrain */\n"
	"    vec3 surfaceLit = mix(lit, vec3(0.35, 0.65, 0.95), 0.22 * uAtmoIntensity);\n"
	"    vec3 finalRgb = mix(surfaceLit, sky, fogFactor * uAtmoIntensity);\n"
	"    /* Core pass marks sprite pixels with alpha 0 (the water pass tells them from already water-shaded seabed by it) */\n"
	"    FragColor = vec4(finalRgb, (uPass == 0) ? 0.0 : outAlpha);\n"
	"}\n";

const char *nd_seven_gl_vs_terrain(void) { return s_vs_terrain; }
const char *nd_seven_gl_fs_terrain(void) { return s_fs_terrain; }
const char *nd_seven_gl_vs_water(void) { return s_vs_water; }
const char *nd_seven_gl_fs_water(void) { return s_fs_water; }
const char *nd_seven_gl_vs_billboard(void) { return s_vs_billboard; }
const char *nd_seven_gl_fs_billboard(void) { return s_fs_billboard; }

/* Cloud-shadow map pass: one full-frame evaluation of the synoptic cloud density on the cloud slice (z = 920) over the
 * snapped window described by uCsm. Wind shear displacement is baked in; terrain, water and sprites index the map by
 * projected slice position. R channel = density (0..1). */
static const char *s_vs_csm =
	"#version 300 es\n"
	"precision highp float;\n"
	"out vec2 vUv;\n"
	"void main() {\n"
	"    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
	"    vUv = p;\n"
	"    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
	"}\n";

static const char *s_fs_csm =
	"#version 300 es\n"
	"precision highp float;\n"
	"precision highp int;\n"
	"in vec2 vUv;\n"
	"uniform vec3 uCsm;\n"
	"uniform float uAnimTime;\n"
	"uniform float uSeaWet;\n"
	"out vec4 FragColor;\n"
	CHAFF_GLSL_HASH_FN
	CHAFF_GLSL_GRAD_NOISE_FN
	CHAFF_GLSL_FLOW_FN
	ND_GLSL_SYNOPTIC_CLOUD_FN
	"void main() {\n"
	"    vec2 projXY = uCsm.xy + (vUv - 0.5) * (2.0 * uCsm.z);\n"
	"    float fw = (2.0 * uCsm.z) / " ND_SL_STR(ND_CSM_RES) ".0;\n"
	"    vec3 worldClimate = chaff_grad_noise2d(projXY * 0.000045, 0x434C494Du);\n"
	"    float latPhi = 6.28318530718 * ((64.0 - projXY.y) / 8388608.0);\n"
	"    float latDeg = asin(clamp(abs(sin(latPhi)), 0.0, 1.0)) * 57.2957795;\n"
	"    float eqEvapDry = smoothstep(24.0, 0.0, latDeg) * 0.18;\n"
	"    float midLatWet = smoothstep(18.0, 0.0, abs(latDeg - 38.0)) * 0.12;\n"
	"    float subpolarWet = smoothstep(10.0, 0.0, abs(latDeg - 58.0)) * 0.08;\n"
	"    float humidity = clamp(0.50 + 0.28 * (worldClimate.x - 0.5) + 0.10 * uSeaWet - eqEvapDry + midLatWet + subpolarWet, 0.08, 0.90);\n"
	"    vec4 synC; vec2 pD; vec2 pZ; vec2 baseG;\n"
	"    if (eval_synoptic_carrier(projXY, uAnimTime, humidity, fw, synC, pD, pZ, baseG)) {\n"
	"        FragColor = vec4(0.0, 0.0, 0.0, 1.0);\n"
	"        return;\n"
	"    }\n"
	"    vec2 atmoWindDir = vec2(0.741, -0.671), shearDisp = vec2(21.8, -19.8);\n"
	"    float atmoGust = 0.65, updraft = 0.50;\n"
	"    if (fw < 200.0) chaff_eval_wind_3d(vec3(projXY, 920.0), 420.0, 2820.0, uAnimTime, fw, atmoWindDir, shearDisp, atmoGust, updraft);\n"
	"    vec2 dummyG; float dummyC;\n"
	"    vec4 syn = eval_synoptic_macro_cloud_from_carrier(projXY - shearDisp, uAnimTime, humidity, fw, false, synC, pZ, baseG, dummyG, dummyC);\n"
	"    FragColor = vec4(clamp(syn.x, 0.0, 1.0), 0.0, 0.0, 1.0);\n"
	"}\n";



/* Temporal Cloud Shadow Map Reprojection Shader */
static const char *s_vs_csm_reproj =
	"#version 300 es\n"
	"precision highp float;\n"
	"out vec2 vUv;\n"
	"void main() {\n"
	"    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
	"    vUv = p;\n"
	"    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
	"}\n";

static const char *s_fs_csm_reproj =
	"#version 300 es\n"
	"precision highp float;\n"
	"in vec2 vUv;\n"
	"uniform sampler2D uHistoryTex;\n"
	"uniform mat4 uPrevToCurr;       // prev_mvp * inverse(curr_mvp)\n"
	"uniform vec3 uCurrWin;          // curr_csm_win (cx, cy, half)\n"
	"uniform vec3 uPrevWin;          // prev_csm_win (cx, cy, half)\n"
	"uniform vec3 uCurrSunDir;       // Current sun direction\n"
	"uniform vec3 uPrevSunDir;       // Previous sun direction\n"
	"uniform float uConfidenceThreshold;\n"
	"out vec4 FragColor;\n"
	"void main() {\n"
	"    // Current CSM UV (0..1) -> world position in CSM space\n"
	"    float cx_curr = uCurrWin.x;\n"
	"    float cy_curr = uCurrWin.y;\n"
	"    float half_curr = uCurrWin.z;\n"
	"    \n"
	"    // Reconstruct world position from current CSM UV\n"
	"    vec2 projXY_curr = vec2(cx_curr, cy_curr) + (vUv - 0.5) * (2.0 * half_curr);\n"
	"    \n"
	"    // Simple approach: compute UV in previous CSM texture\n"
	"    float cx_prev = uPrevWin.x;\n"
	"    float cy_prev = uPrevWin.y;\n"
	"    float half_prev = uPrevWin.z;\n"
	"    \n"
	"    // World position is same, so map to prev CSM UV\n"
	"    vec2 uv_prev = (projXY_curr - vec2(cx_prev, cy_prev)) / (2.0 * half_prev) + 0.5;\n"
	"    \n"
	"    // Sun direction change compensation\n"
	"    float sun_change = 1.0 - dot(normalize(uCurrSunDir.xy), normalize(uPrevSunDir.xy));\n"
	"    \n"
	"    // Sample history texture\n"
	"    float history = texture(uHistoryTex, uv_prev).r;\n"
	"    \n"
	"    // Confidence: high when sun hasn't changed much and UV is in bounds\n"
	"    float confidence = (uv_prev.x >= 0.0 && uv_prev.x <= 1.0 && uv_prev.y >= 0.0 && uv_prev.y <= 1.0) ? 1.0 : 0.0;\n"
	"    confidence *= smoothstep(0.05, 0.0, sun_change);\n"
	"    \n"
	"    // Output: history in R, confidence in G\n"
	"    FragColor = vec4(history, confidence, 0.0, 1.0);\n"
	"}\n";

static const char *s_vs_cloud =
	"#version 300 es\n"
	"precision highp float;\n"
	"precision highp int;\n"
	"precision highp sampler2D;\n"
	"layout (location = 0) in vec2 a_grid_uv;\n"
	"uniform mat4 uMVP;\n"
	"uniform sampler2D uSubstrateTex;\n"
	"uniform vec2 uChunkOrigin;\n"
	"uniform vec2 uChunkRelOrigin;\n"
	"uniform vec2 uChunkSpan;\n"
	"uniform vec4 uRefTrig;\n"
	"uniform vec3 uCameraPos;\n"
	"uniform vec3 uPlanetCenter;\n"
	"uniform mat4 uBodyRot;\n"
	"uniform vec2 uTorusRadii;\n"
	"uniform vec2 uWorldDim;\n"
	"uniform float uAnimTime;\n"
	"uniform vec4 uCloudDeck;\n"
	"out vec2 vUV;\n"
	"out vec2 vWorldPos;\n"
	"out vec2 vRelXY;\n"
	"out float vDistance;\n"
	"out float vShellZ;\n"
	"out vec3 vViewDir;\n"
	"out vec3 vTorusNormal;\n"
	"out vec3 vTangentU;\n"
	"out vec3 vTangentV;\n"
	"void main() {\n"
	"    vUV = a_grid_uv;\n"
	"    vec2 worldXY = vec2(uChunkOrigin.x + a_grid_uv.x * uChunkSpan.x, uChunkOrigin.y + a_grid_uv.y * uChunkSpan.y);\n"
	"    vec2 relXY = vec2(uChunkRelOrigin.x + a_grid_uv.x * uChunkSpan.x, uChunkRelOrigin.y + a_grid_uv.y * uChunkSpan.y);\n"
	"    const float TWO_PI = 6.28318530718;\n"
	"    float R = (uTorusRadii.x > 0.0) ? uTorusRadii.x : 2000000.0;\n"
	"    float r0 = (uTorusRadii.y > 0.0) ? uTorusRadii.y : 1335000.0;\n"
	"    vec2 wdim = (uWorldDim.x > 0.0 && uWorldDim.y > 0.0) ? uWorldDim : vec2(10485760.0, 8388608.0);\n"
	"    float du = TWO_PI * (relXY.x / wdim.x);\n"
	"    float dv = TWO_PI * (relXY.y / wdim.y);\n"
	"    float sdu = sin(du), shdu = sin(0.5 * du);\n"
	"    float sdv = sin(dv), shdv = sin(0.5 * dv);\n"
	"    float cu0 = uRefTrig.x, su0 = uRefTrig.y, cv0 = uRefTrig.z, sv0 = uRefTrig.w;\n"
	"    float dcu = -2.0 * shdu * shdu * cu0 - sdu * su0;\n"
	"    float dsu = -2.0 * shdu * shdu * su0 + sdu * cu0;\n"
	"    float dcv = -2.0 * shdv * shdv * cv0 - sdv * sv0;\n"
	"    float dsv = -2.0 * shdv * shdv * sv0 + sdv * cv0;\n"
	"    float cu = cu0 + dcu, su = su0 + dsu;\n"
	"    float cv = cv0 + dcv, sv = sv0 + dsv;\n"
	"    /* Screen-Projection Proxy Shell:\n"
	"     * uCloudDeck.w holds effShellZ, which is always strictly below the camera's near plane\n"
	"     * so the proxy shell NEVER intersects the camera plane during ascent/descent, while\n"
	"     * s_fs_cloud reconstructs the exact 3D camera ray into the physical [zBase, zTop] atmosphere! */\n"
	"    float deckBase = (uCloudDeck.y > 0.0) ? uCloudDeck.x : 420.0;\n"
	"    float deckSpan = (uCloudDeck.y > 0.0) ? uCloudDeck.y : 2400.0;\n"
	"    float effWz = (uCloudDeck.w != 0.0) ? uCloudDeck.w : (deckBase + 0.35 * deckSpan);\n"
	"    vShellZ = effWz;\n"
	"    float Rv0 = R + r0 * cv0;\n"
	"    float dRv = r0 * dcv + effWz * cv;\n"
	"    mat3 bodyR = mat3(uBodyRot);\n"
	"    vec3 worldPos = uPlanetCenter + bodyR * vec3(Rv0 * dcu + dRv * cu, Rv0 * dsu + dRv * su, r0 * dsv + effWz * sv);\n"
	"    vTorusNormal = bodyR * vec3(cv * cu, cv * su, sv);\n"
	"    vTangentU = bodyR * vec3(-su, cu, 0.0);\n"
	"    vTangentV = bodyR * vec3(-sv * cu, -sv * su, cv);\n"
	"    vWorldPos = worldXY;\n"
	"    vRelXY = relXY;\n"
	"    vDistance = length(worldPos - uCameraPos);\n"
	"    vViewDir = uCameraPos - worldPos;\n"
	"    gl_Position = uMVP * vec4(worldPos, 1.0);\n"
	"}\n";

static const char *s_fs_cloud =
	"#version 300 es\n"
	"precision highp float;\n"
	"precision highp int;\n"
	"precision highp isampler2D;\n"
	"precision highp sampler2D;\n"
	"in vec2 vUV;\n"
	"in vec2 vWorldPos;\n"
	"in vec2 vRelXY;\n"
	"in float vDistance;\n"
	"in float vShellZ;\n"
	"in vec3 vViewDir;\n"
	"in vec3 vTorusNormal;\n"
	"in vec3 vTangentU;\n"
	"in vec3 vTangentV;\n"
	"uniform isampler2D uHeightTex;\n"
	"uniform sampler2D uSubstrateTex;\n"
	"uniform sampler2D uSceneDepth;\n"
	"uniform vec2 uViewport;\n"
	"uniform vec2 uChunkSpan;\n"
	"uniform vec2 uTorusRadii;\n"
	"uniform float uSeaLevel;\n"
	"uniform float uFogStart;\n"
	"uniform float uFogEnd;\n"
	"uniform float uAnimTime;\n"
	"uniform vec3 uSkyColor;\n"
	"uniform float uAtmoIntensity;\n"
	"uniform float uCamDist;\n"
	"uniform vec4 uCloudDeck;\n"
	"uniform vec3 uSunDir;\n"
	"uniform vec3 uMoonDir;\n"
	"uniform vec3 uCameraPos;\n"
	"uniform vec3 uPlanetCenter;\n"
	"uniform vec3 uClusterGridDim;\n"
	"uniform vec4 uClusterDepthParams;\n"
	"uniform int uClusterLightCount;\n"
	"uniform sampler2D uClusterLightData;\n"
	"uniform sampler2D uClusterLightListsTex;\n"
	"out vec4 FragColor;\n"
	"/* Clustered Cloud Lighting (16x16x24 Frustum Grid):\n"
	" * Fetches cluster light list for the fragment's screen tile and logarithmic depth slice,\n"
	" * and evaluates volumetric in-scattering + Henyey-Greenstein phase + Beer-Lambert self-shadowing\n"
	" * for every directional, point, and spot light assigned to the cluster. */\n"
	"vec3 eval_clustered_cloud_lighting(vec2 fragCoord, vec2 vp, vec3 stepWorldXY_Z, float fragDist,\n"
	"                                   float dens, vec3 cloudGradN, vec3 rayDir, float hFrac, float upperDeckOcc) {\n"
	"    if (uClusterLightCount <= 0 || vp.x <= 1.0 || vp.y <= 1.0) return vec3(0.0);\n"
	"    int cx = clamp(int((fragCoord.x / vp.x) * uClusterGridDim.x), 0, int(uClusterGridDim.x) - 1);\n"
	"    int cy = clamp(int((fragCoord.y / vp.y) * uClusterGridDim.y), 0, int(uClusterGridDim.y) - 1);\n"
	"    int cz = clamp(int(log(max(fragDist, uClusterDepthParams.x)) * uClusterDepthParams.z + uClusterDepthParams.w), 0, int(uClusterGridDim.z) - 1);\n"
	"    int clusterIdx = cx + int(uClusterGridDim.x) * (cy + int(uClusterGridDim.y) * cz);\n"
	"    int listCount = int(texelFetch(uClusterLightListsTex, ivec2(0, clusterIdx), 0).r * 255.0 + 0.5);\n"
	"    if (listCount <= 0) return vec3(0.0);\n"
	"    vec3 localContrib = vec3(0.0);\n"
	"    for (int li = 0; li < 32; li++) {\n"
	"        if (li >= listCount) break;\n"
	"        int lightIdx = int(texelFetch(uClusterLightListsTex, ivec2(li + 1, clusterIdx), 0).r * 255.0 + 0.5);\n"
	"        if (lightIdx >= uClusterLightCount) continue;\n"
	"        vec4 p0 = texelFetch(uClusterLightData, ivec2(0, lightIdx), 0);\n"
	"        vec4 p1 = texelFetch(uClusterLightData, ivec2(1, lightIdx), 0);\n"
	"        vec4 p2 = texelFetch(uClusterLightData, ivec2(2, lightIdx), 0);\n"
	"        vec4 p3 = texelFetch(uClusterLightData, ivec2(3, lightIdx), 0);\n"
	"        int lType = int(p3.w * 255.0 + 0.5);\n"
	"        vec3 lCol = p2.rgb;\n"
	"        float lInt = p2.a * 4.0;\n"
	"        if (lType == 0) continue; /* Primary directional lights (sun/moon) handled with full planetary self-eclipse */\n"
	"        vec3 lightPos = vec3((p0.xy - 0.5) * 65536.0, (p0.z - 0.1) * 8192.0);\n"
	"        float lRange = max(16.0, p3.x * 8192.0);\n"
	"        vec3 toLight = lightPos - stepWorldXY_Z;\n"
	"        float distL = length(toLight);\n"
	"        if (distL >= lRange) continue;\n"
	"        vec3 L = toLight / max(1e-3, distL);\n"
	"        float normD = distL / lRange;\n"
	"        float atten = pow(clamp(1.0 - normD * normD, 0.0, 1.0), 2.0) / (1.0 + 2.5 * normD * normD);\n"
	"        if (lType == 2) {\n"
	"            vec3 spotDir = normalize(p1.xyz * 2.0 - 1.0);\n"
	"            float cosAngle = dot(-L, spotDir);\n"
	"            float cosCut = p3.y;\n"
	"            if (cosAngle <= cosCut) continue;\n"
	"            atten *= pow(clamp((cosAngle - cosCut) / max(1e-3, 1.0 - cosCut), 0.0, 1.0), max(1.0, p3.z * 16.0));\n"
	"        }\n"
	"        float lambert = 0.40 + 0.60 * clamp(dot(cloudGradN, L), 0.0, 1.0);\n"
	"        float cosL = dot(rayDir, L);\n"
	"        float hgL = 0.55 + 0.95 * pow(clamp(0.5 + 0.5 * cosL, 0.0, 1.0), 3.0);\n"
	"        float tauL = dens * 1.45 + (1.0 - hFrac) * 0.25;\n"
	"        float scatterL = exp(-tauL) * lambert * hgL;\n"
	"        localContrib += lCol * (lInt * atten * scatterL);\n"
	"    }\n"
	"    return localContrib;\n"
	"}\n"
	"float getH(ivec2 p, ivec2 maxC) {\n"
	"    return float(texelFetch(uHeightTex, clamp(p, ivec2(0), maxC), 0).r);\n"
	"}\n"
	"/* Smooth C2 quintic Hermite height evaluator on the 49x49 chunk heightfield texture:\n"
	" * Provides exact per-pixel analytical mountain-cloud intersection with zero coarse LOD triangle facets! */\n"
	"float sampleHeightCloud(vec2 uv) {\n"
	"    vec2 texSize = vec2(textureSize(uHeightTex, 0));\n"
	"    vec2 maxCoordF = max(vec2(1.0), texSize - 1.0);\n"
	"    vec2 pixelCoord = clamp(uv, 0.0, 1.0) * maxCoordF;\n"
	"    ivec2 maxCoord = ivec2(maxCoordF);\n"
	"    if (uChunkSpan.x > 10240.0) {\n"
	"        return getH(ivec2(floor(pixelCoord + 0.5)), maxCoord) * 0.0625;\n"
	"    }\n"
	"    ivec2 iCoord = ivec2(floor(pixelCoord));\n"
	"    vec2 f = pixelCoord - vec2(iCoord);\n"
	"    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);\n"
	"    float h00 = getH(iCoord, maxCoord);\n"
	"    float h10 = getH(iCoord + ivec2(1, 0), maxCoord);\n"
	"    float h01 = getH(iCoord + ivec2(0, 1), maxCoord);\n"
	"    float h11 = getH(iCoord + ivec2(1, 1), maxCoord);\n"
	"    return mix(mix(h00, h10, u.x), mix(h01, h11, u.x), u.y) * 0.0625;\n"
	"}\n"
	"vec4 sampleSubstrateSmooth(vec2 uv) {\n"
	"    vec2 texSize = vec2(textureSize(uSubstrateTex, 0));\n"
	"    vec2 maxCoordF = max(vec2(1.0), texSize - 1.0);\n"
	"    vec2 p = clamp(uv, 0.0, 1.0) * maxCoordF;\n"
	"    vec2 ip = floor(p);\n"
	"    vec2 fp = p - ip;\n"
	"    vec2 u = fp * fp * fp * (fp * (fp * 6.0 - 15.0) + 10.0);\n"
	"    return texture(uSubstrateTex, (ip + u + 0.5) / texSize);\n"
	"}\n"
	CHAFF_GLSL_HASH_FN
	CHAFF_GLSL_GRAD_NOISE_FN
	CHAFF_GLSL_FLOW_FN
	CHAFF_GLSL_BAND_FN
	ND_GLSL_SYNOPTIC_CLOUD_FN
	ND_GLSL_CSM_LOOKUP_FN
	ND_GLSL_TORUS_ECLIPSE_FN
	"vec4 eval_recursive_wind(vec2 worldPos, float anim_time) {\n"
	"    return chaff_eval_flow_2d(worldPos, anim_time);\n"
	"}\n"
	"float cloud_remap(float v, float l0, float h0, float l1, float h1) {\n"
	"    return l1 + (v - l0) * (h1 - l1) / max(1e-5, h0 - l0);\n"
	"}\n"
	"const float OCTAVE_MIN_PX  = 3.50;\n"
	"const float OCTAVE_FULL_PX = 10.00;\n"
	"float nd_octave_weight(float wavelength, float fw) {\n"
	"    return smoothstep(OCTAVE_MIN_PX, OCTAVE_FULL_PX, wavelength / max(fw, 1e-4));\n"
	"}\n"
	"/* 3D Atmospheric wind column: the xy-dependent flow noises are sampled ONCE per pixel (nd_cloud_wind_column) and the\n"
	" * Ekman altitude veer / shear / gust / updraft are then derived analytically per raymarch step (nd_cloud_wind_at_height),\n"
	" * exactly reproducing chaff_eval_wind_3d(zBase = 380, zTop = 980) without re-evaluating its 5 gradient noises per step. */\n"
	"void nd_cloud_wind_column(vec2 xy, float anim_time, float fw, out vec4 synFlow, out vec4 sfcFlow) {\n"
	"    synFlow = chaff_eval_synoptic_flow_2d(xy, anim_time, fw);\n"
	"    float w0 = smoothstep(0.45, 0.12, 0.0055 * fw);\n"
	"    if (w0 <= 0.001) {\n"
	"        sfcFlow = vec4(0.741, -0.671, 0.65, 0.0);\n"
	"        return;\n"
	"    }\n"
	"    float w1 = smoothstep(0.45, 0.12, 0.0280 * fw);\n"
	"    float w2 = smoothstep(0.45, 0.12, 0.1200 * fw);\n"
	"    sfcFlow = chaff_eval_flow_2d_banded(xy, anim_time, vec3(w0, w1, w2));\n"
	"}\n"
	"void nd_cloud_wind_at_height(vec4 synFlow, vec4 sfcFlow, float h, out vec2 windDir, out vec2 shearDisp, out float gust, out float updraft) {\n"
	"    const float spanZ = 600.0;\n"
	"    float veerAngle = h * 0.488;\n"
	"    float cosV = cos(veerAngle), sinV = sin(veerAngle);\n"
	"    vec2 baseDir = normalize(mix(sfcFlow.xy, synFlow.xy, 0.35 + 0.45 * h));\n"
	"    windDir = vec2(baseDir.x * cosV - baseDir.y * sinV, baseDir.x * sinV + baseDir.y * cosV);\n"
	"    float atmoSpeed = 1.0 + 0.85 * pow(h, 1.35);\n"
	"    shearDisp = windDir * (pow(h, 1.25) * spanZ * 0.35) * atmoSpeed;\n"
	"    gust = clamp(mix(sfcFlow.z, 1.0 + 0.50 * (synFlow.z - 0.5), h), 0.25, 1.75);\n"
	"    updraft = clamp(0.45 * synFlow.z + 0.35 * sfcFlow.z + 0.20 * sfcFlow.w, 0.10, 1.50);\n"
	"}\n"
	"/* Hoisted Per-Pixel Column Weather Envelope:\n"
	" * Evaluates the synoptic/mesoscale macro field and baroclinic altocumulus shelf ONCE per pixel at entryWorldPos\n"
	" * instead of 4 times inside the raymarch loop, returning vec3(deck3Cov, deck2Cov, deck1Cov) so empty decks are\n"
	" * skipped in 1 instruction and clear columns discard before wind/raymarching! */\n"
	"void eval_cloud_column_decks(vec2 entryXY, float anim_time, float humidity, float fw,\n"
	"                             vec4 synFlow, vec4 sfcFlow, vec4 synCarrier, vec2 pZonalBase, vec2 baseMacroGrad,\n"
	"                             out vec3 outDeckCov, out vec3 outDeckH, out vec2 outMacroGrad) {\n"
	"    vec2 windMidDir; vec2 shearMid; float gustMid; float updraft;\n"
	"    nd_cloud_wind_at_height(synFlow, sfcFlow, 0.22, windMidDir, shearMid, gustMid, updraft);\n"
	"    vec2 synQueryXY = entryXY - shearMid;\n"
	"    float highCirrus = 0.0;\n"
	"    vec4 synCloud = eval_synoptic_macro_cloud_from_carrier(synQueryXY, anim_time, humidity, fw, true,\n"
	"                                                           synCarrier, pZonalBase, baseMacroGrad, outMacroGrad, highCirrus);\n"
	"    float macroCoverage = synCloud.x;\n"
	"    float coreStrength  = smoothstep(0.06, 0.75, macroCoverage);\n"
	"    float hTop1         = 0.14 + 0.13 * coreStrength;\n"
	"    float deepCore      = smoothstep(0.62, 0.92, coreStrength);\n"
	"    float towerTopH     = mix(0.24, 0.86, deepCore * (0.45 + 0.55 * updraft));\n"
	"    float altoGate      = smoothstep(0.18, 0.56, synCloud.y - (0.44 - 0.06 * humidity));\n"
	"    float wAlto0        = smoothstep(0.32, 0.08, 0.00000145 * fw);\n"
	"    float altoShelf     = 0.0;\n"
	"    if (altoGate > 0.001 && wAlto0 > 0.001) {\n"
	"        float wAlto = smoothstep(0.30, 0.08, 0.00001600 * fw);\n"
	"        float altoPhase = mod(anim_time * 0.018, 1024.0);\n"
	"        vec2 altoPos = vec2(0.60 * synQueryXY.x - 0.80 * synQueryXY.y, 0.80 * synQueryXY.x + 0.60 * synQueryXY.y)\n"
	"                     + synCloud.zw * 0.18 + vec2(42000.0, -31000.0);\n"
	"        vec3 alto0 = chaff_grad_noise2d(altoPos * 0.00000145 - windMidDir * (altoPhase * 0.45), 0x414C5430u);\n"
	"        vec3 altoN = (wAlto > 0.001) ? chaff_grad_noise2d(altoPos * 0.00001600 - windMidDir * altoPhase, 0x414C544Fu) : vec3(0.55, 0.0, 0.0);\n"
	"        float altoWave0 = smoothstep(0.36, 0.78, 1.0 - 2.0 * abs(alto0.x - 0.5));\n"
	"        float altoWave1 = smoothstep(0.34, 0.74, 1.0 - 2.0 * abs(altoN.x - 0.5));\n"
	"        altoShelf = altoGate * wAlto0 * mix(altoWave0, 0.52 * altoWave0 + 0.48 * altoWave1, wAlto) * 0.72;\n"
	"    }\n"
	"    float anvilCov = smoothstep(0.42, 0.80, macroCoverage) * (0.50 + 0.50 * updraft) * 0.74;\n"
	"    float covDeck3 = max(anvilCov, highCirrus * 0.70);\n"
	"    float covDeck2 = max(macroCoverage * deepCore, altoShelf);\n"
	"    float covDeck1 = macroCoverage;\n"
	"    outDeckCov = vec3(covDeck3, covDeck2, covDeck1);\n"
	"    outDeckH   = vec3(coreStrength, hTop1, towerTopH);\n"
	"}\n"
	"/* Multi-Deck 3D Tropospheric Step Sculptor:\n"
	" * Uses the hoisted column deck coverages (deckCov, deckH, macroGrad) and evaluates 4D billow sculpting\n"
	" * ONLY for the active non-empty deck. At medium/far zoom (dist > 6000) or upper decks, evaluates only the\n"
	" * single dominant band-pass octave from wBand4, cutting 4D hypercube corner hashes by 2x-4x! */\n"
	"vec4 eval_zoom_cloud_density_grad(vec3 worldPos, float anim_time, float dist, vec4 synFlow, vec4 sfcFlow,\n"
	"                                  vec3 deckCov, vec3 deckH, vec2 macroGrad, vec4 wBand4) {\n"
	"    float z = worldPos.z;\n"
	"    const float zBase = 420.0;\n"
	"    const float zTop  = 2820.0;\n"
	"    if (z <= zBase || z >= zTop) return vec4(0.0);\n"
	"    float hNorm = (z - zBase) / (zTop - zBase);\n"
	"    float deckIdx = (hNorm < 0.31) ? 0.0 : ((hNorm < 0.68) ? 1.0 : 2.0);\n"
	"    float coreStrength = deckH.x;\n"
	"    float hTop1        = deckH.y;\n"
	"    float towerTopH    = deckH.z;\n"
	"    float deck1Prof = smoothstep(0.0, 0.045, hNorm) * (1.0 - smoothstep(hTop1 - 0.06, hTop1, hNorm));\n"
	"    float altoProf  = smoothstep(0.35, 0.42, hNorm) * (1.0 - smoothstep(0.57, 0.64, hNorm));\n"
	"    float towerProf = smoothstep(0.12, 0.24, hNorm) * (1.0 - smoothstep(towerTopH - 0.10, towerTopH, hNorm));\n"
	"    float deck2Prof = max(altoProf, towerProf);\n"
	"    float anvilProf = smoothstep(0.72, 0.81, hNorm) * (1.0 - smoothstep(0.92, 1.0, hNorm));\n"
	"    float multiDeck = max(deckCov.z * deck1Prof, max(deckCov.y * deck2Prof, deckCov.x * anvilProf));\n"
	"    float vProf     = clamp(max(deck1Prof, max(deck2Prof, anvilProf)), 0.0, 1.0);\n"
	"    float baseCloud = clamp(cloud_remap(multiDeck, 0.06 * (1.0 - 0.50 * coreStrength), 0.78, 0.0, 1.0), 0.0, 1.0);\n"
	"    if (baseCloud <= 0.001) return vec4(0.0);\n"
	"    vec2 atmoWindDir; vec2 shearDisp; float atmoGust; float updraft;\n"
	"    nd_cloud_wind_at_height(synFlow, sfcFlow, hNorm, atmoWindDir, shearDisp, atmoGust, updraft);\n"
	"    vec2 synQueryXY = worldPos.xy - shearDisp - atmoWindDir * (hNorm * 1400.0);\n"
	"    const float freqs[4] = float[4](0.00026000, 0.00160000, 0.00960000, 0.05800000);\n"
	"    const float spds[4]  = float[4](0.045, 0.095, 0.195, 0.360);\n"
	"    const uint  seeds[4] = uint[4](0x434C4434u, 0x434C4435u, 0x434C4437u, 0x434C4438u);\n"
	"    const float zScales[4] = float[4](1.00, 1.55, 2.40, 3.70);\n"
	"    const float wScales[4] = float[4](1.00, 1.50, 2.25, 3.40);\n"
	"    const float amps[4]    = float[4](0.38, 0.30, 0.24, 0.18);\n"
	"    const float wDispScale[4] = float[4](1.00, 0.50, 0.22, 0.08);\n"
	"    const mat2 rot  = mat2(0.8, -0.6, 0.6, 0.8);\n"
	"    const mat2 irot = mat2(0.8,  0.6, -0.6, 0.8);\n"
	"    float wBoil = mod(anim_time * (0.055 * atmoGust * updraft), 512.0);\n"
	"    float zBaseSlice = hNorm * 38.0 + deckIdx * 23.0;\n"
	"    float wSlice     = hNorm * 22.0 + deckIdx * 15.7;\n"
	"    float dzMacro = (hNorm < 0.30) ? ((hNorm < 0.12) ? 1.2 : -1.4)\n"
	"                  : ((hNorm < 0.67) ? ((hNorm < 0.48) ? 1.1 : -1.3) : ((hNorm < 0.84) ? 1.0 : -1.5));\n"
	"    vec3 gradAcc = vec3(macroGrad * 0.085, dzMacro * 0.045);\n"
	"    float billowFbm = 0.0;\n"
	"    float turretShift = 0.0;\n"
	"    float sliceAngle = hNorm * 18.0 + deckIdx * 2.1;\n"
	"    vec2 sliceDir = vec2(cos(sliceAngle), sin(sliceAngle));\n"
	"    vec2 pRot = synQueryXY;\n"
	"    mat2 cur_rot = mat2(1.0, 0.0, 0.0, 1.0);\n"
	"    bool singleOct = (dist > 6000.0 || deckIdx > 0.5 || (uCloudDeck.z > 0.5 && uCloudDeck.z <= 2.5));\n"
	"    int bestJ = (wBand4.y > wBand4.x) ? 1 : 0;\n"
	"    float bestW = max(wBand4.x, wBand4.y);\n"
	"    if (wBand4.z > bestW) { bestJ = 2; bestW = wBand4.z; }\n"
	"    if (wBand4.w > bestW) { bestJ = 3; }\n"
	"    for (int j = 0; j < 4; j++) {\n"
	"        float w = wBand4[j];\n"
	"        if ((!singleOct || j == bestJ) && w > 0.015) {\n"
	"            float phase = mod(anim_time * spds[j], 1024.0);\n"
	"            vec2 q = (pRot + shearDisp * wDispScale[j]) * freqs[j] - atmoWindDir * phase;\n"
	"            vec4 n4 = chaff_grad_noise4d(vec4(q, zBaseSlice * zScales[j], wBoil * wScales[j] + wSlice * zScales[j]), seeds[j]);\n"
	"            float billow = 1.0 - 2.0 * abs(n4.x - 0.5);\n"
	"            float effAmp = amps[j] * w;\n"
	"            billowFbm += (billow - 0.50) * effAmp;\n"
	"            float dBill = 4.0 * (0.5 - n4.x);\n"
	"            vec2 wg = cur_rot * n4.yz;\n"
	"            turretShift += dot(wg, sliceDir) * effAmp;\n"
	"            gradAcc += vec3(wg * (dBill * freqs[j] * effAmp), n4.w * (dBill * 0.0160 * zScales[j] * effAmp));\n"
	"        }\n"
	"        pRot = rot * pRot;\n"
	"        cur_rot = cur_rot * irot;\n"
	"    }\n"
	"    float hLocal = (hNorm < 0.31) ? clamp(hNorm / max(hTop1, 0.05), 0.0, 1.0)\n"
	"                 : ((hNorm < 0.68) ? clamp((hNorm - 0.35) / 0.29, 0.0, 1.0) : clamp((hNorm - 0.72) / 0.28, 0.0, 1.0));\n"
	"    float crownRamp  = smoothstep(0.18, 0.95, hLocal);\n"
	"    float crownErode = crownRamp * (0.32 * (1.0 - 0.45 * coreStrength));\n"
	"    float sculpted   = baseCloud - crownErode + (billowFbm + turretShift * 0.48) * (0.66 + 0.58 * hLocal);\n"
	"    float dens = smoothstep(0.04, 0.58, clamp(sculpted, 0.0, 1.0)) * vProf;\n"
	"    gradAcc.z -= crownRamp * 0.05 * (1.0 - 0.45 * coreStrength);\n"
	"    return vec4(dens, gradAcc * vProf);\n"
	"}\n"
	"void main() {\n"
	"    vec3 torusN = normalize(vTorusNormal);\n"
	"    vec3 tangentU = normalize(vTangentU);\n"
	"    vec3 tangentV = normalize(vTangentV);\n"
	"    float fragDist = max(1e-3, length(vViewDir));\n"
	"    vec3 viewDir = vViewDir / fragDist;\n"
	"    vec3 rayDir  = -viewDir;\n"
	"    float r0     = (uTorusRadii.y > 0.0) ? uTorusRadii.y : 1335000.0;\n"
	"    float vn     = dot(viewDir, torusN);\n"
	"    if (vn <= 0.02) discard;\n"
	"    float vnDist = dot(vViewDir, torusN);\n"
	"    float tangD2 = max(0.0, fragDist * fragDist - vnDist * vnDist);\n"
	"    float curvSagitta = tangD2 / (2.0 * r0);\n"
	"    float camAlt = vShellZ + vnDist + curvSagitta;\n"
	"    float dz     = dot(rayDir, torusN);\n"
	"    vec2  dxy    = vec2(dot(rayDir, tangentU), dot(rayDir, tangentV));\n"
	"    const float zBase = 420.0;\n"
	"    const float zTop  = 2820.0;\n"
	"    float deckSpan = zTop - zBase;\n"
	"    float zoomRamp = smoothstep(480.0, 1200.0, uCamDist);\n"
	"    float camClearRamp = zoomRamp;\n"
	"    if (camClearRamp <= 0.001) discard;\n"
	"    float effOrbitLift = clamp((uCamDist - 1800.0) * 0.065, 0.0, 16000.0);\n"
	"    float B_shell = (r0 + vShellZ) * vn;\n"
	"    float dz0 = zBase - vShellZ;\n"
	"    float dR0 = dz0 * (2.0 * r0 + zBase + vShellZ);\n"
	"    float u0  = dR0 / max(1.0, sqrt(max(1.0, B_shell * B_shell + dR0)) + B_shell);\n"
	"    float zTopProj = zTop + effOrbitLift;\n"
	"    float dz1 = zTopProj - vShellZ;\n"
	"    float dR1 = dz1 * (2.0 * r0 + zTopProj + vShellZ);\n"
	"    float u1  = dR1 / max(1.0, sqrt(max(1.0, B_shell * B_shell + dR1)) + B_shell);\n"
	"    float sEnter = -u1;\n"
	"    float sExit  = -u0;\n"
	"    if (sExit <= sEnter + 1.0) discard;\n"
	"    float maxHorizShift = mix(4800.0, 1200.0, smoothstep(12000.0, 60000.0, uCamDist));\n"
	"    float sMidSlab = clamp(0.5 * (sEnter + sExit), -maxHorizShift, maxHorizShift);\n"
	"    vec2 entryWorldPos = vWorldPos + dxy * sMidSlab;\n"
	"    float rawFw = max(1e-4, max(length(dFdx(entryWorldPos)), length(dFdy(entryWorldPos))));\n"
	"    float anaFw = max(0.18, fragDist * 0.0014 / max(0.28, vn));\n"
	"    float fw = clamp(mix(rawFw, max(rawFw, anaFw), smoothstep(2500.0, 10000.0, uCamDist)), 0.05, 250000.0);\n"
	"    vec3 worldClimate = chaff_grad_noise2d(entryWorldPos * 0.000045, 0x434C494Du);\n"
	"    float seaWet = (uSeaLevel > -900.0) ? 1.0 : 0.0;\n"
	"    float texBioHumidity = clamp(0.50 + 0.28 * (worldClimate.x - 0.5) + 0.10 * seaWet, 0.08, 0.95);\n"
	"    float latPhi = 6.28318530718 * ((64.0 - entryWorldPos.y) / 8388608.0);\n"
	"    float latDeg = asin(clamp(abs(sin(latPhi)), 0.0, 1.0)) * 57.2957795;\n"
	"    float innerHole = max(0.0, -cos(latPhi));\n"
	"    float eqEvapDry = smoothstep(24.0, 0.0, latDeg) * 0.18;\n"
	"    float midLatWet = smoothstep(18.0, 0.0, abs(latDeg - 38.0)) * 0.12;\n"
	"    float subpolarWet = smoothstep(10.0, 0.0, abs(latDeg - 58.0)) * 0.08;\n"
	"    float worldBioHumidity = clamp(texBioHumidity - eqEvapDry + midLatWet + subpolarWet, 0.06, 0.92);\n"
	"    float bioHumidity = worldBioHumidity;\n"
	"    vec4 synCarrier; vec2 pDispBase; vec2 pZonalBase; vec2 baseMacroGrad;\n"
	"    if (eval_synoptic_carrier(entryWorldPos, uAnimTime, bioHumidity, fw, synCarrier, pDispBase, pZonalBase, baseMacroGrad)) discard;\n"
	"    vec4 windSyn; vec4 windSfc;\n"
	"    nd_cloud_wind_column(entryWorldPos, uAnimTime, fw, windSyn, windSfc);\n"
	"    vec3 deckBaseCov; vec3 deckH; vec2 colMacroGrad;\n"
	"    eval_cloud_column_decks(entryWorldPos, uAnimTime, bioHumidity, fw, windSyn, windSfc,\n"
	"                            synCarrier, pZonalBase, baseMacroGrad, deckBaseCov, deckH, colMacroGrad);\n"
	"    if (max(deckBaseCov.x, max(deckBaseCov.y, deckBaseCov.z)) <= 0.002) discard;\n"
	"    vec3 sDir = (length(uSunDir) > 0.001) ? normalize(uSunDir) : vec3(1.0, 0.0, 0.0);\n"
	"    vec3 mDir = (length(uMoonDir) > 0.001) ? normalize(uMoonDir) : vec3(-sDir.x, -sDir.y, max(0.25, -sDir.z));\n"
	"    float orbitLight = smoothstep(24000.0, 220000.0, uCamDist);\n"
	"    vec3 L_local = normalize(-0.577 * tangentU - 0.577 * tangentV + 0.816 * torusN);\n"
	"    vec3 sunLocal = vec3(dot(sDir, tangentU), dot(sDir, tangentV), dot(sDir, torusN));\n"
	"    vec3 L_day = (sunLocal.z > -0.08) ? normalize(vec3(sunLocal.xy, max(0.12, sunLocal.z))) : L_local;\n"
	"    vec3 sunDir = normalize(mix(L_day, normalize(sDir + 0.35 * torusN), orbitLight));\n"
	"    vec3 moonLocal = vec3(dot(mDir, tangentU), dot(mDir, tangentV), dot(mDir, torusN));\n"
	"    vec3 moonDir = (moonLocal.z > -0.05) ? normalize(vec3(moonLocal.xy, max(0.15, moonLocal.z))) : L_local;\n"
	"    float sunDot = dot(torusN, sDir);\n"
	"    float moonDot = dot(torusN, mDir);\n"
	"    vec3 P_torus = (uCameraPos - vViewDir) - uPlanetCenter;\n"
	"    float R_torus = (uTorusRadii.x > 0.0) ? uTorusRadii.x : 2000000.0;\n"
	"    float r0_torus = (uTorusRadii.y > 0.0) ? uTorusRadii.y : 1335000.0;\n"
	"    float innerEclipse = (sunDot > 0.0 && innerHole > 0.001) ? eval_torus_self_eclipse(P_torus, sDir, R_torus, r0_torus) : 1.0;\n"
	"    float effSun = sunDot * innerEclipse;\n"
	"    float effMoon = max(0.0, moonDot) * (1.0 - smoothstep(-0.10, 0.15, effSun));\n"
	"    float cloudDayFactor = clamp(0.16 + 0.84 * smoothstep(-0.16, 0.22, effSun), 0.16, 1.0);\n"
	"    float cloudTwilight = smoothstep(0.32, -0.06, effSun) * smoothstep(-0.16, 0.12, effSun);\n"
	"    vec3 cloudSunCol = mix(vec3(1.06, 1.00, 0.91), vec3(1.14, 0.62, 0.32), cloudTwilight);\n"
	"    vec3 cloudMoonCol = vec3(0.52, 0.68, 0.96);\n"
	"    vec2 sunHoriz = vec2(-0.7071, -0.7071);\n"
	"    const float bFreqs[4] = float[4](0.00026000, 0.00160000, 0.00960000, 0.05800000);\n"
	"    const float bFar[4]   = float[4](650000.0, 95000.0, 14000.0, 2200.0);\n"
	"    const float bNear[4]  = float[4](150000.0, 18000.0,  2600.0,  380.0);\n"
	"    float wRaw[4];\n"
	"    for (int j = 0; j < 4; j++) {\n"
	"        float wDist = clamp((bFar[j] - uCamDist) / max(1.0, bFar[j] - bNear[j]), 0.0, 1.0);\n"
	"        wRaw[j] = wDist * nd_octave_weight(1.0 / bFreqs[j], fw);\n"
	"    }\n"
	"    vec4 wBand4 = vec4(wRaw[0] * (1.0 - wRaw[2]), wRaw[1] * (1.0 - wRaw[3]), wRaw[2], wRaw[3]);\n"
	"    float effVn = mix(max(0.22, vn), max(0.52, vn), smoothstep(8000.0, 45000.0, uCamDist));\n"
	"    float physMarchSpan = 600.0 / effVn;\n"
	"    float baseStepLen = physMarchSpan / 3.0;\n"
	"    float trans = 1.0;\n"
	"    float rayGroundVis = 1.0;\n"
	"    float upperDeckOcc = 0.0;\n"
	"    vec3 accumLight = vec3(0.0);\n"
	"    float cosTheta = dot(rayDir, sunDir);\n"
	"    float hgForward = 0.62 + 1.55 * pow(clamp(0.5 + 0.5 * cosTheta, 0.0, 1.0), 5.0);\n"
	"    float hgBack    = 0.28 * pow(clamp(0.5 - 0.5 * cosTheta, 0.0, 1.0), 2.2);\n"
	"    float hgPhase   = hgForward + hgBack;\n"
	"    float cosThetaMoon = dot(rayDir, moonDir);\n"
	"    float hgMoon = 0.55 + 1.15 * pow(clamp(0.5 + 0.5 * cosThetaMoon, 0.0, 1.0), 4.0);\n"
	"    float ign       = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));\n"
	"    float jitter    = 0.5 + (ign - 0.5) * 0.22;\n"
	"    float sunStepDist = clamp(max(90.0, fw * 3.2), 90.0, 24000.0);\n"
	"    float baseTowerOcc = 0.0;\n"
	"    bool towerReady = false;\n"
	"    float baseTerrZ = sampleHeightCloud(vUV);\n"
	"    int maxSteps = (uCloudDeck.z >= 1.0 && uCloudDeck.z <= 4.0) ? int(uCloudDeck.z + 0.5) : 4;\n"
	"    for (int s = 0; s < 4; s++) {\n"
	"        if (s >= maxSteps) break;\n"
	"        if (trans < 0.008 || rayGroundVis < 0.005) break;\n"
	"        if (s == 3 && uCamDist > 6000.0) break;\n"
	"        if (s == 2 && uCamDist > 28000.0) break;\n"
	"        int deckIdx = (s < 2) ? s : 2;\n"
	"        float deckCovS = (deckIdx == 0) ? deckBaseCov.x : ((deckIdx == 1) ? deckBaseCov.y : deckBaseCov.z);\n"
	"        if (deckCovS <= 0.002) continue;\n"
	"        float uSub  = (s < 2) ? jitter : ((uCamDist > 6000.0 || maxSteps <= 3) ? (0.25 + 0.50 * jitter) : ((s == 2) ? (0.45 * jitter) : (0.50 + 0.45 * jitter)));\n"
	"        float stepLen = (s < 2 || uCamDist > 6000.0 || maxSteps <= 3) ? baseStepLen : (baseStepLen * 0.5);\n"
	"        float hTopD = (deckIdx == 0) ? 1.00 : ((deckIdx == 1) ? 0.64 : 0.27);\n"
	"        float hBotD = (deckIdx == 0) ? 0.72 : ((deckIdx == 1) ? 0.35 : 0.00);\n"
	"        float hFrac = mix(hTopD, hBotD, uSub);\n"
	"        float z     = zBase + hFrac * deckSpan;\n"
	"        float zProj = z + hFrac * effOrbitLift;\n"
	"        float dzP   = zProj - vShellZ;\n"
	"        float dRP   = dzP * (2.0 * r0 + zProj + vShellZ);\n"
	"        float uRay  = dRP / max(1.0, sqrt(max(1.0, B_shell * B_shell + dRP)) + B_shell);\n"
	"        float sStep = clamp(-uRay, -maxHorizShift, maxHorizShift);\n"
	"        vec2 sampleXY = vWorldPos + dxy * sStep;\n"
	"        float stepClear = smoothstep(baseTerrZ - 24.0, baseTerrZ + 110.0, z);\n"
	"        rayGroundVis *= smoothstep(baseTerrZ - 36.0, baseTerrZ + 28.0, z);\n"
	"        if (stepClear * rayGroundVis <= 0.002) continue;\n"
	"        vec4 cloudSample = eval_zoom_cloud_density_grad(vec3(sampleXY, z), uAnimTime, uCamDist, windSyn, windSfc,\n"
	"                                                        deckBaseCov, deckH, colMacroGrad, wBand4);\n"
	"        float dens = cloudSample.x * camClearRamp * stepClear * rayGroundVis;\n"
	"        if (dens > 0.002) {\n"
	"            vec3 grad = cloudSample.yzw;\n"
	"            if (!towerReady) {\n"
	"                vec2 probeXY = entryWorldPos + sunHoriz * sunStepDist;\n"
	"                float probeDens = 0.0;\n"
	"                if (uCsm.z < 0.0) {\n"
	"                    probeDens = 0.0;\n"
	"                } else if (uCsm.z > 0.0 && max(abs(probeXY.x - uCsm.x), abs(probeXY.y - uCsm.y)) < uCsm.z * 0.96) {\n"
	"                    probeDens = nd_csm_density(probeXY);\n"
	"                } else {\n"
	"                    vec4 sunTowerCloud = eval_synoptic_macro_cloud_fw(probeXY, uAnimTime, bioHumidity, max(fw * 1.5, 400.0));\n"
	"                    probeDens = sunTowerCloud.x;\n"
	"                }\n"
	"                baseTowerOcc = smoothstep(0.10, 0.72, probeDens);\n"
	"                towerReady = true;\n"
	"            }\n"
	"            float towerOcc = baseTowerOcc * (1.0 - 0.55 * hFrac);\n"
	"            vec3 cloudGradN = normalize(\n"
	"                (-grad.x * 28.0) * tangentU +\n"
	"                (-grad.y * 28.0) * tangentV +\n"
	"                (0.32 + 0.58 * hFrac - grad.z * 16.0) * torusN\n"
	"            );\n"
	"            float sunLambert = clamp(dot(cloudGradN, sunDir), 0.0, 1.0);\n"
	"            float moonLambert = clamp(dot(cloudGradN, moonDir), 0.0, 1.0);\n"
	"            float hLocalS = clamp((hFrac - hBotD) / max(1e-3, hTopD - hBotD), 0.0, 1.0);\n"
	"            float sunSlopeOcc = clamp(dot(grad.xy, sunHoriz) * 24.0 + grad.z * 14.0, -0.55, 1.10);\n"
	"            float selfOcc = smoothstep(0.08, 0.75, dens) * (1.0 - 0.72 * hLocalS);\n"
	"            float tauSun = max(0.05, selfOcc * 1.15 + max(0.0, sunSlopeOcc) * 1.65 + towerOcc * 1.35 + upperDeckOcc * 2.60 + (1.0 - hFrac) * 0.55);\n"
	"            float directSun    = exp(-tauSun * 0.95) * (0.35 + 0.65 * sunLambert);\n"
	"            float directMoon   = exp(-tauSun * 0.85) * (0.30 + 0.70 * moonLambert) * effMoon * 0.42;\n"
	"            float multiScatter = exp(-tauSun * 0.35) * (1.0 - exp(-dens * 3.2));\n"
	"            float thinEdge   = smoothstep(0.02, 0.14, dens) * (1.0 - smoothstep(0.22, 0.52, dens));\n"
	"            float silverLining = thinEdge * directSun * clamp(hgPhase, 0.0, 2.2) * 0.38;\n"
	"            float rainCore = smoothstep(0.10, 0.55, dens) * smoothstep(0.40, 0.75, bioHumidity);\n"
	"            vec3 deck1Sh = mix(vec3(0.30, 0.38, 0.54), vec3(0.18, 0.22, 0.32), rainCore * 0.75);\n"
	"            vec3 deck2Sh = vec3(0.46, 0.54, 0.70);\n"
	"            vec3 deck3Sh = vec3(0.72, 0.80, 0.94);\n"
	"            vec3 shadowCol = (deckIdx == 2) ? deck1Sh : ((deckIdx == 1) ? deck2Sh : deck3Sh);\n"
	"            shadowCol *= (1.0 - 0.42 * clamp(upperDeckOcc, 0.0, 1.0));\n"
	"            vec3 scatterCol = vec3(0.84, 0.89, 0.96);\n"
	"            vec3 sunLitCol  = cloudSunCol;\n"
	"            vec3 stepCol = mix(shadowCol, scatterCol * (1.0 - 0.30 * clamp(upperDeckOcc, 0.0, 1.0)), clamp(multiScatter * 0.85, 0.0, 1.0));\n"
	"            stepCol = mix(stepCol, sunLitCol * (0.88 + 0.12 * clamp(hgPhase, 0.0, 2.0)), clamp(directSun * 1.20, 0.0, 1.0));\n"
	"            stepCol += sunLitCol * silverLining;\n"
	"            stepCol += cloudMoonCol * (directMoon * (0.85 + 0.15 * clamp(hgMoon, 0.0, 2.0)));\n"
	"            if (uClusterLightCount > 0 && uCamDist < 18000.0) {\n"
	"                vec2 relStepXY = vRelXY + dxy * sStep;\n"
	"                stepCol += eval_clustered_cloud_lighting(gl_FragCoord.xy, uViewport, vec3(relStepXY, z), fragDist,\n"
	"                                                         dens, cloudGradN, rayDir, hFrac, upperDeckOcc);\n"
	"            }\n"
	"            float deckExt     = (deckIdx == 0) ? 0.0052 : ((deckIdx == 1) ? 0.0076 : (0.0095 + 0.0030 * rainCore));\n"
	"            float stepTau     = pow(dens, 1.08) * (stepLen * deckExt);\n"
	"            float stepAlpha   = 1.0 - exp(-stepTau);\n"
	"            accumLight += trans * stepCol * stepAlpha;\n"
	"            trans *= (1.0 - stepAlpha);\n"
	"            upperDeckOcc += stepAlpha;\n"
	"        }\n"
	"    }\n"
	"    float rawAlpha = 1.0 - trans;\n"
	"    float maxCloudAlpha = mix(0.95, 0.84, smoothstep(12000.0, 65000.0, uCamDist));\n"
	"    float cloudAlpha = clamp(rawAlpha, 0.0, maxCloudAlpha);\n"
	"    if (cloudAlpha <= 0.003) discard;\n"
	"    vec3 cloudRgb = (accumLight / max(1e-4, rawAlpha)) * cloudDayFactor;\n"
	"    vec3 sky = ((length(uSkyColor) > 0.001) ? uSkyColor : vec3(0.55, 0.70, 0.85)) * (0.16 + 0.84 * cloudDayFactor);\n"
	"    float cloudSunAlign = max(0.0, dot(normalize(viewDir), -sDir));\n"
	"    float cloudAntiSunAlign = max(0.0, dot(normalize(viewDir), sDir));\n"
	"    vec3 cloudTwilightGlow = mix(vec3(0.98, 0.54, 0.28), vec3(0.82, 0.36, 0.48), smoothstep(0.12, -0.08, effSun));\n"
	"    vec3 cloudTwilightSky = mix(cloudTwilightGlow * 0.78, cloudTwilightGlow * 1.30, pow(cloudSunAlign, 3.0));\n"
	"    vec3 cloudBelt = mix(vec3(0.24, 0.32, 0.52), vec3(0.92, 0.46, 0.62), smoothstep(0.02, 0.35, max(0.0, dot(torusN, viewDir))));\n"
	"    cloudTwilightSky = mix(cloudTwilightSky, cloudBelt, pow(cloudAntiSunAlign, 2.5) * cloudTwilight);\n"
	"    sky = mix(sky, cloudTwilightSky, cloudTwilight * 0.76);\n"
	"    float limb = pow(clamp(1.0 - max(0.0, dot(torusN, viewDir)), 0.0, 1.0), 3.0);\n"
	"    cloudRgb = mix(cloudRgb, vec3(0.45, 0.72, 0.98), limb * 0.42 * uAtmoIntensity);\n"
	"    float fogFactor = clamp((vDistance - uFogStart) / max(1.0, uFogEnd - uFogStart), 0.0, 1.0);\n"
	"    vec3 finalCloudRgb = mix(cloudRgb, sky, fogFactor * uAtmoIntensity);\n"
	"    FragColor = vec4(finalCloudRgb * cloudAlpha, cloudAlpha);\n"
	"}\n";

static const char *s_fs_cloud_comp =
	"#version 300 es\n"
	"precision highp float;\n"
	"in vec2 vUv;\n"
	"uniform sampler2D uCloudTex;\n"
	"out vec4 FragColor;\n"
	"void main() {\n"
	"    vec4 c = texture(uCloudTex, vUv);\n"
	"    if (c.a <= 0.001) discard;\n"
	"    FragColor = c;\n"
	"}\n";


/* Hi-Z Buffer Downsample Shader (builds hierarchical depth pyramid) */
static const char *s_vs_hiz =
		"#version 300 es\n"
		"precision highp float;\n"
		"layout (location = 0) in vec2 a_pos;\n"
		"out vec2 vUv;\n"
		"void main() {\n"
		"    vUv = a_pos * 0.5 + 0.5;\n"
		"    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
		"}\n";
static const char *s_fs_hiz_downsample =
	"#version 300 es\n"
	"precision highp float;\n"
	"in vec2 vUv;\n"
	"uniform sampler2D uDepthTex;\n"
	"uniform float uTexelSize;\n"
	"out float FragDepth;\n"
	"void main() {\n"
	"    /* 2x2 box filter: compute MIN depth (nearest) over 4 texels */\n"
	"    vec2 texel = uTexelSize * vec2(1.0, 1.0);\n"
	"    float d00 = texture(uDepthTex, vUv - texel * 0.5).r;\n"
	"    float d10 = texture(uDepthTex, vUv + texel * vec2(0.5, -0.5)).r;\n"
	"    float d01 = texture(uDepthTex, vUv + texel * vec2(-0.5, 0.5)).r;\n"
	"    float d11 = texture(uDepthTex, vUv + texel * 0.5).r;\n"
	"    FragDepth = min(min(d00, d10), min(d01, d11));\n"
	"}\n";
const char *nd_seven_gl_vs_csm(void) { return s_vs_csm; }
const char *nd_seven_gl_fs_csm(void) { return s_fs_csm; }
const char *nd_seven_gl_vs_cloud(void) { return s_vs_cloud; }
const char *nd_seven_gl_fs_cloud(void) { return s_fs_cloud; }

/* --- Pure-C Torus Geometry & Camera Math --- */

typedef struct { float x, y; } grass_lamb_pos_t;
static struct { float x, y; } s_grass_lamb_pos = { 0.0f, 0.0f };

/* Global ridge occlusion buffer for water/grass/billboard queue gating */
#define SEVEN_HORIZON_BINS 128
static float s_ridge_horizon_ndc_y[SEVEN_HORIZON_BINS];
static int s_ridge_occlusion_valid = 0;

/* 2D Hierarchical Occlusion Map (replaces 1D ridge buffer for more accurate culling) */
#define ND_OCC2D_MAX_LEVELS 5
#define ND_OCC2D_TILE_SIZE 32  // Base tile size in pixels
static float s_occ2d_pyramid[ND_OCC2D_MAX_LEVELS][64][64];  // Min depth per tile per level
static int s_occ2d_valid = 0;
static int s_occ2d_levels = 0;
static uint32_t s_occ2d_w = 0, s_occ2d_h = 0;
static float s_ridge_cam_flat_y = 0.0f;
static int s_ridge_use_occlusion = 0;

static float s_grass_anim_time = 0.0f;
static float s_view_wind_sx = 0.0f;
static float s_view_wind_sy = 0.0f;
static float s_view_wind_dz = 0.0f;
static float s_view_wind_rustle = 0.0f;
static float s_view_tree_sx = 0.0f;
static float s_view_tree_sy = 0.0f;
static float s_view_tree_rustle = 0.0f;
static int   s_view_wind_ready = 0;

static inline uint32_t gl_chaff_hash3(int32_t px, int32_t py, int32_t pz, uint32_t seed);
static inline void gl_eval_recursive_wind(float wx, float wy, float anim_time,
                                          float *out_dir_x, float *out_dir_y,
                                          float *out_gust, float *out_catspaw);
static void gl_bind_scene_target(void);
static void gl_resolve_scene(GLbitfield mask);

static void gl_update_view_wind(float wx, float wy, float anim_time)
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

static void torus_frame_double(double wx, double wy, double wz,
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

void nd_seven_gl_compute_mvp(uint32_t vp_w, uint32_t vp_h,
                             float tx, float ty, float tz,
                             float dist, float pitch_deg, float fov_deg,
                             const nd_planet_def_t *pdef,
                             float out_mvp[16],
                             float out_eye[3],
                             float out_target[3],
                             float out_up[3])
{
	float target[3], E[3], S[3], N[3];
	nd_seven_gl_torus_frame(tx, ty, tz, pdef, target, E, S, N);

	float pitch_rad = pitch_deg * (PI_F / 180.0f);
	float cos_p = cosf(pitch_rad);
	float sin_p = sinf(pitch_rad);
	float d = (dist > 1.0f) ? dist : ND_SEVEN_CAM_DIST;

	float eye[3] = {
		target[0] + d * (cos_p * S[0] + sin_p * N[0]),
		target[1] + d * (cos_p * S[1] + sin_p * N[1]),
		target[2] + d * (cos_p * S[2] + sin_p * N[2])
	};
	float up[3] = {
		-sin_p * S[0] + cos_p * N[0],
		-sin_p * S[1] + cos_p * N[1],
		-sin_p * S[2] + cos_p * N[2]
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

void nd_seven_gl_compute_flat_mvp(uint32_t vp_w, uint32_t vp_h,
                                  float tx, float ty, float tz,
                                  float dist, float pitch_deg, float fov_deg,
                                  float out_mvp[16])
{
	float pitch_rad = pitch_deg * (PI_F / 180.0f);
	float cos_p = cosf(pitch_rad);
	float sin_p = sinf(pitch_rad);
	float d = (dist > 1.0f) ? dist : ND_SEVEN_CAM_DIST;

	float eye[3] = { tx, ty + d * cos_p, tz + d * sin_p };
	float target[3] = { tx, ty, tz };
	float up[3] = { 0.0f, -sin_p, cos_p };

	float modelview[16], proj[16];
	ar_mat4_lookat_lh(modelview, eye, target, up);

	float fov_rad = fov_deg * (PI_F / 180.0f);
	float aspect = (vp_h > 0) ? ((float)vp_w / (float)vp_h) : 1.333333f;
	float near_z = (d * 0.005f > 2.0f) ? (d * 0.005f) : 2.0f;
	float far_z = (d * 25.0f > 40000.0f) ? (d * 25.0f) : 40000.0f;

	ar_mat4_perspective(proj, fov_rad, aspect, near_z, far_z);
	ar_mat4_multiply(out_mvp, proj, modelview);
}

void nd_seven_gl_billboard_basis(float wx, float wy, float wz,
                                 float pitch_deg,
                                 const nd_planet_def_t *pdef,
                                 float out_base_pos[3],
                                 float out_right[3],
                                 float out_up[3],
                                 float out_view_dir[3])
{
	float E[3], S[3], N[3];
	nd_seven_gl_torus_frame(wx, wy, wz, pdef, out_base_pos, E, S, N);

	float pitch_rad = pitch_deg * (PI_F / 180.0f);
	float cos_p = cosf(pitch_rad);
	float sin_p = sinf(pitch_rad);

	if (out_right) {
		out_right[0] = E[0];
		out_right[1] = E[1];
		out_right[2] = E[2];
	}
	if (out_up) {
		out_up[0] = cos_p * N[0] - sin_p * S[0];
		out_up[1] = cos_p * N[1] - sin_p * S[1];
		out_up[2] = cos_p * N[2] - sin_p * S[2];
	}
	if (out_view_dir) {
		out_view_dir[0] = sin_p * N[0] + cos_p * S[0];
		out_view_dir[1] = sin_p * N[1] + cos_p * S[1];
		out_view_dir[2] = sin_p * N[2] + cos_p * S[2];
	}
}

typedef struct {
	float base_pos[3];
	float E[3];
	float S[3];
	float N[3];
	float grass_sx;
	float grass_sy;
	float grass_dz;
	float grass_rustle;
	float flora_sway_scale;
	float u_min;
	float u_max;
	float v_min;
	float v_max;
} billboard_basis_t;

static const double *s_bb_ref_origin = NULL;
static const nd_planet_def_t *s_bb_ref_pdef = NULL;
static float s_bb_tx = 0.0f;
static float s_bb_ty = 0.0f;
static float s_bb_dist = 0.0f;
static float s_bb_ref_trig[4] = { 1.0f, 0.0f, 1.0f, 0.0f };

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
		if (fabsf(du) <= 0.10f && fabsf(dv) <= 0.10f) {
			float du2 = du * du;
			float dv2 = dv * dv;
			float dcu = du2 * (-0.5f + du2 * (1.0f / 24.0f));
			float sdu = du * (1.0f - du2 * (1.0f / 6.0f));
			float dcv = dv2 * (-0.5f + dv2 * (1.0f / 24.0f));
			float sdv = dv * (1.0f - dv2 * (1.0f / 6.0f));

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
		if (b->pad0 == 1u && b->height > 0.0f) {
			float h_norm = fminf(1.25f, fmaxf(0.55f, b->height * (1.0f / 26.0f)));
			out->flora_sway_scale = 0.92f * h_norm;
		} else if (b->pad0 == 0u && b->height > 0.0f && b->height <= 30.0f) {
			float size_t = (30.0f - b->height) * (1.0f / 18.0f);
			if (size_t < 0.0f) size_t = 0.0f;
			if (size_t > 1.0f) size_t = 1.0f;
			out->flora_sway_scale = 0.52f + 0.33f * size_t;
		} else if (b->pad0 == 2u && b->height > 0.0f) {
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
				int wake_n = 0;
				const nd_grass_wake_step_t *wake = nd_seven_get_wake_steps(&wake_n);
				if (wake && wake_n > 0) {
					const float R_wake = 11.5f;
					const float tau_max = 1.40f;
					for (int i = 0; i < wake_n; i++) {
						if (!wake[i].valid)
							continue;
						float tau = s_grass_anim_time - wake[i].t_sec;
						if (tau < 0.0f || tau > tau_max)
							continue;
						float wdx = b->x - wake[i].wx;
						float wdy = b->y - wake[i].wy;
						float wd2 = wdx * wdx + wdy * wdy;
						if (wd2 >= R_wake * R_wake)
							continue;
						float wdist = sqrtf(wd2);
						float wt = 1.0f - (wdist / R_wake);
						float w_spatial = wt * wt * (3.0f - 2.0f * wt);
						float decay = expf(-2.2f * tau) * (1.0f - (tau / tau_max));
						float rebound = expf(-1.6f * tau);
						float wrx = (wdist > 0.25f) ? (wdx / wdist) : 1.0f;
						float wry = (wdist > 0.25f) ? (wdy / wdist) : 0.0f;
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
                                           float cos_p, float sin_p,
                                           nd_seven_gl_bb_vert_t out_verts[6])
{
	const float *E = basis->E;
	const float *S = basis->S;
	const float *N = basis->N;
	const float *base_pos = basis->base_pos;

	float right[3] = { E[0], E[1], E[2] };
	float up[3] = {
		cos_p * N[0] - sin_p * S[0],
		cos_p * N[1] - sin_p * S[1],
		cos_p * N[2] - sin_p * S[2]
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

	float style_f = (float)b->style;

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

	/* Prevent vertical screen-height collapse (-sy * sin_p - dz * cos_p) on short scaled meadow tufts
	 * so trampled or wind-bent tufts always retain at least 60% of their vertical screen height */
	if (basis->flora_sway_scale > 0.0f && h > 0.0f) {
		float max_vert_loss = 0.40f * h;
		float vert_loss = grass_sy * sin_p + grass_dz * cos_p;
		if (vert_loss > max_vert_loss && vert_loss > 1e-4f) {
			float keep = max_vert_loss / vert_loss;
			grass_sy *= keep;
			grass_dz *= keep;
		}
	}

	float inv_sin_p = 1.0f / (sin_p > 0.1f ? sin_p : 0.819152f);

	for (int i = 0; i < 6; i++) {
		int k = idx[i];
		float tip_mask = (basis->flora_sway_scale > 0.0f && c[k].qy > 0.5f) ? 1.0f : 0.0f;
		float sx = grass_sx * tip_mask;
		float sy = grass_sy * tip_mask;
		float dz = grass_dz * tip_mask;

		out_verts[i].world_pos[0] = base_pos[0] + (c[k].dx + sx) * right[0] + c[k].dy * up[0] + sy * S[0] - dz * N[0];
		out_verts[i].world_pos[1] = base_pos[1] + (c[k].dx + sx) * right[1] + c[k].dy * up[1] + sy * S[1] - dz * N[1];
		out_verts[i].world_pos[2] = base_pos[2] + (c[k].dx + sx) * right[2] + c[k].dy * up[2] + sy * S[2] - dz * N[2];

		out_verts[i].uv[0] = c[k].u;
		out_verts[i].uv[1] = c[k].v;

		float sheen = (b->style == ND_BB_STYLE_GRASS || b->pad0 == 1u) ? (tip_mask * grass_rustle * 0.14f) : 0.0f;
		out_verts[i].tint[0] = fminf(1.0f, tint[0] + sheen);
		out_verts[i].tint[1] = fminf(1.0f, tint[1] + sheen * 1.1f);
		out_verts[i].tint[2] = fminf(1.0f, tint[2] + sheen * 0.5f);
		out_verts[i].tint[3] = tint[3];

		out_verts[i].local_pos[0] = b->x + c[k].dx + sx;
		out_verts[i].local_pos[1] = b->y - c[k].dy * inv_sin_p + sy;
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
                                            float pitch_deg,
                                            const nd_planet_def_t *pdef,
                                            const double ref_origin[3],
                                            nd_seven_gl_bb_vert_t out_verts[6])
{
	if (!b || !out_verts) return;
	billboard_basis_t basis;
	eval_billboard_basis(b, pdef, ref_origin, &basis);
	float pitch_rad = pitch_deg * (PI_F / 180.0f);
	eval_billboard_quad_from_basis(b, &basis, cosf(pitch_rad), sinf(pitch_rad), out_verts);
}

void nd_seven_gl_eval_billboard_quad(const lam_billboard_gpu_t *b,
                                     float pitch_deg,
                                     const nd_planet_def_t *pdef,
                                     nd_seven_gl_bb_vert_t out_verts[6])
{
	eval_billboard_quad_with_origin(b, pitch_deg, pdef, NULL, out_verts);
}

static float s_shadow_sun_dir[3] = { 0.0f, 0.0f, 0.0f };

static void eval_billboard_shadow_quad_from_basis(const lam_billboard_gpu_t *b,
                                                  const billboard_basis_t *basis,
                                                  nd_seven_gl_bb_vert_t out_verts[6])
{
	const float *E = basis->E;
	const float *S = basis->S;
	const float *N = basis->N;
	const float *base_pos = basis->base_pos;

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

	float cast_scale = 0.72f / sun_n;
	if (cast_scale > 1.65f) cast_scale = 1.65f;
	float cast_dx = -sun_e * h * cast_scale;
	float cast_dy = -sun_s * h * cast_scale;

	float sway_dx = basis->grass_sx;
	float sway_dy = basis->grass_sy;

	float base_pad_y = fminf(ry * 0.55f, 4.0f);
	float lift_z = 0.22f;
	float alpha = (float)(b->tint_rgba & 0xFF) / 255.0f;

	struct { float dx, dy, u, v, qx, qy; } c[4] = {
		{ -half_w,                     -base_pad_y,          b->u0, b->v1, -1.0f, -1.0f }, /* Base-Left (0) */
		{  half_w,                     -base_pad_y,          b->u1, b->v1,  1.0f, -1.0f }, /* Base-Right (1) */
		{ -half_w + cast_dx + sway_dx,  cast_dy + sway_dy,   b->u0, b->v0, -1.0f,  1.0f }, /* Crown-Left (2) */
		{  half_w + cast_dx + sway_dx,  cast_dy + sway_dy,   b->u1, b->v0,  1.0f,  1.0f }  /* Crown-Right (3) */
	};

	int idx[6] = { 0, 1, 2, 1, 3, 2 };

	for (int i = 0; i < 6; i++) {
		int k = idx[i];
		out_verts[i].world_pos[0] = base_pos[0] + c[k].dx * E[0] + c[k].dy * S[0] + lift_z * N[0];
		out_verts[i].world_pos[1] = base_pos[1] + c[k].dx * E[1] + c[k].dy * S[1] + lift_z * N[1];
		out_verts[i].world_pos[2] = base_pos[2] + c[k].dx * E[2] + c[k].dy * S[2] + lift_z * N[2];

		out_verts[i].uv[0] = c[k].u;
		out_verts[i].uv[1] = c[k].v;

		out_verts[i].tint[0] = 0.0f;
		out_verts[i].tint[1] = 0.0f;
		out_verts[i].tint[2] = 0.0f;
		out_verts[i].tint[3] = alpha;

		out_verts[i].local_pos[0] = b->x + c[k].dx;
		out_verts[i].local_pos[1] = b->y + c[k].dy;
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

static float s_lamb_shadow_light[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

static bool eval_billboard_lamb_shadow_quad_from_basis(const lam_billboard_gpu_t *b,
                                                       const billboard_basis_t *basis,
                                                       nd_seven_gl_bb_vert_t out_verts[6])
{
	if (!b || !basis || !out_verts) return false;
	float l_int = s_lamb_shadow_light[3];
	if (l_int <= 0.01f) return false;

	float dx_l = b->x - s_lamb_shadow_light[0];
	float dy_l = b->y - s_lamb_shadow_light[1];
	float dist2 = dx_l * dx_l + dy_l * dy_l;
	/* Exclude the Lamb itself (dist < 2.0 wu) and objects outside the 220 wu glow radius */
	if (dist2 < 4.0f || dist2 > 220.0f * 220.0f) return false;

	float dist = sqrtf(dist2);
	float t_fall = 1.0f - (dist * (1.0f / 220.0f));
	float atten = t_fall * t_fall * (3.0f - 2.0f * t_fall) * l_int;
	float base_alpha = (float)(b->tint_rgba & 0xFF) / 255.0f;
	float alpha = base_alpha * atten * 0.78f;
	if (alpha <= 0.01f) return false;

	const float *E = basis->E;
	const float *S = basis->S;
	const float *N = basis->N;
	const float *base_pos = basis->base_pos;

	float rx = (b->shadow_rx > 0) ? (float)b->shadow_rx : fmaxf(2.0f, b->width * 0.34f);
	float ry = (b->shadow_ry > 0) ? (float)b->shadow_ry : fmaxf(1.0f, rx * 0.45f);
	float half_w = fmaxf(rx, b->width * 0.5f);
	float h = fmaxf(ry * 2.0f, b->height);

	float rad_x = dx_l / dist;
	float rad_y = dy_l / dist;
	float lamb_h = fmaxf(8.0f, (s_lamb_shadow_light[2] + 10.0f) - b->z);
	float cast_scale = fminf(1.65f, fmaxf(0.42f, (dist * (1.0f / 68.0f)) * (22.0f / (lamb_h + 10.0f))));
	float cast_dx = rad_x * h * cast_scale;
	float cast_dy = rad_y * h * cast_scale;

	float sway_dx = basis->grass_sx;
	float sway_dy = basis->grass_sy;

	/* Point-light radial divergence widens the projected crown silhouette slightly with distance from the base */
	float top_half_w = half_w * (1.0f + fminf(0.40f, (h * cast_scale) / fmaxf(24.0f, dist)));
	float base_pad_y = fminf(ry * 0.50f, 3.5f);
	float lift_z = 0.20f;

	struct { float dx, dy, u, v, qx, qy; } c[4] = {
		{ -half_w,                         -base_pad_y * rad_y,        b->u0, b->v1, -1.0f, -1.0f }, /* Base-Left (0) */
		{  half_w,                         -base_pad_y * rad_y,        b->u1, b->v1,  1.0f, -1.0f }, /* Base-Right (1) */
		{ -top_half_w + cast_dx + sway_dx,  cast_dy + sway_dy,         b->u0, b->v0, -1.0f,  1.0f }, /* Crown-Left (2) */
		{  top_half_w + cast_dx + sway_dx,  cast_dy + sway_dy,         b->u1, b->v0,  1.0f,  1.0f }  /* Crown-Right (3) */
	};

	int idx[6] = { 0, 1, 2, 1, 3, 2 };

	for (int i = 0; i < 6; i++) {
		int k = idx[i];
		out_verts[i].world_pos[0] = base_pos[0] + c[k].dx * E[0] + c[k].dy * S[0] + lift_z * N[0];
		out_verts[i].world_pos[1] = base_pos[1] + c[k].dx * E[1] + c[k].dy * S[1] + lift_z * N[1];
		out_verts[i].world_pos[2] = base_pos[2] + c[k].dx * E[2] + c[k].dy * S[2] + lift_z * N[2];

		out_verts[i].uv[0] = c[k].u;
		out_verts[i].uv[1] = c[k].v;

		out_verts[i].tint[0] = 0.0f;
		out_verts[i].tint[1] = 0.0f;
		out_verts[i].tint[2] = 0.0f;
		out_verts[i].tint[3] = alpha;

		out_verts[i].local_pos[0] = b->x + c[k].dx;
		out_verts[i].local_pos[1] = b->y + c[k].dy;
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

#define MAX_BB_INSTANCES 16384
static nd_seven_gl_bb_inst_t s_bb_instances[MAX_BB_INSTANCES];
static nd_seven_gl_bb_inst_t s_bb_other_instances[MAX_BB_INSTANCES];

static inline void pack_bb_quad_instance(const lam_billboard_gpu_t *b,
                                         const billboard_basis_t *basis,
                                         float cos_p, float sin_p,
                                         nd_seven_gl_bb_inst_t *out)
{
	const float *E = basis->E;
	const float *S = basis->S;
	const float *N = basis->N;
	const float *base_pos = basis->base_pos;

	float right[3] = { E[0], E[1], E[2] };
	float up[3] = {
		cos_p * N[0] - sin_p * S[0],
		cos_p * N[1] - sin_p * S[1],
		cos_p * N[2] - sin_p * S[2]
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
		float vert_loss = grass_sy * sin_p + grass_dz * cos_p;
		if (vert_loss > max_vert_loss && vert_loss > 1e-4f) {
			float keep = max_vert_loss / vert_loss;
			grass_sy *= keep;
			grass_dz *= keep;
		}
	}

	float sx = (basis->flora_sway_scale > 0.0f) ? grass_sx : 0.0f;
	float sy = (basis->flora_sway_scale > 0.0f) ? grass_sy : 0.0f;
	float dz = (basis->flora_sway_scale > 0.0f) ? grass_dz : 0.0f;

	out->pos0[0] = base_pos[0] - half_w * right[0] - ay * up[0];
	out->pos0[1] = base_pos[1] - half_w * right[1] - ay * up[1];
	out->pos0[2] = base_pos[2] - half_w * right[2] - ay * up[2];
	out->pos0[3] = -half_w;

	out->pos1[0] = base_pos[0] + half_w * right[0] - ay * up[0];
	out->pos1[1] = base_pos[1] + half_w * right[1] - ay * up[1];
	out->pos1[2] = base_pos[2] + half_w * right[2] - ay * up[2];
	out->pos1[3] = half_w;

	out->pos2[0] = base_pos[0] + (-half_w + sx) * right[0] + (h - ay) * up[0] + sy * S[0] - dz * N[0];
	out->pos2[1] = base_pos[1] + (-half_w + sx) * right[1] + (h - ay) * up[1] + sy * S[1] - dz * N[1];
	out->pos2[2] = base_pos[2] + (-half_w + sx) * right[2] + (h - ay) * up[2] + sy * S[2] - dz * N[2];
	out->pos2[3] = -half_w + sx;

	out->pos3[0] = base_pos[0] + (half_w + sx) * right[0] + (h - ay) * up[0] + sy * S[0] - dz * N[0];
	out->pos3[1] = base_pos[1] + (half_w + sx) * right[1] + (h - ay) * up[1] + sy * S[1] - dz * N[1];
	out->pos3[2] = base_pos[2] + (half_w + sx) * right[2] + (h - ay) * up[2] + sy * S[2] - dz * N[2];
	out->pos3[3] = half_w + sx;

	out->uv[0] = b->u0;
	out->uv[1] = b->v0;
	out->uv[2] = b->u1;
	out->uv[3] = b->v1;

	float sheen = (b->style == ND_BB_STYLE_GRASS || b->pad0 == 1u) ? (grass_rustle * 0.14f) : 0.0f;
	out->tint[0] = fminf(1.0f, tint[0] + sheen);
	out->tint[1] = fminf(1.0f, tint[1] + sheen * 1.1f);
	out->tint[2] = fminf(1.0f, tint[2] + sheen * 0.5f);
	out->tint[3] = tint[3];

	out->param[0] = b->x;
	out->param[1] = b->y;
	out->param[2] = b->z;
	out->param[3] = (float)b->style;
}

static inline void pack_bb_shadow_quad_instance(const lam_billboard_gpu_t *b,
                                                const billboard_basis_t *basis,
                                                nd_seven_gl_bb_inst_t *out)
{
	const float *E = basis->E;
	const float *S = basis->S;
	const float *N = basis->N;
	const float *base_pos = basis->base_pos;

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

	float cast_scale = 0.72f / sun_n;
	if (cast_scale > 1.65f) cast_scale = 1.65f;
	float cast_dx = -sun_e * h * cast_scale;
	float cast_dy = -sun_s * h * cast_scale;

	float sway_dx = basis->grass_sx;
	float sway_dy = basis->grass_sy;

	float base_pad_y = fminf(ry * 0.55f, 4.0f);
	float base_alpha = (float)(b->tint_rgba & 0xFF) / 255.0f;
	float alpha = base_alpha * 0.65f;
	float lift_z = 0.22f;

	float c_dx[4] = { -half_w, half_w, -half_w + cast_dx + sway_dx, half_w + cast_dx + sway_dx };
	float c_dy[4] = { -base_pad_y, -base_pad_y, cast_dy + sway_dy, cast_dy + sway_dy };

	out->pos0[0] = base_pos[0] + c_dx[0] * E[0] + c_dy[0] * S[0] + lift_z * N[0];
	out->pos0[1] = base_pos[1] + c_dx[0] * E[1] + c_dy[0] * S[1] + lift_z * N[1];
	out->pos0[2] = base_pos[2] + c_dx[0] * E[2] + c_dy[0] * S[2] + lift_z * N[2];
	out->pos0[3] = c_dx[0];

	out->pos1[0] = base_pos[0] + c_dx[1] * E[0] + c_dy[1] * S[0] + lift_z * N[0];
	out->pos1[1] = base_pos[1] + c_dx[1] * E[1] + c_dy[1] * S[1] + lift_z * N[1];
	out->pos1[2] = base_pos[2] + c_dx[1] * E[2] + c_dy[1] * S[2] + lift_z * N[2];
	out->pos1[3] = c_dx[1];

	out->pos2[0] = base_pos[0] + c_dx[2] * E[0] + c_dy[2] * S[0] + lift_z * N[0];
	out->pos2[1] = base_pos[1] + c_dx[2] * E[1] + c_dy[2] * S[1] + lift_z * N[1];
	out->pos2[2] = base_pos[2] + c_dx[2] * E[2] + c_dy[2] * S[2] + lift_z * N[2];
	out->pos2[3] = c_dx[2];

	out->pos3[0] = base_pos[0] + c_dx[3] * E[0] + c_dy[3] * S[0] + lift_z * N[0];
	out->pos3[1] = base_pos[1] + c_dx[3] * E[1] + c_dy[3] * S[1] + lift_z * N[1];
	out->pos3[2] = base_pos[2] + c_dx[3] * E[2] + c_dy[3] * S[2] + lift_z * N[2];
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
	out->param[1] = b->y;
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

	float dx_l = b->x - s_lamb_shadow_light[0];
	float dy_l = b->y - s_lamb_shadow_light[1];
	float dist2 = dx_l * dx_l + dy_l * dy_l;
	if (dist2 < 4.0f || dist2 > 220.0f * 220.0f) return false;

	float dist = sqrtf(dist2);
	float t_fall = 1.0f - (dist * (1.0f / 220.0f));
	float atten = t_fall * t_fall * (3.0f - 2.0f * t_fall) * l_int;
	float base_alpha = (float)(b->tint_rgba & 0xFF) / 255.0f;
	float alpha = base_alpha * atten * 0.78f;
	if (alpha <= 0.01f) return false;

	const float *E = basis->E;
	const float *S = basis->S;
	const float *N = basis->N;
	const float *base_pos = basis->base_pos;

	float rx = (b->shadow_rx > 0) ? (float)b->shadow_rx : fmaxf(2.0f, b->width * 0.34f);
	float ry = (b->shadow_ry > 0) ? (float)b->shadow_ry : fmaxf(1.0f, rx * 0.45f);
	float half_w = fmaxf(rx, b->width * 0.5f);
	float h = fmaxf(ry * 2.0f, b->height);

	float rad_x = dx_l / dist;
	float rad_y = dy_l / dist;

	float le = rad_x * E[0] + rad_y * E[1];
	float ls = rad_x * S[0] + rad_y * S[1];

	float cast_len = fminf(h * 1.25f, 32.0f) * (0.45f + 0.55f * atten);
	float cast_dx = le * cast_len;
	float cast_dy = ls * cast_len;

	float sway_dx = basis->grass_sx * 0.45f;
	float sway_dy = basis->grass_sy * 0.45f;

	float base_pad_y = fminf(ry * 0.45f, 3.5f);
	float top_half_w = half_w * (1.0f + 0.35f * (cast_len / 32.0f));
	float lift_z = 0.35f;

	float c_dx[4] = { -half_w, half_w, -top_half_w + cast_dx + sway_dx, top_half_w + cast_dx + sway_dx };
	float c_dy[4] = { -base_pad_y, -base_pad_y, cast_dy + sway_dy, cast_dy + sway_dy };

	out->pos0[0] = base_pos[0] + c_dx[0] * E[0] + c_dy[0] * S[0] + lift_z * N[0];
	out->pos0[1] = base_pos[1] + c_dx[0] * E[1] + c_dy[0] * S[1] + lift_z * N[1];
	out->pos0[2] = base_pos[2] + c_dx[0] * E[2] + c_dy[0] * S[2] + lift_z * N[2];
	out->pos0[3] = c_dx[0];

	out->pos1[0] = base_pos[0] + c_dx[1] * E[0] + c_dy[1] * S[0] + lift_z * N[0];
	out->pos1[1] = base_pos[1] + c_dx[1] * E[1] + c_dy[1] * S[1] + lift_z * N[1];
	out->pos1[2] = base_pos[2] + c_dx[1] * E[2] + c_dy[1] * S[2] + lift_z * N[2];
	out->pos1[3] = c_dx[1];

	out->pos2[0] = base_pos[0] + c_dx[2] * E[0] + c_dy[2] * S[0] + lift_z * N[0];
	out->pos2[1] = base_pos[1] + c_dx[2] * E[1] + c_dy[2] * S[1] + lift_z * N[1];
	out->pos2[2] = base_pos[2] + c_dx[2] * E[2] + c_dy[2] * S[2] + lift_z * N[2];
	out->pos2[3] = c_dx[2];

	out->pos3[0] = base_pos[0] + c_dx[3] * E[0] + c_dy[3] * S[0] + lift_z * N[0];
	out->pos3[1] = base_pos[1] + c_dx[3] * E[1] + c_dy[3] * S[1] + lift_z * N[1];
	out->pos3[2] = base_pos[2] + c_dx[3] * E[2] + c_dy[3] * S[2] + lift_z * N[2];
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
	out->param[1] = b->y;
	out->param[2] = b->z;
	out->param[3] = -2.0f;
	return true;
}

static inline float get_h16(const int16_t *buf, int px, int py, int w, int h)
{
	if (px < 0) px = 0;
	if (px >= w) px = w - 1;
	if (py < 0) py = 0;
	if (py >= h) py = h - 1;
	return (float)buf[py * w + px];
}

float nd_seven_gl_sample_height_smooth(const int16_t *height_i16,
                                       uint32_t tex_w, uint32_t tex_h,
                                       float u, float v,
                                       float span_x, float span_y,
                                       float *out_grad_x, float *out_grad_y)
{
	if (!height_i16 || tex_w < 2 || tex_h < 2) return 0.0f;

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
			float val = get_h16(height_i16, cx, cy, w, h);
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

	float h00 = get_h16(height_i16, ix,     iy,     w, h);
	float h10 = get_h16(height_i16, ix + 1, iy,     w, h);
	float h01 = get_h16(height_i16, ix,     iy + 1, w, h);
	float h11 = get_h16(height_i16, ix + 1, iy + 1, w, h);

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

float nd_seven_gl_eval_water_radius(const int16_t *height_i16,
                                    uint32_t tex_w, uint32_t tex_h,
                                    float u, float v,
                                    float span_x, float span_y,
                                    float r0, float sea_level,
                                    float *out_wz, float *out_water_depth)
{
	float wz = nd_seven_gl_sample_height_smooth(height_i16, tex_w, tex_h, u, v, span_x, span_y, NULL, NULL);
	float depth = sea_level - wz;

	if (out_wz) *out_wz = wz;
	if (out_water_depth) *out_water_depth = depth;

	if (depth <= 0.0f) {
		return r0 + fminf(wz, sea_level) - 2.0f;
	} else {
		return r0 + sea_level;
	}
}

/* --- WebGL2 GPU Pipeline Implementation --- */

typedef struct {
	GLuint prog;
	GLint uMVP;
	GLint uHeightTex;
	GLint uSubstrateTex;
	GLint uChunkOrigin;
	GLint uChunkRelOrigin;
	GLint uChunkSpan;
	GLint uRefTrig;
	GLint uHeightScale;
	GLint uSeaLevel;
	GLint uCameraPos;
	GLint uPlanetCenter;
	GLint uBodyRot;
	GLint uTorusRadii;
	GLint uWorldDim;
	GLint uFogStart;
	GLint uFogEnd;
	GLint uAnimTime;
	GLint uSkyColor;
	GLint uAtmoIntensity;
	GLint uLambLight;
	GLint uCloudShadowMap;
	GLint uCsm;
	GLint uBaseSeaLevel;
	GLint uSunDir;
	GLint uMoonDir;
	GLint uLambWakeHead;
	GLint uLambWakeXYT0;
	GLint uLambWakeXYT1;
	GLint uLambWakeVel0;
	GLint uLambWakeVel1;
	GLint uWorldId;
	GLint uSeaOffsetQ12_4;
	GLint aGridUV;
} gl_terrain_pipe_t;

typedef struct {
	GLuint prog;
	GLint uMVP;
	GLint uHeightTex;
	GLint uSubstrateTex;
	GLint uChunkOrigin;
	GLint uChunkRelOrigin;
	GLint uChunkSpan;
	GLint uRefTrig;
	GLint uHeightScale;
	GLint uSeaLevel;
	GLint uCameraPos;
	GLint uPlanetCenter;
	GLint uBodyRot;
	GLint uTorusRadii;
	GLint uWorldDim;
	GLint uFogStart;
	GLint uFogEnd;
	GLint uAnimTime;
	GLint uSkyColor;
	GLint uAtmoIntensity;
	GLint uLambLight;
	GLint uCloudShadowMap;
	GLint uCsm;
	GLint uBaseSeaLevel;
	GLint uSunDir;
	GLint uMoonDir;
	GLint uLambWakeHead;
	GLint uLambWakeXYT0;
	GLint uLambWakeXYT1;
	GLint uLambWakeVel0;
	GLint uLambWakeVel1;
	GLint aGridUV;
	GLint uSceneColor;
	GLint uSceneDepth;
	GLint uViewport;
	GLint uNearFar;
	GLint uProjScale;
} gl_water_pipe_t;

typedef struct {
	GLuint prog;
	GLint uMVP;
	GLint uAtlasTex;
	GLint uAtlasNormTex;
	GLint uSeaLevel;
	GLint uAnimTime;
	GLint uCameraPos;
	GLint uFogStart;
	GLint uFogEnd;
	GLint uSkyColor;
	GLint uAtmoIntensity;
	GLint uLambLight;
	GLint uSunDir;
	GLint uCloudShadowMap;
	GLint uCsm;
	GLint uPass;
	GLint aPos;
	GLint aUV;
	GLint aTint;
	GLint aLocalPos;
	GLint aQuadUV;
	GLint aSpriteBounds;
} gl_billboard_pipe_t;

typedef struct {
	GLuint prog;
	GLint uMVP;
	GLint uHeightTex;
	GLint uSubstrateTex;
	GLint uChunkOrigin;
	GLint uChunkRelOrigin;
	GLint uChunkSpan;
	GLint uRefTrig;
	GLint uSeaLevel;
	GLint uCameraPos;
	GLint uPlanetCenter;
	GLint uBodyRot;
	GLint uTorusRadii;
	GLint uWorldDim;
	GLint uFogStart;
	GLint uFogEnd;
	GLint uAnimTime;
	GLint uSkyColor;
	GLint uAtmoIntensity;
	GLint uCamDist;
	GLint uCloudDeck;
	GLint uSunDir;
	GLint uCloudShadowMap;
	GLint uCloudShadowMapHistory;
	GLint uCsm;
	GLint uPrevCsm;
	GLint uPrevSunDir;
	GLint uCsmConfidence;
	GLint uCsmJitterDelta;
	GLint aGridUV;
	
	/* Clustered Lighting */
	GLint uMoonDir;              /* vec3 celestial moon direction */
	GLint uViewport;             /* vec2 active cloud render target resolution */
	GLint uClusterGridDim;       /* vec3(16.0, 16.0, 24.0) */
	GLint uClusterDepthParams;   /* vec4(near, far, log_scale, log_bias) */
	GLint uClusterLightCount;    /* Number of active lights */
	GLint uClusterLightData;     /* Texture unit for light data (4x256 RGBA8) */
	GLint uClusterLightListsTex; /* Texture unit for cluster light lists (33x6144 R8) */
} gl_cloud_pipe_t;

/* Cluster light list entry (packed) */
typedef struct {
	uint8_t count;
	uint8_t light_indices[ND_MAX_LIGHTS_PER_CLUSTER];
} nd_cluster_light_list_t;

typedef struct {
	GLuint prog;
	GLint uCsm;
	GLint uAnimTime;
	GLint uSeaWet;
} gl_csm_pipe_t;

typedef struct {
	GLuint prog;
	GLint uCloudTex;
} gl_cloud_comp_pipe_t;

typedef struct {
	uint16_t world;
	uint8_t level;
	uint8_t active;
	uint32_t rev;
	int32_t ox, oy;
	int32_t span_w, span_h;
	uint32_t tex_w, tex_h;
	uint32_t last_touch;
	GLuint height_tex;
	GLuint substrate_tex;
} gl_chunk_tex_t;

typedef struct {
	int32_t ox, oy;
	int32_t span_w, span_h;
	uint8_t level;
	float sea_level;
	float rel_center[3];
	float rot_mat4[16];
	float major_r;
	float minor_r;
	GLuint height_tex;
	GLuint substrate_tex;
} gl_water_chunk_t;

static struct {
	bool initialized;
	bool pipelines_ready;
	gl_terrain_pipe_t terrain_pipe;
	gl_water_pipe_t water_pipe;
	gl_billboard_pipe_t bb_pipe;
	gl_cloud_pipe_t cloud_pipe;
	gl_cloud_comp_pipe_t cloud_comp_pipe;
	gl_csm_pipe_t csm_pipe;
	GLuint csm_tex;
	GLuint csm_fbo;
	bool csm_failed;
	bool csm_valid;
	bool csm_clear_sky;
	bool csm_has_cache;
	nd_seven_gl_csm_window_t csm_win;
	nd_seven_gl_csm_window_t csm_rendered_win;
	float csm_rendered_time;
	float csm_rendered_wet;

	/* Temporal Cloud Shadow Map Reprojection */
	GLuint csm_history_tex;        // Previous frame's CSM
	GLuint csm_history_fbo;        // FBO for history
	GLuint csm_reproj_prog;        // Reprojection shader program
	
	/* Clustered Lighting Buffers & Textures */
	GLuint cluster_light_tex;      // 2D data texture for light records (4 x ND_MAX_TOTAL_LIGHTS RGBA8)
	GLuint cluster_list_tex;       // 2D data texture for cluster light index lists (33 x ND_CLUSTER_TOTAL R8)
	int cluster_ssbo_valid;        // Whether cluster textures are initialized
	nd_cluster_light_t local_lights[ND_MAX_TOTAL_LIGHTS];
	uint32_t local_light_count;
	nd_cluster_light_t active_lights[ND_MAX_TOTAL_LIGHTS];
	uint32_t active_light_count;
	uint32_t active_cluster_count;
	uint8_t cluster_light_tex_data[ND_MAX_TOTAL_LIGHTS * 4 * 4];
	uint8_t cluster_list_tex_data[ND_CLUSTER_TOTAL * (ND_MAX_LIGHTS_PER_CLUSTER + 1)];
	float csm_prev_mvp[16];             // Previous frame's CSM MVP
	nd_seven_gl_csm_window_t csm_prev_win;  // Previous frame's CSM window
	float csm_prev_anim_time;      // Previous frame's anim_time
	float csm_prev_sun_dir[3];     // Previous frame's sun direction
	bool csm_history_valid;        // Whether history is valid
	int csm_reproj_mode;           // 0=off, 1=blend, 2=debug
	/* Temporal AA jitter compensation */
	float csm_jitter_x;            // Current frame jitter offset X (pixels)
	float csm_jitter_y;            // Current frame jitter offset Y (pixels)
	float csm_prev_jitter_x;       // Previous frame jitter offset X
	float csm_prev_jitter_y;       // Previous frame jitter offset Y
	int csm_prev_quality;          // Previous frame cloud quality
	int csm_prev_msaa;             // Previous frame MSAA
	float csm_prev_res_scale;      // Previous frame render scale
	uint32_t stage_w;
	uint32_t stage_h;

	/* Scene render target: MSAA colour+depth renderbuffers; the resolve FBO holds sampleable colour+depth snapshots */
	GLuint scene_fbo;
	GLuint scene_rb_color;
	GLuint scene_rb_depth;
	GLuint res_fbo;
	GLuint res_color_tex;
	GLuint res_depth_tex;
	GLuint cloud_fbo;
	GLuint cloud_tex;
	GLuint cloud_depth_rb;
	uint32_t cloud_w;
	uint32_t cloud_h;
	uint32_t scene_w;
	uint32_t scene_h;
	int scene_samples;
	bool scene_ok;
	bool scene_failed;
	bool frame_open;
	bool sprites_in_water;

	GLuint grid_vbo_macro;
	GLuint grid_ibo_macro;
	GLuint grid_vbo;
	GLuint grid_ibo;
	GLuint grid_vbo_hi;
	GLuint grid_ibo_hi;

	GLuint bb_vbo;
	GLuint atlas_tex;
	GLuint atlas_norm_tex;
	uint32_t atlas_ver;

	gl_chunk_tex_t chunk_cache[MAX_CHUNK_TEXTURES];
	uint32_t frame_count;

	gl_water_chunk_t water_queue[MAX_WATER_QUEUE_BASE];
	uint32_t water_count;
	gl_water_chunk_t cloud_queue[MAX_WATER_QUEUE_BASE];
	uint32_t cloud_count;

	/* Occlusion stats for HUD/debugging */
	uint32_t water_culled_occ2d;
	uint32_t cloud_culled_hiz;
	uint32_t billboard_culled_hiz;

	float cur_mvp[16];
	ar_frustum_t cur_frustum;
	float cur_eye[3];
	float cur_target[3];
	float cur_up[3];
	double cur_origin[3];
	float cur_tx;
	float cur_ty;
	float cur_tz;
	float cur_ref_trig[4];
	float cur_dist;
	float cur_near;
	float cur_far;
	float cur_tan_half;
	float cur_aspect;
	float cur_pitch_deg;
	float cur_anim_time;
	float cur_sea_level;
	float cur_base_sea_level;
	float focal_sea_level;
	float cur_sun_dir[3];
	float cur_moon_dir[3];
	float cur_lamb_light[4];
	float cur_wake_head[4];
	float cur_wake_xyt[ND_LAMB_WAKE_NODES][4];
	float cur_wake_vel[ND_LAMB_WAKE_NODES][4];
	uint32_t cur_wake_cursor;
	float cur_wake_last_x;
	float cur_wake_last_y;
	float cur_wake_last_t;
	nd_planet_def_t cur_pdef;

/* Hi-Z Buffer (hierarchical depth pyramid for occlusion culling) */
#define ND_HIZ_MAX_LEVELS 6
GLuint hiz_fbo;
GLuint hiz_tex;
GLuint hiz_depth_tex;
uint32_t hiz_w;
uint32_t hiz_h;
int hiz_levels;
bool hiz_valid;
bool hiz_failed;
float hiz_readback[ND_HIZ_MAX_LEVELS][16][16]; /* CPU cache of min depth per level */
	GLuint hiz_downsample_prog;
} s_gl;

static GLuint compile_shader(GLenum type, const char *src)
{
	GLuint sh = glCreateShader(type);
	if (!sh) return 0;
	glShaderSource(sh, src, strlen(src));
	glCompileShader(sh);
	return sh;
}

static GLuint create_program_async(const char *vs_src, const char *fs_src)
{
	GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
	GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
	if (!vs || !fs) {
		if (vs) glDeleteShader(vs);
		if (fs) glDeleteShader(fs);
		return 0;
	}
	GLuint prog = glCreateProgram();
	if (!prog) {
		glDeleteShader(vs);
		glDeleteShader(fs);
		return 0;
	}
	glAttachShader(prog, vs);
	glAttachShader(prog, fs);
	glLinkProgram(prog);
	glDeleteShader(vs);
	glDeleteShader(fs);
	return prog;
}

static void create_static_grid(int quads, float *uv_buf, uint16_t *idx_buf,
                               GLuint *out_vbo, GLuint *out_ibo)
{
	int verts = quads + 1;
	for (int y = 0; y < verts; y++) {
		for (int x = 0; x < verts; x++) {
			int idx = (y * verts + x) * 2;
			uv_buf[idx]     = (float)x / (float)quads;
			uv_buf[idx + 1] = (float)y / (float)quads;
		}
	}

	int i_idx = 0;
	for (int y = 0; y < quads; y++) {
		for (int x = 0; x < quads; x++) {
			uint16_t i0 = (uint16_t)(y * verts + x);
			uint16_t i1 = (uint16_t)(i0 + 1);
			uint16_t i2 = (uint16_t)((y + 1) * verts + x);
			uint16_t i3 = (uint16_t)(i2 + 1);

			if ((x + y) & 1) {
				idx_buf[i_idx++] = i0;
				idx_buf[i_idx++] = i1;
				idx_buf[i_idx++] = i3;
				idx_buf[i_idx++] = i0;
				idx_buf[i_idx++] = i3;
				idx_buf[i_idx++] = i2;
			} else {
				idx_buf[i_idx++] = i0;
				idx_buf[i_idx++] = i1;
				idx_buf[i_idx++] = i2;
				idx_buf[i_idx++] = i1;
				idx_buf[i_idx++] = i3;
				idx_buf[i_idx++] = i2;
			}
		}
	}

	*out_vbo = glCreateBuffer();
	glBindBuffer(GL_ARRAY_BUFFER, *out_vbo);
	glBufferData(GL_ARRAY_BUFFER, uv_buf, (GLsizei)((size_t)verts * (size_t)verts * 2 * sizeof(float)), GL_STATIC_DRAW);

	*out_ibo = glCreateBuffer();
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, *out_ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx_buf, (GLsizei)((size_t)quads * (size_t)quads * 6 * sizeof(uint16_t)), GL_STATIC_DRAW);
}

static void init_grid_buffers(void)
{
	static float grid_uv_scratch[GL_TOTAL_VERTS_HI * 2];
	static uint16_t indices_scratch[GL_TOTAL_INDICES_HI];

	create_static_grid(GL_GRID_QUADS_MACRO, grid_uv_scratch, indices_scratch, &s_gl.grid_vbo_macro, &s_gl.grid_ibo_macro);
	create_static_grid(GL_GRID_QUADS_LO, grid_uv_scratch, indices_scratch, &s_gl.grid_vbo, &s_gl.grid_ibo);
	create_static_grid(GL_GRID_QUADS_HI, grid_uv_scratch, indices_scratch, &s_gl.grid_vbo_hi, &s_gl.grid_ibo_hi);

	s_gl.bb_vbo = glCreateBuffer();
	glBindBuffer(GL_ARRAY_BUFFER, s_gl.bb_vbo);
	glBufferData(GL_ARRAY_BUFFER, NULL, MAX_BB_INSTANCES * sizeof(nd_seven_gl_bb_inst_t), GL_DYNAMIC_DRAW);
}

static void init_pipelines(void)
{
	s_gl.terrain_pipe.prog    = create_program_async(s_vs_terrain, s_fs_terrain);
	s_gl.water_pipe.prog      = create_program_async(s_vs_water, s_fs_water);
	s_gl.bb_pipe.prog         = create_program_async(s_vs_billboard, s_fs_billboard);
	s_gl.cloud_pipe.prog      = create_program_async(s_vs_cloud, s_fs_cloud);
	s_gl.cloud_comp_pipe.prog = create_program_async(s_vs_csm, s_fs_cloud_comp);
	s_gl.csm_pipe.prog        = create_program_async(s_vs_csm, s_fs_csm);
	init_grid_buffers();
	s_gl.initialized = true;
	s_gl.pipelines_ready = false;
}

static bool check_pipelines_ready(void)
{
	if (s_gl.pipelines_ready)
		return true;
	if (!s_gl.initialized)
		return false;

	if (s_gl.terrain_pipe.prog && !glGetProgramParameter(s_gl.terrain_pipe.prog, GL_COMPLETION_STATUS_KHR))
		return false;
	if (s_gl.water_pipe.prog && !glGetProgramParameter(s_gl.water_pipe.prog, GL_COMPLETION_STATUS_KHR))
		return false;
	if (s_gl.bb_pipe.prog && !glGetProgramParameter(s_gl.bb_pipe.prog, GL_COMPLETION_STATUS_KHR))
		return false;
	if (s_gl.cloud_pipe.prog && !glGetProgramParameter(s_gl.cloud_pipe.prog, GL_COMPLETION_STATUS_KHR))
		return false;
	if (s_gl.cloud_comp_pipe.prog && !glGetProgramParameter(s_gl.cloud_comp_pipe.prog, GL_COMPLETION_STATUS_KHR))
		return false;
	if (s_gl.csm_pipe.prog && !glGetProgramParameter(s_gl.csm_pipe.prog, GL_COMPLETION_STATUS_KHR))
		return false;

	/* Terrain pipeline */
	if (s_gl.terrain_pipe.prog) {
		if (!glGetProgramParameter(s_gl.terrain_pipe.prog, GL_LINK_STATUS)) {
			glDeleteProgram(s_gl.terrain_pipe.prog);
			s_gl.terrain_pipe.prog = 0;
		} else {
			GLuint p = s_gl.terrain_pipe.prog;
			s_gl.terrain_pipe.uMVP            = glGetUniformLocation(p, "uMVP", 4);
			s_gl.terrain_pipe.uHeightTex      = glGetUniformLocation(p, "uHeightTex", 10);
			s_gl.terrain_pipe.uSubstrateTex   = glGetUniformLocation(p, "uSubstrateTex", 13);
			s_gl.terrain_pipe.uChunkOrigin    = glGetUniformLocation(p, "uChunkOrigin", 12);
			s_gl.terrain_pipe.uChunkRelOrigin = glGetUniformLocation(p, "uChunkRelOrigin", 15);
			s_gl.terrain_pipe.uChunkSpan      = glGetUniformLocation(p, "uChunkSpan", 10);
			s_gl.terrain_pipe.uRefTrig        = glGetUniformLocation(p, "uRefTrig", 8);
			s_gl.terrain_pipe.uHeightScale    = glGetUniformLocation(p, "uHeightScale", 12);
			s_gl.terrain_pipe.uSeaLevel       = glGetUniformLocation(p, "uSeaLevel", 9);
			s_gl.terrain_pipe.uCameraPos      = glGetUniformLocation(p, "uCameraPos", 10);
			s_gl.terrain_pipe.uPlanetCenter   = glGetUniformLocation(p, "uPlanetCenter", 13);
			s_gl.terrain_pipe.uBodyRot        = glGetUniformLocation(p, "uBodyRot", 8);
			s_gl.terrain_pipe.uTorusRadii     = glGetUniformLocation(p, "uTorusRadii", 11);
			s_gl.terrain_pipe.uWorldDim       = glGetUniformLocation(p, "uWorldDim", 9);
			s_gl.terrain_pipe.uFogStart       = glGetUniformLocation(p, "uFogStart", 9);
			s_gl.terrain_pipe.uFogEnd         = glGetUniformLocation(p, "uFogEnd", 7);
			s_gl.terrain_pipe.uAnimTime       = glGetUniformLocation(p, "uAnimTime", 9);
			s_gl.terrain_pipe.uSkyColor       = glGetUniformLocation(p, "uSkyColor", 9);
			s_gl.terrain_pipe.uAtmoIntensity  = glGetUniformLocation(p, "uAtmoIntensity", 14);
			s_gl.terrain_pipe.uLambLight      = glGetUniformLocation(p, "uLambLight", 10);
			s_gl.terrain_pipe.uCloudShadowMap = glGetUniformLocation(p, "uCloudShadowMap", 15);
			s_gl.terrain_pipe.uCsm            = glGetUniformLocation(p, "uCsm", 4);
			s_gl.terrain_pipe.uBaseSeaLevel   = glGetUniformLocation(p, "uBaseSeaLevel", 13);
			s_gl.terrain_pipe.uSunDir         = glGetUniformLocation(p, "uSunDir", 7);
			s_gl.terrain_pipe.uMoonDir        = glGetUniformLocation(p, "uMoonDir", 8);
			s_gl.terrain_pipe.uLambWakeHead   = glGetUniformLocation(p, "uLambWakeHead", 13);
			s_gl.terrain_pipe.uLambWakeXYT0   = glGetUniformLocation(p, "uLambWakeXYT0", 13);
			s_gl.terrain_pipe.uLambWakeXYT1   = glGetUniformLocation(p, "uLambWakeXYT1", 13);
			s_gl.terrain_pipe.uLambWakeVel0   = glGetUniformLocation(p, "uLambWakeVel0", 13);
			s_gl.terrain_pipe.uLambWakeVel1   = glGetUniformLocation(p, "uLambWakeVel1", 13);
			s_gl.terrain_pipe.uWorldId        = glGetUniformLocation(p, "uWorldId", 8);
			s_gl.terrain_pipe.uSeaOffsetQ12_4 = glGetUniformLocation(p, "uSeaOffsetQ12_4", 15);
			s_gl.terrain_pipe.aGridUV         = glGetAttribLocation(p, "a_grid_uv", 9);
		}
	}

	/* Water pipeline */
	if (s_gl.water_pipe.prog) {
		if (!glGetProgramParameter(s_gl.water_pipe.prog, GL_LINK_STATUS)) {
			glDeleteProgram(s_gl.water_pipe.prog);
			s_gl.water_pipe.prog = 0;
		} else {
			GLuint p = s_gl.water_pipe.prog;
			s_gl.water_pipe.uMVP            = glGetUniformLocation(p, "uMVP", 4);
			s_gl.water_pipe.uHeightTex      = glGetUniformLocation(p, "uHeightTex", 10);
			s_gl.water_pipe.uSubstrateTex   = glGetUniformLocation(p, "uSubstrateTex", 13);
			s_gl.water_pipe.uChunkOrigin    = glGetUniformLocation(p, "uChunkOrigin", 12);
			s_gl.water_pipe.uChunkRelOrigin = glGetUniformLocation(p, "uChunkRelOrigin", 15);
			s_gl.water_pipe.uChunkSpan      = glGetUniformLocation(p, "uChunkSpan", 10);
			s_gl.water_pipe.uRefTrig        = glGetUniformLocation(p, "uRefTrig", 8);
			s_gl.water_pipe.uHeightScale    = glGetUniformLocation(p, "uHeightScale", 12);
			s_gl.water_pipe.uSeaLevel       = glGetUniformLocation(p, "uSeaLevel", 9);
			s_gl.water_pipe.uCameraPos      = glGetUniformLocation(p, "uCameraPos", 10);
			s_gl.water_pipe.uPlanetCenter   = glGetUniformLocation(p, "uPlanetCenter", 13);
			s_gl.water_pipe.uBodyRot        = glGetUniformLocation(p, "uBodyRot", 8);
			s_gl.water_pipe.uTorusRadii     = glGetUniformLocation(p, "uTorusRadii", 11);
			s_gl.water_pipe.uWorldDim       = glGetUniformLocation(p, "uWorldDim", 9);
			s_gl.water_pipe.uFogStart       = glGetUniformLocation(p, "uFogStart", 9);
			s_gl.water_pipe.uFogEnd         = glGetUniformLocation(p, "uFogEnd", 7);
			s_gl.water_pipe.uAnimTime       = glGetUniformLocation(p, "uAnimTime", 9);
			s_gl.water_pipe.uSkyColor       = glGetUniformLocation(p, "uSkyColor", 9);
			s_gl.water_pipe.uAtmoIntensity  = glGetUniformLocation(p, "uAtmoIntensity", 14);
			s_gl.water_pipe.uLambLight      = glGetUniformLocation(p, "uLambLight", 10);
			s_gl.water_pipe.uCloudShadowMap = glGetUniformLocation(p, "uCloudShadowMap", 15);
			s_gl.water_pipe.uCsm            = glGetUniformLocation(p, "uCsm", 4);
			s_gl.water_pipe.uBaseSeaLevel   = glGetUniformLocation(p, "uBaseSeaLevel", 13);
			s_gl.water_pipe.uSunDir         = glGetUniformLocation(p, "uSunDir", 7);
			s_gl.water_pipe.uMoonDir        = glGetUniformLocation(p, "uMoonDir", 8);
			s_gl.water_pipe.uLambWakeHead   = glGetUniformLocation(p, "uLambWakeHead", 13);
			s_gl.water_pipe.uLambWakeXYT0   = glGetUniformLocation(p, "uLambWakeXYT0", 13);
			s_gl.water_pipe.uLambWakeXYT1   = glGetUniformLocation(p, "uLambWakeXYT1", 13);
			s_gl.water_pipe.uLambWakeVel0   = glGetUniformLocation(p, "uLambWakeVel0", 13);
			s_gl.water_pipe.uLambWakeVel1   = glGetUniformLocation(p, "uLambWakeVel1", 13);
			s_gl.water_pipe.uSceneColor     = glGetUniformLocation(p, "uSceneColor", 11);
			s_gl.water_pipe.uSceneDepth     = glGetUniformLocation(p, "uSceneDepth", 11);
			s_gl.water_pipe.uViewport       = glGetUniformLocation(p, "uViewport", 9);
			s_gl.water_pipe.uNearFar        = glGetUniformLocation(p, "uNearFar", 8);
			s_gl.water_pipe.uProjScale      = glGetUniformLocation(p, "uProjScale", 10);
			s_gl.water_pipe.aGridUV         = glGetAttribLocation(p, "a_grid_uv", 9);
		}
	}

	/* Billboard pipeline */
	if (s_gl.bb_pipe.prog) {
		if (!glGetProgramParameter(s_gl.bb_pipe.prog, GL_LINK_STATUS)) {
			glDeleteProgram(s_gl.bb_pipe.prog);
			s_gl.bb_pipe.prog = 0;
		} else {
			GLuint p = s_gl.bb_pipe.prog;
			s_gl.bb_pipe.uMVP           = glGetUniformLocation(p, "uMVP", 4);
			s_gl.bb_pipe.uAtlasTex      = glGetUniformLocation(p, "uAtlasTex", 9);
			s_gl.bb_pipe.uAtlasNormTex  = glGetUniformLocation(p, "uAtlasNormTex", 13);
			s_gl.bb_pipe.uSeaLevel      = glGetUniformLocation(p, "uSeaLevel", 9);
			s_gl.bb_pipe.uAnimTime      = glGetUniformLocation(p, "uAnimTime", 9);
			s_gl.bb_pipe.uCameraPos     = glGetUniformLocation(p, "uCameraPos", 10);
			s_gl.bb_pipe.uFogStart      = glGetUniformLocation(p, "uFogStart", 9);
			s_gl.bb_pipe.uFogEnd        = glGetUniformLocation(p, "uFogEnd", 7);
			s_gl.bb_pipe.uSkyColor      = glGetUniformLocation(p, "uSkyColor", 9);
			s_gl.bb_pipe.uAtmoIntensity = glGetUniformLocation(p, "uAtmoIntensity", 14);
			s_gl.bb_pipe.uLambLight     = glGetUniformLocation(p, "uLambLight", 10);
			s_gl.bb_pipe.uSunDir        = glGetUniformLocation(p, "uSunDir", 7);
			s_gl.bb_pipe.uCloudShadowMap = glGetUniformLocation(p, "uCloudShadowMap", 15);
			s_gl.bb_pipe.uCsm           = glGetUniformLocation(p, "uCsm", 4);
			s_gl.bb_pipe.uPass          = glGetUniformLocation(p, "uPass", 5);
			s_gl.bb_pipe.aPos           = glGetAttribLocation(p, "a_pos", 5);
			s_gl.bb_pipe.aUV            = glGetAttribLocation(p, "a_uv", 4);
			s_gl.bb_pipe.aTint          = glGetAttribLocation(p, "a_tint", 6);
			s_gl.bb_pipe.aLocalPos      = glGetAttribLocation(p, "a_local_pos", 11);
			s_gl.bb_pipe.aQuadUV        = glGetAttribLocation(p, "a_quad_uv", 9);
			s_gl.bb_pipe.aSpriteBounds  = glGetAttribLocation(p, "a_sprite_bounds", 15);
		}
	}

	/* Cloud pipeline */
	if (s_gl.cloud_pipe.prog) {
		if (!glGetProgramParameter(s_gl.cloud_pipe.prog, GL_LINK_STATUS)) {
			glDeleteProgram(s_gl.cloud_pipe.prog);
			s_gl.cloud_pipe.prog = 0;
		} else {
			GLuint p = s_gl.cloud_pipe.prog;
			s_gl.cloud_pipe.uMVP            = glGetUniformLocation(p, "uMVP", 4);
			s_gl.cloud_pipe.uHeightTex      = glGetUniformLocation(p, "uHeightTex", 10);
			s_gl.cloud_pipe.uSubstrateTex   = glGetUniformLocation(p, "uSubstrateTex", 13);
			s_gl.cloud_pipe.uChunkOrigin    = glGetUniformLocation(p, "uChunkOrigin", 12);
			s_gl.cloud_pipe.uChunkRelOrigin = glGetUniformLocation(p, "uChunkRelOrigin", 15);
			s_gl.cloud_pipe.uChunkSpan      = glGetUniformLocation(p, "uChunkSpan", 10);
			s_gl.cloud_pipe.uRefTrig        = glGetUniformLocation(p, "uRefTrig", 8);
			s_gl.cloud_pipe.uSeaLevel       = glGetUniformLocation(p, "uSeaLevel", 9);
			s_gl.cloud_pipe.uCameraPos      = glGetUniformLocation(p, "uCameraPos", 10);
			s_gl.cloud_pipe.uPlanetCenter   = glGetUniformLocation(p, "uPlanetCenter", 13);
			s_gl.cloud_pipe.uBodyRot        = glGetUniformLocation(p, "uBodyRot", 8);
			s_gl.cloud_pipe.uTorusRadii     = glGetUniformLocation(p, "uTorusRadii", 11);
			s_gl.cloud_pipe.uWorldDim       = glGetUniformLocation(p, "uWorldDim", 9);
			s_gl.cloud_pipe.uFogStart       = glGetUniformLocation(p, "uFogStart", 9);
			s_gl.cloud_pipe.uFogEnd         = glGetUniformLocation(p, "uFogEnd", 7);
			s_gl.cloud_pipe.uAnimTime       = glGetUniformLocation(p, "uAnimTime", 9);
			s_gl.cloud_pipe.uSkyColor       = glGetUniformLocation(p, "uSkyColor", 9);
			s_gl.cloud_pipe.uAtmoIntensity  = glGetUniformLocation(p, "uAtmoIntensity", 14);
			s_gl.cloud_pipe.uCamDist        = glGetUniformLocation(p, "uCamDist", 8);
			s_gl.cloud_pipe.uCloudDeck      = glGetUniformLocation(p, "uCloudDeck", 10);
			s_gl.cloud_pipe.uSunDir               = glGetUniformLocation(p, "uSunDir", 7);
			s_gl.cloud_pipe.uCloudShadowMap       = glGetUniformLocation(p, "uCloudShadowMap", 15);
			s_gl.cloud_pipe.uCloudShadowMapHistory = glGetUniformLocation(p, "uCloudShadowMapHistory", 20);
			s_gl.cloud_pipe.uCsm                  = glGetUniformLocation(p, "uCsm", 4);
			s_gl.cloud_pipe.uPrevCsm              = glGetUniformLocation(p, "uPrevCsm", 8);
			s_gl.cloud_pipe.uPrevSunDir           = glGetUniformLocation(p, "uPrevSunDir", 11);
			s_gl.cloud_pipe.uCsmConfidence        = glGetUniformLocation(p, "uCsmConfidence", 14);
			s_gl.cloud_pipe.uCsmJitterDelta       = glGetUniformLocation(p, "uCsmJitterDelta", 15);
			s_gl.cloud_pipe.uMoonDir              = glGetUniformLocation(p, "uMoonDir", 8);
			s_gl.cloud_pipe.uViewport             = glGetUniformLocation(p, "uViewport", 9);
			s_gl.cloud_pipe.uClusterGridDim       = glGetUniformLocation(p, "uClusterGridDim", 15);
			s_gl.cloud_pipe.uClusterDepthParams   = glGetUniformLocation(p, "uClusterDepthParams", 19);
			s_gl.cloud_pipe.uClusterLightCount    = glGetUniformLocation(p, "uClusterLightCount", 18);
			s_gl.cloud_pipe.uClusterLightData     = glGetUniformLocation(p, "uClusterLightData", 17);
			s_gl.cloud_pipe.uClusterLightListsTex = glGetUniformLocation(p, "uClusterLightListsTex", 21);
			s_gl.cloud_pipe.aGridUV               = glGetAttribLocation(p, "a_grid_uv", 9);
		}
	}

	/* Cloud half-res composite pipeline */
	if (s_gl.cloud_comp_pipe.prog) {
		if (!glGetProgramParameter(s_gl.cloud_comp_pipe.prog, GL_LINK_STATUS)) {
			glDeleteProgram(s_gl.cloud_comp_pipe.prog);
			s_gl.cloud_comp_pipe.prog = 0;
		} else {
			GLuint p = s_gl.cloud_comp_pipe.prog;
			s_gl.cloud_comp_pipe.uCloudTex = glGetUniformLocation(p, "uCloudTex", 9);
		}
	}

	/* Cloud-shadow map pipeline */
	if (s_gl.csm_pipe.prog) {
		if (!glGetProgramParameter(s_gl.csm_pipe.prog, GL_LINK_STATUS)) {
			glDeleteProgram(s_gl.csm_pipe.prog);
			s_gl.csm_pipe.prog = 0;
		} else {
			GLuint p = s_gl.csm_pipe.prog;
			s_gl.csm_pipe.uCsm      = glGetUniformLocation(p, "uCsm", 4);
			s_gl.csm_pipe.uAnimTime = glGetUniformLocation(p, "uAnimTime", 9);
			s_gl.csm_pipe.uSeaWet   = glGetUniformLocation(p, "uSeaWet", 7);
		}
	}

	s_gl.pipelines_ready = true;
	return true;
}

bool nd_seven_gl_pipelines_ready(void)
{
	if (!s_gl.initialized)
		init_pipelines();
	return check_pipelines_ready();
}

void nd_seven_gl_set_focal_water(float water_level)
{
	s_gl.focal_sea_level = water_level;
	if (water_level > -900.0f) {
		if (s_gl.cur_sea_level <= -900.0f || water_level > s_gl.cur_sea_level)
			s_gl.cur_sea_level = water_level;
	}
}

void nd_seven_gl_set_lamb_light(float wx, float wy, float wz, float intensity)
{
	float cl_int = (intensity > 0.0f) ? intensity : 0.0f;
	s_gl.cur_lamb_light[0] = wx;
	s_gl.cur_lamb_light[1] = wy;
	s_gl.cur_lamb_light[2] = wz;
	s_gl.cur_lamb_light[3] = cl_int;
	s_lamb_shadow_light[0] = wx;
	s_lamb_shadow_light[1] = wy;
	s_lamb_shadow_light[2] = wz;
	s_lamb_shadow_light[3] = cl_int;
	s_grass_lamb_pos.x = wx;
	s_grass_lamb_pos.y = wy;
	if (!s_view_wind_ready)
		gl_update_view_wind(0.0f, 0.0f, s_grass_anim_time);
}

float nd_seven_gl_eval_lamb_light(float frag_wx, float frag_wy, float frag_wz,
                                  float lamb_wx, float lamb_wy, float lamb_wz,
                                  float intensity)
{
	if (intensity <= 0.001f)
		return 0.0f;
	float dx = lamb_wx - frag_wx;
	float dy = lamb_wy - frag_wy;
	float dz = (lamb_wz + 10.0f) - frag_wz;
	float d2 = dx * dx + dy * dy + dz * dz;
	float r_max2 = 220.0f * 220.0f;
	float r_min2 = 28.0f * 28.0f;
	float t = (d2 - r_max2) / (r_min2 - r_max2);
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;
	float smooth_falloff = t * t * (3.0f - 2.0f * t);
	float atten = (1.0f / (1.0f + d2 * 0.00055f)) * smooth_falloff;
	return intensity * 0.24f * atten;
}

bool nd_seven_gl_csm_window(float dist, float tx, float ty, nd_seven_gl_csm_window_t *out)
{
	if (!out || dist > 180000.0f) return false;
	float need = 4.5f * dist + 2000.0f;
	float half = 4096.0f;
	while (half < need && half < 1048576.0f) half *= 2.0f;
	float texel = (2.0f * half) / (float)ND_CSM_RES;
	out->half = half;
	out->cx = floorf(tx / texel + 0.5f) * texel;
	out->cy = floorf(ty / texel + 0.5f) * texel;
	return true;
}

static void gl_free_scene_targets(void)
{
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	glActiveTexture(GL_TEXTURE0 + 4);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0 + 5);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0 + 6);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);
	if (s_gl.scene_fbo) glDeleteFramebuffer(s_gl.scene_fbo);
	if (s_gl.res_fbo) glDeleteFramebuffer(s_gl.res_fbo);
	if (s_gl.cloud_fbo) glDeleteFramebuffer(s_gl.cloud_fbo);
	if (s_gl.scene_rb_color) glDeleteRenderbuffer(s_gl.scene_rb_color);
	if (s_gl.scene_rb_depth) glDeleteRenderbuffer(s_gl.scene_rb_depth);
	if (s_gl.cloud_depth_rb) glDeleteRenderbuffer(s_gl.cloud_depth_rb);
	if (s_gl.res_color_tex) glDeleteTexture(s_gl.res_color_tex);
	if (s_gl.res_depth_tex) glDeleteTexture(s_gl.res_depth_tex);
	if (s_gl.cloud_tex) glDeleteTexture(s_gl.cloud_tex);
	s_gl.scene_fbo = s_gl.res_fbo = s_gl.cloud_fbo = 0;
	s_gl.scene_rb_color = s_gl.scene_rb_depth = s_gl.cloud_depth_rb = 0;
	s_gl.res_color_tex = s_gl.res_depth_tex = s_gl.cloud_tex = 0;
	s_gl.cloud_w = s_gl.cloud_h = 0;
	s_gl.scene_ok = false;
}

static bool gl_alloc_scene_targets(uint32_t w, uint32_t h, int samples)
{
	/* Multisampled colour + depth renderbuffers (antialiased scene) */
	s_gl.scene_rb_color = glCreateRenderbuffer();
	glBindRenderbuffer(GL_RENDERBUFFER, s_gl.scene_rb_color);
	glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, (GLsizei)w, (GLsizei)h);
	s_gl.scene_rb_depth = glCreateRenderbuffer();
	glBindRenderbuffer(GL_RENDERBUFFER, s_gl.scene_rb_depth);
	glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH32F_STENCIL8, (GLsizei)w, (GLsizei)h);
	s_gl.scene_fbo = glCreateFramebuffer();
	glBindFramebuffer(GL_FRAMEBUFFER, s_gl.scene_fbo);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, s_gl.scene_rb_color);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, s_gl.scene_rb_depth);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;

	/* Resolved, sampleable colour + depth */
	s_gl.res_color_tex = glCreateTexture();
	glBindTexture(GL_TEXTURE_2D, s_gl.res_color_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	s_gl.res_depth_tex = glCreateTexture();
	glBindTexture(GL_TEXTURE_2D, s_gl.res_depth_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH32F_STENCIL8, (GLsizei)w, (GLsizei)h, 0, GL_DEPTH_STENCIL,
	             GL_FLOAT_32_UNSIGNED_INT_24_8_REV, NULL);
	s_gl.res_fbo = glCreateFramebuffer();
	glBindFramebuffer(GL_FRAMEBUFFER, s_gl.res_fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_gl.res_color_tex, 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, s_gl.res_depth_tex, 0);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;

	/* Volumetric cloud pass FBO (1/2 res for High, 1/3 res for Medium, 1/4 res for Low; single-sampled RGBA8 + depth/stencil) */
	int cq = nd_seven_get_cloud_quality();
	uint32_t cdiv = (cq <= ND_CLOUD_QUALITY_LOW) ? 4u : ((cq == ND_CLOUD_QUALITY_MEDIUM) ? 3u : 2u);
	uint32_t cw = (w > cdiv) ? (w / cdiv) : 1u;
	uint32_t ch = (h > cdiv) ? (h / cdiv) : 1u;
	s_gl.cloud_tex = glCreateTexture();
	glBindTexture(GL_TEXTURE_2D, s_gl.cloud_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)cw, (GLsizei)ch, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	s_gl.cloud_depth_rb = glCreateRenderbuffer();
	glBindRenderbuffer(GL_RENDERBUFFER, s_gl.cloud_depth_rb);
	glRenderbufferStorageMultisample(GL_RENDERBUFFER, 0, GL_DEPTH32F_STENCIL8, (GLsizei)cw, (GLsizei)ch);
	s_gl.cloud_fbo = glCreateFramebuffer();
	glBindFramebuffer(GL_FRAMEBUFFER, s_gl.cloud_fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_gl.cloud_tex, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, s_gl.cloud_depth_rb);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	s_gl.cloud_w = cw;
	s_gl.cloud_h = ch;

	/* Hi-Z Buffer (hierarchical depth pyramid for occlusion culling) */
	/* Alias the existing resolved depth texture - no separate texture needed */
	s_gl.hiz_w = w;
	s_gl.hiz_h = h;
	s_gl.hiz_levels = 1;
	s_gl.hiz_valid = true;
	s_gl.hiz_failed = false;

	/* Alias the Hi-Z texture to the resolved depth texture */
	s_gl.hiz_tex = s_gl.res_depth_tex;
	s_gl.hiz_depth_tex = s_gl.res_depth_tex;

	glBindTexture(GL_TEXTURE_2D, 0);

	/* Create Hi-Z downsample program */
	if (!s_gl.hiz_downsample_prog) {
		s_gl.hiz_downsample_prog = create_program_async(s_vs_hiz, s_fs_hiz_downsample);
	}
	return true;
}

/* (Re)create the scene targets for the stage size; falls back to single-sampled, then to the canvas itself. */
static void gl_ensure_scene_targets(uint32_t w, uint32_t h)
{
	if (s_gl.scene_failed || w == 0 || h == 0) return;
	int want_samples = nd_seven_get_msaa() ? ND_SCENE_MSAA_SAMPLES : 0;
	int cq = nd_seven_get_cloud_quality();
	uint32_t cdiv = (cq <= ND_CLOUD_QUALITY_LOW) ? 4u : ((cq == ND_CLOUD_QUALITY_MEDIUM) ? 3u : 2u);
	uint32_t want_cw = (w > cdiv) ? (w / cdiv) : 1u;
	uint32_t want_ch = (h > cdiv) ? (h / cdiv) : 1u;
	if (s_gl.scene_ok && s_gl.scene_w == w && s_gl.scene_h == h &&
	    s_gl.scene_samples == want_samples &&
	    s_gl.cloud_w == want_cw && s_gl.cloud_h == want_ch)
		return;
	if (s_gl.scene_fbo || s_gl.res_fbo) gl_free_scene_targets();
	int try_samples[2] = { want_samples, 0 };
	int num_tries = (want_samples > 0) ? 2 : 1;
	for (int i = 0; i < num_tries; i++) {
		if (gl_alloc_scene_targets(w, h, try_samples[i])) {
			s_gl.scene_ok = true;
			s_gl.scene_w = w;
			s_gl.scene_h = h;
			s_gl.scene_samples = try_samples[i];
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			return;
		}
		gl_free_scene_targets();
	}
	s_gl.scene_failed = true;
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

/* Build Hi-Z depth pyramid: downsample resolved depth texture to mip levels 1..N-1.
 * Returns true on success. */
static inline bool gl_build_hiz_pyramid(void)
{
    if (!s_gl.hiz_valid || s_gl.hiz_levels <= 1) return true;
    
    GLuint prog = s_gl.hiz_downsample_prog;
    if (!prog) return false;
    
    glUseProgram(prog);
    glBindFramebuffer(GL_FRAMEBUFFER, s_gl.hiz_fbo);
    glViewport(0, 0, (GLsizei)s_gl.hiz_w, (GLsizei)s_gl.hiz_h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    
    GLint loc_tex = glGetUniformLocation(prog, "uDepthTex", 8);
    GLint loc_ts = glGetUniformLocation(prog, "uTexelSize", 10);
    
    GLuint src_tex = s_gl.hiz_tex;  // Level 0 = resolved depth
    
    for (int level = 1; level < s_gl.hiz_levels; level++) {
        uint32_t lw = (s_gl.hiz_w > (1u << level)) ? (s_gl.hiz_w >> level) : 1u;
        uint32_t lh = (s_gl.hiz_h > (1u << level)) ? (s_gl.hiz_h >> level) : 1u;
        
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 
                              GL_TEXTURE_2D, s_gl.hiz_depth_tex, level);
        
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            return false;
        
        glViewport(0, 0, (GLsizei)lw, (GLsizei)lh);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, src_tex);
        glUniform1i(loc_tex, 0);
        
        float texel_size = 1.0f / (float)(lw << 1);  // Source level texel size
        glUniform1f(loc_ts, texel_size);
        
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        
        src_tex = s_gl.hiz_depth_tex;  // Next level reads from this level
    }
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

#ifndef __wasm__
/* Read back Hi-Z pyramid to CPU for occlusion queries.
 * Reads level 0..min(hiz_levels, 6) at up to 16x16 blocks per level. */
static void gl_readback_hiz(void)
{
    if (!s_gl.hiz_valid || s_gl.hiz_levels == 0) return;
    
    int levels_to_read = s_gl.hiz_levels < ND_HIZ_MAX_LEVELS ? s_gl.hiz_levels : ND_HIZ_MAX_LEVELS;
    
    for (int level = 0; level < levels_to_read; level++) {
        uint32_t lw = (s_gl.hiz_w > (1u << level)) ? (s_gl.hiz_w >> level) : 1u;
        uint32_t lh = (s_gl.hiz_h > (1u << level)) ? (s_gl.hiz_h >> level) : 1u;
        
        int read_w = lw < 16 ? lw : 16;
        int read_h = lh < 16 ? lh : 16;
        
        // Bind the Hi-Z texture level to a temporary FBO for readback
        GLuint tmp_fbo = glCreateFramebuffer();
        glBindFramebuffer(GL_FRAMEBUFFER, tmp_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_TEXTURE_2D, s_gl.hiz_depth_tex, level);
        
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
            glReadPixels(0, 0, read_w, read_h, GL_RED, GL_FLOAT,
                        &s_gl.hiz_readback[level][0][0]);
        }
        
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffer(tmp_fbo);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}
#else
static void gl_readback_hiz(void)
{
    (void)s_gl;
}
#endif

#ifndef __wasm__
/* Build 2D hierarchical occlusion map from Hi-Z pyramid.
 * Creates a min-depth pyramid at tile granularity for fast rectangle queries. */
static void gl_build_occ2d_map(void)
{
    if (!s_gl.hiz_valid || s_gl.hiz_levels == 0) {
        s_occ2d_valid = 0;
        return;
    }
    
    // Use Hi-Z level 2 as base (4x downsampled from full res)
    int base_level = s_gl.hiz_levels > 2 ? 2 : s_gl.hiz_levels - 1;
    uint32_t base_w = (s_gl.hiz_w > (1u << base_level)) ? (s_gl.hiz_w >> base_level) : 1u;
    uint32_t base_h = (s_gl.hiz_h > (1u << base_level)) ? (s_gl.hiz_h >> base_level) : 1u;
    
    // Number of tiles in each dimension
    int tiles_x = (base_w + ND_OCC2D_TILE_SIZE - 1) / ND_OCC2D_TILE_SIZE;
    int tiles_y = (base_h + ND_OCC2D_TILE_SIZE - 1) / ND_OCC2D_TILE_SIZE;
    tiles_x = tiles_x > 64 ? 64 : tiles_x;
    tiles_y = tiles_y > 64 ? 64 : tiles_y;
    
    s_occ2d_w = base_w;
    s_occ2d_h = base_h;
    
    // Level 0: compute min depth per tile from Hi-Z readback
    for (int ty = 0; ty < tiles_y; ty++) {
        for (int tx = 0; tx < tiles_x; tx++) {
            int px0 = tx * ND_OCC2D_TILE_SIZE;
            int py0 = ty * ND_OCC2D_TILE_SIZE;
            int px1 = (px0 + ND_OCC2D_TILE_SIZE < (int)base_w) ? (px0 + ND_OCC2D_TILE_SIZE) : (int)base_w;
            int py1 = (py0 + ND_OCC2D_TILE_SIZE < (int)base_h) ? (py0 + ND_OCC2D_TILE_SIZE) : (int)base_h;
            
            float min_depth = 1.0f;  // Far plane = 1.0 in NDC
            for (int y = py0; y < py1; y++) {
                for (int x = px0; x < px1; x++) {
                    float d = s_gl.hiz_readback[base_level][y][x];
                    if (d < min_depth) min_depth = d;
                }
            }
            s_occ2d_pyramid[0][ty][tx] = min_depth;
        }
    }
    
    // Build higher levels (2x2 min filter)
    int level_w = tiles_x;
    int level_h = tiles_y;
    for (int level = 1; level < ND_OCC2D_MAX_LEVELS; level++) {
        int next_w = (level_w + 1) / 2;
        int next_h = (level_h + 1) / 2;
        if (next_w == 0 || next_h == 0) break;
        
        for (int ty = 0; ty < next_h; ty++) {
            for (int tx = 0; tx < next_w; tx++) {
                float d00 = s_occ2d_pyramid[level-1][ty*2][tx*2];
                float d01 = (tx*2+1 < level_w) ? s_occ2d_pyramid[level-1][ty*2][tx*2+1] : d00;
                float d10 = (ty*2+1 < level_h) ? s_occ2d_pyramid[level-1][ty*2+1][tx*2] : d00;
                float d11 = (tx*2+1 < level_w && ty*2+1 < level_h) ? s_occ2d_pyramid[level-1][ty*2+1][tx*2+1] : d00;
                
                float min_d = d00;
                if (d01 < min_d) min_d = d01;
                if (d10 < min_d) min_d = d10;
                if (d11 < min_d) min_d = d11;
                
                s_occ2d_pyramid[level][ty][tx] = min_d;
            }
        }
        level_w = next_w;
        level_h = next_h;
        s_occ2d_levels = level + 1;
    }
    
    s_occ2d_valid = 1;
}
#endif

/* Test if a world-space bounding sphere is occluded by Hi-Z buffer.
 * Returns true (1) if fully occluded (sphere behind depth at all samples). */
int nd_seven_hiz_test_sphere(float cx, float cy, float cz, float radius)
{
    if (!s_gl.hiz_valid || s_gl.hiz_levels == 0) return 0;
    
    // Project sphere center to clip space using current MVP
    const float *clip = s_gl.cur_mvp;
    
    float px = clip[0]*cx + clip[4]*cy + clip[8]*cz + clip[12];
    float py = clip[1]*cx + clip[5]*cy + clip[9]*cz + clip[13];
    float pz = clip[2]*cx + clip[6]*cy + clip[10]*cz + clip[14];
    float pw = clip[3]*cx + clip[7]*cy + clip[11]*cz + clip[15];
    
    if (pw <= 0.0f) return 0;  // Behind camera
    
    float ndc_x = px / pw;
    float ndc_y = py / pw;
    float ndc_z = pz / pw;
    
    // Sphere radius in NDC (approximate)
    float r_ndc = radius * 2.0f / fmaxf(1.0f, fabsf(pw));
    
    // Test at Hi-Z level 2 (4x downsampled) for efficiency
    int level = s_gl.hiz_levels > 2 ? 2 : s_gl.hiz_levels - 1;
    int read_w = 16, read_h = 16;
    
    // UV in [0,1] for texture lookup
    float uv_x = ndc_x * 0.5f + 0.5f;
    float uv_y = -ndc_y * 0.5f + 0.5f;  // Flip Y for texture coords
    
    if (uv_x < 0.0f || uv_x > 1.0f || uv_y < 0.0f || uv_y > 1.0f) return 0;
    
    // Sample 3x3 neighborhood in Hi-Z
    float texel = 1.0f / (float)read_w;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int sx = (int)((uv_x + dx * texel) * read_w);
            int sy = (int)((uv_y + dy * texel) * read_h);
            sx = sx < 0 ? 0 : (sx >= read_w ? read_w - 1 : sx);
            sy = sy < 0 ? 0 : (sy >= read_h ? read_h - 1 : sy);
            
            float hiz_depth = s_gl.hiz_readback[level][sy][sx];
            if (ndc_z + r_ndc > hiz_depth) {
                return 0;  // Not occluded at this sample
            }
        }
    }
    return 1;  // Fully occluded
}

/* Ridge occlusion query for water queue gating: returns 1 if the water chunk at
 * (ox, oy, span_w, span_h) would be fully occluded by the current ridge buffer. */
int nd_seven_is_water_ridge_occluded(int32_t ox, int32_t oy, int32_t span_w, int32_t span_h,
                                     const float flat_mvp[16], float lwx, float lwy, float r_planet)
{
    (void)lwx;
    (void)lwy;
    (void)r_planet;
    if (!s_ridge_occlusion_valid || !s_ridge_use_occlusion)
        return 0;
    
    // Only check if the water chunk is behind the camera (farther than lamb)
    float cam_flat_y = s_ridge_cam_flat_y;
    if (oy + span_h >= cam_flat_y - 64.0f)
        return 0;
    
    float x0 = (float)ox, x1 = (float)(ox + span_w);
    float y0 = (float)oy, y1 = (float)(oy + span_h);
    float z_top = 96.0f; // water surface margin for occlusion test
    float corners[4][2] = { { x0, y0 }, { x1, y0 }, { x0, y1 }, { x1, y1 } };
    float min_nx = 2.0f, max_nx = -2.0f, max_ny = -2.0f;
    bool all_front = true;
    for (int c = 0; c < 4; c++) {
        float cx = corners[c][0], cy = corners[c][1];
        float cw = flat_mvp[3] * cx + flat_mvp[7] * cy + flat_mvp[11] * z_top + flat_mvp[15];
        if (cw <= 1.0f) {
            all_front = false;
            break;
        }
        float inv_w = 1.0f / cw;
        float nx = (flat_mvp[0] * cx + flat_mvp[4] * cy + flat_mvp[8] * z_top + flat_mvp[12]) * inv_w;
        float ny = (flat_mvp[1] * cx + flat_mvp[5] * cy + flat_mvp[9] * z_top + flat_mvp[13]) * inv_w;
        if (nx < min_nx) min_nx = nx;
        if (nx > max_nx) max_nx = nx;
        if (ny > max_ny) max_ny = ny;
    }
    if (!all_front || max_nx < -1.0f || min_nx > 1.0f || max_ny >= 1.0f)
        return 0;
    
    int b0 = (int)floorf((fmaxf(-1.0f, min_nx) + 1.0f) * (0.5f * SEVEN_HORIZON_BINS));
    int b1 = (int)ceilf((fminf(1.0f, max_nx) + 1.0f) * (0.5f * SEVEN_HORIZON_BINS));
    if (b0 < 0) b0 = 0;
    if (b1 >= SEVEN_HORIZON_BINS) b1 = SEVEN_HORIZON_BINS - 1;
    if (b0 > b1)
        return 0;
    
    for (int b = b0; b <= b1; b++) {
        if (max_ny + 0.015f >= s_ridge_horizon_ndc_y[b]) {
            return 0; // Not occluded - ridge buffer is higher than water chunk
        }
    }
    return 1; // Fully occluded by ridge
}

/* Cloud chunk occlusion test using Hi-Z */
int nd_seven_hiz_test_cloud_chunk(int32_t ox, int32_t oy, int32_t span_w, int32_t span_h,
                                   const float mvp[16], float cloud_z)
{
    (void)mvp;
    // Compute bounding sphere of cloud chunk
    float cx = (float)ox + span_w * 0.5f;
    float cy = (float)oy + span_h * 0.5f;
    float cz = cloud_z;
    float radius = fmaxf(span_w, span_h) * 0.7071f;
    
    return nd_seven_hiz_test_sphere(cx, cy, cz, radius);
}

/* Grass/billboard batch occlusion test using Hi-Z */
int nd_seven_hiz_test_billboard_batch(const lam_billboard_gpu_t *batch, uint32_t count,
                                       const float mvp[16], float max_radius)
{
    (void)mvp;
    if (!s_gl.hiz_valid || count == 0) return 0;
    
    int occluded = 0;
    for (uint32_t i = 0; i < count; i++) {
        const lam_billboard_gpu_t *b = &batch[i];
        float radius = fmaxf(b->width, b->height) * 0.5f;
        radius = fmaxf(radius, max_radius);
        
        if (nd_seven_hiz_test_sphere(b->x, b->y, b->z, radius)) {
            occluded++;
        }
    }
    return occluded;
}

/* Query 2D occlusion map for a screen-space rectangle.
 * Returns true if the rectangle is fully occluded (min depth <= near_z). */
int nd_seven_occ2d_test_rect(float ndc_x0, float ndc_y0, float ndc_x1, float ndc_y1, float near_z)
{
    if (!s_occ2d_valid || s_occ2d_levels == 0) return 0;
    
    // Convert NDC [-1,1] to texture coordinates [0,1]
    float u0 = ndc_x0 * 0.5f + 0.5f;
    float v0 = -ndc_y0 * 0.5f + 0.5f;
    float u1 = ndc_x1 * 0.5f + 0.5f;
    float v1 = -ndc_y1 * 0.5f + 0.5f;
    
    if (u0 > u1) { float t = u0; u0 = u1; u1 = t; }
    if (v0 > v1) { float t = v0; v0 = v1; v1 = t; }
    
    // Clamp to [0,1]
    if (u0 < 0.0f) u0 = 0.0f;
    if (v0 < 0.0f) v0 = 0.0f;
    if (u1 > 1.0f) u1 = 1.0f;
    if (v1 > 1.0f) v1 = 1.0f;
    if (u0 >= u1 || v0 >= v1) return 0;
    
    // Choose appropriate pyramid level based on rectangle size
    float rect_w = u1 - u0;
    float rect_h = v1 - v0;
    float tile_w = (float)ND_OCC2D_TILE_SIZE / (float)s_occ2d_w;
    float tile_h = (float)ND_OCC2D_TILE_SIZE / (float)s_occ2d_h;
    
    int level = 0;
    while (level + 1 < s_occ2d_levels && rect_w > tile_w * 4.0f && rect_h > tile_h * 4.0f) {
        level++;
        tile_w *= 2.0f;
        tile_h *= 2.0f;
    }
    
    // Compute tile range
    int tx0 = (int)(u0 / tile_w);
    int ty0 = (int)(v0 / tile_h);
    int tx1 = (int)(u1 / tile_w);
    int ty1 = (int)(v1 / tile_h);
    
    int level_w = (s_occ2d_w + (ND_OCC2D_TILE_SIZE << level) - 1) / (ND_OCC2D_TILE_SIZE << level);
    int level_h = (s_occ2d_h + (ND_OCC2D_TILE_SIZE << level) - 1) / (ND_OCC2D_TILE_SIZE << level);
    
    if (tx0 < 0) tx0 = 0;
    if (ty0 < 0) ty0 = 0;
    if (tx1 >= level_w) tx1 = level_w - 1;
    if (ty1 >= level_h) ty1 = level_h - 1;
    
    // Test min depth in rectangle
    float min_depth = 1.0f;
    for (int ty = ty0; ty <= ty1; ty++) {
        for (int tx = tx0; tx <= tx1; tx++) {
            float d = s_occ2d_pyramid[level][ty][tx];
            if (d < min_depth) min_depth = d;
        }
    }
    
    // Occluded if min depth in rectangle is in front of our near_z
    return (min_depth <= near_z) ? 1 : 0;
}

/* Water chunk 2D occ map test - projects water chunk to NDC and tests against occ map. */
int nd_seven_occ2d_test_water_chunk(int32_t ox, int32_t oy, int32_t span_w, int32_t span_h,
                                     const float mvp[16], float water_z)
{
    if (!s_occ2d_valid || s_occ2d_levels == 0) return 0;
    
    // Project 4 corners of water chunk to clip space
    float x0 = (float)ox, x1 = (float)(ox + span_w);
    float y0 = (float)oy, y1 = (float)(oy + span_h);
    
    float in_corners[4][4] = {
        { x0, y0, water_z, 1.0f },
        { x1, y0, water_z, 1.0f },
        { x0, y1, water_z, 1.0f },
        { x1, y1, water_z, 1.0f }
    };
    
    float min_nx = 2.0f, max_nx = -2.0f;
    float min_ny = 2.0f, max_ny = -2.0f;
    bool all_front = true;
    
    for (int c = 0; c < 4; c++) {
        float cx = in_corners[c][0];
        float cy = in_corners[c][1];
        float cz = in_corners[c][2];
        float cw = in_corners[c][3];
        
        float px = mvp[0]*cx + mvp[4]*cy + mvp[8]*cz + mvp[12]*cw;
        float py = mvp[1]*cx + mvp[5]*cy + mvp[9]*cz + mvp[13]*cw;
        float pw = mvp[3]*cx + mvp[7]*cy + mvp[11]*cz + mvp[15]*cw;
        
        if (pw <= 0.0f) {
            all_front = false;
            break;
        }
        
        float nx = px / pw;
        float ny = py / pw;
        
        if (nx < min_nx) min_nx = nx;
        if (nx > max_nx) max_nx = nx;
        if (ny < min_ny) min_ny = ny;
        if (ny > max_ny) max_ny = ny;
    }
    
    if (!all_front || max_nx < -1.0f || min_nx > 1.0f || max_ny < -1.0f || min_ny > 1.0f)
        return 0;  // Off screen or behind camera
    
    // Clamp to NDC bounds
    if (min_nx < -1.0f) min_nx = -1.0f;
    if (max_nx > 1.0f) max_nx = 1.0f;
    if (min_ny < -1.0f) min_ny = -1.0f;
    if (max_ny > 1.0f) max_ny = 1.0f;
    
    // Near Z for water (slightly in front to be conservative)
    float near_z = 0.0f;
    
    return nd_seven_occ2d_test_rect(min_nx, min_ny, max_nx, max_ny, near_z);
}

/* Bind the frame's render target (scene FBO, or the canvas if offscreen targets are unavailable) and the stage viewport. */
static void gl_bind_scene_target(void)
{
	glBindFramebuffer(GL_FRAMEBUFFER, s_gl.scene_ok ? s_gl.scene_fbo : 0);
	glViewport(0, 0, (GLsizei)s_gl.stage_w, (GLsizei)s_gl.stage_h);
}

/* Resolve the multisampled scene into the sampleable colour (and depth) textures. */
static void gl_resolve_scene(GLbitfield mask)
{
	if (!s_gl.scene_ok) return;
	glBindFramebuffer(GL_READ_FRAMEBUFFER, s_gl.scene_fbo);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_gl.res_fbo);
	glBlitFramebuffer(0, 0, (GLint)s_gl.scene_w, (GLint)s_gl.scene_h,
	                  0, 0, (GLint)s_gl.scene_w, (GLint)s_gl.scene_h, mask, GL_NEAREST);
	gl_bind_scene_target();
}

void nd_seven_gl_end_frame(void)
{
	if (!s_gl.frame_open) return;
	s_gl.frame_open = false;
	if (!s_gl.scene_ok) return;
	gl_resolve_scene(GL_COLOR_BUFFER_BIT);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, s_gl.res_fbo);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glBlitFramebuffer(0, 0, (GLint)s_gl.scene_w, (GLint)s_gl.scene_h,
	                  0, 0, (GLint)s_gl.scene_w, (GLint)s_gl.scene_h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	
	/* Read back Hi-Z pyramid for CPU occlusion culling next frame */
	if (s_gl.hiz_valid) {
		gl_readback_hiz();
	}
	
#ifndef __wasm__
	/* Build 2D hierarchical occlusion map from Hi-Z readback */
	if (s_gl.hiz_valid) {
		gl_build_occ2d_map();
	}
#endif
	
	/* The sprite marker lives in the scene alpha: the canvas must end up fully opaque */
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

void nd_seven_gl_get_target_info(uint32_t *out_stage_w, uint32_t *out_stage_h,
                                 uint32_t *out_scene_w, uint32_t *out_scene_h,
                                 int *out_samples,
                                 uint32_t *out_cloud_w, uint32_t *out_cloud_h,
                                 bool *out_csm_active)
{
	if (out_stage_w) *out_stage_w = s_gl.stage_w;
	if (out_stage_h) *out_stage_h = s_gl.stage_h;
	if (out_scene_w) *out_scene_w = s_gl.scene_w;
	if (out_scene_h) *out_scene_h = s_gl.scene_h;
	if (out_samples) *out_samples = s_gl.scene_samples;
	if (out_cloud_w) *out_cloud_w = s_gl.cloud_w;
	if (out_cloud_h) *out_cloud_h = s_gl.cloud_h;
	if (out_csm_active) *out_csm_active = (nd_seven_get_cloud_quality() >= ND_CLOUD_QUALITY_MEDIUM);
}

uint32_t nd_seven_gl_get_frame_count(void)
{
	return s_gl.frame_count;
}

/* Occlusion stats for HUD/debugging */
void nd_seven_gl_get_occlusion_stats(uint32_t *out_water_culled,
                                     uint32_t *out_cloud_culled,
                                     uint32_t *out_billboard_culled,
                                     uint32_t *out_water_queued,
                                     uint32_t *out_cloud_queued)
{
	if (out_water_culled) *out_water_culled = s_gl.water_culled_occ2d;
	if (out_cloud_culled) *out_cloud_culled = s_gl.cloud_culled_hiz;
	if (out_billboard_culled) *out_billboard_culled = s_gl.billboard_culled_hiz;
	if (out_water_queued) *out_water_queued = s_gl.water_count;
	if (out_cloud_queued) *out_cloud_queued = s_gl.cloud_count;
}

static bool gl_ensure_csm_target(void)
{
	if (s_gl.csm_fbo) return true;
	if (s_gl.csm_failed) return false;
	s_gl.csm_tex = glCreateTexture();
	glBindTexture(GL_TEXTURE_2D, s_gl.csm_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, ND_CSM_RES, ND_CSM_RES, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
	s_gl.csm_fbo = glCreateFramebuffer();
	glBindFramebuffer(GL_FRAMEBUFFER, s_gl.csm_fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_gl.csm_tex, 0);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glDeleteFramebuffer(s_gl.csm_fbo);
		glDeleteTexture(s_gl.csm_tex);
		s_gl.csm_fbo = 0;
		s_gl.csm_tex = 0;
		s_gl.csm_failed = true;
		return false;
	}
	return true;
}

static inline void gl_chaff_grad_noise2d(float px, float py, uint32_t seed,
                                         float *out_val, float *out_gx, float *out_gy);
static inline float gl_smoothstep(float e0, float e1, float x);

static float gl_smoothstep01(float e0, float e1, float x)
{
	float t = (x - e0) / (e1 - e0);
	t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
	return t * t * (3.0f - 2.0f * t);
}

static float gl_wrap1024(float v)
{
	return v - 1024.0f * floorf(v / 1024.0f);
}

static float gl_eval_world_humidity_cpu(float wx, float wy, float sea_wet)
{
	float wc_v = 0.5f, wc_gx = 0.0f, wc_gy = 0.0f;
	gl_chaff_grad_noise2d(wx * 0.000045f, wy * 0.000045f, 0x434C494Du, &wc_v, &wc_gx, &wc_gy);
	float tex_hum = fminf(0.95f, fmaxf(0.08f, 0.50f + 0.28f * (wc_v - 0.5f) + 0.10f * sea_wet));
	float lat_phi = 6.28318530718f * ((64.0f - wy) / 8388608.0f);
	float lat_deg = asinf(fminf(1.0f, fabsf(sinf(lat_phi)))) * 57.2957795f;
	float hum = tex_hum
	          - gl_smoothstep01(24.0f, 0.0f, lat_deg) * 0.18f
	          + gl_smoothstep01(18.0f, 0.0f, fabsf(lat_deg - 38.0f)) * 0.12f
	          + gl_smoothstep01(10.0f, 0.0f, fabsf(lat_deg - 58.0f)) * 0.08f;
	return fminf(0.92f, fmaxf(0.06f, hum));
}

static float gl_eval_synoptic_front_cpu(float wx, float wy, float anim_time, float fw,
                                        float *out_warp_x, float *out_warp_y)
{
	const float prevDirX = 0.741f, prevDirY = -0.671f;
	const float prevPerpX = 0.671f, prevPerpY = 0.741f;
	float tFlow = gl_wrap1024(anim_time * 0.005f);
	float phase0 = gl_wrap1024(anim_time * 0.004f);
	float phase1 = gl_wrap1024(anim_time * 0.008f);

	float psi0_v = 0.5f, psi0_gx = 0.0f, psi0_gy = 0.0f;
	gl_chaff_grad_noise2d(wx * 0.00000042f - prevDirX * (tFlow * 0.40f),
	                      wy * 0.00000042f - prevDirY * (tFlow * 0.40f),
	                      0x4359434Cu, &psi0_v, &psi0_gx, &psi0_gy);

	float psi1_v = 0.5f, psi1_gx = 0.0f, psi1_gy = 0.0f;
	gl_chaff_grad_noise2d(wx * 0.00000180f - psi0_gy * 0.30f - prevDirX * tFlow,
	                      wy * 0.00000180f + psi0_gx * 0.30f - prevDirY * tFlow,
	                      0x5354524Du, &psi1_v, &psi1_gx, &psi1_gy);

	float warp_x = -psi0_gy * 260000.0f - psi1_gy * 68000.0f;
	float warp_y =  psi0_gx * 260000.0f + psi1_gx * 68000.0f;
	if (out_warp_x) *out_warp_x = warp_x;
	if (out_warp_y) *out_warp_y = warp_y;

	float pDispX = wx + warp_x, pDispY = wy + warp_y;
	float pAlong = pDispX * prevDirX + pDispY * prevDirY;
	float pCross = pDispX * prevPerpX + pDispY * prevPerpY;
	float pZonalX = prevDirX * (pAlong * 0.65f) + prevPerpX * (pCross * 1.55f);
	float pZonalY = prevDirY * (pAlong * 0.65f) + prevPerpY * (pCross * 1.55f);

	float oct0_v = 0.5f, oct0_gx = 0.0f, oct0_gy = 0.0f;
	gl_chaff_grad_noise2d(pZonalX * 0.00000050f - prevDirX * phase0,
	                      pZonalY * 0.00000050f - prevDirY * phase0,
	                      0x434C4430u, &oct0_v, &oct0_gx, &oct0_gy);

	float wOct1 = gl_smoothstep01(0.32f, 0.08f, 0.00000240f * fw);
	float oct1_v = 0.5f, oct1_gx = 0.0f, oct1_gy = 0.0f;
	if (wOct1 > 0.001f) {
		float pRotX =  0.8f * pZonalX + 0.6f * pZonalY;
		float pRotY = -0.6f * pZonalX + 0.8f * pZonalY;
		gl_chaff_grad_noise2d(pRotX * 0.00000240f - prevDirX * phase1,
		                      pRotY * 0.00000240f - prevDirY * phase1,
		                      0x434C4431u, &oct1_v, &oct1_gx, &oct1_gy);
	}

	float r0 = 1.0f - 2.0f * fabsf(oct0_v - 0.5f);
	float r1 = 1.0f - 2.0f * fabsf(oct1_v - 0.5f);
	float front0 = oct0_v + (r0 - 0.68f) * 0.28f;
	float front1 = oct1_v + (r1 - 0.68f) * 0.24f;
	return 0.50f + (front0 - 0.50f) * 0.72f + (front1 - 0.50f) * (0.54f * wOct1) + (psi0_v - 0.50f) * 0.34f;
}

static bool gl_eval_synoptic_is_clear_sky_cpu(float wx, float wy, float anim_time,
                                              float sea_wet, float fw,
                                              bool want_cirrus, float margin)
{
	float humidity = gl_eval_world_humidity_cpu(wx, wy, sea_wet);
	float warp_x = 0.0f, warp_y = 0.0f;
	float syn_front = gl_eval_synoptic_front_cpu(wx, wy, anim_time, fw, &warp_x, &warp_y);
	float thresh = 0.555f - 0.105f * humidity;
	if (want_cirrus && (syn_front + 0.118f + margin) > (0.56f - 0.08f * humidity))
		return false;

	float wReg2 = gl_smoothstep01(0.26f, 0.07f, 0.00001150f * fw);
	float wReg3 = gl_smoothstep01(0.24f, 0.06f, 0.00005500f * fw);
	if (syn_front + 0.116f * wReg2 + 0.070f * wReg3 + margin <= thresh)
		return true;

	if (wReg2 > 0.001f) {
		float phase2 = gl_wrap1024(anim_time * 0.015f);
		float p2x =  0.28f * wx + 0.96f * wy + warp_x * 0.14f;
		float p2y = -0.96f * wx + 0.28f * wy + warp_y * 0.14f;
		float oct2_v = 0.5f, oct2_gx = 0.0f, oct2_gy = 0.0f;
		gl_chaff_grad_noise2d(p2x * 0.00001150f - 0.741f * phase2,
		                      p2y * 0.00001150f + 0.671f * phase2,
		                      0x434C4432u, &oct2_v, &oct2_gx, &oct2_gy);
		float r2 = 1.0f - 2.0f * fabsf(oct2_v - 0.5f);
		syn_front += ((oct2_v - 0.50f) + (r2 - 0.68f) * 0.25f) * 0.20f * wReg2;
	}
	return (syn_front + 0.070f * wReg3 + margin) <= thresh;
}

/* Sun-Pyramid Cloud Shadow Map:
 * Forms the pyramid with apex at the sun and base covering the visible ground frustum around (tx, ty, tz),
 * intersects that pyramid with the cloud slice zk = 920 at (pyr_cx, pyr_cy), and:
 * 1) Pre-culls the entire CSM pass on the CPU when the Sun-Pyramid slice intersects 100% clear sky (~65-75% of planet),
 *    setting csm_clear_sky = true (uCsm.z = -1) so terrain/water/clouds skip all shadow texture fetches and procedural noise.
 * 2) Otherwise renders the 128x128 R8 map centered directly on the Sun-Pyramid cloud-deck intersection. */
static void gl_render_cloud_shadow_map(void)
{
	s_gl.csm_valid = false;
	s_gl.csm_clear_sky = false;
	int cq = nd_seven_get_cloud_quality();
	if (cq <= ND_CLOUD_QUALITY_LOW) {
		s_gl.csm_clear_sky = true;
		return;
	}
	if (!s_gl.csm_pipe.prog || s_gl.cur_dist > 180000.0f)
		return;

	/* Temporal CSM history invalidation */
	if (s_gl.csm_history_valid) {
		bool invalidate = false;
		
		// Camera teleport / large motion
		float cam_motion = sqrtf((s_gl.cur_tx - s_gl.csm_prev_win.cx) * (s_gl.cur_tx - s_gl.csm_prev_win.cx) +
		                         (s_gl.cur_ty - s_gl.csm_prev_win.cy) * (s_gl.cur_ty - s_gl.csm_prev_win.cy));
		if (cam_motion > 500.0f) {
			invalidate = true;  // Teleport/cut
		} else if (s_gl.csm_prev_win.half > 0.0f && cam_motion > 0.1f * s_gl.csm_prev_win.half) {
			invalidate = true;  // Large motion relative to CSM window
		}
		
		// Quality change
		if (cq != s_gl.csm_prev_quality) invalidate = true;
		
		// MSAA change
		if (nd_seven_get_msaa() != s_gl.csm_prev_msaa) invalidate = true;
		
		// Render scale change
		float res_scale = nd_seven_get_res_scale();
		if (fabsf(res_scale - s_gl.csm_prev_res_scale) > 0.01f) invalidate = true;
		
		// Sun direction change > 15 degrees
		float sun_dot = s_gl.cur_sun_dir[0] * s_gl.csm_prev_sun_dir[0] +
		                s_gl.cur_sun_dir[1] * s_gl.csm_prev_sun_dir[1] +
		                s_gl.cur_sun_dir[2] * s_gl.csm_prev_sun_dir[2];
		if (sun_dot < 0.9659258f) {  // cos(15°) = 0.9659258
			invalidate = true;
		}
		
		if (invalidate) {
			s_gl.csm_history_valid = false;
		}
	}
	
	// Store current settings for next frame invalidation check
	s_gl.csm_prev_quality = cq;
	s_gl.csm_prev_msaa = nd_seven_get_msaa();
	s_gl.csm_prev_res_scale = nd_seven_get_res_scale();

	float sun_xy_len = sqrtf(s_gl.cur_sun_dir[0] * s_gl.cur_sun_dir[0] + s_gl.cur_sun_dir[1] * s_gl.cur_sun_dir[1]);
	float sun_px = (sun_xy_len > 0.05f) ? (-s_gl.cur_sun_dir[0] / sun_xy_len) : -0.70710678f;
	float sun_py = (sun_xy_len > 0.05f) ? (-s_gl.cur_sun_dir[1] / sun_xy_len) : -0.70710678f;
	float orbit_off = fminf(14000.0f, fmaxf(0.0f, s_gl.cur_dist * 0.045f));
	float dz_ref = fmaxf(0.0f, 920.0f - fmaxf(0.0f, s_gl.cur_tz)) + orbit_off;
	float pyr_cx = s_gl.cur_tx + sun_px * dz_ref;
	float pyr_cy = s_gl.cur_ty + sun_py * dz_ref;

	if (!nd_seven_gl_csm_window(s_gl.cur_dist, pyr_cx, pyr_cy, &s_gl.csm_win))
		return;

	float sea_wet = (s_gl.cur_sea_level > -900.0f) ? 1.0f : 0.0f;
	float fw_csm = (2.0f * s_gl.csm_win.half) / (float)ND_CSM_RES;
	float pyr_half = fminf(s_gl.csm_win.half, fmaxf(1200.0f, s_gl.cur_dist * 1.8f + 800.0f));
	bool pyr_clear = true;
	for (int gy = -1; gy <= 1 && pyr_clear; gy++) {
		float sy = pyr_cy + (float)gy * pyr_half;
		for (int gx = -1; gx <= 1; gx++) {
			float sx = pyr_cx + (float)gx * pyr_half;
			if (!gl_eval_synoptic_is_clear_sky_cpu(sx, sy, s_gl.cur_anim_time, sea_wet, fw_csm, false, 0.028f)) {
				pyr_clear = false;
				break;
			}
		}
	}
	if (pyr_clear) {
		s_gl.csm_clear_sky = true;
		return;
	}

	if (!gl_ensure_csm_target()) return;
	float csm_cache_dt = (cq == ND_CLOUD_QUALITY_MEDIUM) ? 0.35f : 0.10f;
	if (s_gl.csm_has_cache &&
	    s_gl.csm_rendered_win.cx == s_gl.csm_win.cx &&
	    s_gl.csm_rendered_win.cy == s_gl.csm_win.cy &&
	    s_gl.csm_rendered_win.half == s_gl.csm_win.half &&
	    s_gl.csm_rendered_wet == sea_wet &&
	    fabsf(s_gl.cur_anim_time - s_gl.csm_rendered_time) < csm_cache_dt) {
		s_gl.csm_valid = true;
		return;
	}

	/* --- TEMPORAL REPROJECTION SETUP --- */
	// Create history texture/FBO on first use
	if (!s_gl.csm_history_tex) {
		s_gl.csm_history_tex = glCreateTexture();
		glBindTexture(GL_TEXTURE_2D, s_gl.csm_history_tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, ND_CSM_RES, ND_CSM_RES, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
		
		s_gl.csm_history_fbo = glCreateFramebuffer();
		glBindFramebuffer(GL_FRAMEBUFFER, s_gl.csm_history_fbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_gl.csm_history_tex, 0);
		
		// Create reprojection shader program
		s_gl.csm_reproj_prog = create_program_async(s_vs_csm_reproj, s_fs_csm_reproj);
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	
	// Compute previous frame's CSM MVP for reprojection
	// We store the previous frame's CSM window and sun direction
	// The reprojection maps from prev CSM UV to current CSM UV
	
	/* --- RENDER CURRENT CSM --- */
	glBindFramebuffer(GL_FRAMEBUFFER, s_gl.csm_fbo);
	glViewport(0, 0, ND_CSM_RES, ND_CSM_RES);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glUseProgram(s_gl.csm_pipe.prog);
	glUniform3f(s_gl.csm_pipe.uCsm, s_gl.csm_win.cx, s_gl.csm_win.cy, s_gl.csm_win.half);
	glUniform1f(s_gl.csm_pipe.uAnimTime, s_gl.cur_anim_time);
	glUniform1f(s_gl.csm_pipe.uSeaWet, sea_wet);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	
	/* --- TEMPORAL REPROJECTION PASS --- */
	if (s_gl.csm_history_valid && s_gl.csm_reproj_prog) {
		// Bind history texture and render reprojection to a temp texture
		// For now, we'll blend directly by sampling history in the cloud shader
		// This is a simplified approach - full reprojection would need a separate pass
	}
	
	/* --- COPY CURRENT TO HISTORY FOR NEXT FRAME --- */
	// Blit current CSM to history texture
	glBindFramebuffer(GL_READ_FRAMEBUFFER, s_gl.csm_fbo);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_gl.csm_history_fbo);
	glBlitFramebuffer(0, 0, ND_CSM_RES, ND_CSM_RES,
	                  0, 0, ND_CSM_RES, ND_CSM_RES, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	
	/* --- UPDATE HISTORY STATE --- */
	s_gl.csm_history_valid = true;
	memcpy(s_gl.csm_prev_sun_dir, s_gl.cur_sun_dir, sizeof(s_gl.cur_sun_dir));
	s_gl.csm_prev_win = s_gl.csm_win;
	s_gl.csm_prev_anim_time = s_gl.cur_anim_time;
	memcpy(s_gl.csm_prev_mvp, s_gl.cur_mvp, sizeof(s_gl.cur_mvp));  // Approximate
	
	gl_bind_scene_target();
	glEnable(GL_DEPTH_TEST);
	s_gl.csm_rendered_win = s_gl.csm_win;
	s_gl.csm_rendered_time = s_gl.cur_anim_time;
	s_gl.csm_rendered_wet = sea_wet;
	s_gl.csm_has_cache = true;
	s_gl.csm_valid = true;
}

/* Bind the cloud-shadow map (texture unit 7) and window to a program; half < 0 means Sun-Pyramid is clear sky; half = 0 means no map is available. */
static void gl_bind_csm(GLint loc_tex, GLint loc_csm)
{
	glActiveTexture(GL_TEXTURE0 + 7);
	glBindTexture(GL_TEXTURE_2D, s_gl.csm_valid ? s_gl.csm_tex : 0);
	glUniform1i(loc_tex, 7);
	if (s_gl.csm_valid)
		glUniform3f(loc_csm, s_gl.csm_win.cx, s_gl.csm_win.cy, s_gl.csm_win.half);
	else if (nd_seven_get_cloud_quality() <= ND_CLOUD_QUALITY_LOW)
		glUniform3f(loc_csm, 0.0f, 0.0f, -2.0f);
	else if (s_gl.csm_clear_sky)
		glUniform3f(loc_csm, 0.0f, 0.0f, -1.0f);
	else
		glUniform3f(loc_csm, 0.0f, 0.0f, 0.0f);
	glActiveTexture(GL_TEXTURE0);
}

float nd_seven_gl_cloud_shadow_from_density(float density, float frag_z)
{
	const float zk = 920.0f;
	if (frag_z >= zk + 240.0f) return 0.0f;
	float slice_fade = gl_smoothstep01(zk + 240.0f, zk - 120.0f, frag_z);
	float s = gl_smoothstep01(0.05f, 0.66f, density * slice_fade);
	return s > 0.95f ? 0.95f : s;
}

float nd_seven_gl_scene_light(float cloud_shadow, float diffuse)
{
	float sun_trans = 1.0f - (float)ND_CLOUD_SHADOW_DIRECT_LOSS * cloud_shadow;
	float amb_trans = 1.0f - (float)ND_CLOUD_SHADOW_AMBIENT_LOSS * cloud_shadow;
	return 0.45f * amb_trans + 0.55f * diffuse * sun_trans;
}

void nd_seven_gl_begin_frame(uint32_t stage_w, uint32_t stage_h,
                             float tx, float ty, float tz,
                             float dist, float pitch_deg, float fov_deg,
                             float anim_time,
                             const nd_planet_def_t *pdef)
{
	s_gl.frame_count++;
	s_gl.water_count = 0;
	s_gl.cloud_count = 0;
	s_gl.sprites_in_water = false;
	s_gl.water_culled_occ2d = 0;
	s_gl.cloud_culled_hiz = 0;
	s_gl.billboard_culled_hiz = 0;
	s_gl.local_light_count = 0;
	s_gl.cur_sea_level = pdef ? (float)nd_field_water_level(pdef->world_id) : -999.0f;
	s_gl.cur_base_sea_level = s_gl.cur_sea_level;
	s_gl.focal_sea_level = -999.0f;
	if (pdef) s_gl.cur_pdef = *pdef;
	nd_seven_gl_eval_celestial_dirs(anim_time, s_gl.cur_sun_dir, s_gl.cur_moon_dir);
	s_shadow_sun_dir[0] = s_gl.cur_sun_dir[0];
	s_shadow_sun_dir[1] = s_gl.cur_sun_dir[1];
	s_shadow_sun_dir[2] = s_gl.cur_sun_dir[2];
	s_gl.cur_dist = (dist > 1.0f) ? dist : ND_SEVEN_CAM_DIST;
	s_gl.cur_pitch_deg = pitch_deg;
	s_gl.cur_anim_time = anim_time;
	s_gl.cur_tx = tx;
	s_gl.cur_ty = ty;
	s_gl.cur_tz = tz;
	s_grass_anim_time = anim_time;
	s_grass_lamb_pos.x = tx;
	s_grass_lamb_pos.y = ty;
	gl_update_view_wind(tx, ty, anim_time);

	/* Evaluate double-precision reference origin at (tx, ty, 0) to eliminate 32-bit float cancellation */
	float E[3], S[3], N[3];
	torus_frame_double((double)tx, (double)ty, 0.0, pdef, s_gl.cur_origin, E, S, N);

	double u0 = 6.28318530717958647692 * ((double)tx / (double)ND_SEVEN_WORLD_DIM_W);
	double v0 = 6.28318530717958647692 * ((double)ty / (double)ND_SEVEN_WORLD_DIM_H);
	s_gl.cur_ref_trig[0] = (float)cos(u0);
	s_gl.cur_ref_trig[1] = (float)sin(u0);
	s_gl.cur_ref_trig[2] = (float)cos(v0);
	s_gl.cur_ref_trig[3] = (float)sin(v0);
	s_bb_ref_origin = s_gl.cur_origin;
	s_bb_ref_pdef = &s_gl.cur_pdef;
	s_bb_tx = tx;
	s_bb_ty = ty;
	s_bb_dist = s_gl.cur_dist;
	s_bb_ref_trig[0] = s_gl.cur_ref_trig[0];
	s_bb_ref_trig[1] = s_gl.cur_ref_trig[1];
	s_bb_ref_trig[2] = s_gl.cur_ref_trig[2];
	s_bb_ref_trig[3] = s_gl.cur_ref_trig[3];

	float pitch_rad = pitch_deg * (PI_F / 180.0f);
	float cos_p = cosf(pitch_rad);
	float sin_p = sinf(pitch_rad);
	float d = s_gl.cur_dist;

	s_gl.cur_target[0] = tz * N[0];
	s_gl.cur_target[1] = tz * N[1];
	s_gl.cur_target[2] = tz * N[2];

	s_gl.cur_eye[0] = s_gl.cur_target[0] + d * (cos_p * S[0] + sin_p * N[0]);
	s_gl.cur_eye[1] = s_gl.cur_target[1] + d * (cos_p * S[1] + sin_p * N[1]);
	s_gl.cur_eye[2] = s_gl.cur_target[2] + d * (cos_p * S[2] + sin_p * N[2]);

	s_gl.cur_up[0] = -sin_p * S[0] + cos_p * N[0];
	s_gl.cur_up[1] = -sin_p * S[1] + cos_p * N[1];
	s_gl.cur_up[2] = -sin_p * S[2] + cos_p * N[2];

	float modelview[16], proj[16];
	ar_mat4_lookat_lh(modelview, s_gl.cur_eye, s_gl.cur_target, s_gl.cur_up);
	float fov_rad = fov_deg * (PI_F / 180.0f);
	float aspect = (stage_h > 0) ? ((float)stage_w / (float)stage_h) : 1.333333f;
	float near_z = (d * 0.005f > 2.0f) ? (d * 0.005f) : 2.0f;
	float far_z = (d * 20.0f > 80000000.0f) ? (d * 20.0f) : 80000000.0f;
	ar_mat4_perspective(proj, fov_rad, aspect, near_z, far_z);
	
	/* Temporal AA jitter: apply sub-pixel offset to projection matrix */
	/* Halton sequence base 2,3 for jitter */
	static uint32_t s_halton_idx = 0;
	s_halton_idx++;
	uint32_t idx = s_halton_idx;
	float halton_x = 0.0f, halton_y = 0.0f;
	float f = 0.5f;
	while (idx > 0) {
		halton_x += (idx & 1) * f;
		idx >>= 1;
		f *= 0.5f;
	}
	idx = s_halton_idx;
	f = 1.0f / 3.0f;
	while (idx > 0) {
		halton_y += (idx % 3) * f;
		idx /= 3;
		f *= (1.0f / 3.0f);
	}
	// Map to [-0.5, 0.5] pixel range
	float jitter_x = (halton_x - 0.5f) * 2.0f;
	float jitter_y = (halton_y - 0.5f) * 2.0f;
	
	// Apply jitter to projection matrix (column-major)
	// proj[8] = x offset, proj[9] = y offset (in clip space)
	float px_size_x = 2.0f / stage_w;
	float px_size_y = 2.0f / stage_h;
	proj[8] += jitter_x * px_size_x;
	proj[9] += jitter_y * px_size_y;
	
	// Store jitter for temporal reprojection
	s_gl.csm_prev_jitter_x = s_gl.csm_jitter_x;
	s_gl.csm_prev_jitter_y = s_gl.csm_jitter_y;
	s_gl.csm_jitter_x = jitter_x;
	s_gl.csm_jitter_y = jitter_y;
	
	s_gl.cur_near = near_z;
	s_gl.cur_far = far_z;
	s_gl.cur_tan_half = tanf(fov_rad * 0.5f);
	s_gl.cur_aspect = aspect;
	ar_mat4_multiply(s_gl.cur_mvp, proj, modelview);
	ar_frustum_from_matrix(&s_gl.cur_frustum, s_gl.cur_mvp);

	if (!s_gl.initialized) init_pipelines();
	(void)check_pipelines_ready();

	s_gl.stage_w = stage_w;
	s_gl.stage_h = stage_h;
	s_gl.frame_open = true;
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, (GLsizei)stage_w, (GLsizei)stage_h);
	if (s_gl.pipelines_ready)
		gl_ensure_scene_targets(stage_w, stage_h);
	gl_bind_scene_target();
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_TRUE);
	glDisable(GL_CULL_FACE);

	float space_t = fminf(1.0f, fmaxf(0.0f, (s_gl.cur_dist - 12000.0f) / 40000.0f));
	float sky_r = 0.55f * (1.0f - space_t) + 0.012f * space_t;
	float sky_g = 0.70f * (1.0f - space_t) + 0.016f * space_t;
	float sky_b = 0.85f * (1.0f - space_t) + 0.035f * space_t;

	glClearColor(sky_r, sky_g, sky_b, 1.0f);
	glClearStencil(0);
	glStencilMask(0xFF);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	if (s_gl.pipelines_ready)
		gl_render_cloud_shadow_map();
	else {
		s_gl.csm_valid = false;
		s_gl.csm_clear_sky = false;
	}
}

static void select_grid_buffers(uint8_t level, int32_t ox, int32_t oy, int32_t span_w, int32_t span_h,
                                GLuint *out_vbo, GLuint *out_ibo, GLsizei *out_indices)
{
	/* Compute distance from camera focal point to nearest point on chunk box */
	float min_x = (float)ox, max_x = (float)(ox + span_w);
	float min_y = (float)oy, max_y = (float)(oy + span_h);
	float dx = 0.0f, dy = 0.0f;
	if (s_gl.cur_tx < min_x) dx = min_x - s_gl.cur_tx;
	else if (s_gl.cur_tx > max_x) dx = s_gl.cur_tx - max_x;
	if (s_gl.cur_ty < min_y) dy = min_y - s_gl.cur_ty;
	else if (s_gl.cur_ty > max_y) dy = s_gl.cur_ty - max_y;
	float eff_dist = sqrtf(s_gl.cur_dist * s_gl.cur_dist + dx * dx + dy * dy);

	uint32_t quads = nd_seven_gl_grid_quads_for_zoom(level, eff_dist);
	if (quads >= GL_GRID_QUADS_HI) {
		*out_vbo = s_gl.grid_vbo_hi;
		*out_ibo = s_gl.grid_ibo_hi;
		*out_indices = GL_TOTAL_INDICES_HI;
	} else if (quads >= GL_GRID_QUADS_LO) {
		*out_vbo = s_gl.grid_vbo;
		*out_ibo = s_gl.grid_ibo;
		*out_indices = GL_TOTAL_INDICES;
	} else {
		*out_vbo = s_gl.grid_vbo_macro;
		*out_ibo = s_gl.grid_ibo_macro;
		*out_indices = GL_TOTAL_INDICES_MACRO;
	}
}

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

void nd_seven_gl_flush_terrain_chunk(uint16_t world, uint8_t level, uint32_t rev,
                                     int32_t ox, int32_t oy, int32_t span_w, int32_t span_h,
                                     const uint32_t *color_rgba, const int16_t *height_i16,
                                     uint32_t chunk_w, uint32_t chunk_h,
                                     uint32_t stage_w, uint32_t stage_h,
                                     float sea_level,
                                     const nd_planet_def_t *pdef)
{
	(void)stage_w; (void)stage_h;
	if (!s_gl.initialized || !s_gl.pipelines_ready || !s_gl.terrain_pipe.prog) return;
	const nd_planet_def_t *eff_pdef = pdef ? pdef : &s_gl.cur_pdef;

	float terrain_sea_lvl = sea_level;
	if (world == s_gl.cur_pdef.world_id && terrain_sea_lvl > -900.0f) {
		bool contains_focal = (s_gl.cur_tx >= (float)ox && s_gl.cur_tx <= (float)(ox + span_w) &&
		                       s_gl.cur_ty >= (float)oy && s_gl.cur_ty <= (float)(oy + span_h));
		if (contains_focal && (level == 0 || terrain_sea_lvl > s_gl.cur_sea_level)) {
			s_gl.cur_sea_level = terrain_sea_lvl;
		} else if (s_gl.cur_sea_level <= -900.0f) {
			s_gl.cur_sea_level = terrain_sea_lvl;
		}
	}

	/* Find or allocate chunk textures in LRU cache */
	int slot_idx = -1;
	int oldest_idx = 0;
	uint32_t oldest_touch = 0xFFFFFFFFu;

	for (int i = 0; i < MAX_CHUNK_TEXTURES; i++) {
		if (s_gl.chunk_cache[i].active &&
		    s_gl.chunk_cache[i].world == world &&
		    s_gl.chunk_cache[i].level == level &&
		    s_gl.chunk_cache[i].ox == ox &&
		    s_gl.chunk_cache[i].oy == oy &&
		    s_gl.chunk_cache[i].span_w == span_w &&
		    s_gl.chunk_cache[i].span_h == span_h) {
			slot_idx = i;
			break;
		}
		if (!s_gl.chunk_cache[i].active && slot_idx < 0) {
			slot_idx = i;
		}
		if (s_gl.chunk_cache[i].last_touch < oldest_touch) {
			oldest_touch = s_gl.chunk_cache[i].last_touch;
			oldest_idx = i;
		}
	}

	if (slot_idx < 0) slot_idx = oldest_idx;
	gl_chunk_tex_t *slot = &s_gl.chunk_cache[slot_idx];

	if (!slot->height_tex) {
		slot->height_tex = glCreateTexture();
		glBindTexture(GL_TEXTURE_2D, slot->height_tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}
	if (!slot->substrate_tex) {
		slot->substrate_tex = glCreateTexture();
		glBindTexture(GL_TEXTURE_2D, slot->substrate_tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}

	bool needs_upload = (!slot->active || slot->rev != rev ||
	                     slot->world != world || slot->ox != ox || slot->oy != oy ||
	                     slot->tex_w != chunk_w || slot->tex_h != chunk_h);

	if (needs_upload) {
		slot->active = 1;
		slot->world = world;
		slot->level = level;
		slot->rev = rev;
		slot->ox = ox;
		slot->oy = oy;
		slot->span_w = span_w;
		slot->span_h = span_h;
		slot->tex_w = chunk_w;
		slot->tex_h = chunk_h;

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, slot->height_tex);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_R16I, chunk_w, chunk_h, 0, GL_RED_INTEGER, GL_SHORT, height_i16);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, slot->substrate_tex);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, chunk_w, chunk_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, color_rgba);
	}
	slot->last_touch = s_gl.frame_count;

	double p0_chunk[3];
	torus_frame_double((double)s_gl.cur_tx, (double)s_gl.cur_ty, 0.0, eff_pdef, p0_chunk, NULL, NULL, NULL);
	float rel_center[3] = {
		(float)(p0_chunk[0] - s_gl.cur_origin[0]),
		(float)(p0_chunk[1] - s_gl.cur_origin[1]),
		(float)(p0_chunk[2] - s_gl.cur_origin[2])
	};
	bool has_rot = (eff_pdef->rot[0] != 0.0f || eff_pdef->rot[4] != 0.0f || eff_pdef->rot[8] != 0.0f);
	float rot_mat4[16] = {
		has_rot ? eff_pdef->rot[0] : 1.0f, has_rot ? eff_pdef->rot[3] : 0.0f, has_rot ? eff_pdef->rot[6] : 0.0f, 0.0f,
		has_rot ? eff_pdef->rot[1] : 0.0f, has_rot ? eff_pdef->rot[4] : 1.0f, has_rot ? eff_pdef->rot[7] : 0.0f, 0.0f,
		has_rot ? eff_pdef->rot[2] : 0.0f, has_rot ? eff_pdef->rot[5] : 0.0f, has_rot ? eff_pdef->rot[8] : 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	};

	/* Draw terrain chunk mesh */
	glUseProgram(s_gl.terrain_pipe.prog);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, slot->height_tex);
	glUniform1i(s_gl.terrain_pipe.uHeightTex, 0);

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, slot->substrate_tex);
	glUniform1i(s_gl.terrain_pipe.uSubstrateTex, 1);
	gl_bind_csm(s_gl.terrain_pipe.uCloudShadowMap, s_gl.terrain_pipe.uCsm);

	glUniformMatrix4fv(s_gl.terrain_pipe.uMVP, 1, GL_FALSE, s_gl.cur_mvp);
	glUniform2f(s_gl.terrain_pipe.uChunkOrigin, (float)ox, (float)oy);
	glUniform2f(s_gl.terrain_pipe.uChunkRelOrigin,
	            (float)((double)ox - (double)s_gl.cur_tx),
	            (float)((double)oy - (double)s_gl.cur_ty));
	glUniform2f(s_gl.terrain_pipe.uChunkSpan, (float)span_w, (float)span_h);
	glUniform4f(s_gl.terrain_pipe.uRefTrig,
	            s_gl.cur_ref_trig[0], s_gl.cur_ref_trig[1],
	            s_gl.cur_ref_trig[2], s_gl.cur_ref_trig[3]);
	glUniform1f(s_gl.terrain_pipe.uHeightScale, 1.0f);
	glUniform1f(s_gl.terrain_pipe.uSeaLevel, terrain_sea_lvl);
	glUniform3fv(s_gl.terrain_pipe.uCameraPos, 1, s_gl.cur_eye);
	glUniform3fv(s_gl.terrain_pipe.uPlanetCenter, 1, rel_center);
	glUniformMatrix4fv(s_gl.terrain_pipe.uBodyRot, 1, GL_FALSE, rot_mat4);
	glUniform2f(s_gl.terrain_pipe.uTorusRadii, eff_pdef->major_r, eff_pdef->minor_r);
	glUniform2f(s_gl.terrain_pipe.uWorldDim, ND_SEVEN_WORLD_DIM_W, ND_SEVEN_WORLD_DIM_H);

	float space_t = fminf(1.0f, fmaxf(0.0f, (s_gl.cur_dist - 12000.0f) / 40000.0f));
	/* In deep space (space_t -> 1), push distance fog out to infinity so distant celestial bodies
	 * (like the Moon behind the main planet) remain crisp and un-fogged against space */
	float fog_scale = 1.0f + space_t * 250.0f;
	glUniform1f(s_gl.terrain_pipe.uFogStart, s_gl.cur_dist * 1.5f * fog_scale);
	glUniform1f(s_gl.terrain_pipe.uFogEnd, s_gl.cur_dist * 4.0f * fog_scale);
	glUniform1f(s_gl.terrain_pipe.uAnimTime, s_gl.cur_anim_time);

	glUniform3f(s_gl.terrain_pipe.uSkyColor,
	            0.55f * (1.0f - space_t) + 0.012f * space_t,
	            0.70f * (1.0f - space_t) + 0.016f * space_t,
	            0.85f * (1.0f - space_t) + 0.035f * space_t);
	glUniform1f(s_gl.terrain_pipe.uAtmoIntensity, fminf(1.0f, fmaxf(0.0f, (s_gl.cur_dist - 1200.0f) / 8000.0f)));
	glUniform4f(s_gl.terrain_pipe.uLambLight,
	            s_gl.cur_lamb_light[0], s_gl.cur_lamb_light[1],
	            s_gl.cur_lamb_light[2], s_gl.cur_lamb_light[3]);
	glUniform1f(s_gl.terrain_pipe.uBaseSeaLevel, s_gl.cur_base_sea_level);
	glUniform3fv(s_gl.terrain_pipe.uSunDir, 1, s_gl.cur_sun_dir);
	glUniform3fv(s_gl.terrain_pipe.uMoonDir, 1, s_gl.cur_moon_dir);
	glUniform4f(s_gl.terrain_pipe.uLambWakeHead,
	            s_gl.cur_wake_head[0], s_gl.cur_wake_head[1],
	            s_gl.cur_wake_head[2], s_gl.cur_wake_head[3]);
	glUniformMatrix4fv(s_gl.terrain_pipe.uLambWakeXYT0, 1, GL_FALSE, &s_gl.cur_wake_xyt[0][0]);
	glUniformMatrix4fv(s_gl.terrain_pipe.uLambWakeXYT1, 1, GL_FALSE, &s_gl.cur_wake_xyt[4][0]);
	glUniformMatrix4fv(s_gl.terrain_pipe.uLambWakeVel0, 1, GL_FALSE, &s_gl.cur_wake_vel[0][0]);
	glUniformMatrix4fv(s_gl.terrain_pipe.uLambWakeVel1, 1, GL_FALSE, &s_gl.cur_wake_vel[4][0]);

	/* Pass canonical world ID and sea_offset_q12_4 for GPU canonical field evaluation */
	{
		static uint16_t s_cached_gl_world = 0xFFFFu;
		static int32_t s_cached_gl_sea_offset = 0;
		if (world != s_cached_gl_world) {
			uint32_t w_seed = 0x48454947u ^ ((uint32_t)world * 0x9E3779B1u);
			int32_t origin_bias = nd_field_eval_coarse_unwrapped_q12_4(w_seed, 0, 0) >> 2;
			uint32_t w_noise = (((world >> 2) + 2) & 3) * 65536u;
			s_cached_gl_sea_offset = (int32_t)((((int64_t)w_noise * 200 / 262144 - 100)) << 4) +
			                         ND_FIELD_ORIGIN_SEA_OFFSET_Q12_4 + origin_bias;
			s_cached_gl_world = world;
		}
		glUniform1i(s_gl.terrain_pipe.uWorldId, (GLint)world);
		glUniform1i(s_gl.terrain_pipe.uSeaOffsetQ12_4, (GLint)s_cached_gl_sea_offset);
	}

	GLuint active_vbo = 0, active_ibo = 0;
	GLsizei active_indices = 0;
	select_grid_buffers(level, ox, oy, span_w, span_h, &active_vbo, &active_ibo, &active_indices);

	glBindBuffer(GL_ARRAY_BUFFER, active_vbo);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, 0);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, active_ibo);
	glDrawElements(GL_TRIANGLES, active_indices, GL_UNSIGNED_SHORT, 0);

	/* Queue volumetric cloud shell pass for this chunk (gated by 3D cloud-shell frustum,
	 * planetary limb visibility, zoom ramp, and synoptic clear-sky pre-cull) */
	if (s_gl.cloud_count < (uint32_t)nd_seven_gl_max_water_queue() &&
	    nd_seven_gl_should_queue_cloud_chunk(ox, oy, span_w, span_h, eff_pdef)) {
		
		/* Hi-Z occlusion test for cloud chunk */
		if (s_gl.hiz_valid) {
			float cloud_z = 920.0f;  // Cloud slice altitude
			if (nd_seven_hiz_test_cloud_chunk(ox, oy, span_w, span_h, s_gl.cur_mvp, cloud_z)) {
				s_gl.cloud_culled_hiz++;
				// Fully occluded - skip this cloud chunk
			} else {
				gl_water_chunk_t *cq = &s_gl.cloud_queue[s_gl.cloud_count++];
				cq->ox = ox;
				cq->oy = oy;
				cq->span_w = span_w;
				cq->span_h = span_h;
				cq->level = level;
				cq->sea_level = terrain_sea_lvl;
				cq->height_tex = slot->height_tex;
				cq->substrate_tex = slot->substrate_tex;
				cq->rel_center[0] = rel_center[0];
				cq->rel_center[1] = rel_center[1];
				cq->rel_center[2] = rel_center[2];
				for (int k = 0; k < 16; k++) cq->rot_mat4[k] = rot_mat4[k];
				cq->major_r = eff_pdef->major_r;
				cq->minor_r = eff_pdef->minor_r;
			}
		} else {
			// Hi-Z not available, queue anyway
			gl_water_chunk_t *cq = &s_gl.cloud_queue[s_gl.cloud_count++];
			cq->ox = ox;
			cq->oy = oy;
			cq->span_w = span_w;
			cq->span_h = span_h;
			cq->level = level;
			cq->sea_level = terrain_sea_lvl;
			cq->height_tex = slot->height_tex;
			cq->substrate_tex = slot->substrate_tex;
			cq->rel_center[0] = rel_center[0];
			cq->rel_center[1] = rel_center[1];
			cq->rel_center[2] = rel_center[2];
			for (int k = 0; k < 16; k++) cq->rot_mat4[k] = rot_mat4[k];
			cq->major_r = eff_pdef->major_r;
			cq->minor_r = eff_pdef->minor_r;
		}
	}

	/* Queue the water surface of this chunk for the submerged-object pass */
	if (terrain_sea_lvl > -900.0f && s_gl.water_count < (uint32_t)nd_seven_gl_max_water_queue()) {
		/* Skip water chunks that are occluded (2D hierarchical occlusion map) */
#ifndef __wasm__
		if (s_occ2d_valid && s_gl.hiz_valid) {
			if (nd_seven_occ2d_test_water_chunk(ox, oy, span_w, span_h, s_gl.cur_mvp, terrain_sea_lvl)) {
				s_gl.water_culled_occ2d++;
				return; /* Don't queue this water chunk */
			}
		}
#else
		/* WASM: fall back to 1D ridge test */
		if (nd_seven_is_water_ridge_occluded(ox, oy, span_w, span_h,
		                                     s_gl.cur_mvp, s_gl.cur_lamb_light[0], s_gl.cur_lamb_light[1],
		                                     s_gl.cur_pdef.minor_r))
			return; /* Don't queue this water chunk */
#endif
		gl_water_chunk_t *wq = &s_gl.water_queue[s_gl.water_count++];
		wq->ox = ox;
		wq->oy = oy;
		wq->span_w = span_w;
		wq->span_h = span_h;
		wq->level = level;
		wq->sea_level = terrain_sea_lvl;
		wq->height_tex = slot->height_tex;
		wq->substrate_tex = slot->substrate_tex;
		wq->rel_center[0] = rel_center[0];
		wq->rel_center[1] = rel_center[1];
		wq->rel_center[2] = rel_center[2];
		for (int k = 0; k < 16; k++) wq->rot_mat4[k] = rot_mat4[k];
		wq->major_r = eff_pdef->major_r;
		wq->minor_r = eff_pdef->minor_r;
	}
}

void nd_seven_gl_flush_water(void)
{
	/* The terrain pass already shades water over the seabed. This pass composites only what stands BELOW the surface on top
	 * of that (sprites, marked in the stencil buffer): refraction of the resolved scene, true view-ray absorption from the
	 * depth difference, then the same air-water surface layer as the terrain pass. Skipped when nothing is submerged. */
	if (!s_gl.initialized || !s_gl.pipelines_ready || !s_gl.water_pipe.prog || s_gl.water_count == 0 ||
	    !s_gl.sprites_in_water || !s_gl.scene_ok || s_gl.cur_dist > 2000.0f) {
		s_gl.water_count = 0;
		return;
	}

	gl_resolve_scene(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glUseProgram(s_gl.water_pipe.prog);
	gl_bind_csm(s_gl.water_pipe.uCloudShadowMap, s_gl.water_pipe.uCsm);

	glActiveTexture(GL_TEXTURE0 + 4);
	glBindTexture(GL_TEXTURE_2D, s_gl.res_color_tex);
	glUniform1i(s_gl.water_pipe.uSceneColor, 4);
	glActiveTexture(GL_TEXTURE0 + 5);
	glBindTexture(GL_TEXTURE_2D, s_gl.res_depth_tex);
	glUniform1i(s_gl.water_pipe.uSceneDepth, 5);
	glActiveTexture(GL_TEXTURE0);
	glUniform2f(s_gl.water_pipe.uViewport, (float)s_gl.scene_w, (float)s_gl.scene_h);
	glUniform2f(s_gl.water_pipe.uNearFar, s_gl.cur_near, s_gl.cur_far);
	glUniform2f(s_gl.water_pipe.uProjScale, s_gl.cur_tan_half * s_gl.cur_aspect, s_gl.cur_tan_half);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_FALSE);
	glDisable(GL_BLEND);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
	glEnable(GL_STENCIL_TEST);
	glStencilMask(0);
	glStencilFunc(GL_EQUAL, 1, 0xFF);
	glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

	float zero_center[3] = { 0.0f, 0.0f, 0.0f };
	glUniformMatrix4fv(s_gl.water_pipe.uMVP, 1, GL_FALSE, s_gl.cur_mvp);
	glUniform4f(s_gl.water_pipe.uRefTrig,
	            s_gl.cur_ref_trig[0], s_gl.cur_ref_trig[1],
	            s_gl.cur_ref_trig[2], s_gl.cur_ref_trig[3]);
	glUniform1f(s_gl.water_pipe.uHeightScale, 1.0f);
	glUniform3fv(s_gl.water_pipe.uCameraPos, 1, s_gl.cur_eye);
	glUniform3fv(s_gl.water_pipe.uPlanetCenter, 1, zero_center);
	glUniform2f(s_gl.water_pipe.uTorusRadii, s_gl.cur_pdef.major_r, s_gl.cur_pdef.minor_r);
	glUniform2f(s_gl.water_pipe.uWorldDim, ND_SEVEN_WORLD_DIM_W, ND_SEVEN_WORLD_DIM_H);

	glUniform1f(s_gl.water_pipe.uFogStart, s_gl.cur_dist * 1.5f);
	glUniform1f(s_gl.water_pipe.uFogEnd, s_gl.cur_dist * 4.0f);
	glUniform1f(s_gl.water_pipe.uAnimTime, s_gl.cur_anim_time);

	float space_t = fminf(1.0f, fmaxf(0.0f, (s_gl.cur_dist - 12000.0f) / 40000.0f));
	glUniform3f(s_gl.water_pipe.uSkyColor,
	            0.55f * (1.0f - space_t) + 0.012f * space_t,
	            0.70f * (1.0f - space_t) + 0.016f * space_t,
	            0.85f * (1.0f - space_t) + 0.035f * space_t);
	glUniform1f(s_gl.water_pipe.uAtmoIntensity, fminf(1.0f, fmaxf(0.0f, (s_gl.cur_dist - 1200.0f) / 8000.0f)));
	glUniform4f(s_gl.water_pipe.uLambLight,
	            s_gl.cur_lamb_light[0], s_gl.cur_lamb_light[1],
	            s_gl.cur_lamb_light[2], s_gl.cur_lamb_light[3]);
	glUniform1f(s_gl.water_pipe.uBaseSeaLevel, s_gl.cur_base_sea_level);
	glUniform3fv(s_gl.water_pipe.uSunDir, 1, s_gl.cur_sun_dir);
	glUniform3fv(s_gl.water_pipe.uMoonDir, 1, s_gl.cur_moon_dir);
	glUniform4f(s_gl.water_pipe.uLambWakeHead,
	            s_gl.cur_wake_head[0], s_gl.cur_wake_head[1],
	            s_gl.cur_wake_head[2], s_gl.cur_wake_head[3]);
	glUniformMatrix4fv(s_gl.water_pipe.uLambWakeXYT0, 1, GL_FALSE, &s_gl.cur_wake_xyt[0][0]);
	glUniformMatrix4fv(s_gl.water_pipe.uLambWakeXYT1, 1, GL_FALSE, &s_gl.cur_wake_xyt[4][0]);
	glUniformMatrix4fv(s_gl.water_pipe.uLambWakeVel0, 1, GL_FALSE, &s_gl.cur_wake_vel[0][0]);
	glUniformMatrix4fv(s_gl.water_pipe.uLambWakeVel1, 1, GL_FALSE, &s_gl.cur_wake_vel[4][0]);

	for (uint32_t i = 0; i < s_gl.water_count; i++) {
		const gl_water_chunk_t *chk = &s_gl.water_queue[i];
		GLuint w_vbo = 0, w_ibo = 0;
		GLsizei w_indices = 0;
		select_grid_buffers(chk->level, chk->ox, chk->oy, chk->span_w, chk->span_h, &w_vbo, &w_ibo, &w_indices);

		glBindBuffer(GL_ARRAY_BUFFER, w_vbo);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, 0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, w_ibo);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, chk->height_tex);
		glUniform1i(s_gl.water_pipe.uHeightTex, 0);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, chk->substrate_tex);
		glUniform1i(s_gl.water_pipe.uSubstrateTex, 1);

		glUniform3fv(s_gl.water_pipe.uPlanetCenter, 1, chk->rel_center);
		glUniformMatrix4fv(s_gl.water_pipe.uBodyRot, 1, GL_FALSE, chk->rot_mat4);
		glUniform2f(s_gl.water_pipe.uTorusRadii, chk->major_r, chk->minor_r);
		glUniform2f(s_gl.water_pipe.uChunkOrigin, (float)chk->ox, (float)chk->oy);
		glUniform2f(s_gl.water_pipe.uChunkRelOrigin,
		            (float)((double)chk->ox - (double)s_gl.cur_tx),
		            (float)((double)chk->oy - (double)s_gl.cur_ty));
		glUniform2f(s_gl.water_pipe.uChunkSpan, (float)chk->span_w, (float)chk->span_h);
		glUniform1f(s_gl.water_pipe.uSeaLevel, chk->sea_level);

		glDrawElements(GL_TRIANGLES, w_indices, GL_UNSIGNED_SHORT, 0);
	}

	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDepthMask(GL_TRUE);
	glDisable(GL_STENCIL_TEST);
	glStencilMask(0xFF);
	glEnable(GL_BLEND);
	s_gl.water_count = 0;
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

	/* Clear cluster light lists (stride = 33 bytes per cluster: byte 0 = count, bytes 1..32 = light indices) */
	const uint32_t stride = ND_MAX_LIGHTS_PER_CLUSTER + 1;
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
	}

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glBindTexture(GL_TEXTURE_2D, s_gl.cluster_light_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, ND_MAX_TOTAL_LIGHTS, 0,
	             GL_RGBA, GL_UNSIGNED_BYTE, s_gl.cluster_light_tex_data);

	glBindTexture(GL_TEXTURE_2D, s_gl.cluster_list_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, (GLsizei)stride, ND_CLUSTER_TOTAL, 0,
	             GL_RED, GL_UNSIGNED_BYTE, s_gl.cluster_list_tex_data);
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

void nd_seven_gl_flush_billboards(const lam_billboard_gpu_t *batch,
                                  uint32_t count,
                                  const uint32_t *atlas_rgba)
{
	if (!batch || count == 0) return;
	if (!s_gl.initialized || !s_gl.pipelines_ready || !s_gl.bb_pipe.prog) return;

	uint32_t cur_ver = atlas_rgba_version();
	if ((!s_gl.atlas_tex || !s_gl.atlas_norm_tex || s_gl.atlas_ver != cur_ver) && atlas_rgba) {
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

		const uint32_t *norm_rgba = atlas_norm_rgba();
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
		s_gl.atlas_ver = cur_ver;
	}

	float det_scale = nd_seven_get_detail_scale();
	bool emit_shadows = (s_gl.cur_dist < 5000.0f * det_scale);
	bool emit_grass_shadows = (s_gl.cur_dist < 1800.0f * det_scale);
	bool emit_lamb_shadows = (s_lamb_shadow_light[3] > 0.01f && s_gl.cur_dist < 3200.0f * det_scale);
	float pitch_rad = s_gl.cur_pitch_deg * (PI_F / 180.0f);
	float cos_p = cosf(pitch_rad);
	float sin_p = sinf(pitch_rad);

	glUseProgram(s_gl.bb_pipe.prog);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, s_gl.atlas_tex);
	glUniform1i(s_gl.bb_pipe.uAtlasTex, 0);
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, s_gl.atlas_norm_tex);
	glUniform1i(s_gl.bb_pipe.uAtlasNormTex, 2);
	gl_bind_csm(s_gl.bb_pipe.uCloudShadowMap, s_gl.bb_pipe.uCsm);

	glUniformMatrix4fv(s_gl.bb_pipe.uMVP, 1, GL_FALSE, s_gl.cur_mvp);
	float bb_sea_level = (s_gl.focal_sea_level > -900.0f) ? s_gl.focal_sea_level : s_gl.cur_sea_level;
	glUniform1f(s_gl.bb_pipe.uSeaLevel, bb_sea_level);
	glUniform1f(s_gl.bb_pipe.uAnimTime, s_gl.cur_anim_time);
	glUniform3fv(s_gl.bb_pipe.uCameraPos, 1, s_gl.cur_eye);
	glUniform1f(s_gl.bb_pipe.uFogStart, s_gl.cur_dist * 1.5f);
	glUniform1f(s_gl.bb_pipe.uFogEnd, s_gl.cur_dist * 4.0f);

	float space_t = fminf(1.0f, fmaxf(0.0f, (s_gl.cur_dist - 12000.0f) / 40000.0f));
	glUniform3f(s_gl.bb_pipe.uSkyColor,
	            0.55f * (1.0f - space_t) + 0.012f * space_t,
	            0.70f * (1.0f - space_t) + 0.016f * space_t,
	            0.85f * (1.0f - space_t) + 0.035f * space_t);
	glUniform1f(s_gl.bb_pipe.uAtmoIntensity, fminf(1.0f, fmaxf(0.0f, (s_gl.cur_dist - 1200.0f) / 8000.0f)));
	glUniform4f(s_gl.bb_pipe.uLambLight,
	            s_gl.cur_lamb_light[0], s_gl.cur_lamb_light[1],
	            s_gl.cur_lamb_light[2], s_gl.cur_lamb_light[3]);
	glUniform3fv(s_gl.bb_pipe.uSunDir, 1, s_gl.cur_sun_dir);

	glBindBuffer(GL_ARRAY_BUFFER, s_gl.bb_vbo);

	for (GLuint a = 0; a < 7; a++) {
		glEnableVertexAttribArray(a);
		glVertexAttribPointer(a, 4, GL_FLOAT, GL_FALSE, sizeof(nd_seven_gl_bb_inst_t), (size_t)(a * 16));
		glVertexAttribDivisor(a, 1);
	}

	/* Sprites are drawn in the same depth buffer as terrain and water:
	 *   pass 0 (core)   opaque pixels (alpha >= 0.5): depth write + stencil mark, so the water pass can refract and
	 *                   absorb exactly the pixels that stand below the surface;
	 *   pass 1 (fringe) anti-aliased edges of the same sprites (alpha < 0.5), blended, no depth write;
	 *   pass 2 (other)  ground contact shadows, glow auras and spray particles, blended, no depth write.
	 * Core and "other" quads are streamed as instanced triangle strips via glDrawArraysInstanced. */
	uint32_t core_n = 0, other_n = 0;
	for (uint32_t i = 0; i <= count; i++) {
		bool last = (i == count);
		bool is_other = false, want_shadow = false, want_lamb_shadow = false;
		uint32_t need_core = 0, need_other = 0;
		if (!last) {
			/* Hi-Z occlusion culling for billboards (native build) */
#ifndef __wasm__
			if (s_gl.hiz_valid && s_occ2d_valid) {
				float radius = fmaxf(batch[i].width, batch[i].height) * 0.5f;
				if (nd_seven_hiz_test_sphere(batch[i].x, batch[i].y, batch[i].z, radius)) {
					s_gl.billboard_culled_hiz++;
					continue;  // Skip this billboard
				}
			}
#endif
			bool is_grass = (batch[i].style == ND_BB_STYLE_GRASS);
			is_other = (batch[i].style == ND_BB_STYLE_GLOW) || (batch[i].style == ND_BB_STYLE_PARTICLE);
			if (batch[i].style == ND_BB_STYLE_GLOW && s_gl.local_light_count < ND_MAX_TOTAL_LIGHTS) {
				float gr = (float)(batch[i].tint_rgba & 0xFFu) / 255.0f;
				float gg = (float)((batch[i].tint_rgba >> 8) & 0xFFu) / 255.0f;
				float gb = (float)((batch[i].tint_rgba >> 16) & 0xFFu) / 255.0f;
				float ga = (float)((batch[i].tint_rgba >> 24) & 0xFFu) / 255.0f;
				if (ga > 0.05f) {
					float dx_l = batch[i].x - s_gl.cur_lamb_light[0];
					float dy_l = batch[i].y - s_gl.cur_lamb_light[1];
					/* Skip duplicate of primary Lamb glow if within 4 world units */
					if (s_gl.cur_lamb_light[3] <= 0.01f || (dx_l * dx_l + dy_l * dy_l) > 16.0f) {
						(void)nd_seven_gl_add_point_light(batch[i].x, batch[i].y, batch[i].z + 360.0f,
						                                  gr, gg, gb, ga * 1.25f, 1200.0f);
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
				float dx_l = batch[i].x - s_lamb_shadow_light[0];
				float dy_l = batch[i].y - s_lamb_shadow_light[1];
				float d2_l = dx_l * dx_l + dy_l * dy_l;
				want_lamb_shadow = (d2_l >= 4.0f && d2_l <= 220.0f * 220.0f);
			}
			need_core = is_other ? 0u : 1u;
			need_other = (is_other ? 1u : 0u) + (want_shadow ? 1u : 0u) + (want_lamb_shadow ? 1u : 0u);
		}
		if (last || core_n + need_core > MAX_BB_INSTANCES || other_n + need_other > MAX_BB_INSTANCES) {
			if (core_n > 0) {
				glBufferData(GL_ARRAY_BUFFER, s_bb_instances, core_n * sizeof(nd_seven_gl_bb_inst_t), GL_DYNAMIC_DRAW);
				/* pass 0: core */
				glUniform1i(s_gl.bb_pipe.uPass, 0);
				glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
				glDisable(GL_BLEND);
				glDepthMask(GL_TRUE);
				glEnable(GL_STENCIL_TEST);
				glStencilMask(0xFF);
				glStencilFunc(GL_ALWAYS, 1, 0xFF);
				glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
				glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)core_n);
				glDisable(GL_STENCIL_TEST);
				/* pass 1: anti-aliased fringe */
				glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
				glUniform1i(s_gl.bb_pipe.uPass, 1);
				glEnable(GL_BLEND);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				glDepthMask(GL_FALSE);
				glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)core_n);
			}
			if (other_n > 0) {
				glBufferData(GL_ARRAY_BUFFER, s_bb_other_instances,
				             other_n * sizeof(nd_seven_gl_bb_inst_t), GL_DYNAMIC_DRAW);
				glUniform1i(s_gl.bb_pipe.uPass, 2);
				glEnable(GL_BLEND);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				glDepthMask(GL_FALSE);
				glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)other_n);
			}
			core_n = 0;
			other_n = 0;
		}
		if (last) break;
		billboard_basis_t basis;
		eval_billboard_basis(&batch[i], &s_gl.cur_pdef, s_gl.cur_origin, &basis);
		if (want_shadow) {
			pack_bb_shadow_quad_instance(&batch[i], &basis, &s_bb_other_instances[other_n++]);
		}
		if (want_lamb_shadow) {
			if (pack_bb_lamb_shadow_quad_instance(&batch[i], &basis, &s_bb_other_instances[other_n]))
				other_n++;
		}
		if (is_other) {
			pack_bb_quad_instance(&batch[i], &basis, cos_p, sin_p, &s_bb_other_instances[other_n++]);
		} else {
			pack_bb_quad_instance(&batch[i], &basis, cos_p, sin_p, &s_bb_instances[core_n++]);
			if (bb_sea_level > -900.0f && batch[i].z < bb_sea_level + 1.0f)
				s_gl.sprites_in_water = true;
		}
	}

	for (GLuint a = 0; a < 7; a++) {
		glVertexAttribDivisor(a, 0);
		if (a > 0) glDisableVertexAttribArray(a);
	}
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDepthMask(GL_TRUE);
}

uint32_t nd_seven_gl_grid_quads_for_zoom(uint8_t level, float cam_dist)
{
	if (level >= 3 || cam_dist >= 5000.0f)
		return GL_GRID_QUADS_MACRO; /* 16x16 quads = 512 triangles (4x lighter macro mesh) */
	if (level > 0)
		return GL_GRID_QUADS_LO;    /* 32x32 quads = 2,048 triangles */
	/* Fixed performance budget: level 0 never subdivides past 48x48 quads (4,608 triangles) at any zoom */
	(void)cam_dist;
	return GL_GRID_QUADS_HI;
}

uint32_t nd_seven_gl_grid_quads_for_level(uint8_t level)
{
	return nd_seven_gl_grid_quads_for_zoom(level, 400.0f);
}

static inline uint32_t gl_chaff_rotl32(uint32_t x, int r)
{
	return (x << r) | (x >> (32 - r));
}

static inline uint32_t gl_chaff_hash3(int32_t px, int32_t py, int32_t pz, uint32_t seed)
{
	uint32_t v1 = ((uint32_t)py & 0xFFFFu) | (((uint32_t)px & 0xFFFFu) << 16u);
	uint32_t v2 = ((uint32_t)pz & 0xFFFFu);
	uint32_t h = seed + 0x165667B1u + 6u;
	h += v1 * 0xC2B2AE3Du;
	h = gl_chaff_rotl32(h, 17) * 0x27D4EB2Fu;
	h += v2 * 0x85EBCA77u;
	h = gl_chaff_rotl32(h, 13) * 0xC2B2AE3Du;
	h ^= h >> 15u;
	h *= 0x85EBCA77u;
	h ^= h >> 16u;
	return h;
}

static inline void gl_chaff_grad_dir(int32_t cx, int32_t cy, uint32_t seed,
                                     float *out_gx, float *out_gy)
{
	uint32_t h = gl_chaff_hash3(cx, cy, 0, seed);
	float gx = (float)((int32_t)(h & 0xFFFFu) - 32768) * (1.0f / 32768.0f);
	float gy = (float)((int32_t)(h >> 16u) - 32768) * (1.0f / 32768.0f);
	float inv_len = 1.0f / sqrtf(gx * gx + gy * gy + 1e-6f);
	*out_gx = gx * inv_len;
	*out_gy = gy * inv_len;
}

static inline void gl_chaff_grad_noise2d(float px, float py, uint32_t seed,
                                         float *out_val, float *out_gx, float *out_gy)
{
	float ipx = floorf(px);
	float ipy = floorf(py);
	float fpx = px - ipx;
	float fpy = py - ipy;
	float ux = fpx * fpx * fpx * (fpx * (fpx * 6.0f - 15.0f) + 10.0f);
	float uy = fpy * fpy * fpy * (fpy * (fpy * 6.0f - 15.0f) + 10.0f);
	float dux = 30.0f * fpx * fpx * (fpx * (fpx - 2.0f) + 1.0f);
	float duy = 30.0f * fpy * fpy * (fpy * (fpy - 2.0f) + 1.0f);
	int32_t cx = (int32_t)ipx;
	int32_t cy = (int32_t)ipy;

	float g00x, g00y, g10x, g10y, g01x, g01y, g11x, g11y;
	gl_chaff_grad_dir(cx,     cy,     seed, &g00x, &g00y);
	gl_chaff_grad_dir(cx + 1, cy,     seed, &g10x, &g10y);
	gl_chaff_grad_dir(cx,     cy + 1, seed, &g01x, &g01y);
	gl_chaff_grad_dir(cx + 1, cy + 1, seed, &g11x, &g11y);

	float v00 = g00x * fpx + g00y * fpy;
	float v10 = g10x * (fpx - 1.0f) + g10y * fpy;
	float v01 = g01x * fpx + g01y * (fpy - 1.0f);
	float v11 = g11x * (fpx - 1.0f) + g11y * (fpy - 1.0f);

	float k0 = v10 - v00;
	float k1 = v01 - v00;
	float k2 = v11 - v01 - k0;
	float val = 0.5f + 0.70710678f * (v00 + k0 * ux + k1 * uy + k2 * ux * uy);
	if (val < 0.0f) val = 0.0f;
	if (val > 1.0f) val = 1.0f;

	float gix = g00x + (g10x - g00x) * ux + (g01x - g00x) * uy + (g11x - g01x - g10x + g00x) * (ux * uy);
	float giy = g00y + (g10y - g00y) * ux + (g01y - g00y) * uy + (g11y - g01y - g10y + g00y) * (ux * uy);

	*out_val = val;
	*out_gx  = 0.70710678f * (gix + dux * (k0 + k2 * uy));
	*out_gy  = 0.70710678f * (giy + duy * (k1 + k2 * ux));
}

static inline float gl_smoothstep(float edge0, float edge1, float x)
{
	float t = (x - edge0) / (edge1 - edge0);
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;
	return t * t * (3.0f - 2.0f * t);
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

static inline void gl_eval_recursive_wind(float wx, float wy, float anim_time,
                                          float *out_dir_x, float *out_dir_y,
                                          float *out_gust, float *out_catspaw)
{
	float raw_t0 = anim_time * 0.05f;
	float t0 = raw_t0 - 1024.0f * floorf(raw_t0 / 1024.0f);
	float p0x = wx * 0.0055f - 0.741f * t0;
	float p0y = wy * 0.0055f + 0.671f * t0;
	float w0v = 0.0f, w0gx = 0.0f, w0gy = 0.0f;
	gl_chaff_grad_noise2d(p0x, p0y, 0x57494E44u, &w0v, &w0gx, &w0gy);

	float bwx = 0.741f - w0gy * 0.28f;
	float bwy = -0.671f + w0gx * 0.28f;
	float bw_inv = 1.0f / sqrtf(bwx * bwx + bwy * bwy + 1e-6f);
	bwx *= bw_inv;
	bwy *= bw_inv;

	float raw_t1 = anim_time * 0.12f;
	float t1 = raw_t1 - 1024.0f * floorf(raw_t1 / 1024.0f);
	float u1x = wx * 0.028f + w0gx * 0.65f;
	float u1y = wy * 0.028f + w0gy * 0.65f;
	float p1x = ( 0.8f * u1x + 0.6f * u1y) - 0.822f * t1;
	float p1y = (-0.6f * u1x + 0.8f * u1y) + 0.569f * t1;
	float w1v = 0.0f, w1gx = 0.0f, w1gy = 0.0f;
	gl_chaff_grad_noise2d(p1x, p1y, 0x47555354u, &w1v, &w1gx, &w1gy);
	float mgx =  0.8f * w1gx - 0.6f * w1gy;
	float mgy =  0.6f * w1gx + 0.8f * w1gy;

	float lwx = bwx - mgy * 0.22f;
	float lwy = bwy + mgx * 0.22f;
	float lw_inv = 1.0f / sqrtf(lwx * lwx + lwy * lwy + 1e-6f);
	lwx *= lw_inv;
	lwy *= lw_inv;

	float raw_t2 = anim_time * 0.22f;
	float t2 = raw_t2 - 1024.0f * floorf(raw_t2 / 1024.0f);
	float r1x =  0.8f * (wx * 0.120f) + 0.6f * (wy * 0.120f) + w0gx * 0.45f + w1gx * 0.55f;
	float r1y = -0.6f * (wx * 0.120f) + 0.8f * (wy * 0.120f) + w0gy * 0.45f + w1gy * 0.55f;
	float p2x = ( 0.8f * r1x + 0.6f * r1y) - 0.640f * t2;
	float p2y = (-0.6f * r1x + 0.8f * r1y) + 0.768f * t2;
	float w2v = 0.0f, w2gx = 0.0f, w2gy = 0.0f;
	gl_chaff_grad_noise2d(p2x, p2y, 0x45444459u, &w2v, &w2gx, &w2gy);
	/* irot * irot = mat2(0.28, 0.96, -0.96, 0.28) */
	float u2gx =  0.28f * w2gx - 0.96f * w2gy;
	float u2gy =  0.96f * w2gx + 0.28f * w2gy;

	float fwx = lwx - u2gy * 0.16f;
	float fwy = lwy + u2gx * 0.16f;
	float fw_inv = 1.0f / sqrtf(fwx * fwx + fwy * fwy + 1e-6f);
	fwx *= fw_inv;
	fwy *= fw_inv;

	float gust = 0.42f + 0.45f * w0v + 0.55f * (w1v - 0.38f) + 0.30f * (w2v - 0.42f);
	if (gust < 0.25f) gust = 0.25f;
	if (gust > 1.35f) gust = 1.35f;

	float cp = gl_smoothstep(0.38f, 0.76f, w1v * 0.62f + w2v * 0.38f);
	if (out_dir_x) *out_dir_x = fwx;
	if (out_dir_y) *out_dir_y = fwy;
	if (out_gust) *out_gust = gust;
	if (out_catspaw) *out_catspaw = cp;
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



