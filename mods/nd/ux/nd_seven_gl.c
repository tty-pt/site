/*
 * nd_seven_gl.c — Pure-C WebGL2 3D Terrain, Water, and Billboard Renderer.
 * Part of the NeverDark Seven 2.5D diorama platform.
 *
 * Implements the 3D rendering pipeline entirely in pure C over <ttypt/webgl.h>.
 * Dual-compiles for native SSR (safe no-op stubs) and WebAssembly (WebGL2).
 */

#include "nd_seven_gl.h"
#include "nd_seven_atlas.h"
#include "nd_seven_atlas_draw.h"
#include <ttypt/webgl.h>
#include <ttypt/areole.h>
#include <ttypt/chaff_glsl.h>
#include <nd/field.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

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

/* Analytic planetary cloud shadow (per-pixel evaluation): used above 14 km and as fallback when the cloud-shadow map is unavailable. */
#define ND_GLSL_CLOUD_SHADOW_FN \
	"/* Scale-Adaptive 2D Projection of the 3D Cloud Volume along the Sun Vector L_sun = (-0.577, -0.577, 0.816):\n" \
	" * Projects the 3D wind-sheared cloud decks onto the 2D ground/water surface with orbital shadow offset\n" \
	" * so continental cyclone arms and cumulus towers cast unmistakable 3D ground shadows from ground to orbit! */\n" \
	"float eval_planetary_cloud_shadow(vec2 fragXY, float fragZ, float anim_time, float humidity, float dist, float fw) {\n" \
	"    const float zBase = 420.0;\n" \
	"    const float zTop  = 2820.0;\n" \
	"    float orbitOffset = clamp(dist * 0.045, 0.0, 14000.0);\n" \
	"    if (dist > 14000.0) {\n" \
	"        float zk = 1150.0;\n" \
	"        float dz = max(0.0, zk - fragZ) + orbitOffset;\n" \
	"        vec2 projXY = fragXY + vec2(-0.7071, -0.7071) * dz;\n" \
	"        vec2 atmoWindDir = vec2(0.741, -0.671), shearDisp = vec2(0.0);\n" \
	"        float atmoGust = 0.65, updraft = 0.50;\n" \
	"        chaff_eval_wind_3d(vec3(projXY, zk), zBase, zTop, anim_time, fw, atmoWindDir, shearDisp, atmoGust, updraft);\n" \
	"        vec4 synCloud = eval_synoptic_macro_cloud_fw(projXY - shearDisp, anim_time, humidity, fw);\n" \
	"        return clamp(smoothstep(0.05, 0.65, synCloud.x), 0.0, 0.95);\n" \
	"    }\n" \
	"    /* Single representative projection slice at zk = 920 (the 2D synoptic field is identical at every height; the\n" \
	" * two former slices differed only by a ~640 unit sun-ward shift, far below the 1.1..4.2 km cell size) */\n" \
	"    const float zk = 920.0;\n" \
	"    if (fragZ >= zk + 240.0) return 0.0;\n" \
	"    float sliceFade = smoothstep(zk + 240.0, zk - 120.0, fragZ);\n" \
	"    float dz = max(0.0, zk - fragZ) + orbitOffset;\n" \
	"    vec2 projXY = fragXY + vec2(-0.7071, -0.7071) * dz;\n" \
	"    vec2 atmoWindDir = vec2(0.741, -0.671), shearDisp = vec2(0.0);\n" \
	"    float atmoGust = 0.65, updraft = 0.50;\n" \
	"    chaff_eval_wind_3d(vec3(projXY, zk), zBase, zTop, anim_time, fw, atmoWindDir, shearDisp, atmoGust, updraft);\n" \
	"    vec4 synCloud = eval_synoptic_macro_cloud_fw(projXY - shearDisp, anim_time, humidity, fw);\n" \
	"    return clamp(smoothstep(0.05, 0.66, synCloud.x * sliceFade), 0.0, 0.95);\n" \
	"}\n"

/* Cloud-shadow map (R8, one frame-wide evaluation of the synoptic density on the cloud slice z = 920):
 * the map is indexed by projected slice position, so terrain, water and sprites each do ONE texture fetch.
 * uCsm = (window centre x, window centre y, window half-extent); half <= 0 means "no map bound". */
#define ND_GLSL_CSM_LOOKUP_FN \
	"uniform sampler2D uCloudShadowMap;\n" \
	"uniform vec3 uCsm;\n" \
	"float nd_csm_density(vec2 sliceXY) {\n" \
	"    vec2 uv = (sliceXY - uCsm.xy) / (2.0 * uCsm.z) + 0.5;\n" \
	"    vec2 e = min(uv, 1.0 - uv);\n" \
	"    float edge = smoothstep(0.0, 0.04, min(e.x, e.y));\n" \
	"    return texture(uCloudShadowMap, uv).r * edge;\n" \
	"}\n" \
	"float nd_cloud_shadow_map(vec2 fragXY, float fragZ, float dist) {\n" \
	"    const float zk = 920.0;\n" \
	"    if (uCsm.z <= 0.0 || fragZ >= zk + 240.0) return 0.0;\n" \
	"    float sliceFade = smoothstep(zk + 240.0, zk - 120.0, fragZ);\n" \
	"    float dz = max(0.0, zk - fragZ) + clamp(dist * 0.045, 0.0, 14000.0);\n" \
	"    vec2 projXY = fragXY + vec2(-0.7071, -0.7071) * dz;\n" \
	"    return clamp(smoothstep(0.05, 0.66, nd_csm_density(projXY) * sliceFade), 0.0, 0.95);\n" \
	"}\n"

/* Terrain / water entry point: one map fetch up to 14 km, analytic evaluation in orbit or without a map. */
#define ND_GLSL_CLOUD_SHADOW_ENTRY_FN \
	"float nd_cloud_shadow(vec2 fragXY, float fragZ, float anim_time, float humidity, float dist, float fw) {\n" \
	"    if (dist > 14000.0 || uCsm.z <= 0.0) return eval_planetary_cloud_shadow(fragXY, fragZ, anim_time, humidity, dist, fw);\n" \
	"    return nd_cloud_shadow_map(fragXY, fragZ, dist);\n" \
	"}\n"

/* Canonical 2D Synoptic & Mesoscale Cellular Weather Envelope Function:
 * Pure function of (worldXY, anim_time, humidity, fw). Zero camera-distance morphing.
 * Driven by the evolved synoptic wind streamfunction and divergence-free curl flow:
 * 1. Synoptic Scale (600 km - 2,800 km): Planetary Rossby waves psi0/psi1 produce unbroken continental
 *    overcast shields (macroCoverage 0.82..1.00) in cyclonic convergence regimes, frontal transition corridors,
 *    and clear subsidence basins in high-pressure anticyclones.
 * 2. Regional Frontal Squall Scale (18 km - 120 km): Frontal rainbands and wind-shift squall lines.
 * 3. Mesoscale Cellular Cumulus & Cloud Streets (1.1 km - 4.2 km): Wind-aligned roll vortices and
 *    convective cumulus cells (FEW / SCT / BKN) with clear flight corridors. */
#define ND_GLSL_SYNOPTIC_CLOUD_FN \
	"vec4 eval_synoptic_macro_cloud_core(vec2 worldXY, float anim_time, float humidity, float fw, bool wantCirrus, out vec2 outGrad, out float outHighCirrus) {\n" \
	"    const vec2 prevDir = vec2(0.741, -0.671);\n" \
	"    const vec2 prevPerp = vec2(0.671, 0.741);\n" \
	"    float tFlow = mod(anim_time * 0.005, 1024.0);\n" \
	"    vec3 psi0 = chaff_grad_noise2d(worldXY * 0.00000042 - prevDir * (tFlow * 0.40), 0x4359434Cu);\n" \
	"    vec3 psi1 = chaff_grad_noise2d(worldXY * 0.00000180 + vec2(-psi0.z, psi0.y) * 0.30 - prevDir * tFlow, 0x5354524Du);\n" \
	"    vec2 synWarp = vec2(-psi0.z, psi0.y) * 260000.0 + vec2(-psi1.z, psi1.y) * 68000.0;\n" \
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
	"    /* Zero-mean ridge shaping (r - 0.68) preserves a balanced 0.50 global synoptic mean\n" \
	"     * so anticyclonic subsidence basins keep >= 55%..62% of the planet sunny & clear! */\n" \
	"    float front0 = oct0.x + (r0 - 0.68) * 0.28;\n" \
	"    float front1 = oct1.x + (r1 - 0.68) * 0.24;\n" \
	"    vec2 g0 = oct0.yz * (1.0 + 0.28 * dR0) * 0.72;\n" \
	"    vec2 g1 = (irot * oct1.yz) * (1.0 + 0.24 * dR1) * (0.54 * 1.65 * wOct1);\n" \
	"    vec2 macroGrad = g0 + g1;\n" \
	"    float synFront = 0.50 + (front0 - 0.50) * 0.72 + (front1 - 0.50) * (0.54 * wOct1) + (psi0.x - 0.50) * 0.34;\n" \
	"    float wReg2 = smoothstep(0.26, 0.07, 0.00001150 * fw);\n" \
	"    float wReg3 = smoothstep(0.24, 0.06, 0.00005500 * fw);\n" \
	"    /* Exact analytical upper bound of regional squalls (0.116*wReg2 + 0.070*wReg3) and cirrus (+0.164):\n" \
	"     * bails out immediately in anticyclonic clear-sky basins before evaluating 7 cirrus/squall/meso noises! */\n" \
	"    float maxFront = synFront + 0.116 * wReg2 + 0.070 * wReg3;\n" \
	"    if (maxFront <= (0.510 - 0.125 * humidity) && (!wantCirrus || (synFront + 0.164) <= (0.48 - 0.08 * humidity))) {\n" \
	"        outGrad = vec2(0.0);\n" \
	"        outHighCirrus = 0.0;\n" \
	"        return vec4(0.0, synFront, synWarp);\n" \
	"    }\n" \
	"    outHighCirrus = 0.0;\n" \
	"    if (wantCirrus) {\n" \
	"    /* High-troposphere jet-stream cirrus & frontal outflow shield (2-scale wave visible from orbit to regional flight) */\n" \
	"        vec2 pJet = irot * pZonal + vec2(38000.0, -52000.0);\n" \
	"        vec3 cir0 = chaff_grad_noise2d(pJet * 0.00000115 - prevDir * (phase1 * 1.35), 0x43495230u);\n" \
	"        float wCir = smoothstep(0.32, 0.08, 0.00000380 * fw);\n" \
	"        vec3 cirN = (wCir > 0.001) ? chaff_grad_noise2d((irot * pJet) * 0.00000380 - prevDir * (phase1 * 1.45), 0x43495252u) : vec3(0.5, 0.0, 0.0);\n" \
	"        float cirStreak0 = 1.0 - 2.0 * abs(cir0.x - 0.5);\n" \
	"        float cirStreak1 = 1.0 - 2.0 * abs(cirN.x - 0.5);\n" \
	"        float cirStreak  = mix(cirStreak0, 0.55 * cirStreak0 + 0.45 * cirStreak1, wCir);\n" \
	"        outHighCirrus = smoothstep(0.48 - 0.08 * humidity, 0.74 - 0.06 * humidity, synFront + (cirStreak - 0.52) * 0.34)\n" \
	"                      * smoothstep(0.34, 0.78, cirStreak);\n" \
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
	"    float synRegime = smoothstep(0.510 - 0.125 * humidity, 0.685 - 0.075 * humidity, synFront);\n" \
	"    float macroCoverage = synRegime;\n" \
	"    /* Multi-regime weather hierarchy: Continental Overcast Shields vs Frontal Broken Decks vs Fair Cumulus */\n" \
	"    float overcastShield = smoothstep(0.62, 0.86, synRegime);\n" \
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
	"        macroCoverage = mix(synRegime, structuredCov, wMeso0);\n" \
	"    } else {\n" \
	"        float orbitArm = smoothstep(0.32, 0.70, r0);\n" \
	"        macroCoverage = synRegime * mix(0.72 + 0.28 * orbitArm, 1.0, overcastShield);\n" \
	"    }\n" \
	"    outGrad = macroGrad;\n" \
	"    return vec4(macroCoverage, synFront, synWarp);\n" \
	"}\n" \
	"vec4 eval_synoptic_macro_cloud_grad(vec2 worldXY, float anim_time, float humidity, float fw, out vec2 outGrad, out float outHighCirrus) {\n" \
	"    return eval_synoptic_macro_cloud_core(worldXY, anim_time, humidity, fw, true, outGrad, outHighCirrus);\n" \
	"}\n" \
	"vec4 eval_synoptic_macro_cloud_fw(vec2 worldXY, float anim_time, float humidity, float fw) {\n" \
	"    vec2 dummyG; float dummyC;\n" \
	"    return eval_synoptic_macro_cloud_grad(worldXY, anim_time, humidity, fw, dummyG, dummyC);\n" \
	"}\n" \
	"vec4 eval_synoptic_macro_cloud(vec2 worldXY, float anim_time, float humidity) {\n" \
	"    return eval_synoptic_macro_cloud_fw(worldXY, anim_time, humidity, 12000.0);\n" \
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
#define MAX_WATER_QUEUE 192
#define MAX_BB_VERTS 65536
#define MAX_BB_OTHER_VERTS 16384

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
	"    return float(texelFetch(uHeightTex, clamp(p, ivec2(0), maxC), 0).r);\n"
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
	"    /* Watertight chunk seam: blend smoothly to bilinear on the outer 1 texel perimeter */\n"
	"    float borderDist = min(min(pixelCoord.x, maxCoordF.x - pixelCoord.x), min(pixelCoord.y, maxCoordF.y - pixelCoord.y));\n"
	"    float borderBlend = clamp(borderDist, 0.0, 1.0);\n"
	"    float finalH = hBicubic;\n"
	"    vec2 finalGrad = gradBicubic;\n"
	"    if (borderBlend < 1.0) {\n"
	"        float h00 = getH(iCoord, maxCoord);\n"
	"        float h10 = getH(iCoord + ivec2(1, 0), maxCoord);\n"
	"        float h01 = getH(iCoord + ivec2(0, 1), maxCoord);\n"
	"        float h11 = getH(iCoord + ivec2(1, 1), maxCoord);\n"
	"        float hLinear = mix(mix(h00, h10, f.x), mix(h01, h11, f.x), f.y);\n"
	"        vec2 gradLinear = vec2(mix(h10 - h00, h11 - h01, f.y), mix(h01 - h00, h11 - h10, f.x));\n"
	"        finalH = mix(hLinear, hBicubic, borderBlend);\n"
	"        finalGrad = mix(gradLinear, gradBicubic, borderBlend);\n"
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
	"    float edgeFade = clamp(min(min(a_grid_uv.x, 1.0 - a_grid_uv.x), min(a_grid_uv.y, 1.0 - a_grid_uv.y)) * 12.0, 0.0, 1.0);\n"
	"    if (uChunkSpan.x <= 160.5 && edgeFade > 0.001) {\n"
	"        float camHoriz = length(relXY);\n"
	"        float shoreClear = (uSeaLevel <= -900.0) ? 1.0 : smoothstep(uSeaLevel + 0.5, uSeaLevel + 3.0, wz);\n"
	"        float closeW = clamp((420.0 - camHoriz) / 260.0, 0.0, 1.0) * edgeFade * shoreClear;\n"
	"        if (closeW > 0.001) {\n"
	"            vec3 g1 = chaff_grad_noise2d(worldXY * 0.22, 0xA341316Cu);\n"
	"            vec3 g2 = chaff_grad_noise2d(worldXY * 0.68, 0xC8013EA4u);\n"
	"            float disp = ((g1.x - 0.5) * 1.15 + (g2.x - 0.5) * 0.45) * closeW;\n"
	"            wz += disp;\n"
	"            grad += (g1.yz * (0.22 * 1.15) + g2.yz * (0.68 * 0.45)) * (closeW / effectiveScale);\n"
	"        }\n"
	"    }\n"
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
	"    vec3 baseTorusPos = uPlanetCenter + vec3(Rv0 * dcu + (r0 * dcv) * cu, Rv0 * dsu + (r0 * dcv) * su, r0 * dsv);\n"
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
	"    vec3 worldPos = uPlanetCenter + vec3(Rv0 * dcu + dRv * cu, Rv0 * dsu + dRv * su, r0 * dsv + wz * sv);\n"
	"    vec3 torusN = vec3(cv * cu, cv * su, sv);\n"
	"    vec3 tangentU = vec3(-su, cu, 0.0);\n"
	"    vec3 tangentV = vec3(-sv * cu, -sv * su, cv);\n"
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
	"out vec4 FragColor;\n"
	"float getH(ivec2 p, ivec2 maxC) {\n"
	"    return float(texelFetch(uHeightTex, clamp(p, ivec2(0), maxC), 0).r);\n"
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
	"    /* Watertight chunk seam: blend smoothly to bilinear on the outer 1 texel perimeter */\n"
	"    float borderDist = min(min(pixelCoord.x, maxCoordF.x - pixelCoord.x), min(pixelCoord.y, maxCoordF.y - pixelCoord.y));\n"
	"    float borderBlend = clamp(borderDist, 0.0, 1.0);\n"
	"    float finalH = hBicubic;\n"
	"    vec2 finalGrad = gradBicubic;\n"
	"    if (borderBlend < 1.0) {\n"
	"        float h00 = getH(iCoord, maxCoord);\n"
	"        float h10 = getH(iCoord + ivec2(1, 0), maxCoord);\n"
	"        float h01 = getH(iCoord + ivec2(0, 1), maxCoord);\n"
	"        float h11 = getH(iCoord + ivec2(1, 1), maxCoord);\n"
	"        float hLinear = mix(mix(h00, h10, f.x), mix(h01, h11, f.x), f.y);\n"
	"        vec2 gradLinear = vec2(mix(h10 - h00, h11 - h01, f.y), mix(h01 - h00, h11 - h10, f.x));\n"
	"        finalH = mix(hLinear, hBicubic, borderBlend);\n"
	"        finalGrad = mix(gradLinear, gradBicubic, borderBlend);\n"
	"    }\n"
	"    out_grad.x = finalGrad.x * (maxCoordF.x / max(1.0, uChunkSpan.x));\n"
	"    out_grad.y = finalGrad.y * (maxCoordF.y / max(1.0, uChunkSpan.y));\n"
	"    return finalH;\n"
	"}\n"
	CHAFF_GLSL_HASH_FN
	CHAFF_GLSL_GRAD_NOISE_FN
	CHAFF_GLSL_FLOW_FN
	ND_GLSL_SYNOPTIC_CLOUD_FN
	"int chaff_octave_amplitude_ex(int shift, int min_shift, int base_amp) {\n"
	"    if (shift < min_shift || base_amp <= 0) return 0;\n"
	"    int k = shift - min_shift;\n"
	"    int base = base_amp << (k >> 1);\n"
	"    if ((k & 1) != 0) {\n"
	"        base += (base >> 1);\n"
	"    }\n"
	"    return base;\n"
	"}\n"
	"float chaff_wpoint_coarse_2d_f(int x, int y, int shift, uint seed) {\n"
	"    uint ushift = uint(shift);\n"
	"    uint cell_count_mask = (1u << (16u - ushift)) - 1u;\n"
	"    uint mask_frac = (1u << ushift) - 1u;\n"
	"    uint cshift = (shift >= 9) ? 0u : ushift;\n"
	"    uint ux = uint(x) & 0xFFFFu;\n"
	"    uint uy = uint(y) & 0xFFFFu;\n"
	"    uint kx0 = ux >> ushift;\n"
	"    uint ky0 = uy >> ushift;\n"
	"    uint kx1 = (kx0 + 1u) & cell_count_mask;\n"
	"    uint ky1 = (ky0 + 1u) & cell_count_mask;\n"
	"    int cx0 = int(kx0 << cshift);\n"
	"    int cy0 = int(ky0 << cshift);\n"
	"    int cx1 = int(kx1 << cshift);\n"
	"    int cy1 = int(ky1 << cshift);\n"
	"    float inv_step = 1.0 / float(1 << shift);\n"
	"    float fx = float(ux & mask_frac) * inv_step;\n"
	"    float fy = float(uy & mask_frac) * inv_step;\n"
	"    float u = fx * fx * fx * (fx * (fx * 6.0 - 15.0) + 10.0);\n"
	"    float v = fy * fy * fy * (fy * (fy * 6.0 - 15.0) + 10.0);\n"
	"    float v00 = float(chaff_hash2(ivec2(cx0, cy0), seed) >> 24u);\n"
	"    float v10 = float(chaff_hash2(ivec2(cx1, cy0), seed) >> 24u);\n"
	"    float v01 = float(chaff_hash2(ivec2(cx0, cy1), seed) >> 24u);\n"
	"    float v11 = float(chaff_hash2(ivec2(cx1, cy1), seed) >> 24u);\n"
	"    return mix(mix(v00, v10, u), mix(v01, v11, u), v);\n"
	"}\n"
	"vec3 chaff_wpoint_2d_grad_f(vec2 p, int shift, uint seed) {\n"
	"    float step_f = float(1 << shift);\n"
	"    vec2 q = p / step_f;\n"
	"    vec2 iq = floor(q);\n"
	"    vec2 f = q - iq;\n"
	"    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);\n"
	"    vec2 du = (30.0 * f * f * (f * (f - 2.0) + 1.0)) / step_f;\n"
	"    uint step_u = 1u << uint(shift);\n"
	"    uint sx0 = (uint(int(iq.x)) << uint(shift)) & 0xFFFFu;\n"
	"    uint sy0 = (uint(int(iq.y)) << uint(shift)) & 0xFFFFu;\n"
	"    uint sx1 = (sx0 + step_u) & 0xFFFFu;\n"
	"    uint sy1 = (sy0 + step_u) & 0xFFFFu;\n"
	"    float v00 = float(chaff_hash2(ivec2(int(sx0), int(sy0)), seed) >> 24u);\n"
	"    float v10 = float(chaff_hash2(ivec2(int(sx1), int(sy0)), seed) >> 24u);\n"
	"    float v01 = float(chaff_hash2(ivec2(int(sx0), int(sy1)), seed) >> 24u);\n"
	"    float v11 = float(chaff_hash2(ivec2(int(sx1), int(sy1)), seed) >> 24u);\n"
	"    float k0 = v10 - v00;\n"
	"    float k1 = v01 - v00;\n"
	"    float k2 = v11 - v01 - k0;\n"
	"    float val = v00 + k0 * u.x + k1 * u.y + k2 * u.x * u.y;\n"
	"    vec2 grad = vec2((k0 + k2 * u.y) * du.x, (k1 + k2 * u.x) * du.y);\n"
	"    return vec3(val, grad.x, grad.y);\n"
	"}\n"
	"/* Unified Scale-Space Filter Function:\n"
	" * Replaces discrete octave switch-cases and premature camera-distance cutoffs with a single\n"
	" * continuous function of world wavelength and pixel footprint fw. Every feature (lake, island,\n"
	" * peninsula, fjord, archipelago, mountain chain) remains at 100% strength down to OCTAVE_FULL_PX\n"
	" * pixels on screen, and smoothly fades to 0% at the Nyquist limit OCTAVE_MIN_PX. */\n"
	"const float OCTAVE_MIN_PX  = 1.35;\n"
	"const float OCTAVE_FULL_PX = 4.50;\n"
	"float nd_octave_weight(float wavelength, float fw) {\n"
	"    return smoothstep(OCTAVE_MIN_PX, OCTAVE_FULL_PX, wavelength / max(fw, 1e-4));\n"
	"}\n"
	"float nd_gpu_eval_coarse_unwrapped(uint seed, int rx, int ry, float fw) {\n"
	"    int cur_rx = rx;\n"
	"    int cur_ry = ry;\n"
	"    float coarse_q12_4 = 0.0;\n"
	"    for (int s = 15; s >= 2; s--) {\n"
	"        float cell_w = float(1 << s) * 144.0;\n"
	"        float w_oct = nd_octave_weight(cell_w, fw);\n"
	"        if (w_oct <= 0.001) break;\n"
	"        int amp = chaff_octave_amplitude_ex(s, 2, 1);\n"
	"        uint oct_seed = seed ^ (uint(s) * 0x85EBCA77u + 1u);\n"
	"        float raw = chaff_wpoint_coarse_2d_f(cur_rx, cur_ry, s, oct_seed) - 128.0;\n"
	"        coarse_q12_4 += (raw * float(amp)) * 0.125 * w_oct;\n"
	"        int nx = (cur_rx * 29304 - cur_ry * 14652) >> 15;\n"
	"        int ny = (cur_rx * 14652 + cur_ry * 29304) >> 15;\n"
	"        cur_rx = nx;\n"
	"        cur_ry = ny;\n"
	"    }\n"
	"    return coarse_q12_4;\n"
	"}\n"
	"float nd_gpu_bimodal_hypsometry(float elev) {\n"
	"    if (elev < 0.0) {\n"
	"        if (elev > -8.0) return elev;\n"
	"        float d = -elev - 8.0;\n"
	"        float deep = 8.0 + d * 1.8 + d * d * 0.45;\n"
	"        return -min(deep, 2000.0);\n"
	"    } else {\n"
	"        if (elev <= 8.0) return elev * 1.5;\n"
	"        if (elev <= 25.0) return 12.0 + (elev - 8.0) * 1.8;\n"
	"        float m = elev - 25.0;\n"
	"        float peak = 43.0 + m * 2.2 + m * m * 0.12;\n"
	"        return min(peak, 1854.0);\n"
	"    }\n"
	"}\n"
	"float nd_gpu_room_hyp_coarse(uint seed, int rx_unwrapped, int ry_unwrapped, float fw) {\n"
	"    int rx = ((rx_unwrapped + 32768) & 65535) - 32768;\n"
	"    int ry = ((ry_unwrapped + 32768) & 65535) - 32768;\n"
	"    uint ux = (uint(rx) - 32512u) & 0xFFFFu;\n"
	"    uint uy = (uint(ry) - 32512u) & 0xFFFFu;\n"
	"    bool wrap_x = (ux < 512u);\n"
	"    bool wrap_y = (uy < 512u);\n"
	"    float tx = wrap_x ? (float(ux) * (1.0 / 512.0)) : 0.0;\n"
	"    float ty = wrap_y ? (float(uy) * (1.0 / 512.0)) : 0.0;\n"
	"    int rx_base = wrap_x ? (32512 + int(ux)) : rx;\n"
	"    int ry_base = wrap_y ? (32512 + int(uy)) : ry;\n"
	"    int k_limit = wrap_y ? 4 : (wrap_x ? 2 : 1);\n"
	"    float c00 = 0.0, c10 = 0.0, c01 = 0.0, c11 = 0.0;\n"
	"    for (int k = 0; k < k_limit; k++) {\n"
	"        if ((k & 1) != 0 && !wrap_x) continue;\n"
	"        int cur_x = rx_base - (k & 1) * 65536;\n"
	"        int cur_y = ry_base - (k >> 1) * 65536;\n"
	"        float val = nd_gpu_eval_coarse_unwrapped(seed, cur_x, cur_y, fw);\n"
	"        if (k == 0) c00 = val;\n"
	"        else if (k == 1) c10 = val;\n"
	"        else if (k == 2) c01 = val;\n"
	"        else c11 = val;\n"
	"    }\n"
	"    float c_y0 = wrap_x ? mix(c00, c10, tx) : c00;\n"
	"    float c_y1 = wrap_x ? mix(c01, c11, tx) : c01;\n"
	"    float c = wrap_y ? mix(c_y0, c_y1, ty) : c_y0;\n"
	"    c -= float(uSeaOffsetQ12_4);\n"
	"    return nd_gpu_bimodal_hypsometry(c * 0.0625);\n"
	"}\n"
	"vec3 nd_gpu_eval_canonical_field(vec2 worldPos, float fw, out float out_fine_elev) {\n"
	"    uint seed = 0x48454947u ^ (uint(uWorldId) * 0x9E3779B1u);\n"
	"    float rx_f = (worldPos.x - 80.0) / 160.0;\n"
	"    float ry_f = (64.0 - worldPos.y) / 128.0;\n"
	"    int rx0 = int(floor(rx_f));\n"
	"    int ry0 = int(floor(ry_f));\n"
	"    vec2 f = vec2(rx_f - floor(rx_f), ry_f - floor(ry_f));\n"
	"    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);\n"
	"    vec2 du = 30.0 * f * f * (f * (f - 2.0) + 1.0);\n"
	"    int corner_limit = (fw >= 0.0) ? 4 : 1;\n"
	"    float h_c[4];\n"
	"    for (int c = 0; c < corner_limit; c++) {\n"
	"        h_c[c] = nd_gpu_room_hyp_coarse(seed, rx0 + (c & 1), ry0 + (c >> 1), fw);\n"
	"    }\n"
	"    float h00 = h_c[0];\n"
	"    float h10 = h_c[1];\n"
	"    float h01 = h_c[2];\n"
	"    float h11 = h_c[3];\n"
	"    float k0 = h10 - h00;\n"
	"    float k1 = h01 - h00;\n"
	"    float k2 = h11 - h01 - k0;\n"
	"    float coarseH = h00 + k0 * u.x + k1 * u.y + k2 * u.x * u.y;\n"
	"    vec2 coarseGrad = vec2((k0 + k2 * u.y) * du.x / 160.0, -(k1 + k2 * u.x) * du.y / 128.0);\n"
	"    /* Evaluate canonical rotated fBm fine octaves (s = 9..3) governed by the same nd_octave_weight law */\n"
	"    if (nd_octave_weight(512.0, fw) <= 0.001) {\n"
	"        out_fine_elev = 0.0;\n"
	"        return vec3(coarseH, coarseGrad.x, coarseGrad.y);\n"
	"    }\n"
	"    uint rug_seed = 0x52554747u ^ (uint(uWorldId) * 0x9E3779B1u);\n"
	"    const mat2 rotAtan = mat2(0.8942871, 0.44714355, -0.44714355, 0.8942871);\n"
	"    const mat2 irotAtan = mat2(0.8942871, -0.44714355, 0.44714355, 0.8942871);\n"
	"    float raw_macro = chaff_wpoint_2d_grad_f(worldPos, 10, rug_seed ^ 0xA531u).x;\n"
	"    vec2 p_rug_rot = rotAtan * worldPos;\n"
	"    float raw_meso = chaff_wpoint_2d_grad_f(p_rug_rot, 8, rug_seed ^ 0x5CA9u).x;\n"
	"    float raw_rug = (raw_macro * 5.0 + raw_meso * 3.0) * 0.125;\n"
	"    float rug_env = smoothstep(120.0, 168.0, raw_rug);\n"
	"    vec2 cur_p = worldPos;\n"
	"    mat2 cur_inv = mat2(1.0, 0.0, 0.0, 1.0);\n"
	"    float base_fine = 0.0;\n"
	"    vec2 base_grad = vec2(0.0);\n"
	"    float meso_ridge = 0.0;\n"
	"    vec2 meso_grad = vec2(0.0);\n"
	"    for (int s = 9; s >= 3; s--) {\n"
	"        float wNyq = nd_octave_weight(float(1 << s), fw);\n"
	"        if (wNyq <= 0.001) break;\n"
	"        int amp_b = chaff_octave_amplitude_ex(s, 3, 1);\n"
	"        uint b_seed = seed ^ (uint(s) * 0x85EBCA77u + 0xBEEFu);\n"
	"        vec3 gb = chaff_wpoint_2d_grad_f(cur_p, s, b_seed);\n"
	"        float vb = gb.x - 128.0;\n"
	"        vec2 db = cur_inv * gb.yz;\n"
	"        if (s >= 7) {\n"
	"            if (vb >= 0.0) db = -db;\n"
	"            vb = 64.0 - abs(vb);\n"
	"        }\n"
	"        float scale_b = (float(amp_b) / 128.0) * wNyq;\n"
	"        base_fine += vb * scale_b;\n"
	"        base_grad += db * scale_b;\n"
	"        if (s >= 5 && rug_env > 0.001) {\n"
	"            int amp_m = chaff_octave_amplitude_ex(s, 5, 10);\n"
	"            uint m_seed = (seed ^ 0x7F4A7C15u) ^ (uint(s) * 0x85EBCA77u + 0xCAFEu);\n"
	"            vec3 gm = chaff_wpoint_2d_grad_f(cur_p, s, m_seed);\n"
	"            float vm = gm.x - 128.0;\n"
	"            vec2 dm = cur_inv * gm.yz;\n"
	"            if (s >= 6) {\n"
	"                if (vm >= 0.0) dm = -dm;\n"
	"                vm = 64.0 - abs(vm);\n"
	"            }\n"
	"            float scale_m = (float(amp_m) / 128.0) * wNyq;\n"
	"            meso_ridge += vm * scale_m;\n"
	"            meso_grad += dm * scale_m;\n"
	"        }\n"
	"        cur_p = rotAtan * cur_p;\n"
	"        cur_inv = cur_inv * irotAtan;\n"
	"    }\n"
	"    float b_scale = 0.5 + 1.5 * rug_env;\n"
	"    float fineH = base_fine * b_scale + meso_ridge * rug_env;\n"
	"    vec2 fineGrad = base_grad * b_scale + meso_grad * rug_env;\n"
	"    /* Coastal/bathymetric ramp relative to uSeaLevel */\n"
	"    float rel_water = coarseH - uSeaLevel;\n"
	"    float coast_w = 1.0;\n"
	"    if (rel_water <= -8.0) {\n"
	"        coast_w = 0.5;\n"
	"    } else if (rel_water <= 0.0) {\n"
	"        coast_w = 0.25 + (-rel_water / 8.0) * 0.25;\n"
	"    } else if (rel_water < 6.0) {\n"
	"        coast_w = 0.25 + (rel_water / 6.0) * 0.75;\n"
	"    }\n"
	"    fineH = clamp(fineH * coast_w, -127.0, 127.0);\n"
	"    fineGrad *= coast_w;\n"
	"    out_fine_elev = fineH;\n"
	"    return vec3(coarseH + fineH, coarseGrad.x + fineGrad.x, coarseGrad.y + fineGrad.y);\n"
	"}\n"
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
	"    if (env <= 0.001) return vec3(0.0);\n"
	"    float wNyq1 = nd_octave_weight(1.0 / 0.18, fw);\n"
	"    float wNyq2 = nd_octave_weight(1.0 / 0.52, fw);\n"
	"    vec3 g1 = (wNyq1 > 0.001) ? chaff_grad_noise2d(worldPos * 0.18, 0xA341316Cu) : vec3(0.5, 0.0, 0.0);\n"
	"    vec3 g2 = (wNyq2 > 0.001) ? chaff_grad_noise2d(worldPos * 0.52 + vec2(17.3, -29.1), 0xC8013EA4u) : vec3(0.5, 0.0, 0.0);\n"
	"    float disp = ((g1.x - 0.5) * 0.38 * wNyq1 + (g2.x - 0.5) * 0.16 * wNyq2) * env;\n"
	"    vec2 gTotal = (g1.yz * (0.18 * 0.38 * wNyq1) + g2.yz * (0.52 * 0.16 * wNyq2)) * env;\n"
	"    return vec3(disp, gTotal.x, gTotal.y);\n"
	"}\n"
	"vec4 eval_zoom_terrain_detail(vec2 pos, float dist, float fw, out vec4 out_octaves) {\n"
	"    const float freqs[4] = float[4](0.16, 0.68, 1.85, 6.20);\n"
	"    const float amps[4]  = float[4](0.28, 0.25, 0.22, 0.16);\n"
	"    const float dFar[4]  = float[4](9500.0, 4200.0, 1850.0, 720.0);\n"
	"    const float dNear[4] = float[4](2400.0, 1050.0,  420.0, 165.0);\n"
	"    const uint  seeds[4] = uint[4](0xA341316Cu, 0xC8013EA4u, 0x3891053Bu, 0x85EBCA6Bu);\n"
	"    const mat2  rot  = mat2(0.8, -0.6, 0.6, 0.8);\n"
	"    const mat2  irot = mat2(0.8,  0.6, -0.6, 0.8);\n"
	"    float h = 0.0;\n"
	"    vec2 grad = vec2(0.0);\n"
	"    float cavity = 0.0;\n"
	"    vec2 pRot = pos;\n"
	"    mat2 curInvRot = mat2(1.0, 0.0, 0.0, 1.0);\n"
	"    float oct[4] = float[4](0.5, 0.5, 0.5, 0.5);\n"
	"    /* 2 octaves (6 and 1.5 unit wavelengths); a 3rd (0.54 unit) only inside the 420 unit close-up band */\n"
	"    int maxDetailOct = (dist < 420.0) ? 3 : 2;\n"
	"    for (int i = 0; i < 4; i++) {\n"
	"        if (i >= maxDetailOct) break;\n"
	"        float wDist = clamp((dFar[i] - dist) / max(1.0, dFar[i] - dNear[i]), 0.0, 1.0);\n"
	"        float wNyq  = nd_octave_weight(1.0 / freqs[i], fw);\n"
	"        float w = wDist * wNyq;\n"
	"        if (w <= 0.001) break;\n"
	"        vec3 g = chaff_grad_noise2d(pRot * freqs[i], seeds[i]);\n"
	"        oct[i] = mix(0.5, g.x, w);\n"
	"        float signedVal = g.x - 0.5;\n"
	"        vec2 worldG = curInvRot * g.yz;\n"
	"        h += signedVal * amps[i] * w;\n"
	"        grad += worldG * (freqs[i] * amps[i] * w);\n"
	"        if (i >= 1) {\n"
	"            cavity += max(0.0, 0.48 - g.x) * (amps[i] * 2.4 * w);\n"
	"        }\n"
	"        pRot += g.yz * (0.08 * w);\n"
	"        pRot = rot * pRot;\n"
	"        curInvRot = curInvRot * irot;\n"
	"    }\n"
	"    out_octaves = vec4(oct[0], oct[1], oct[2], oct[3]);\n"
	"    return vec4(h, grad.x, grad.y, clamp(cavity, 0.0, 0.45));\n"
	"}\n"
	"/* High-Definition Macro & Orbital Procedural Noise Image (atmosphere -> low orbit -> full planet):\n"
	" * Governed by the same continuous nd_octave_weight(wavelength, fw) scale law across 6 octaves\n"
	" * spanning planetary cordilleras (200,000 units) down to local valleys (28 units), so full-planet\n"
	" * continents retain crisp tectonic mountain chains, jagged coastlines, and natural relief at all zooms. */\n"
	"vec4 eval_macro_planetary_detail(vec2 pos, float dist, float fw, out vec4 out_macro_oct) {\n"
	"    float highAltWeight = smoothstep(1400.0, 4200.0, dist);\n"
	"    if (highAltWeight <= 0.001) {\n"
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
	"        float wNyq = nd_octave_weight(1.0 / freqs[i], fw) * highAltWeight;\n"
	"        if (wNyq <= 0.001) break;\n"
	"        vec3 g = chaff_grad_noise2d(pRot * freqs[i], seeds[i]);\n"
	"        if (i <= 2) oct.x = mix(oct.x, g.x, 0.5 * wNyq);\n"
	"        else if (i == 3) oct.y = mix(0.5, g.x, wNyq);\n"
	"        else if (i == 4) oct.z = mix(0.5, g.x, wNyq);\n"
	"        else oct.w = mix(0.5, g.x, wNyq);\n"
	"        float signedVal = g.x - 0.5;\n"
	"        float ridge = 0.5 - abs(signedVal);\n"
	"        float shaped = mix(signedVal, ridge - 0.16, 0.48);\n"
	"        vec2 worldG = curInvRot * g.yz;\n"
	"        h += shaped * amps[i] * wNyq;\n"
	"        grad += worldG * (freqs[i] * amps[i] * wNyq);\n"
	"        ridgeAcc += max(0.0, ridge - 0.16) * (0.55 * wNyq);\n"
	"        pRot += g.yz * (0.14 * wNyq);\n"
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
	"    float rippleEnv = (0.55 + 0.65 * wind.w) * distVis;\n"
	"    float f1 = 1.35;\n"
	"    float f2 = 3.40;\n"
	"    float nyq1 = smoothstep(0.60, 0.15, f1 * fw);\n"
	"    float nyq2 = smoothstep(0.60, 0.15, f2 * fw);\n"
	"    float t1 = mod(anim_time * 1.15, 512.0);\n"
	"    float t2 = mod(anim_time * 1.65, 512.0);\n"
	"    vec2 gustShift = wDir * (wind.z * 0.55) + wPerp * (wind.w * 0.35);\n"
	"    vec2 wCoord1 = vec2(dot(pos, prevW) * 1.45, dot(pos, prevP) * 0.78) * f1 + gustShift - vec2(t1, 0.18 * t1);\n"
	"    vec2 wCoord2 = vec2(dot(pos, prevW) * 1.35 + dot(pos, prevP) * 0.35, dot(pos, prevP) * 0.85 - dot(pos, prevW) * 0.25) * f2 + gustShift * 1.4 - vec2(t2, -0.22 * t2);\n"
	"    vec3 c1 = (nyq1 > 0.001) ? chaff_grad_noise2d(wCoord1, 0xCA9111A1u) : vec3(0.5, 0.0, 0.0);\n"
	"    vec3 c2 = (nyq2 > 0.001) ? chaff_grad_noise2d(wCoord2 + c1.yz * 0.25, 0xCA9111A2u) : vec3(0.5, 0.0, 0.0);\n"
	"    vec2 localG = c1.yz * (0.14 * nyq1) + c2.yz * (0.11 * nyq2);\n"
	"    vec2 worldCapGrad = (wDir * localG.x * 1.40 + wPerp * localG.y * 0.80) * rippleEnv;\n"
	"    return worldCapGrad;\n"
	"}\n"
	"/* Laplace / Equilibrium Astronomical Ocean Tide on the Torus:\n"
	" * Evaluates the second-order Legendre tidal potential P_2(cos gamma) = 0.5 * (3 * (N . D)^2 - 1)\n"
	" * from the Moon (uMoonDir) and Sun (uSunDir) with the physical 1 : 0.46 Earth-Moon-Sun amplitude ratio.\n"
	" * Gated to 0 on perched mountain lakes (uSeaLevel > uBaseSeaLevel + 0.5). */\n"
	"float eval_astronomical_tide(vec3 torusN, float openSeaFactor) {\n"
	"    if (uSeaLevel <= -900.0) return 0.0;\n"
	"    float isOcean = smoothstep(0.20, 0.65, openSeaFactor) * (1.0 - smoothstep(0.25, 0.75, abs(uSeaLevel - uBaseSeaLevel)));\n"
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
	"vec4 eval_shore_swash(vec2 pos, float baseTideZ, float terrainZ, vec2 slopeGrad, float anim_time, float dist, float fw, float openSeaFactor) {\n"
	"    float isOcean = smoothstep(0.20, 0.65, openSeaFactor) * (1.0 - smoothstep(0.25, 0.75, abs(uSeaLevel - uBaseSeaLevel)));\n"
	"    float slopeMag = length(slopeGrad);\n"
	"    float relZ = terrainZ - baseTideZ;\n"
	"    float nearShore = smoothstep(2.4, 0.0, max(0.0, relZ)) * smoothstep(4.5, 0.0, max(0.0, -relZ));\n"
	"    if (nearShore <= 0.001 || dist > 3800.0) {\n"
	"        float tideHigh = uSeaLevel + (0.15 + 1.05 * isOcean);\n"
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
	"    float highWaterZ = mix(max(baseTideZ, uSeaLevel + 0.15 + 1.05 * isOcean), max(swashSurfZ, recentSwashTop), nearShore);\n"
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
	"    float spanBlend = smoothstep(3.3, 13.5, texelSpan);\n"
	"    float distBlend = smoothstep(1400.0, 4800.0, vDistance);\n"
	"    float gpuFieldBlend = clamp(max(spanBlend, distBlend), 0.0, 1.0);\n"
	"    float texZ = fragZ;\n"
	"    float gpuFineElev = 0.0;\n"
	"    if (gpuFieldBlend > 0.001) {\n"
	"        vec3 gpuField = nd_gpu_eval_canonical_field(vWorldPos, fw, gpuFineElev);\n"
	"        fragZ = mix(fragZ, gpuField.x, gpuFieldBlend);\n"
	"        /* Never let Nyquist octave band-limiting in gpuField submerge dry land or flatten mountain peaks */\n"
	"        if (texZ > uSeaLevel) fragZ = max(fragZ, texZ);\n"
	"        worldSlopeGrad = mix(worldSlopeGrad, gpuField.yz, gpuFieldBlend);\n"
	"    }\n"
	"    vec3 torusN = normalize(vTorusNormal);\n"
	"    vec3 tangentU = normalize(vTangentU);\n"
	"    vec3 tangentV = normalize(vTangentV);\n"
	"    vec3 fragNormal = normalize(torusN - worldSlopeGrad.x * tangentU - worldSlopeGrad.y * tangentV);\n"
	"    vec4 macroOct = vec4(0.5);\n"
	"    vec4 macroDetail = eval_macro_planetary_detail(vWorldPos, vDistance, fw, macroOct);\n"
	"    vec4 wind = eval_recursive_wind(vWorldPos, uAnimTime, fw, vDistance);\n"
	"    float preDepth = (uSeaLevel > -900.0) ? (uSeaLevel - fragZ) : -999.0;\n"
	"    vec2 basin = (uSeaLevel > -900.0) ? eval_water_fetch_and_basin(vUV, wind.xy, worldSlopeGrad, max(0.0, preDepth), effectiveScale) : vec2(0.0);\n"
	"    float openSeaFactor = basin.x;\n"
	"    float windFetch = basin.y;\n"
	"    /* Astronomical ocean tide (open ocean only; zero on lakes) + shoreline wave swash excursion */\n"
	"    float astroTide = eval_astronomical_tide(torusN, openSeaFactor);\n"
	"    float baseTideSeaLevel = (uSeaLevel > -900.0) ? (uSeaLevel + astroTide) : -999.0;\n"
	"    /* Refine coarse macro heightmap & normals with high-definition procedural noise image at altitude */\n"
	"    float rawMacroDepth = (uSeaLevel > -900.0) ? (baseTideSeaLevel - fragZ) : -999.0;\n"
	"    float coastDetailEnv = (uSeaLevel > -900.0) ? (0.35 + 0.65 * smoothstep(0.0, 12.0, abs(rawMacroDepth))) : 1.0;\n"
	"    fragZ += macroDetail.x * coastDetailEnv * (1.0 - 0.65 * gpuFieldBlend);\n"
	"    fragNormal = normalize(fragNormal - macroDetail.y * 0.65 * tangentU - macroDetail.z * 0.65 * tangentV);\n"
	"    float rawDepth = (uSeaLevel > -900.0) ? (baseTideSeaLevel - fragZ) : -999.0;\n"
	"    vec3 shoreRelief = (uSeaLevel > -900.0) ? eval_shore_micro_relief(vWorldPos, rawDepth, fw) : vec3(0.0);\n"
	"    fragZ += shoreRelief.x;\n"
	"    worldSlopeGrad += shoreRelief.yz;\n"
	"    fragNormal = normalize(fragNormal - shoreRelief.y * tangentU - shoreRelief.z * tangentV);\n"
	"    vec4 swashState = (uSeaLevel > -900.0) ? eval_shore_swash(vWorldPos, baseTideSeaLevel, fragZ, worldSlopeGrad, uAnimTime, vDistance, fw, openSeaFactor) : vec4(-999.0, -999.0, 0.0, 0.0);\n"
	"    vec4 lambWake = (uSeaLevel > -900.0) ? eval_lamb_wake_ribbon(vWorldPos, uAnimTime, vDistance, fw) : vec4(0.0);\n"
	"    float liveSeaLevel = swashState.x + lambWake.x;\n"
	"    float highWaterZ   = swashState.y;\n"
	"    float fragWaterDepth = (uSeaLevel > -900.0) ? (liveSeaLevel - fragZ) : -999.0;\n"
	"    vec4 octaves = vec4(0.5);\n"
	"    vec4 detail = (fragWaterDepth <= 18.0) ? eval_zoom_terrain_detail(vWorldPos, vDistance, fw, octaves) : vec4(0.0);\n"
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
	"    /* Continuous biome water/seabed weights derived from uSubstrateTex (populated via ndw_biome_base_rgb) */\n"
	"    float maxBio = max(rawBiome.r, max(rawBiome.g, rawBiome.b));\n"
	"    float minBio = min(rawBiome.r, min(rawBiome.g, rawBiome.b));\n"
	"    float wVolcanic = smoothstep(0.32, 0.14, maxBio);\n"
	"    float wPolar = smoothstep(0.52, 0.72, minBio) * smoothstep(0.18, 0.04, max(rawBiome.r, rawBiome.g) - rawBiome.b);\n"
	"    float wDesert = smoothstep(0.65, 0.85, rawBiome.r) * smoothstep(0.50, 0.70, rawBiome.g) * smoothstep(0.65, 0.45, rawBiome.b) * (1.0 - wPolar);\n"
	"    float wTropical = smoothstep(0.40, 0.65, rawBiome.g) * smoothstep(0.04, 0.35, rawBiome.g - rawBiome.r) * (1.0 - wPolar) * (1.0 - wDesert);\n"
	"    float wTemperate = clamp(1.0 - (wVolcanic + wPolar + wTropical + wDesert), 0.0, 1.0);\n"
	"    float texBioHumidity = clamp(0.52 * wTemperate + 0.80 * wTropical + 0.45 * wPolar + 0.15 * wVolcanic + 0.10 * wDesert + 0.25 * smoothstep(-4.0, 8.0, fragWaterDepth), 0.08, 0.95);\n"
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
	"    float bioHumidity = worldBioHumidity;\n"
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
	"    float innerEclipse = (sunDot > 0.0) ? (1.0 - 0.45 * innerHole * (1.0 - abs(sDir.z))) : 1.0;\n"
	"    float effSun = sunDot * innerEclipse;\n"
	"    float dayFactor = clamp(0.12 + 0.88 * smoothstep(-0.16, 0.22, effSun), 0.12, 1.0);\n"
	"    float directSun = smoothstep(-0.06, 0.18, effSun);\n"
	"    float baseDiff = max(0.0, dot(fragNormal, L_frag)) * sunTrans * directSun;\n"
	"    float effLighting = (0.45 * nd_amb_trans(cloudShadow) * (0.20 + 0.80 * dayFactor)) + 0.55 * baseDiff;\n"
	"    vec3 dryCol = baseCol.rgb;\n"
	"    float dryLighting = effLighting;\n"
	"    float slope = 1.0 - max(0.0, dot(fragNormal, torusN));\n"
	"    float elevAboveWater = (uSeaLevel > -900.0) ? max(0.0, fragZ - liveSeaLevel) : max(0.0, fragZ);\n"
	"    float elevAboveHighTide = (uSeaLevel > -900.0) ? max(0.0, fragZ - highWaterZ) : max(0.0, fragZ);\n"
	"    float h0 = detail.x;\n"
	"    vec2 microGrad = detail.yz;\n"
	"    float microCavity = detail.w;\n"
	"    float nMeso  = octaves.x;\n"
	"    float nStd   = octaves.y;\n"
	"    float nClose = octaves.z;\n"
	"    float nUltra = octaves.w;\n"
	"    float macroFade = clamp((44000.0 - vDistance) / 38000.0, 0.0, 1.0);\n"
	"    float distFade  = clamp(1.0 - (vDistance - 1800.0) / 26200.0, 0.0, 1.0);\n"
	"    float closeZoom = clamp((820.0 - vDistance) / 480.0, 0.0, 1.0);\n"
	"    float ultraZoom = clamp((480.0 - vDistance) / 260.0, 0.0, 1.0);\n"
	"    /* 1. Terrestrial & shallow-submerged substrate shading (evaluated down to fragWaterDepth < 6.5 so shallow water preserves exact shore detail) */\n"
	"    if (fragWaterDepth < 6.5) {\n"
	"        float rockFactor  = smoothstep(0.065, 0.22, slope);\n"
	"        float talusFactor = smoothstep(0.032, 0.10, slope) * (1.0 - smoothstep(0.14, 0.25, slope));\n"
	"        float rockCrevice = 0.0;\n"
	"        if (rockFactor > 0.0 || talusFactor > 0.0) {\n"
	"            float warpedZ = fragZ + h0 * 2.8 + (nMeso - 0.5) * 3.4;\n"
	"            float strata = sin(warpedZ * 1.85) * 0.13 + (nStd - 0.5) * 0.18;\n"
	"            vec3 fracG = (distFade > 0.01) ? chaff_grad_noise2d(vec2(vWorldPos.x * 1.65 + fragZ * 0.75, vWorldPos.y * 1.65 - fragZ * 0.55) + microGrad * 0.12, 0x524F434Bu) : vec3(0.5, 0.0, 0.0);\n"
	"            rockCrevice = smoothstep(0.13, 0.015, abs(fracG.x - 0.5)) * distFade;\n"
	"            float fineGrain = (nClose - 0.5) * 0.18 * closeZoom + (nUltra - 0.5) * 0.12 * ultraZoom;\n"
	"            vec3 slateWarm = vec3(0.41, 0.38, 0.35);\n"
	"            vec3 graniteCool = vec3(0.33, 0.35, 0.37);\n"
	"            vec3 rockCol = mix(slateWarm, graniteCool, clamp(fracG.x, 0.0, 1.0)) * (0.88 + strata + fineGrain);\n"
	"            rockCol *= (1.0 - rockCrevice * 0.46);\n"
	"            float mossLedge = smoothstep(0.20, 0.09, slope) * smoothstep(0.45, 0.75, nMeso * 0.6 + nStd * 0.4) * distFade;\n"
	"            rockCol = mix(rockCol, vec3(0.22, 0.34, 0.16), mossLedge * 0.55);\n"
	"            if (talusFactor > 0.0) {\n"
	"                float scree = nStd * 0.5 + nClose * 0.35 + nUltra * 0.15;\n"
	"                vec3 screeCol = vec3(0.36, 0.33, 0.29) * (0.78 + 0.42 * scree);\n"
	"                dryCol = mix(dryCol, screeCol, talusFactor * 0.65 * distFade);\n"
	"            }\n"
	"            dryCol = mix(dryCol, rockCol, rockFactor);\n"
	"            microGrad += fracG.yz * (rockFactor * 0.75 * distFade);\n"
	"        }\n"
	"        if (macroFade > 0.0) {\n"
	"            float soilFactor = smoothstep(0.018, 0.085, slope) * (1.0 - rockFactor);\n"
	"            if (soilFactor > 0.0 && distFade > 0.0) {\n"
	"                float soilMoisture = clamp(1.0 - elevAboveWater / 12.0, 0.0, 1.0);\n"
	"                float pebble = smoothstep(0.68, 0.86, nClose * 0.45 + nUltra * 0.55) * ultraZoom;\n"
	"                vec3 loamCol = vec3(0.39, 0.29, 0.19) * (0.80 + 0.32 * nStd + 0.20 * (nClose - 0.5) * closeZoom);\n"
	"                loamCol = mix(loamCol, vec3(0.46, 0.43, 0.39), pebble * 0.50);\n"
	"                loamCol *= (1.0 - soilMoisture * 0.35);\n"
	"                dryCol = mix(dryCol, loamCol, soilFactor * distFade);\n"
	"            }\n"
	"            float sandAridity = clamp((dryCol.r - dryCol.b * 1.10) * 2.8, 0.0, 1.0) * smoothstep(0.42, 0.62, dryCol.r);\n"
	"            float shoreFactor = smoothstep(6.0, 0.4, min(elevAboveWater, elevAboveHighTide)) * (1.0 - rockFactor);\n"
	"            float sandFactor = clamp(max(sandAridity, shoreFactor), 0.0, 1.0);\n"
	"            float sandSparkle = 0.0;\n"
	"            float wetSand = 0.0;\n"
	"            if (sandFactor > 0.001 && distFade > 0.0) {\n"
	"                float duneWave = nMeso * 0.45 + nStd * 0.55;\n"
	"                float ripplePhase = dot(vWorldPos.xy, vec2(0.671, 0.741)) * 0.75;\n"
	"                float sandRipple = sin(ripplePhase) * (0.60 * nClose + 0.40 * nUltra) * 0.16 * sandFactor;\n"
	"                /* Intertidal wet-sand hysteresis: sand between liveSeaLevel and highWaterZ retains moisture & specular sheen */\n"
	"                wetSand = max(smoothstep(2.6, 0.0, elevAboveWater), smoothstep(1.8, -0.2, elevAboveHighTide) * 0.85);\n"
	"                vec3 drySandCol = vec3(0.75, 0.67, 0.49) * (0.88 + 0.22 * duneWave + sandRipple + 0.16 * (nClose - 0.5) * closeZoom + 0.10 * (nUltra - 0.5) * ultraZoom);\n"
	"                vec3 wetSandCol = vec3(0.54, 0.46, 0.34) * (0.90 + 0.18 * nStd + 0.12 * (nClose - 0.5) * closeZoom);\n"
	"                vec3 sandCol = mix(drySandCol, wetSandCol, wetSand);\n"
	"                dryCol = mix(dryCol, sandCol, sandFactor * distFade);\n"
	"                sandSparkle = smoothstep(0.68, 0.88, nClose * 0.45 + nUltra * 0.55) * closeZoom;\n"
	"            }\n"
	"            float vegFactor = clamp(1.0 - sandFactor * 1.35 - rockFactor * 1.4, 0.0, 1.0) * smoothstep(-0.15, 0.10, dryCol.g - dryCol.r * 0.88);\n"
	"            if (vegFactor > 0.001 && fragWaterDepth < 0.35) {\n"
	"                vec3 deepMoss = dryCol * vec3(0.78, 0.96, 0.76);\n"
	"                vec3 lushTurf = dryCol * vec3(0.92, 1.09, 0.88);\n"
	"                vec3 warmClover = dryCol * vec3(1.12, 1.04, 0.74);\n"
	"                vec3 vegCol = mix(deepMoss, lushTurf, smoothstep(0.25, 0.72, nMeso));\n"
	"                vegCol = mix(vegCol, warmClover, smoothstep(0.38, 0.76, nStd) * 0.70);\n"
	"                float bladeRelief = (nStd - 0.5) * 0.16 * distFade + (nClose - 0.5) * 0.20 * closeZoom + (nUltra - 0.5) * 0.14 * ultraZoom;\n"
	"                vec4 meadowWind = eval_recursive_wind(vWorldPos, uAnimTime, fw, vDistance);\n"
	"                float meadowGustWave = clamp((meadowWind.z - 0.55) * 0.55 + meadowWind.w * 0.45, 0.0, 1.0);\n"
	"                vec3 windSheenCol = vegCol * vec3(1.16, 1.14, 0.86);\n"
	"                vegCol = mix(vegCol, windSheenCol, meadowGustWave * 0.28 * distFade);\n"
	"                vegCol *= clamp(1.0 + bladeRelief, 0.76, 1.26);\n"
	"                dryCol = mix(dryCol, vegCol, vegFactor * macroFade * smoothstep(0.35, -0.10, fragWaterDepth));\n"
	"            }\n"
	"            float normStrength = 0.24 + 0.22 * distFade + 0.16 * closeZoom;\n"
	"            vec3 N_frag = normalize(fragNormal - microGrad.x * normStrength * tangentU - microGrad.y * normStrength * tangentV);\n"
	"            vec3 viewDir = normalize(vViewDir);\n"
	"            vec3 H_half = normalize(L_frag + viewDir);\n"
	"            float perFragDiff = max(0.0, dot(N_frag, L_frag));\n"
	"            float cavityAO = clamp(1.0 - slope * 0.26 + h0 * 0.26 - microCavity * 0.42 - rockCrevice * 0.34, 0.54, 1.05);\n"
	"            float fragLit = ((0.40 * nd_amb_trans(cloudShadow)) + 0.60 * perFragDiff * sunTrans) * cavityAO;\n"
	"            dryLighting = mix(dryLighting, fragLit, macroFade);\n"
	"            float microShade = clamp(1.0 + h0 * (0.30 + 0.16 * closeZoom) * macroFade, 0.72, 1.28);\n"
	"            dryCol *= microShade;\n"
	"            /* Capillary wet-shore darkening and sub-aerial wet specular sheen across intertidal & swash memory zone */\n"
	"            float shoreWetMoisture = max(smoothstep(-0.38, 0.15, fragWaterDepth), swashState.w * 0.88);\n"
	"            dryCol *= (1.0 - shoreWetMoisture * 0.22);\n"
	"            float subAerialMask = smoothstep(0.25, -0.02, fragWaterDepth);\n"
	"            float wetSpecAngle = max(0.0, dot(N_frag, H_half));\n"
	"            float wetFilmSpec = pow(wetSpecAngle, 36.0) * (0.32 * wetSand + 0.34 * shoreWetMoisture) * subAerialMask;\n"
	"            float quartzGlint = pow(wetSpecAngle, 64.0) * sandSparkle * 0.42 * subAerialMask;\n"
	"            dryCol += vec3((wetFilmSpec + quartzGlint) * (0.35 + 0.65 * sandFactor) * distFade);\n"
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
	"    float peakElevAboveWater = (uSeaLevel > -900.0) ? max(elevAboveWater, texZ - liveSeaLevel) : max(elevAboveWater, texZ);\n"
	"    float effElev = peakElevAboveWater + max(0.0, macroDetail.x * 3.0);\n"
	"    float snowBaseZ = 280.0 - 220.0 * wPolar + 140.0 * wTropical + 120.0 * wDesert;\n"
	"    float snowShed = smoothstep(0.52, 0.15, slope);\n"
	"    float alpineSnow = smoothstep(snowBaseZ, snowBaseZ + 180.0, effElev) * snowShed;\n"
	"    float alpineRock = smoothstep(snowBaseZ - 120.0, snowBaseZ + 40.0, effElev) * (1.0 - wPolar);\n"
	"    if (alpineRock > 0.001 || wPolar > 0.01) {\n"
	"        vec3 graniteRock = vec3(0.62, 0.60, 0.57);\n"
	"        dryCol = mix(dryCol, graniteRock, alpineRock * 0.55);\n"
	"        float snowMask = max(alpineSnow, wPolar * smoothstep(-1.0, 2.5, peakElevAboveWater));\n"
	"        if (snowMask > 0.001) {\n"
	"            float snowNoise = (nStd - 0.5) * 0.10 * distFade + (nClose - 0.5) * 0.08 * closeZoom;\n"
	"            vec3 snowCol = vec3(0.97, 0.99, 1.00) * (0.98 + snowNoise);\n"
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
	"        float alpineSnowline = 280.0 - 220.0 * wPolar + 140.0 * wTropical + 120.0 * wDesert;\n"
	"        float mountainSnow = smoothstep(alpineSnowline, alpineSnowline + 180.0, effElev) * smoothstep(0.55, 0.15, slope);\n"
	"        float spaceSnow = max(polarIce, mountainSnow);\n"
	"        vec3 snowOrbitCol = vec3(0.97, 0.99, 1.00);\n"
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
	"    vec3 lit = mix(dryCol * dryLighting, subCol * subLighting, submergedBlend);\n"
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
	"        float opticalDepth = d / cosView;\n"
	"        /* Non-linear shallow optical path: crystal-clear wading shallows (d < 4), smooth shelf ramp (4..16) */\n"
	"        float shallowFactor = smoothstep(1.5, 18.0, d);\n"
	"        float effectiveOpticalDepth = opticalDepth * (0.04 + 0.96 * shallowFactor);\n"
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
	"        /* Gentle Beer-Lambert spectral absorption: red attenuates gradually while shallows remain clear */\n"
	"        vec3 seabedAbsorption = exp(-vec3(0.058, 0.022, 0.007) * effectiveOpticalDepth);\n"
	"        vec3 transmittedBed = lit * seabedAbsorption;\n"
	"        /* Depth-staged in-scattered water body color across shallow shelf (2.5..16), outer shelf (16..180), and abyssal trench (180..950) */\n"
	"        /* Shallow ramps as in the crystal-clear shelf look (2.5..9.5..20..42); trenches keep extra gradation below 42 */\n"
	"        float tShelf = smoothstep(2.5, 9.5, d);\n"
	"        float tOuter = smoothstep(9.5, 20.0, d);\n"
	"        float tAbyss = smoothstep(20.0, 42.0, d);\n"
	"        float tTrench = smoothstep(180.0, 950.0, d);\n"
	"        vec3 inscatterCol = mix(C_SHALLOW, C_SHELF, tShelf);\n"
	"        inscatterCol = mix(inscatterCol, C_OUTER, tOuter);\n"
	"        inscatterCol = mix(inscatterCol, C_ABYSS, tAbyss);\n"
	"        inscatterCol = mix(inscatterCol, C_ABYSS * 0.55, tTrench);\n"
	"        float bodyScatterWeight = shallowFactor * (1.0 - exp(-0.052 * effectiveOpticalDepth));\n"
	"        vec3 waterBody = mix(transmittedBed, inscatterCol, bodyScatterWeight);\n"
	"        /* Single-pass unified air-water surface reflection, 3-tier sun glitter, Fresnel & surf foam\n"
	"         * (eliminates the duplicate overlapping water mesh and all water/earth Z-fighting flicker) */\n"
	"        float shoreWaveDamp = smoothstep(0.0, 1.8, fragWaterDepth);\n"
	"        float basinWaveScale = (0.38 + 0.62 * openSeaFactor) * windFetch * (0.30 + 0.70 * shoreWaveDamp) * (1.0 - freezeFactor * 0.95);\n"
	"        vec4 waves = eval_zoom_water_waves(vWorldPos, uAnimTime, vDistance, fw, openSeaFactor) * (1.0 - freezeFactor * 0.95);\n"
	"        vec2 capGrad = eval_wind_capillary_facets(vWorldPos, uAnimTime, wind, vDistance, fw) * smoothstep(0.4, 2.8, fragWaterDepth) * (1.0 - freezeFactor * 0.95);\n"
	"        vec2 waveGrad = waves.yz * basinWaveScale + capGrad * 0.45 + lambWake.yz * 1.85 * (1.0 - freezeFactor);\n"
	"        vec2 specGrad = waves.yz * (basinWaveScale * 1.45 + 0.35) + capGrad * 1.35 + lambWake.yz * 2.85 * (1.0 - freezeFactor);\n"
	"        vec3 waveNormal = normalize(torusN - waveGrad.x * tangentU - waveGrad.y * tangentV);\n"
	"        vec3 specNormal = normalize(torusN - specGrad.x * tangentU - specGrad.y * tangentV);\n"
	"        float NdotV = max(0.0, dot(waveNormal, viewDir));\n"
	"        vec3 flatRefl = reflect(-viewDir, torusN);\n"
	"        vec3 glintSunDir = normalize(flatRefl - 0.14 * tangentU - 0.04 * tangentV + 0.08 * torusN);\n"
	"        vec3 halfVec = normalize(glintSunDir + viewDir);\n"
	"        float NdotH_cap  = max(0.0, dot(specNormal, halfVec));\n"
	"        float NdotH_wave = max(0.0, dot(waveNormal, halfVec));\n"
	"        float closeWeight = clamp(1.0 - (vDistance - 350.0) / 1400.0, 0.0, 1.0);\n"
	"        float specMicro   = pow(smoothstep(0.9942, 0.9997, NdotH_cap), 2.5) * (0.42 + 0.52 * wind.w) * (0.50 + 0.50 * closeWeight);\n"
	"        float specWavelet = pow(smoothstep(0.9840, 0.9982, NdotH_wave), 2.0) * (0.22 + 0.18 * wind.z);\n"
	"        float specBroad   = smoothstep(0.9620, 0.9935, NdotH_wave) * (0.09 + 0.05 * (1.0 - closeWeight));\n"
	"        float shoreSpecDamp = smoothstep(0.3, 3.2, fragWaterDepth);\n"
	"        float specSun     = (specMicro * 0.70 + specWavelet * 0.85 + specBroad * 0.55) * shoreSpecDamp * sunTrans;\n"
	"        float iceGlint    = pow(max(0.0, dot(torusN, halfVec)), 22.0) * 0.42;\n"
	"        specSun           = mix(specSun * directSun, iceGlint * sunTrans * directSun, freezeFactor);\n"
	"        float flatNdotV = max(0.0, dot(torusN, viewDir));\n"
	"        float facetTilt = clamp((flatNdotV - dot(specNormal, viewDir)) * 2.8, -0.18, 0.32);\n"
	"        float fresnel = clamp(0.02 + 0.98 * pow(clamp(1.0 - NdotV, 0.0, 1.0), 5.0) + max(0.0, facetTilt) * 0.22, 0.015, 0.65);\n"
	"        vec3 skySurf = ((length(uSkyColor) > 0.001) ? uSkyColor : vec3(0.55, 0.72, 0.88)) * (0.16 + 0.84 * dayFactor);\n"
	"        vec3 sunColor = vec3(1.0, 0.98, 0.92);\n"
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
	"        float foam = clamp(((crestLap + whitecap + swashState.z) * foamDistFade + lambWake.w) * (1.0 - freezeFactor), 0.0, 0.55);\n"
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
	"        if (uCsm.z > 0.0 && vDistance <= 14000.0) {\n"
	"            vec3 waveRefl = reflect(-viewDir, waveNormal);\n"
	"            float reflUp = max(0.30, dot(waveRefl, torusN));\n"
	"            vec2 reflHoriz = vec2(dot(waveRefl, tangentU), dot(waveRefl, tangentV));\n"
	"            reflCloudDens = smoothstep(0.05, 0.66, nd_csm_density(vWorldPos + reflHoriz * ((920.0 - liveSeaLevel) / reflUp)));\n"
	"        }\n"
	"        vec3 reflCloudCol = mix(vec3(0.76, 0.82, 0.90), vec3(0.98, 0.97, 0.95), clamp(0.55 + facetTilt * 1.4, 0.0, 1.0));\n"
	"        reflSkyCol = mix(reflSkyCol * nd_amb_trans(cloudShadow), reflCloudCol, clamp(reflCloudDens * 0.85, 0.0, 0.88));\n"
	"        vec3 surfaceNum = reflSkyCol * skyReflWeight + sssTint + sunColor * specSun + foamColor * foamWeight;\n"
	"        vec3 surfaceRgb = (totalWeight > 1e-4) ? clamp(surfaceNum / max(totalWeight, 1e-4), 0.0, 1.0) : reflSkyCol;\n"
	"        if (uLambLight.w > 0.001) {\n"
	"            vec2 dXY = uLambLight.xy - vWorldPos;\n"
	"            float d2 = dot(dXY, dXY);\n"
	"            float atten = (1.0 / (1.0 + d2 * 0.00055)) * smoothstep(220.0 * 220.0, 28.0 * 28.0, d2);\n"
	"            surfaceRgb += vec3(1.06, 0.99, 0.84) * (uLambLight.w * 0.16 * atten);\n"
	"        }\n"
	"        /* Procedural frozen ice sheet structure (crystals, micro-fractures, pack-ice crevasses) */\n"
	"        vec3 iceNoise = chaff_grad_noise2d(vWorldPos * 0.05, 0x1CE54E5Eu);\n"
	"        vec3 iceDetail = chaff_grad_noise2d(vWorldPos * 0.28, 0x1CE07E17u);\n"
	"        float icePebble = (iceNoise.x - 0.5) * 0.16 + (iceDetail.x - 0.5) * 0.08;\n"
	"        float iceCracks = smoothstep(0.47, 0.52, abs(iceNoise.x - 0.5)) * 0.22;\n"
	"        vec3 iceAlbedo = mix(vec3(0.68, 0.86, 0.96), vec3(0.96, 0.98, 1.00), clamp(0.52 + icePebble + iceCracks, 0.0, 1.0));\n"
	"        vec3 iceLit = iceAlbedo * effLighting;\n"
	"        waterBody = mix(waterBody, iceLit, freezeFactor * 0.95);\n"
	"        surfaceRgb = mix(surfaceRgb, iceLit, freezeFactor * 0.96);\n"
	"        float surfEdgeFade = smoothstep(0.0, 0.18, fragWaterDepth);\n"
	"        float liquidAlpha = clamp(1.0 - (1.0 - skyReflWeight) * (1.0 - sssWeight) * (1.0 - specWeight) * (1.0 - foamWeight), 0.0, 0.94) * surfEdgeFade;\n"
	"        float iceAlpha = clamp(0.88 + 0.10 * smoothstep(0.2, 3.0, fragWaterDepth), 0.0, 0.98);\n"
	"        float surfAlpha = mix(liquidAlpha, iceAlpha, freezeFactor);\n"
	"        vec3 finalWater = mix(waterBody, surfaceRgb, surfAlpha) * (0.16 + 0.84 * dayFactor);\n"
	"        lit = mix(lit, finalWater, wetBlend);\n"
	"    }\n"
	"    float fogFactor = clamp((vDistance - uFogStart) / max(1.0, uFogEnd - uFogStart), 0.0, 1.0);\n"
	"    vec3 sky = ((length(uSkyColor) > 0.001) ? uSkyColor : vec3(0.55, 0.70, 0.85)) * (0.18 + 0.82 * dayFactor);\n"
	"    vec3 viewDirNorm = normalize(vViewDir);\n"
	"    float limb = pow(clamp(1.0 - max(0.0, dot(torusN, viewDirNorm)), 0.0, 1.0), 3.0);\n"
	"    vec3 surfaceLit = mix(lit, vec3(0.35, 0.65, 0.95) * (0.15 + 0.85 * dayFactor), limb * 0.45 * uAtmoIntensity);\n"
	"    FragColor = vec4(mix(surfaceLit, sky, fogFactor * uAtmoIntensity), 1.0);\n"
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
	"uniform vec2 uChunkOrigin;\n"
	"uniform vec2 uChunkRelOrigin;\n"
	"uniform vec2 uChunkSpan;\n"
	"uniform vec4 uRefTrig;\n"
	"uniform float uHeightScale;\n"
	"uniform float uSeaLevel;\n"
	"uniform vec3 uCameraPos;\n"
	"uniform vec3 uPlanetCenter;\n"
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
	"    return float(texelFetch(uHeightTex, clamp(p, ivec2(0), maxC), 0).r);\n"
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
	"void main() {\n"
	"    vUV = a_grid_uv;\n"
	"    float scale = (uHeightScale != 0.0) ? uHeightScale : 1.0;\n"
	"    float effectiveScale = scale * 0.0625;\n"
	"    float minNeighborRaw = 0.0;\n"
	"    float raw_h = sampleHeightSmooth(a_grid_uv, minNeighborRaw);\n"
	"    float wz = raw_h * effectiveScale;\n"
	"    float minNeighborWz = minNeighborRaw * effectiveScale;\n"
	"    float depth = uSeaLevel - wz;\n"
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
	"    float effWz = uSeaLevel;\n"
	"    float Rv0 = R + r0 * cv0;\n"
	"    float dRv = r0 * dcv + effWz * cv;\n"
	"    vec3 worldPos = uPlanetCenter + vec3(Rv0 * dcu + dRv * cu, Rv0 * dsu + dRv * su, r0 * dsv + effWz * sv);\n"
	"    vTorusNormal = vec3(cv * cu, cv * su, sv);\n"
	"    vTangentU = vec3(-su, cu, 0.0);\n"
	"    vTangentV = vec3(-sv * cu, -sv * su, cv);\n"
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
	"out vec4 FragColor;\n"
	"float getH(ivec2 p, ivec2 maxC) {\n"
	"    return float(texelFetch(uHeightTex, clamp(p, ivec2(0), maxC), 0).r);\n"
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
	"vec4 eval_recursive_wind(vec2 worldPos, float anim_time) {\n"
	"    return chaff_eval_flow_2d(worldPos, anim_time);\n"
	"}\n"
	ND_GLSL_SCENE_LIGHT
	ND_GLSL_CLOUD_SHADOW_FN
	ND_GLSL_CSM_LOOKUP_FN
	ND_GLSL_CLOUD_SHADOW_ENTRY_FN
	"vec3 eval_shore_micro_relief(vec2 worldPos, float depth, float fw) {\n"
	"    float env = smoothstep(3.2, 0.25, abs(depth));\n"
	"    if (env <= 0.001) return vec3(0.0);\n"
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
	"    float rippleEnv = (0.55 + 0.65 * wind.w) * distVis;\n"
	"    float f1 = 1.35;\n"
	"    float f2 = 3.40;\n"
	"    float nyq1 = smoothstep(0.60, 0.15, f1 * fw);\n"
	"    float nyq2 = smoothstep(0.60, 0.15, f2 * fw);\n"
	"    float t1 = mod(anim_time * 1.15, 512.0);\n"
	"    float t2 = mod(anim_time * 1.65, 512.0);\n"
	"    /* Steady coordinate projection + bounded spatial wind-gust warp so velocity never grows with anim_time */\n"
	"    vec2 gustShift = wDir * (wind.z * 0.55) + wPerp * (wind.w * 0.35);\n"
	"    vec2 wCoord1 = vec2(dot(pos, prevW) * 1.45, dot(pos, prevP) * 0.78) * f1 + gustShift - vec2(t1, 0.18 * t1);\n"
	"    vec2 wCoord2 = vec2(dot(pos, prevW) * 1.35 + dot(pos, prevP) * 0.35, dot(pos, prevP) * 0.85 - dot(pos, prevW) * 0.25) * f2 + gustShift * 1.4 - vec2(t2, -0.22 * t2);\n"
	"    vec3 c1 = (nyq1 > 0.001) ? chaff_grad_noise2d(wCoord1, 0xCA9111A1u) : vec3(0.5, 0.0, 0.0);\n"
	"    vec3 c2 = (nyq2 > 0.001) ? chaff_grad_noise2d(wCoord2 + c1.yz * 0.25, 0xCA9111A2u) : vec3(0.5, 0.0, 0.0);\n"
	"    vec2 localG = c1.yz * (0.14 * nyq1) + c2.yz * (0.11 * nyq2);\n"
	"    vec2 worldCapGrad = (wDir * localG.x * 1.40 + wPerp * localG.y * 0.80) * rippleEnv;\n"
	"    return worldCapGrad;\n"
	"}\n"
	"float nd_octave_weight(float wavelength, float fw) {\n"
	"    return smoothstep(0.92, 0.28, fw / max(1e-4, wavelength));\n"
	"}\n"
	"float eval_astronomical_tide(vec3 torusN, float openSeaFactor) {\n"
	"    if (uSeaLevel <= -900.0) return 0.0;\n"
	"    float isOcean = smoothstep(0.20, 0.65, openSeaFactor) * (1.0 - smoothstep(0.25, 0.75, abs(uSeaLevel - uBaseSeaLevel)));\n"
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
	"vec4 eval_shore_swash(vec2 pos, float baseTideZ, float terrainZ, vec2 slopeGrad, float anim_time, float dist, float fw, float openSeaFactor) {\n"
	"    float isOcean = smoothstep(0.20, 0.65, openSeaFactor) * (1.0 - smoothstep(0.25, 0.75, abs(uSeaLevel - uBaseSeaLevel)));\n"
	"    float slopeMag = length(slopeGrad);\n"
	"    float relZ = terrainZ - baseTideZ;\n"
	"    float nearShore = smoothstep(2.4, 0.0, max(0.0, relZ)) * smoothstep(4.5, 0.0, max(0.0, -relZ));\n"
	"    if (nearShore <= 0.001 || dist > 3800.0) {\n"
	"        float tideHigh = uSeaLevel + (0.15 + 1.05 * isOcean);\n"
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
	"    float highWaterZ = mix(max(baseTideZ, uSeaLevel + 0.15 + 1.05 * isOcean), max(swashSurfZ, recentSwashTop), nearShore);\n"
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
	"    vec4 windPre = eval_recursive_wind(vWorldPos, uAnimTime);\n"
	"    vec2 basinPre = eval_water_fetch_and_basin(vUV, windPre.xy, shoreGrad, max(0.0, uSeaLevel - fragZ), effectiveScale);\n"
	"    float openSeaFactor = basinPre.x;\n"
	"    float windFetch = basinPre.y;\n"
	"    float astroTide = eval_astronomical_tide(torusN, openSeaFactor);\n"
	"    float baseTideSeaLevel = uSeaLevel + astroTide;\n"
	"    float rawDepth = baseTideSeaLevel - fragZ;\n"
	"    vec3 shoreRelief = eval_shore_micro_relief(vWorldPos, rawDepth, fw);\n"
	"    fragZ += shoreRelief.x;\n"
	"    shoreGrad += shoreRelief.yz;\n"
	"    vec4 swashState = eval_shore_swash(vWorldPos, baseTideSeaLevel, fragZ, shoreGrad, uAnimTime, vDistance, fw, openSeaFactor);\n"
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
	"    vec3 rawBiome = texture(uSubstrateTex, vUV).rgb;\n"
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
	"    float innerEclipse = (sunDot > 0.0) ? (1.0 - 0.45 * innerHole * (1.0 - abs(sDir.z))) : 1.0;\n"
	"    float effSun = sunDot * innerEclipse;\n"
	"    float dayFactor = clamp(0.12 + 0.88 * smoothstep(-0.16, 0.22, effSun), 0.12, 1.0);\n"
	"    float directSun = smoothstep(-0.06, 0.18, effSun);\n"
	"    float latPolar = smoothstep(54.0, 68.0, latDeg);\n"
	"    float alpineFreeze = smoothstep(280.0, 480.0, fragZ) * (1.0 - wTropical);\n"
	"    float freezeFactor = clamp(max(wPolar, max(latPolar, alpineFreeze)), 0.0, 1.0);\n"
	"    float eqEvapDry = smoothstep(24.0, 0.0, latDeg) * 0.18;\n"
	"    float midLatWet = smoothstep(18.0, 0.0, abs(latDeg - 38.0)) * 0.12;\n"
	"    float subpolarWet = smoothstep(10.0, 0.0, abs(latDeg - 58.0)) * 0.08;\n"
	"    float bioHumidity = clamp(0.65 * wTemperate + 0.88 * wTropical + 0.52 * wPolar + 0.25 * wVolcanic + 0.15 * smoothstep(0.0, 16.0, fragWaterDepth) - eqEvapDry + midLatWet + subpolarWet, 0.15, 0.95);\n"
	"    float cloudShadow = nd_cloud_shadow(vWorldPos, uSeaLevel, uAnimTime, bioHumidity, vDistance, fw);\n"
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
	"    vec2 waveGrad = waves.yz * basinWaveScale + capGrad * 0.45 + lambWake.yz * 1.85 * (1.0 - freezeFactor);\n"
	"    vec2 specGrad = waves.yz * (basinWaveScale * 1.45 + 0.35) + capGrad * 1.35 + lambWake.yz * 2.85 * (1.0 - freezeFactor);\n"
	"    vec3 waveNormal = normalize(torusN - waveGrad.x * tangentU - waveGrad.y * tangentV);\n"
	"    vec3 specNormal = normalize(torusN - specGrad.x * tangentU - specGrad.y * tangentV);\n"
	"    float NdotV = max(0.0, dot(waveNormal, viewDir));\n"
	"    float NdotL = max(0.0, dot(waveNormal, sunDir));\n"
	"    /* Mirror-cone sky/solar glitter axis in the local torus frame so wavelets across the entire surface sparkle */\n"
	"    vec3 flatRefl = reflect(-viewDir, torusN);\n"
	"    vec3 glintSunDir = normalize(flatRefl - 0.14 * tangentU - 0.04 * tangentV + 0.08 * torusN);\n"
	"    vec3 halfVec = normalize(glintSunDir + viewDir);\n"
	"    float NdotH_cap  = max(0.0, dot(specNormal, halfVec));\n"
	"    float NdotH_wave = max(0.0, dot(waveNormal, halfVec));\n"
	"    /* Three-tier specular hierarchy across the entire water surface:\n"
	"     * 1. Crisp pinpoint capillary sun glints (modulated by recursive wind cat's-paws)\n"
	"     * 2. Travelling wavelet slope highlights\n"
	"     * 3. Broad sky-sheen facet modulation */\n"
	"    float closeWeight = clamp(1.0 - (vDistance - 350.0) / 1400.0, 0.0, 1.0);\n"
	"    float specMicro   = pow(smoothstep(0.9942, 0.9997, NdotH_cap), 2.5) * (0.42 + 0.52 * wind.w) * (0.50 + 0.50 * closeWeight);\n"
	"    float specWavelet = pow(smoothstep(0.9840, 0.9982, NdotH_wave), 2.0) * (0.22 + 0.18 * wind.z);\n"
	"    float specBroad   = smoothstep(0.9620, 0.9935, NdotH_wave) * (0.09 + 0.05 * (1.0 - closeWeight));\n"
	"    float shoreSpecDamp = smoothstep(0.3, 3.2, fragWaterDepth);\n"
	"    float specSun     = (specMicro * 0.70 + specWavelet * 0.85 + specBroad * 0.55) * shoreSpecDamp * sunTrans;\n"
	"    float iceGlint    = pow(max(0.0, dot(torusN, halfVec)), 22.0) * 0.42;\n"
	"    specSun           = mix(specSun * directSun, iceGlint * sunTrans * directSun, freezeFactor);\n"
	"    /* Schlick Fresnel for air-water interface (R0 = 0.02) + per-wavelet facet tilt contrast */\n"
	"    float flatNdotV = max(0.0, dot(torusN, viewDir));\n"
	"    float facetTilt = clamp((flatNdotV - dot(specNormal, viewDir)) * 2.8, -0.18, 0.32);\n"
	"    float fresnel = clamp(0.02 + 0.98 * pow(clamp(1.0 - NdotV, 0.0, 1.0), 5.0) + max(0.0, facetTilt) * 0.22, 0.015, 0.65);\n"
	"    vec3 sky = ((length(uSkyColor) > 0.001) ? uSkyColor : vec3(0.55, 0.72, 0.88)) * (0.16 + 0.84 * dayFactor);\n"
	"    vec3 sunColor = vec3(1.0, 0.98, 0.92);\n"
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
	"    float foam = clamp(((crestLap + whitecap + swashState.z) * foamDistFade + lambWake.w) * (1.0 - freezeFactor), 0.0, 0.55);\n"
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
	"    if (uCsm.z > 0.0 && vDistance <= 14000.0) {\n"
	"        vec3 waveRefl = reflect(-viewDir, waveNormal);\n"
	"        float reflUp = max(0.30, dot(waveRefl, torusN));\n"
	"        vec2 reflHoriz = vec2(dot(waveRefl, tangentU), dot(waveRefl, tangentV));\n"
	"        reflCloudDens = smoothstep(0.05, 0.66, nd_csm_density(vWorldPos + reflHoriz * ((920.0 - uSeaLevel) / reflUp)));\n"
	"    }\n"
	"    vec3 reflCloudCol = mix(vec3(0.76, 0.82, 0.90), vec3(0.98, 0.97, 0.95), clamp(0.55 + facetTilt * 1.4, 0.0, 1.0));\n"
	"    reflSkyCol = mix(reflSkyCol * nd_amb_trans(cloudShadow), reflCloudCol, clamp(reflCloudDens * 0.85, 0.0, 0.88));\n"
	"    vec3 surfaceNum = reflSkyCol * skyReflWeight + sssTint + sunColor * specSun + foamColor * foamWeight;\n"
	"    vec3 surfaceRgb = (totalWeight > 1e-4) ? clamp(surfaceNum / max(totalWeight, 1e-4), 0.0, 1.0) : reflSkyCol;\n"
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
	"    surfaceRgb = mix(surfaceRgb, vec3(0.35, 0.70, 1.0) * (0.15 + 0.85 * dayFactor), limb * 0.55 * uAtmoIntensity);\n"
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
	"    float shallowFactor = smoothstep(1.5, 18.0, dObj);\n"
	"    float effOptical = pathF * (0.04 + 0.96 * shallowFactor);\n"
	"    vec3 absorbed = sceneS.rgb * exp(-vec3(0.058, 0.022, 0.007) * effOptical);\n"
	"    float dBed = clamp(fragWaterDepth, 0.0, 2000.0);\n"
	"    float tShelf = smoothstep(2.5, 9.5, dBed);\n"
	"    float tOuter = smoothstep(9.5, 20.0, dBed);\n"
	"    float tAbyss = smoothstep(20.0, 42.0, dBed);\n"
	"    float tTrench = smoothstep(180.0, 950.0, dBed);\n"
	"    vec3 inscatterCol = mix(C_SHALLOW, C_SHELF, tShelf);\n"
	"    inscatterCol = mix(inscatterCol, C_OUTER, tOuter);\n"
	"    inscatterCol = mix(inscatterCol, C_ABYSS, tAbyss);\n"
	"    inscatterCol = mix(inscatterCol, C_ABYSS * 0.55, tTrench);\n"
	"    float bodyScatterWeight = shallowFactor * (1.0 - exp(-0.052 * effOptical));\n"
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
	"layout (location = 0) in vec3 a_pos;\n"
	"layout (location = 1) in vec2 a_uv;\n"
	"layout (location = 2) in vec4 a_tint;\n"
	"layout (location = 3) in vec3 a_local_pos;\n"
	"layout (location = 4) in vec4 a_quad_uv;\n"
	"layout (location = 5) in vec4 a_sprite_bounds;\n"
	"uniform mat4 uMVP;\n"
	"uniform vec3 uCameraPos;\n"
	"out vec2 vUV;\n"
	"out vec4 vTint;\n"
	"out vec3 vWorldPos;\n"
	"out vec3 vLocalPos;\n"
	"out vec4 vQuadUV;\n"
	"out vec4 vSpriteBounds;\n"
	"void main() {\n"
	"    vUV = a_uv;\n"
	"    vTint = a_tint;\n"
	"    vWorldPos = a_pos;\n"
	"    vLocalPos = a_local_pos;\n"
	"    vQuadUV = a_quad_uv;\n"
	"    vSpriteBounds = a_sprite_bounds;\n"
	"    vec3 toCam = uCameraPos - a_pos;\n"
	"    float dCam = length(toCam);\n"
	"    vec3 dirToCam = (dCam > 0.001) ? (toCam / dCam) : vec3(0.0, 0.0, 1.0);\n"
	"    float bias = (a_quad_uv.w < -0.5) ? min(10.0, dCam * 0.035) : min(24.0, dCam * 0.08);\n"
	"    gl_Position = uMVP * vec4(a_pos + dirToCam * bias, 1.0);\n"
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
	"    vec2 fw = fwidth(p);\n"
	"    float minFilterW = ((style > 0.5 && style < 2.5) || (style > 4.5 && style < 5.5)) ? 0.36 : 0.26;\n"
	"    vec2 filterW = clamp(fw * 0.92, vec2(minFilterW), vec2(0.50));\n"
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
	"    float l00 = dot(c00.rgb, vec3(0.299, 0.587, 0.114));\n"
	"    float l10 = dot(c10.rgb, vec3(0.299, 0.587, 0.114));\n"
	"    float l01 = dot(c01.rgb, vec3(0.299, 0.587, 0.114));\n"
	"    float l11 = dot(c11.rgb, vec3(0.299, 0.587, 0.114));\n"
	"    /* Favor non-outline interior texel luminance so 1px dark retro ink borders soften into surface albedo */\n"
	"    float w00 = c00.a * b00 * (0.28 + 0.72 * smoothstep(0.08, 0.28, l00));\n"
	"    float w10 = c10.a * b10 * (0.28 + 0.72 * smoothstep(0.08, 0.28, l10));\n"
	"    float w01 = c01.a * b01 * (0.28 + 0.72 * smoothstep(0.08, 0.28, l01));\n"
	"    float w11 = c11.a * b11 * (0.28 + 0.72 * smoothstep(0.08, 0.28, l11));\n"
	"    float wSum = w00 + w10 + w01 + w11;\n"
	"    vec3 rgb = (wSum > 1e-5) ? ((c00.rgb * w00 + c10.rgb * w10 + c01.rgb * w01 + c11.rgb * w11) / wSum) : vec3(0.0);\n"
	"    if (style > 0.5 && style < 3.5 && a > 0.15) {\n"
	"        /* Interior tonal de-banding for scenery: smooths discrete pixel-art steps into continuous 3D gradients */\n"
	"        ivec2 ic = ivec2(floor(uv * texSize));\n"
	"        vec4 n0 = fetchBoundedTexel(ic + ivec2(-1, 0), iBounds, maxTex);\n"
	"        vec4 n1 = fetchBoundedTexel(ic + ivec2( 1, 0), iBounds, maxTex);\n"
	"        vec4 n2 = fetchBoundedTexel(ic + ivec2(0, -1), iBounds, maxTex);\n"
	"        vec4 n3 = fetchBoundedTexel(ic + ivec2(0,  1), iBounds, maxTex);\n"
	"        float nw0 = n0.a * (0.25 + 0.75 * smoothstep(0.10, 0.30, dot(n0.rgb, vec3(0.299, 0.587, 0.114))));\n"
	"        float nw1 = n1.a * (0.25 + 0.75 * smoothstep(0.10, 0.30, dot(n1.rgb, vec3(0.299, 0.587, 0.114))));\n"
	"        float nw2 = n2.a * (0.25 + 0.75 * smoothstep(0.10, 0.30, dot(n2.rgb, vec3(0.299, 0.587, 0.114))));\n"
	"        float nw3 = n3.a * (0.25 + 0.75 * smoothstep(0.10, 0.30, dot(n3.rgb, vec3(0.299, 0.587, 0.114))));\n"
	"        float nwSum = nw0 + nw1 + nw2 + nw3;\n"
	"        if (nwSum > 0.25) {\n"
	"            vec3 nRgb = (n0.rgb * nw0 + n1.rgb * nw1 + n2.rgb * nw2 + n3.rgb * nw3) / nwSum;\n"
	"            float curLuma = dot(rgb, vec3(0.299, 0.587, 0.114));\n"
	"            float blendAmt = (curLuma < 0.22) ? 0.52 : 0.28;\n"
	"            rgb = mix(rgb, nRgb, blendAmt);\n"
	"        }\n"
	"    }\n"
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
	"    float rippleEnv = (0.55 + 0.65 * wind.w) * distVis;\n"
	"    float f1 = 1.35;\n"
	"    float f2 = 3.40;\n"
	"    float nyq1 = smoothstep(0.60, 0.15, f1 * fw);\n"
	"    float nyq2 = smoothstep(0.60, 0.15, f2 * fw);\n"
	"    float t1 = mod(anim_time * 1.15, 512.0);\n"
	"    float t2 = mod(anim_time * 1.65, 512.0);\n"
	"    vec2 gustShift = wDir * (wind.z * 0.55) + wPerp * (wind.w * 0.35);\n"
	"    vec2 wCoord1 = vec2(dot(pos, prevW) * 1.45, dot(pos, prevP) * 0.78) * f1 + gustShift - vec2(t1, 0.18 * t1);\n"
	"    vec2 wCoord2 = vec2(dot(pos, prevW) * 1.35 + dot(pos, prevP) * 0.35, dot(pos, prevP) * 0.85 - dot(pos, prevW) * 0.25) * f2 + gustShift * 1.4 - vec2(-0.25 * t2, t2);\n"
	"    vec3 c1 = (nyq1 > 0.001) ? chaff_grad_noise2d(wCoord1, 0xCA9111A1u) : vec3(0.5, 0.0, 0.0);\n"
	"    vec3 c2 = (nyq2 > 0.001) ? chaff_grad_noise2d(wCoord2 + c1.yz * 0.25, 0xCA9111A2u) : vec3(0.5, 0.0, 0.0);\n"
	"    vec2 localG = c1.yz * (0.14 * nyq1) + c2.yz * (0.11 * nyq2);\n"
	"    vec2 worldCapGrad = (wDir * localG.x * 1.40 + wPerp * localG.y * 0.80) * rippleEnv;\n"
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
	"            waveRefGrad = waves.yz + capGrad * 0.65;\n"
	"        }\n"
	"    }\n"
	"    /* 1. 3D Ground-Contact Drop Shadow Pass (vQuadUV.w < -0.5) */\n"
	"    if (vQuadUV.w < -0.5) {\n"
	"        if (isSubmerged && d > 4.0) discard;\n"
	"        float r2 = dot(vQuadUV.xy, vQuadUV.xy);\n"
	"        if (r2 >= 1.0) discard;\n"
	"        vec3 shNoise = chaff_grad_noise2d(vLocalPos.xy * 0.28, 0x53484457u);\n"
	"        float penumbra = pow(clamp(1.0 - r2, 0.0, 1.0), 1.65);\n"
	"        float dapple = 0.86 + 0.28 * shNoise.x;\n"
	"        vec2 umbraDelta = vQuadUV.xy + vec2(0.20, 0.16);\n"
	"        float umbra = exp(-dot(umbraDelta, umbraDelta) * 3.2) * 0.16;\n"
	"        float waterAtten = isSubmerged ? clamp(1.0 - d * 0.25, 0.0, 1.0) : 1.0;\n"
	"        float distAtten = clamp((16000.0 - vDist) / 12000.0, 0.0, 1.0);\n"
	"        float shadowAlpha = clamp((penumbra * 0.36 * dapple + umbra) * nd_sun_trans(cloudSh) * vTint.a * waterAtten * distAtten * (1.0 - fogFactor * uAtmoIntensity), 0.0, 0.48);\n"
	"        if (shadowAlpha <= 0.005) discard;\n"
	"        vec3 shadowRgb = mix(vec3(0.05, 0.07, 0.10), sky * 0.16, 0.45);\n"
	"        FragColor = vec4(shadowRgb, shadowAlpha);\n"
	"        return;\n"
	"    }\n"
	"    /* 2. Sub-Texel Anti-Aliased, Snell-Refracted & Outline-Softened Sprite Sampling */\n"
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
	"        : smoothstep(0.12, 0.46, effSampleAlpha);\n"
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
	"    /* Soften dark 1px retro ink outlines toward natural surface chroma */\n"
	"    float rawLuma = dot(albedo, vec3(0.299, 0.587, 0.114));\n"
	"    if (rawLuma < 0.22 && style > 0.5 && !isLambGlow) {\n"
	"        float inkLift = smoothstep(0.22, 0.04, rawLuma) * 0.48;\n"
	"        vec3 liftedTone = (style > 2.5) ? vec3(0.32, 0.31, 0.29) : vec3(0.22, 0.28, 0.18);\n"
	"        albedo = mix(albedo, liftedTone, inkLift);\n"
	"    }\n"
	"    /* 4. 3D Volumetric Canopy/Body Normals, Micro-Relief & Directional Sun Lighting */\n"
	"    float qx = clamp(vQuadUV.x, -1.0, 1.0);\n"
	"    float qy = clamp(vQuadUV.y, 0.0, 1.0);\n"
	"    float distFade = clamp(1.0 - (vDist - 1800.0) / 36000.0, 0.0, 1.0);\n"
	"    float closeZoom = clamp((820.0 - vDist) / 480.0, 0.0, 1.0);\n"
	"    vec2 surfPos = vec2(vLocalPos.x, vLocalPos.z);\n"
	"    vec3 g1 = chaff_grad_noise2d(surfPos * 0.24, 0xA341316Cu);\n"
	"    vec3 g2 = (distFade > 0.01 && !isGrass) ? chaff_grad_noise2d(surfPos * 0.78 + vec2(17.3, -29.1), 0xC8013EA4u) : vec3(0.5, 0.0, 0.0);\n"
	"    float microH = (g1.x - 0.5) * 0.26 + (g2.x - 0.5) * 0.16 * distFade;\n"
	"    vec2 microGrad = g1.yz * 0.22 + g2.yz * (0.32 * distFade);\n"
	"    float microCavity = clamp(max(0.0, 0.48 - g1.x) * 0.42 + max(0.0, 0.48 - g2.x) * 0.32 * distFade, 0.0, 0.34);\n"
	"    bool isFoliage = !isLambGlow && (isGrass || isFloraObj || (albedo.g > max(albedo.r, albedo.b) * 1.06));\n"
	"    bool isMineral = !isLambGlow && !isGrass && (style > 2.5);\n"
	"    if (isFoliage && distFade > 0.01) {\n"
	"        /* Harmonize foliage palette with terrain vegetation (deepMoss / lushTurf / warmClover) */\n"
	"        vec3 warmFoliage = albedo * vec3(1.06, 1.02, 0.84);\n"
	"        vec3 deepLeaf    = albedo * vec3(0.84, 0.96, 0.80);\n"
	"        albedo = mix(deepLeaf, warmFoliage, smoothstep(0.28, 0.72, g1.x));\n"
	"        float leafRelief = (g1.x - 0.5) * 0.16 * distFade + (g2.x - 0.5) * 0.14 * closeZoom;\n"
	"        albedo *= clamp(1.0 + leafRelief, 0.80, 1.22);\n"
	"        if (isGrass) {\n"
	"            /* Cheap travelling gust phase wave replaces a per-pixel 3-noise wind evaluation on every grass fragment */\n"
	"            float gustWave = 0.5 + 0.5 * sin(dot(vLocalPos.xy, vec2(0.052, -0.047)) + mod(uAnimTime, 628.3185) * " ND_SL_STR(ND_GRASS_SHEEN_FREQ_RAD) " + (g1.x - 0.5) * 3.0);\n"
	"            float tipSheen = smoothstep(0.28, 0.96, qy) * clamp(gustWave * 0.80, 0.0, 1.0);\n"
	"            vec3 groundTurf = mix(vec3(0.29, 0.49, 0.23), vec3(0.37, 0.60, 0.27), smoothstep(0.25, 0.75, g1.x)) * vTint.rgb;\n"
	"            vec3 bladeBody  = mix(albedo * vec3(0.98, 1.02, 0.92), albedo * vec3(1.16, 1.14, 0.88), smoothstep(0.18, 0.92, qy + tipSheen * 0.32));\n"
	"            albedo = mix(groundTurf, bladeBody, smoothstep(0.0, 0.30, qy));\n"
	"        }\n"
	"    } else if (isMineral && distFade > 0.01) {\n"
	"        /* Harmonize mineral sprites with terrain slate/granite strata & fracture crevices */\n"
	"        vec3 slateWarm   = vec3(0.42, 0.39, 0.35);\n"
	"        vec3 graniteCool = vec3(0.34, 0.36, 0.38);\n"
	"        vec3 rockBase    = mix(slateWarm, graniteCool, clamp(g1.x, 0.0, 1.0)) * (0.75 + 0.65 * rawLuma);\n"
	"        albedo = mix(albedo, rockBase, 0.38 * distFade);\n"
	"        float rockCrevice = smoothstep(0.11, 0.015, abs(g2.x - 0.5)) * distFade;\n"
	"        albedo *= (1.0 - rockCrevice * 0.28);\n"
	"    }\n"
	"    vec3 proxyN = isGrass\n"
	"        ? normalize(mix(vec3(0.0, 0.18, 0.98), vec3(qx * 0.45, 0.42, 0.78), smoothstep(0.0, 0.45, qy)))\n"
	"        : normalize(vec3(qx * 0.65, 0.55 - 0.15 * qy, 0.22 + 0.68 * qy));\n"
	"    vec3 N_bb = normalize(proxyN - vec3(microGrad.x * (isGrass ? 0.18 : 0.38), 0.0, microGrad.y * (isGrass ? 0.18 : 0.38)));\n"
	"    vec3 L_sun = normalize(vec3(-0.577, -0.577, 0.816));\n"
	"    float sunDiff = max(0.0, dot(N_bb, L_sun));\n"
	"    float cavityAO = isGrass ? clamp(1.0 + microH * 0.12, 0.94, 1.08) : clamp(1.0 + microH * 0.28 - microCavity, 0.68, 1.15);\n"
	"    float hAboveGround = max(0.0, vLocalPos.z - vQuadUV.z);\n"
	"    float contactAO = isLambGlow ? 1.0 : (isGrass ? mix(0.96, 1.0, smoothstep(0.0, 2.5, hAboveGround)) : mix(0.68, 1.0, smoothstep(0.0, 5.5, hAboveGround)));\n"
	"    float hemiSky = 0.5 + 0.5 * N_bb.z;\n"
	"    vec3 ambCol = mix(vec3(0.50, 0.55, 0.62), vec3(0.68, 0.74, 0.82), hemiSky);\n"
	"    vec3 sunCol = vec3(1.06, 1.02, 0.95);\n"
	"    vec3 lightTerm = (ambCol * (0.52 * nd_amb_trans(cloudSh) * (0.20 + 0.80 * dayFactor)) + sunCol * (0.58 * sunDiff * nd_sun_trans(cloudSh) * directSun)) * (cavityAO * contactAO);\n"
	"    vec3 lit = albedo * mix(vec3(1.0), lightTerm, isLambGlow ? 0.32 : 0.78);\n"
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
	"        float dClamped = clamp(d, 0.0, 64.0);\n"
	"        float baseWaterDepth = max(dClamped, uSeaLevel - vQuadUV.z);\n"
	"        float baseSubFade = clamp((waveSurfZ + 0.35 - vQuadUV.z) / 0.50, 0.0, 1.0);\n"
	"        float caustDistFade = clamp(1.0 - (vDist - 900.0) / 2500.0, 0.0, 1.0);\n"
	"        float caustCloseZoom = clamp((420.0 - vDist) / 300.0, 0.0, 1.0);\n"
	"        float caustic = 0.0;\n"
	"        if (caustDistFade > 0.01 && dClamped < 16.0) {\n"
	"            float tC = mod(uAnimTime, 256.0);\n"
	"            vec2 cUV = waterSurfPos + waveRefGrad * 1.6;\n"
	"            caustic = nd_caustic_web(cUV, tC, caustCloseZoom) * caustDistFade;\n"
	"        }\n"
	"        float causticFocus = smoothstep(0.04, 0.85, dClamped) * exp(-0.11 * dClamped);\n"
	"        vec3 caustLit = lit * (1.0 + caustic * causticFocus * 0.45);\n"
	"        float cosView = 0.819152;\n"
	"        float opticalDepth = dClamped / cosView;\n"
	"        float subRamp = smoothstep(0.0, 0.35, dClamped);\n"
	"        float effectiveOpticalDepth = (0.85 + opticalDepth * 1.35) * subRamp;\n"
	"        vec3 seabedAbsorption = exp(-vec3(0.24, 0.085, 0.024) * effectiveOpticalDepth);\n"
	"        vec3 transmittedSpr = caustLit * seabedAbsorption;\n"
	"        vec3 C_SHALLOW = vec3(0.11, 0.42, 0.46);\n"
	"        vec3 C_SHELF   = vec3(0.08, 0.32, 0.44);\n"
	"        vec3 C_OUTER   = vec3(0.05, 0.21, 0.36);\n"
	"        vec3 C_ABYSS   = vec3(0.024, 0.10, 0.24);\n"
	"        vec3 inscatterCol = mix(C_SHALLOW, C_SHELF, smoothstep(1.2, 6.5, baseWaterDepth));\n"
	"        inscatterCol = mix(inscatterCol, C_OUTER, smoothstep(6.5, 18.0, baseWaterDepth));\n"
	"        inscatterCol = mix(inscatterCol, C_ABYSS, smoothstep(18.0, 42.0, baseWaterDepth));\n"
	"        float bodyScatterWeight = subRamp * (0.28 + 0.32 * (1.0 - exp(-0.18 * effectiveOpticalDepth)));\n"
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
	"    float humidity = clamp(0.50 + 0.28 * (worldClimate.x - 0.5) + 0.08 * uSeaWet, 0.15, 0.88);\n"
	"    vec2 atmoWindDir = vec2(0.741, -0.671), shearDisp = vec2(0.0);\n"
	"    float atmoGust = 0.65, updraft = 0.50;\n"
	"    chaff_eval_wind_3d(vec3(projXY, 920.0), 420.0, 2820.0, uAnimTime, fw, atmoWindDir, shearDisp, atmoGust, updraft);\n"
	"    vec4 syn = eval_synoptic_macro_cloud_fw(projXY - shearDisp, uAnimTime, humidity, fw);\n"
	"    FragColor = vec4(clamp(syn.x, 0.0, 1.0), 0.0, 0.0, 1.0);\n"
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
	"    vec3 worldPos = uPlanetCenter + vec3(Rv0 * dcu + dRv * cu, Rv0 * dsu + dRv * su, r0 * dsv + effWz * sv);\n"
	"    vTorusNormal = vec3(cv * cu, cv * su, sv);\n"
	"    vTangentU = vec3(-su, cu, 0.0);\n"
	"    vTangentV = vec3(-sv * cu, -sv * su, cv);\n"
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
	"out vec4 FragColor;\n"
	"float getH(ivec2 p, ivec2 maxC) {\n"
	"    return float(texelFetch(uHeightTex, clamp(p, ivec2(0), maxC), 0).r);\n"
	"}\n"
	"/* Smooth C2 quintic Hermite height evaluator on the 49x49 chunk heightfield texture:\n"
	" * Provides exact per-pixel analytical mountain-cloud intersection with zero coarse LOD triangle facets! */\n"
	"float sampleHeightCloud(vec2 uv) {\n"
	"    vec2 texSize = vec2(textureSize(uHeightTex, 0));\n"
	"    vec2 maxCoordF = max(vec2(1.0), texSize - 1.0);\n"
	"    vec2 pixelCoord = clamp(uv, 0.0, 1.0) * maxCoordF;\n"
	"    ivec2 iCoord = ivec2(floor(pixelCoord));\n"
	"    vec2 f = pixelCoord - vec2(iCoord);\n"
	"    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);\n"
	"    ivec2 maxCoord = ivec2(maxCoordF);\n"
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
	ND_GLSL_SYNOPTIC_CLOUD_FN
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
	"/* Multi-Deck 3D Tropospheric Cloud Field (4D Noise -> 3D Projection):\n"
	" * Three physically distinct, vertically separated decks, each driven by its OWN horizontal field so no two layers\n"
	" * share a silhouette when seen from above:\n"
	" *   Deck 1 (Lower Cumulus, h in [0.00, 0.27])    : mesoscale cumulus cells of the synoptic macro field\n"
	" *   Deck 2 (Altocumulus Shelf, h in [0.35, 0.64]) : cross-wind-displaced baroclinic bands + only the deepest convective cores\n"
	" *   Deck 3 (Anvil / Cirrus, h in [0.72, 1.00])   : downwind anvil plumes (coarse macro sampled 38 km upwind) + jet-stream cirrus\n"
	" * Returns vec4(density, gradX, gradY, gradZ). */\n"
	"vec4 eval_zoom_cloud_density_grad(vec3 worldPos, float anim_time, float humidity, float dist, float fw, vec4 synFlow, vec4 sfcFlow) {\n"
	"    float z = worldPos.z;\n"
	"    const float zBase = 420.0;\n"
	"    const float zTop  = 2820.0;\n"
	"    if (z <= zBase || z >= zTop) return vec4(0.0);\n"
	"    float hNorm = (z - zBase) / (zTop - zBase);\n"
	"    float deckIdx = (hNorm < 0.31) ? 0.0 : ((hNorm < 0.68) ? 1.0 : 2.0);\n"
	"    vec2 atmoWindDir; vec2 shearDisp; float atmoGust; float updraft;\n"
	"    nd_cloud_wind_at_height(synFlow, sfcFlow, hNorm, atmoWindDir, shearDisp, atmoGust, updraft);\n"
	"    vec2 windPerp = vec2(-atmoWindDir.y, atmoWindDir.x);\n"
	"    /* Per-deck horizontal coordinate & single synoptic macro evaluation per step (zero 4D noise on clear sky):\n"
	"     *   Deck 1 (hNorm < 0.31): wind-sheared cumulus field\n"
	"     *   Deck 2 (0.31..0.68)  : cross-wind & along-wind displaced baroclinic altocumulus + deep convective towers\n"
	"     *   Deck 3 (hNorm >= 0.68): 38 km upwind anvil outflow source + high-troposphere jet-stream cirrus */\n"
	"    bool upperDeck = (hNorm >= 0.68);\n"
	"    vec2 shearedXY = worldPos.xy - shearDisp - atmoWindDir * (hNorm * 1400.0);\n"
	"    vec2 synQueryXY = upperDeck\n"
	"        ? (shearedXY - atmoWindDir * 14000.0 + windPerp * 8000.0)\n"
	"        : ((deckIdx > 0.5) ? (shearedXY + windPerp * 3200.0 + atmoWindDir * 2000.0) : shearedXY);\n"
	"    float synFw = upperDeck ? max(fw, 4000.0) : fw;\n"
	"    vec2 macroGrad = vec2(0.0);\n"
	"    float highCirrus = 0.0;\n"
	"    vec4 synCloud = eval_synoptic_macro_cloud_core(synQueryXY, anim_time, humidity, synFw, upperDeck, macroGrad, highCirrus);\n"
	"    float macroCoverage = synCloud.x;\n"
	"    if (macroCoverage <= 0.008 && (!upperDeck || highCirrus <= 0.015)) return vec4(0.0);\n"
	"    float coreStrength = smoothstep(0.06, 0.75, macroCoverage);\n"
	"    /* 1. Deck 1: Lower Cumulus / Stratocumulus (hNorm in [0.00, 0.27]); core cells billow higher than fringes */\n"
	"    float hTop1 = 0.14 + 0.13 * coreStrength;\n"
	"    float deck1Prof = smoothstep(0.0, 0.045, hNorm) * (1.0 - smoothstep(hTop1 - 0.06, hTop1, hNorm));\n"
	"    float deck1Cov  = macroCoverage * deck1Prof;\n"
	"    /* 2. Deck 2: Mid-Level Altocumulus Mackerel Bands (hNorm in [0.35, 0.64]) + deepest convective cores */\n"
	"    float altoShelfProf = smoothstep(0.35, 0.42, hNorm) * (1.0 - smoothstep(0.57, 0.64, hNorm));\n"
	"    float altoShelf = 0.0;\n"
	"    if (altoShelfProf > 0.001) {\n"
	"        float wAlto = smoothstep(0.30, 0.08, 0.00001600 * fw);\n"
	"        float altoPhase = mod(anim_time * 0.018, 1024.0);\n"
	"        vec2 altoPos = vec2(0.60 * synQueryXY.x - 0.80 * synQueryXY.y, 0.80 * synQueryXY.x + 0.60 * synQueryXY.y)\n"
	"                     + synCloud.zw * 0.18 + vec2(42000.0, -31000.0);\n"
	"        vec3 alto0 = chaff_grad_noise2d(altoPos * 0.00000145 - atmoWindDir * (altoPhase * 0.45), 0x414C5430u);\n"
	"        vec3 altoN = (wAlto > 0.001) ? chaff_grad_noise2d(altoPos * 0.00001600 - atmoWindDir * altoPhase, 0x414C544Fu) : vec3(0.55, 0.0, 0.0);\n"
	"        float altoWave0 = smoothstep(0.28, 0.74, 1.0 - 2.0 * abs(alto0.x - 0.5));\n"
	"        float altoWave1 = smoothstep(0.30, 0.72, 1.0 - 2.0 * abs(altoN.x - 0.5));\n"
	"        float altoRibs  = mix(altoWave0, 0.52 * altoWave0 + 0.48 * altoWave1, wAlto);\n"
	"        altoShelf = altoShelfProf * smoothstep(0.12, 0.52, synCloud.y - 0.36) * altoRibs * 0.86;\n"
	"    }\n"
	"    float deepCore  = smoothstep(0.62, 0.92, coreStrength);\n"
	"    float towerTopH = mix(0.24, 0.86, deepCore * (0.45 + 0.55 * updraft));\n"
	"    float towerProf = smoothstep(0.12, 0.24, hNorm) * (1.0 - smoothstep(towerTopH - 0.10, towerTopH, hNorm)) * deepCore;\n"
	"    float deck2Cov  = max(macroCoverage * towerProf, altoShelf);\n"
	"    /* 3. Deck 3: Anvil Outflow Plumes & Jet-Stream Cirrus (reuses single synCloud evaluation at synQueryXY) */\n"
	"    float anvilProf = smoothstep(0.72, 0.81, hNorm) * (1.0 - smoothstep(0.92, 1.0, hNorm));\n"
	"    float anvilCov  = smoothstep(0.38, 0.78, macroCoverage) * (0.55 + 0.45 * updraft) * 0.82;\n"
	"    float deck3Cov  = max(anvilCov, highCirrus * 0.78) * anvilProf;\n"
	"    /* Combined 3-deck vertical envelope with clean inter-deck air clefts */\n"
	"    float multiDeck = max(deck1Cov, max(deck2Cov, deck3Cov));\n"
	"    float vProf     = clamp(max(deck1Prof, max(max(altoShelfProf, towerProf), anvilProf)), 0.0, 1.0);\n"
	"    float baseCloud = clamp(cloud_remap(multiDeck, 0.06 * (1.0 - 0.50 * coreStrength), 0.78, 0.0, 1.0), 0.0, 1.0);\n"
	"    if (baseCloud <= 0.001) return vec4(0.0);\n"
	"    /* 4-Octave Geometric 4D Spatiotemporal Cascade (lambda = 3,846 down to 17.2 units) with Sliding 2-Octave Band-Pass:\n"
	"     * When fine octaves j+2 reach full weight at close zoom, coarse octaves j fade out via (1.0 - wRaw[j+2]),\n"
	"     * so at most 2 octaves execute at close zoom and at orbital zoom while preserving full close-up cumulus detail! */\n"
	"    const float freqs[4] = float[4](0.00026000, 0.00160000, 0.00960000, 0.05800000);\n"
	"    const float spds[4]  = float[4](0.045, 0.095, 0.195, 0.360);\n"
	"    const uint  seeds[4] = uint[4](0x434C4434u, 0x434C4435u, 0x434C4437u, 0x434C4438u);\n"
	"    const float zScales[4] = float[4](1.00, 1.55, 2.40, 3.70);\n"
	"    const float wScales[4] = float[4](1.00, 1.50, 2.25, 3.40);\n"
	"    const float amps[4]    = float[4](0.38, 0.30, 0.24, 0.18);\n"
	"    const float dFar[4]    = float[4](650000.0, 95000.0, 14000.0, 2200.0);\n"
	"    const float dNear[4]   = float[4](150000.0, 18000.0,  2600.0,  380.0);\n"
	"    const float wDispScale[4] = float[4](1.00, 0.50, 0.22, 0.08);\n"
	"    const mat2 rot  = mat2(0.8, -0.6, 0.6, 0.8);\n"
	"    const mat2 irot = mat2(0.8,  0.6, -0.6, 0.8);\n"
	"    float wRaw[4];\n"
	"    for (int j = 0; j < 4; j++) {\n"
	"        float wDist = clamp((dFar[j] - dist) / max(1.0, dFar[j] - dNear[j]), 0.0, 1.0);\n"
	"        wRaw[j] = wDist * nd_octave_weight(1.0 / freqs[j], fw);\n"
	"    }\n"
	"    float wBoil = mod(anim_time * (0.055 * atmoGust * updraft), 512.0);\n"
	"    /* Calibrated 4D slice separation: intra-deck slices advance ~3.0 lattice cells in (z, w) so turret crowns\n"
	"     * evolve out of their bases with distinct 3D contours, while decks are offset by 23.0 / 15.7 cells. */\n"
	"    float zBaseSlice = hNorm * 38.0 + deckIdx * 23.0;\n"
	"    float wSlice     = hNorm * 22.0 + deckIdx * 15.7;\n"
	"    float dzDeck1 = (hNorm < 0.12) ? 1.2 : -1.4;\n"
	"    float dzDeck2 = (hNorm < 0.48) ? 1.1 : -1.3;\n"
	"    float dzDeck3 = (hNorm < 0.84) ? 1.0 : -1.5;\n"
	"    float dzMacro = (hNorm < 0.30) ? dzDeck1 : ((hNorm < 0.67) ? dzDeck2 : dzDeck3);\n"
	"    vec3 gradAcc = vec3(macroGrad * 0.085, dzMacro * 0.045);\n"
	"    float billowFbm = 0.0;\n"
	"    float turretShift = 0.0;\n"
	"    float sliceAngle = hNorm * 18.0 + deckIdx * 2.1;\n"
	"    vec2 sliceDir = vec2(cos(sliceAngle), sin(sliceAngle));\n"
	"    vec2 pRot = synQueryXY;\n"
	"    mat2 cur_rot = mat2(1.0, 0.0, 0.0, 1.0);\n"
	"    for (int j = 0; j < 4; j++) {\n"
	"        float wBand = (j < 2) ? (1.0 - wRaw[j + 2]) : 1.0;\n"
	"        float w     = wRaw[j] * wBand;\n"
	"        if (w > 0.002) {\n"
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
	"    /* 3D height-erosive sculpting: each deck is broad at its flat condensation base and erodes inward toward its crown,\n"
	"     * while turretShift displaces each altitude slice laterally along its 4D gradient so slices never stack identically. */\n"
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
	"    /* True 3D Curved-Torus Volumetric Cloud Projection:\n"
	"     * Each screen pixel reconstructs its 3D camera ray through the 3 concentric tropospheric cloud decks\n"
	"     * z in [zBase, zTop] = [380, 980] above the curved torus (radius r0 = 1,335,000), with zero\n"
	"     * camera-plane truncation and full hardware Z-buffer + back-face horizon occlusion! */\n"
	"    vec3 torusN = normalize(vTorusNormal);\n"
	"    vec3 tangentU = normalize(vTangentU);\n"
	"    vec3 tangentV = normalize(vTangentV);\n"
	"    float fragDist = max(1e-3, length(vViewDir));\n"
	"    vec3 viewDir = vViewDir / fragDist;\n"
	"    vec3 rayDir  = -viewDir;\n"
	"    float r0     = (uTorusRadii.y > 0.0) ? uTorusRadii.y : 1335000.0;\n"
	"    float vn     = dot(viewDir, torusN);\n"
	"    /* Back-face & horizon limb guard: never render clouds on the back-facing side of the torus tube! */\n"
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
	"    float zoomRamp = smoothstep(480.0, 2200.0, uCamDist);\n"
	"    float camClearRamp = zoomRamp;\n"
	"    if (camClearRamp <= 0.001) discard;\n"
	"    /* Exact 3D curved-torus shell intersection along viewDir from worldPos (vShellZ) through [zBase, zTop]:\n"
	"     * Solves |(r0 + vShellZ)*torusN + u*viewDir|^2 = (r0 + zProj)^2 without camera-plane clipping!\n"
	"     * Signed dz0/dz1 handles vShellZ anywhere below, inside, or above [zBase, zTop].\n"
	"     * Scale-adaptive orbital 3D vertical separation (effOrbitLift) keeps the 3 cloud decks visibly separated\n"
	"     * in 3D parallax from regional flight all the way to full-planet deep orbit. */\n"
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
	"    /* Evaluate true screen-space Nyquist filter width at the cloud slab midpoint */\n"
	"    float sMidSlab = 0.5 * (sEnter + sExit);\n"
	"    vec2 entryWorldPos = vWorldPos + dxy * sMidSlab;\n"
	"    float rawFw = max(1e-4, max(length(dFdx(entryWorldPos)), length(dFdy(entryWorldPos))));\n"
	"    float fw = clamp(rawFw, 0.05, 25000.0);\n"
	"    vec2 invChunkSpan = 1.0 / max(uChunkSpan, vec2(160.0));\n"
	"    vec3 rawBiome = sampleSubstrateSmooth(vUV).rgb;\n"
	"    float maxBio = max(rawBiome.r, max(rawBiome.g, rawBiome.b));\n"
	"    float minBio = min(rawBiome.r, min(rawBiome.g, rawBiome.b));\n"
	"    float wVolcanic = smoothstep(0.32, 0.14, maxBio);\n"
	"    float wPolar = smoothstep(0.52, 0.72, minBio) * smoothstep(0.18, 0.04, max(rawBiome.r, rawBiome.g) - rawBiome.b);\n"
	"    float wDesert = smoothstep(0.65, 0.85, rawBiome.r) * smoothstep(0.50, 0.70, rawBiome.g) * smoothstep(0.65, 0.45, rawBiome.b) * (1.0 - wPolar);\n"
	"    float wTropical = smoothstep(0.40, 0.65, rawBiome.g) * smoothstep(0.04, 0.35, rawBiome.g - rawBiome.r) * (1.0 - wPolar) * (1.0 - wDesert);\n"
	"    float wTemperate = clamp(1.0 - (wVolcanic + wPolar + wTropical + wDesert), 0.0, 1.0);\n"
	"    float texBioHumidity = clamp(0.52 * wTemperate + 0.80 * wTropical + 0.45 * wPolar + 0.15 * wVolcanic + 0.10 * wDesert + ((uSeaLevel > -900.0) ? 0.10 : 0.0), 0.08, 0.95);\n"
	"    float latPhi = 6.28318530718 * ((64.0 - entryWorldPos.y) / 8388608.0);\n"
	"    float latDeg = asin(clamp(abs(sin(latPhi)), 0.0, 1.0)) * 57.2957795;\n"
	"    float innerHole = max(0.0, -cos(latPhi));\n"
	"    float eqEvapDry = smoothstep(24.0, 0.0, latDeg) * 0.18;\n"
	"    float midLatWet = smoothstep(18.0, 0.0, abs(latDeg - 38.0)) * 0.12;\n"
	"    float subpolarWet = smoothstep(10.0, 0.0, abs(latDeg - 58.0)) * 0.08;\n"
	"    float worldBioHumidity = clamp(texBioHumidity - eqEvapDry + midLatWet + subpolarWet, 0.06, 0.92);\n"
	"    float bioHumidity = worldBioHumidity;\n"
	"    vec3 sDir = (length(uSunDir) > 0.001) ? normalize(uSunDir) : vec3(1.0, 0.0, 0.0);\n"
	"    float orbitLight = smoothstep(24000.0, 220000.0, uCamDist);\n"
	"    vec3 L_local = normalize(-0.577 * tangentU - 0.577 * tangentV + 0.816 * torusN);\n"
	"    vec3 sunLocal = vec3(dot(sDir, tangentU), dot(sDir, tangentV), dot(sDir, torusN));\n"
	"    vec3 L_day = (sunLocal.z > -0.08) ? normalize(vec3(sunLocal.xy, max(0.12, sunLocal.z))) : L_local;\n"
	"    vec3 sunDir = normalize(mix(L_day, normalize(sDir + 0.35 * torusN), orbitLight));\n"
	"    float sunDot = dot(torusN, sDir);\n"
	"    float innerEclipse = (sunDot > 0.0) ? (1.0 - 0.45 * innerHole * (1.0 - abs(sDir.z))) : 1.0;\n"
	"    float effSun = sunDot * innerEclipse;\n"
	"    float cloudDayFactor = clamp(0.16 + 0.84 * smoothstep(-0.16, 0.22, effSun), 0.16, 1.0);\n"
	"    vec2 sunHoriz = vec2(-0.7071, -0.7071);\n"
	"    /* 4-Step Asymmetric Deck-Stratified 3D Raymarching (1 step in Deck 3, 1 step in Deck 2, 2 steps in Deck 1):\n"
	"     * Gives the primary lower cumulus deck (Deck 1) a true 2-slice 3D base + eroded turret crown volume while\n"
	"     * keeping total steps at 4 (3x fewer than 12). Optical path stepLen is weighted by 0.5 on each Deck 1 half-step\n"
	"     * so integrated optical thickness is 100% invariant! */\n"
	"    float physMarchSpan = 600.0 / max(0.22, vn);\n"
	"    float baseStepLen = physMarchSpan / 3.0;\n"
	"    float trans = 1.0;\n"
	"    float rayGroundVis = 1.0;\n"
	"    float upperDeckOcc = 0.0;\n"
	"    vec3 accumLight = vec3(0.0);\n"
	"    float cosTheta = dot(rayDir, sunDir);\n"
	"    float hgForward = 0.62 + 1.55 * pow(clamp(0.5 + 0.5 * cosTheta, 0.0, 1.0), 5.0);\n"
	"    float hgBack    = 0.28 * pow(clamp(0.5 - 0.5 * cosTheta, 0.0, 1.0), 2.2);\n"
	"    float hgPhase   = hgForward + hgBack;\n"
	"    float ign       = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));\n"
	"    float jitter    = 0.5 + (ign - 0.5) * 0.22;\n"
	"    /* Scale-adaptive sun shadow step: 90 units up close, scaling up to 24 km in orbit so\n"
	"     * continental cyclone arms and upper cloud decks cast crisp 3D shadows visible from space! */\n"
	"    float sunStepDist = clamp(max(90.0, fw * 3.2), 90.0, 24000.0);\n"
	"    /* Sun-ward tower occlusion probe is evaluated lazily, only for pixels whose ray actually hits cloud density */\n"
	"    float baseTowerOcc = 0.0;\n"
	"    bool towerReady = false;\n"
	"    /* Wind column: the 5 flow noises are sampled once per pixel at the slab midpoint; per-deck Ekman veer/shear is analytic */\n"
	"    vec4 windSyn; vec4 windSfc;\n"
	"    nd_cloud_wind_column(entryWorldPos, uAnimTime, fw, windSyn, windSfc);\n"
	"    for (int s = 0; s < 4; s++) {\n"
	"        if (trans < 0.008 || rayGroundVis < 0.005) break;\n"
	"        int deckIdx = (s < 2) ? s : 2;\n"
	"        float uSub  = (s < 2) ? jitter : ((s == 2) ? (0.45 * jitter) : (0.50 + 0.45 * jitter));\n"
	"        float stepLen = (s < 2) ? baseStepLen : (baseStepLen * 0.5);\n"
	"        float hTopD = (deckIdx == 0) ? 1.00 : ((deckIdx == 1) ? 0.64 : 0.27);\n"
	"        float hBotD = (deckIdx == 0) ? 0.72 : ((deckIdx == 1) ? 0.35 : 0.00);\n"
	"        float hFrac = mix(hTopD, hBotD, uSub);\n"
	"        float z     = zBase + hFrac * deckSpan;\n"
	"        float zProj = z + hFrac * effOrbitLift;\n"
	"        float dzP   = zProj - vShellZ;\n"
	"        float dRP   = dzP * (2.0 * r0 + zProj + vShellZ);\n"
	"        float uRay  = dRP / max(1.0, sqrt(max(1.0, B_shell * B_shell + dRP)) + B_shell);\n"
	"        float sStep = -uRay;\n"
	"        vec2 sampleXY = vWorldPos + dxy * sStep;\n"
	"        /* Smooth per-pixel analytical mountain-cloud intersection using the 49x49 C2 Hermite heightfield:\n"
	"         * Eliminates all coarse LOD triangle polygon cutouts while allowing high alpine summits (up to 1,981 wu)\n"
	"         * to emerge smoothly through Deck 1 & Deck 2 and occlude lower clouds behind the ridge! */\n"
	"        vec2 stepUV = clamp(vUV + (dxy * sStep) * invChunkSpan, 0.0, 1.0);\n"
	"        float terrZ = sampleHeightCloud(stepUV);\n"
	"        float stepClear = smoothstep(terrZ - 24.0, terrZ + 110.0, z);\n"
	"        rayGroundVis *= smoothstep(terrZ - 36.0, terrZ + 28.0, z);\n"
	"        if (stepClear * rayGroundVis <= 0.002) continue;\n"
	"        vec4 cloudSample = eval_zoom_cloud_density_grad(vec3(sampleXY, z), uAnimTime, bioHumidity, uCamDist, fw, windSyn, windSfc);\n"
	"        float dens = cloudSample.x * camClearRamp * stepClear * rayGroundVis;\n"
	"        if (dens > 0.002) {\n"
	"            vec3 grad = cloudSample.yzw;\n"
	"            /* Scale-adaptive 3D sunward shadow probe (lazy) & analytical directional derivative along sun vector */\n"
	"            if (!towerReady) {\n"
	"                vec4 sunTowerCloud = eval_synoptic_macro_cloud_fw(entryWorldPos + sunHoriz * sunStepDist, uAnimTime, bioHumidity, max(fw * 1.5, 400.0));\n"
	"                baseTowerOcc = smoothstep(0.10, 0.72, sunTowerCloud.x);\n"
	"                towerReady = true;\n"
	"            }\n"
	"            float towerOcc = baseTowerOcc * (1.0 - 0.55 * hFrac);\n"
	"            /* Construct 3D volumetric normal in the local torus frame so NW sun illuminates cloud tops and NW rims */\n"
	"            vec3 cloudGradN = normalize(\n"
	"                (-grad.x * 28.0) * tangentU +\n"
	"                (-grad.y * 28.0) * tangentV +\n"
	"                (0.32 + 0.58 * hFrac - grad.z * 16.0) * torusN\n"
	"            );\n"
	"            float sunLambert = clamp(dot(cloudGradN, sunDir), 0.0, 1.0);\n"
	"            float hLocalS = clamp((hFrac - hBotD) / max(1e-3, hTopD - hBotD), 0.0, 1.0);\n"
	"            /* Directional derivative of density toward the sun: positive where the sun ray must traverse more cloud\n"
	"             * (leeward SE flanks, underbellies), negative on sun-facing NW flanks and upper crowns. */\n"
	"            float sunSlopeOcc = clamp(dot(grad.xy, sunHoriz) * 24.0 + grad.z * 14.0, -0.55, 1.10);\n"
	"            float selfOcc = smoothstep(0.08, 0.75, dens) * (1.0 - 0.72 * hLocalS);\n"
	"            /* Upper decks cast strong vertical inter-deck shadows (upperDeckOcc) onto lower decks! */\n"
	"            float tauSun = max(0.05, selfOcc * 1.15 + max(0.0, sunSlopeOcc) * 1.65 + towerOcc * 1.35 + upperDeckOcc * 2.60 + (1.0 - hFrac) * 0.55);\n"
	"            /* Three-zone solar scattering: direct sunlit crown / multiply-scattered interior / Rayleigh-blue shadowed underbelly */\n"
	"            float directSun    = exp(-tauSun * 0.95) * (0.35 + 0.65 * sunLambert);\n"
	"            float multiScatter = exp(-tauSun * 0.35) * (1.0 - exp(-dens * 3.2));\n"
	"            /* Thin-edge Mie silver lining: luminous rim only on optically thin margins facing the sun */\n"
	"            float thinEdge   = smoothstep(0.02, 0.14, dens) * (1.0 - smoothstep(0.22, 0.52, dens));\n"
	"            float silverLining = thinEdge * directSun * clamp(hgPhase, 0.0, 2.2) * 0.38;\n"
	"            vec3 deck1Sh = vec3(0.30, 0.38, 0.54);\n"
	"            vec3 deck2Sh = vec3(0.46, 0.54, 0.70);\n"
	"            vec3 deck3Sh = vec3(0.72, 0.80, 0.94);\n"
	"            vec3 shadowCol = (deckIdx == 2) ? deck1Sh : ((deckIdx == 1) ? deck2Sh : deck3Sh);\n"
	"            shadowCol *= (1.0 - 0.42 * clamp(upperDeckOcc, 0.0, 1.0));\n"
	"            vec3 scatterCol = vec3(0.84, 0.89, 0.96);\n"
	"            vec3 sunLitCol  = vec3(1.06, 1.00, 0.91);\n"
	"            vec3 stepCol = mix(shadowCol, scatterCol * (1.0 - 0.30 * clamp(upperDeckOcc, 0.0, 1.0)), clamp(multiScatter * 0.85, 0.0, 1.0));\n"
	"            stepCol = mix(stepCol, sunLitCol * (0.88 + 0.12 * clamp(hgPhase, 0.0, 2.0)), clamp(directSun * 1.20, 0.0, 1.0));\n"
	"            stepCol += sunLitCol * silverLining;\n"
	"            /* Per-deck optical extinction: translucent high ice cirrus (0.0065), structured mid altocumulus (0.0095), rich low cumulus (0.0115) */\n"
	"            float deckExt     = (deckIdx == 0) ? 0.0065 : ((deckIdx == 1) ? 0.0095 : 0.0115);\n"
	"            float stepTau     = pow(dens, 1.08) * (stepLen * deckExt);\n"
	"            float stepAlpha   = 1.0 - exp(-stepTau);\n"
	"            accumLight += trans * stepCol * stepAlpha;\n"
	"            trans *= (1.0 - stepAlpha);\n"
	"            upperDeckOcc += stepAlpha;\n"
	"        }\n"
	"    }\n"
	"    float rawAlpha = 1.0 - trans;\n"
	"    float cloudAlpha = clamp(rawAlpha, 0.0, 0.96);\n"
	"    if (cloudAlpha <= 0.003) discard;\n"
	"    vec3 cloudRgb = (accumLight / max(1e-4, rawAlpha)) * cloudDayFactor;\n"
	"    vec3 sky = ((length(uSkyColor) > 0.001) ? uSkyColor : vec3(0.55, 0.70, 0.85)) * (0.16 + 0.84 * cloudDayFactor);\n"
	"    float limb = pow(clamp(1.0 - max(0.0, dot(torusN, viewDir)), 0.0, 1.0), 3.0);\n"
	"    cloudRgb = mix(cloudRgb, vec3(0.45, 0.72, 0.98), limb * 0.42 * uAtmoIntensity);\n"
	"    float fogFactor = clamp((vDistance - uFogStart) / max(1.0, uFogEnd - uFogStart), 0.0, 1.0);\n"
	"    FragColor = vec4(mix(cloudRgb, sky, fogFactor * uAtmoIntensity), cloudAlpha);\n"
	"}\n";

const char *nd_seven_gl_vs_csm(void) { return s_vs_csm; }
const char *nd_seven_gl_fs_csm(void) { return s_fs_csm; }
const char *nd_seven_gl_vs_cloud(void) { return s_vs_cloud; }
const char *nd_seven_gl_fs_cloud(void) { return s_fs_cloud; }

/* --- Pure-C Torus Geometry & Camera Math --- */

static float s_grass_lamb_wx = 0.0f;
static float s_grass_lamb_wy = 0.0f;
static float s_grass_anim_time = 0.0f;

static inline uint32_t gl_chaff_hash3(int32_t px, int32_t py, int32_t pz, uint32_t seed);
static inline void gl_eval_recursive_wind(float wx, float wy, float anim_time,
                                          float *out_dir_x, float *out_dir_y,
                                          float *out_gust, float *out_catspaw);

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

	double u = 6.28318530717958647692 * (wx / wdim_x);
	double v = 6.28318530717958647692 * (wy / wdim_y);
	double cu = cos(u), su = sin(u);
	double cv = cos(v), sv = sin(v);
	double r = r0 + wz;

	if (out_pos) {
		out_pos[0] = cx + (R + r * cv) * cu;
		out_pos[1] = cy + (R + r * cv) * su;
		out_pos[2] = cz + r * sv;
	}
	if (out_E) {
		out_E[0] = (float)(-su);
		out_E[1] = (float)cu;
		out_E[2] = 0.0f;
	}
	if (out_S) {
		out_S[0] = (float)(-sv * cu);
		out_S[1] = (float)(-sv * su);
		out_S[2] = (float)cv;
	}
	if (out_N) {
		out_N[0] = (float)(cv * cu);
		out_N[1] = (float)(cv * su);
		out_N[2] = (float)sv;
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

static void eval_billboard_quad_with_origin(const lam_billboard_gpu_t *b,
                                            float pitch_deg,
                                            const nd_planet_def_t *pdef,
                                            const double ref_origin[3],
                                            nd_seven_gl_bb_vert_t out_verts[6])
{
	if (!b || !out_verts) return;

	double pos_d[3];
	float E[3], S[3], N[3];
	torus_frame_double((double)b->x, (double)b->y, (double)b->z, pdef, pos_d, E, S, N);

	float base_pos[3] = {
		(float)(pos_d[0] - (ref_origin ? ref_origin[0] : 0.0)),
		(float)(pos_d[1] - (ref_origin ? ref_origin[1] : 0.0)),
		(float)(pos_d[2] - (ref_origin ? ref_origin[2] : 0.0))
	};

	float pitch_rad = pitch_deg * (PI_F / 180.0f);
	float cos_p = cosf(pitch_rad);
	float sin_p = sinf(pitch_rad);

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

	float u_min = fminf(b->u0, b->u1);
	float u_max = fmaxf(b->u0, b->u1);
	float v_min = fminf(b->v0, b->v1);
	float v_max = fmaxf(b->v0, b->v1);
	float style_f = (float)b->style;

	struct { float dx, dy, u, v, loc_z, qx, qy; } c[4] = {
		{ -half_w, -ay,     b->u0, b->v1, b->z - wz_off,     -1.0f, 0.0f }, /* BL (0) */
		{  half_w, -ay,     b->u1, b->v1, b->z - wz_off,      1.0f, 0.0f }, /* BR (1) */
		{ -half_w, h - ay,  b->u0, b->v0, b->z + h - wz_off, -1.0f, 1.0f }, /* TL (2) */
		{  half_w, h - ay,  b->u1, b->v0, b->z + h - wz_off,  1.0f, 1.0f }  /* TR (3) */
	};

	/* Triangle 1: BL, BR, TL (indices 0, 1, 2) */
	/* Triangle 2: BR, TR, TL (indices 1, 3, 2) */
	int idx[6] = { 0, 1, 2, 1, 3, 2 };

	float grass_sx = 0.0f, grass_sy = 0.0f, grass_dz = 0.0f, grass_rustle = 0.0f;
	float flora_sway_scale = 0.0f;
	int is_woody_tree = 0;
	if (b->style == ND_BB_STYLE_GRASS) {
		flora_sway_scale = (b->height > 0.0f && b->height < 14.0f)
		                   ? fmaxf(0.36f, b->height * (1.0f / 14.0f))
		                   : 1.0f;
	} else if (b->style == ND_BB_STYLE_LEAF_TINT || b->style == ND_BB_STYLE_FLORA) {
		if (b->pad0 == 1u && b->height > 0.0f) {
			/* Non-trunk real-object plants (daisies, cattails, flax, wild carrot, nightshade, bushes):
			 * full ground-pinned top-vertex wind & contact sway scaled to plant height so their
			 * angular bend matches the surrounding meadow grass */
			float h_norm = fminf(1.25f, fmaxf(0.55f, b->height * (1.0f / 26.0f)));
			flora_sway_scale = 0.92f * h_norm;
		} else if (b->pad0 == 0u && b->height > 0.0f && b->height <= 30.0f) {
			/* Fallback for untagged small shrub/flora billboards */
			float size_t = (30.0f - b->height) * (1.0f / 18.0f);
			if (size_t < 0.0f) size_t = 0.0f;
			if (size_t > 1.0f) size_t = 1.0f;
			flora_sway_scale = 0.52f + 0.33f * size_t;
		} else if (b->pad0 == 2u && b->height > 0.0f) {
			/* Woody trunked trees (oaks, birches, pines, firs, willows, maples, olives):
			 * homogeneous ground-pinned quad shear evaluated once at the tree root (b->x, b->y)
			 * with heavy woody stiffness, slow pendulum period, and wind-only response */
			float h_norm = fminf(1.30f, fmaxf(0.75f, b->height * (1.0f / 80.0f)));
			flora_sway_scale = 1.05f * h_norm;
			is_woody_tree = 1;
		}
	}
	if (flora_sway_scale > 0.0f) {
		if (is_woody_tree) {
			float wDirX = 0.7071f, wDirY = -0.7071f, gust = 1.0f, catspaw = 0.5f;
			gl_eval_recursive_wind(b->x, b->y, s_grass_anim_time, &wDirX, &wDirY, &gust, &catspaw);
			uint32_t th = gl_chaff_hash3((int32_t)floorf(b->x), (int32_t)floorf(b->y), 0, 0x54524545u);
			float phase = (float)(th & 0xFFFFu) * (TWO_PI_F / 65536.0f);
			float stiff = 0.92f + 0.16f * ((float)((th >> 16u) & 0xFFu) / 255.0f);
			float wind_proj = b->x * wDirX + b->y * wDirY;
			float slow_sway = sinf(s_grass_anim_time * ND_TREE_TRUNK_SWAY_FREQ_RAD - wind_proj * 0.024f + phase * 0.25f) * (0.10f + 0.12f * gust);
			float bough_nod = sinf(s_grass_anim_time * ND_TREE_BOUGH_NOD_FREQ_RAD - wind_proj * 0.038f + phase * 0.20f) * (0.04f + 0.06f * catspaw);
			float wind_mag = (ND_TREE_WIND_BASE_LEAN + ND_TREE_WIND_GUST_SCALE * gust + ND_TREE_WIND_CATSPAW_SCALE * catspaw + slow_sway + bough_nod) * stiff * flora_sway_scale;
			grass_sx = wDirX * wind_mag + (-wDirY) * (bough_nod * 0.10f * flora_sway_scale);
			grass_sy = wDirY * wind_mag + ( wDirX) * (bough_nod * 0.10f * flora_sway_scale);
			grass_dz = 0.0f;
			grass_rustle = 0.04f * catspaw;
		} else {
			nd_seven_gl_eval_grass_displacement(b->x, b->y, s_grass_anim_time,
			                                    s_grass_lamb_wx, s_grass_lamb_wy,
			                                    &grass_sx, &grass_sy, &grass_dz, &grass_rustle);
			grass_sx *= flora_sway_scale;
			grass_sy *= flora_sway_scale;
			grass_dz *= (b->style == ND_BB_STYLE_GRASS) ? flora_sway_scale : (flora_sway_scale * 0.45f);
			grass_rustle *= fminf(1.0f, flora_sway_scale);
		}

		/* Prevent vertical screen-height collapse (-sy * sin_p - dz * cos_p) on short scaled meadow tufts
		 * so trampled or wind-bent tufts always retain at least 60% of their vertical screen height */
		if (h > 0.0f) {
			float max_vert_loss = 0.40f * h;
			float vert_loss = grass_sy * sin_p + grass_dz * cos_p;
			if (vert_loss > max_vert_loss && vert_loss > 1e-4f) {
				float keep = max_vert_loss / vert_loss;
				grass_sy *= keep;
				grass_dz *= keep;
			}
		}
	}

	for (int i = 0; i < 6; i++) {
		int k = idx[i];
		float tip_mask = (flora_sway_scale > 0.0f && c[k].qy > 0.5f) ? 1.0f : 0.0f;
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
		out_verts[i].local_pos[1] = b->y - c[k].dy / (sin_p > 0.1f ? sin_p : 0.819152f) + sy;
		out_verts[i].local_pos[2] = c[k].loc_z - dz;

		out_verts[i].quad_uv[0] = c[k].qx;
		out_verts[i].quad_uv[1] = c[k].qy;
		out_verts[i].quad_uv[2] = b->z;
		out_verts[i].quad_uv[3] = style_f;

		out_verts[i].sprite_bounds[0] = u_min;
		out_verts[i].sprite_bounds[1] = v_min;
		out_verts[i].sprite_bounds[2] = u_max;
		out_verts[i].sprite_bounds[3] = v_max;
	}
}

void nd_seven_gl_eval_billboard_quad(const lam_billboard_gpu_t *b,
                                     float pitch_deg,
                                     const nd_planet_def_t *pdef,
                                     nd_seven_gl_bb_vert_t out_verts[6])
{
	eval_billboard_quad_with_origin(b, pitch_deg, pdef, NULL, out_verts);
}

static void eval_billboard_shadow_quad_with_origin(const lam_billboard_gpu_t *b,
                                                   const nd_planet_def_t *pdef,
                                                   const double ref_origin[3],
                                                   nd_seven_gl_bb_vert_t out_verts[6])
{
	if (!b || !out_verts) return;

	double pos_d[3];
	float E[3], S[3], N[3];
	torus_frame_double((double)b->x, (double)b->y, (double)b->z, pdef, pos_d, E, S, N);

	float base_pos[3] = {
		(float)(pos_d[0] - (ref_origin ? ref_origin[0] : 0.0)),
		(float)(pos_d[1] - (ref_origin ? ref_origin[1] : 0.0)),
		(float)(pos_d[2] - (ref_origin ? ref_origin[2] : 0.0))
	};

	float rx = (b->shadow_rx > 0) ? (float)b->shadow_rx : fmaxf(3.5f, b->width * 0.34f);
	float ry = (b->shadow_ry > 0) ? (float)b->shadow_ry : fmaxf(2.0f, rx * 0.45f);

	/* Shift shadow slightly Southeast (+E, +S) away from the Northwest directional sun */
	float off_x = rx * 0.22f;
	float off_y = ry * 0.18f;
	float lift_z = 0.22f;

	float alpha = (float)(b->tint_rgba & 0xFF) / 255.0f;

	struct { float dx, dy, qx, qy; } c[4] = {
		{ off_x - rx, off_y - ry, -1.0f, -1.0f }, /* NW (0) */
		{ off_x + rx, off_y - ry,  1.0f, -1.0f }, /* NE (1) */
		{ off_x - rx, off_y + ry, -1.0f,  1.0f }, /* SW (2) */
		{ off_x + rx, off_y + ry,  1.0f,  1.0f }  /* SE (3) */
	};

	int idx[6] = { 0, 1, 2, 1, 3, 2 };

	for (int i = 0; i < 6; i++) {
		int k = idx[i];
		out_verts[i].world_pos[0] = base_pos[0] + c[k].dx * E[0] + c[k].dy * S[0] + lift_z * N[0];
		out_verts[i].world_pos[1] = base_pos[1] + c[k].dx * E[1] + c[k].dy * S[1] + lift_z * N[1];
		out_verts[i].world_pos[2] = base_pos[2] + c[k].dx * E[2] + c[k].dy * S[2] + lift_z * N[2];

		out_verts[i].uv[0] = 0.0f;
		out_verts[i].uv[1] = 0.0f;

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
		out_verts[i].quad_uv[3] = -1.0f; /* -1.0f marks 3D ground contact shadow quad */

		out_verts[i].sprite_bounds[0] = 0.0f;
		out_verts[i].sprite_bounds[1] = 0.0f;
		out_verts[i].sprite_bounds[2] = 1.0f;
		out_verts[i].sprite_bounds[3] = 1.0f;
	}
}

void nd_seven_gl_eval_billboard_shadow_quad(const lam_billboard_gpu_t *b,
                                            const nd_planet_def_t *pdef,
                                            nd_seven_gl_bb_vert_t out_verts[6])
{
	eval_billboard_shadow_quad_with_origin(b, pdef, NULL, out_verts);
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
	GLint aGridUV;
} gl_cloud_pipe_t;

typedef struct {
	GLuint prog;
	GLint uCsm;
	GLint uAnimTime;
	GLint uSeaWet;
} gl_csm_pipe_t;

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
	gl_csm_pipe_t csm_pipe;
	GLuint csm_tex;
	GLuint csm_fbo;
	bool csm_failed;
	bool csm_valid;
	nd_seven_gl_csm_window_t csm_win;
	uint32_t stage_w;
	uint32_t stage_h;

	/* Scene render target: MSAA colour+depth renderbuffers; the resolve FBO holds sampleable colour+depth snapshots */
	GLuint scene_fbo;
	GLuint scene_rb_color;
	GLuint scene_rb_depth;
	GLuint res_fbo;
	GLuint res_color_tex;
	GLuint res_depth_tex;
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
	uint32_t atlas_ver;

	gl_chunk_tex_t chunk_cache[MAX_CHUNK_TEXTURES];
	uint32_t frame_count;

	gl_water_chunk_t water_queue[MAX_WATER_QUEUE];
	uint32_t water_count;
	gl_water_chunk_t cloud_queue[MAX_WATER_QUEUE];
	uint32_t cloud_count;

	float cur_mvp[16];
	float cur_eye[3];
	float cur_target[3];
	float cur_up[3];
	double cur_origin[3];
	float cur_tx;
	float cur_ty;
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
	glBufferData(GL_ARRAY_BUFFER, NULL, MAX_BB_VERTS * sizeof(nd_seven_gl_bb_vert_t), GL_DYNAMIC_DRAW);
}

static void init_pipelines(void)
{
	s_gl.terrain_pipe.prog = create_program_async(s_vs_terrain, s_fs_terrain);
	s_gl.water_pipe.prog   = create_program_async(s_vs_water, s_fs_water);
	s_gl.bb_pipe.prog      = create_program_async(s_vs_billboard, s_fs_billboard);
	s_gl.cloud_pipe.prog   = create_program_async(s_vs_cloud, s_fs_cloud);
	s_gl.csm_pipe.prog     = create_program_async(s_vs_csm, s_fs_csm);
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
			s_gl.cloud_pipe.uTorusRadii     = glGetUniformLocation(p, "uTorusRadii", 11);
			s_gl.cloud_pipe.uWorldDim       = glGetUniformLocation(p, "uWorldDim", 9);
			s_gl.cloud_pipe.uFogStart       = glGetUniformLocation(p, "uFogStart", 9);
			s_gl.cloud_pipe.uFogEnd         = glGetUniformLocation(p, "uFogEnd", 7);
			s_gl.cloud_pipe.uAnimTime       = glGetUniformLocation(p, "uAnimTime", 9);
			s_gl.cloud_pipe.uSkyColor       = glGetUniformLocation(p, "uSkyColor", 9);
			s_gl.cloud_pipe.uAtmoIntensity  = glGetUniformLocation(p, "uAtmoIntensity", 14);
			s_gl.cloud_pipe.uCamDist        = glGetUniformLocation(p, "uCamDist", 8);
			s_gl.cloud_pipe.uCloudDeck      = glGetUniformLocation(p, "uCloudDeck", 10);
			s_gl.cloud_pipe.uSunDir         = glGetUniformLocation(p, "uSunDir", 7);
			s_gl.cloud_pipe.aGridUV         = glGetAttribLocation(p, "a_grid_uv", 9);
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
	s_gl.cur_lamb_light[0] = wx;
	s_gl.cur_lamb_light[1] = wy;
	s_gl.cur_lamb_light[2] = wz;
	s_gl.cur_lamb_light[3] = (intensity > 0.0f) ? intensity : 0.0f;
	s_grass_lamb_wx = wx;
	s_grass_lamb_wy = wy;
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
	if (!out || dist > 14000.0f) return false;
	float need = 4.5f * dist + 2000.0f;
	float half = 4096.0f;
	while (half < need && half < 65536.0f) half *= 2.0f;
	float texel = (2.0f * half) / (float)ND_CSM_RES;
	out->half = half;
	out->cx = floorf(tx / texel + 0.5f) * texel;
	out->cy = floorf(ty / texel + 0.5f) * texel;
	return true;
}

static void gl_free_scene_targets(void)
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	if (s_gl.scene_fbo) glDeleteFramebuffer(s_gl.scene_fbo);
	if (s_gl.res_fbo) glDeleteFramebuffer(s_gl.res_fbo);
	if (s_gl.scene_rb_color) glDeleteRenderbuffer(s_gl.scene_rb_color);
	if (s_gl.scene_rb_depth) glDeleteRenderbuffer(s_gl.scene_rb_depth);
	if (s_gl.res_color_tex) glDeleteTexture(s_gl.res_color_tex);
	if (s_gl.res_depth_tex) glDeleteTexture(s_gl.res_depth_tex);
	s_gl.scene_fbo = s_gl.res_fbo = 0;
	s_gl.scene_rb_color = s_gl.scene_rb_depth = 0;
	s_gl.res_color_tex = s_gl.res_depth_tex = 0;
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
	return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

/* (Re)create the scene targets for the stage size; falls back to single-sampled, then to the canvas itself. */
static void gl_ensure_scene_targets(uint32_t w, uint32_t h)
{
	if (s_gl.scene_failed || w == 0 || h == 0) return;
	if (s_gl.scene_ok && s_gl.scene_w == w && s_gl.scene_h == h) return;
	if (s_gl.scene_fbo || s_gl.res_fbo) gl_free_scene_targets();
	static const int try_samples[2] = { ND_SCENE_MSAA_SAMPLES, 0 };
	for (int i = 0; i < 2; i++) {
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
	/* The sprite marker lives in the scene alpha: the canvas must end up fully opaque */
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
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

/* One frame-wide evaluation of the cloud density on the cloud slice (replaces per-pixel shadow evaluation). */
static void gl_render_cloud_shadow_map(void)
{
	s_gl.csm_valid = false;
	if (!s_gl.csm_pipe.prog || !nd_seven_gl_csm_window(s_gl.cur_dist, s_gl.cur_tx, s_gl.cur_ty, &s_gl.csm_win))
		return;
	if (!gl_ensure_csm_target()) return;
	glBindFramebuffer(GL_FRAMEBUFFER, s_gl.csm_fbo);
	glViewport(0, 0, ND_CSM_RES, ND_CSM_RES);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glUseProgram(s_gl.csm_pipe.prog);
	glUniform3f(s_gl.csm_pipe.uCsm, s_gl.csm_win.cx, s_gl.csm_win.cy, s_gl.csm_win.half);
	glUniform1f(s_gl.csm_pipe.uAnimTime, s_gl.cur_anim_time);
	glUniform1f(s_gl.csm_pipe.uSeaWet, (s_gl.cur_sea_level > -900.0f) ? 1.0f : 0.0f);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	gl_bind_scene_target();
	glEnable(GL_DEPTH_TEST);
	s_gl.csm_valid = true;
}

/* Bind the cloud-shadow map (texture unit 7) and window to a program; half = 0 tells the shader no map is available. */
static void gl_bind_csm(GLint loc_tex, GLint loc_csm)
{
	glActiveTexture(GL_TEXTURE0 + 7);
	glBindTexture(GL_TEXTURE_2D, s_gl.csm_valid ? s_gl.csm_tex : 0);
	glUniform1i(loc_tex, 7);
	if (s_gl.csm_valid)
		glUniform3f(loc_csm, s_gl.csm_win.cx, s_gl.csm_win.cy, s_gl.csm_win.half);
	else
		glUniform3f(loc_csm, 0.0f, 0.0f, 0.0f);
	glActiveTexture(GL_TEXTURE0);
}

static float gl_smoothstep01(float e0, float e1, float x)
{
	float t = (x - e0) / (e1 - e0);
	t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
	return t * t * (3.0f - 2.0f * t);
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
	s_gl.cur_sea_level = pdef ? (float)nd_field_water_level(pdef->world_id) : -999.0f;
	s_gl.cur_base_sea_level = s_gl.cur_sea_level;
	s_gl.focal_sea_level = -999.0f;
	nd_seven_gl_eval_celestial_dirs(anim_time, s_gl.cur_sun_dir, s_gl.cur_moon_dir);
	s_gl.cur_dist = (dist > 1.0f) ? dist : ND_SEVEN_CAM_DIST;
	s_gl.cur_pitch_deg = pitch_deg;
	s_gl.cur_anim_time = anim_time;
	s_gl.cur_tx = tx;
	s_gl.cur_ty = ty;
	s_grass_anim_time = anim_time;
	s_grass_lamb_wx = tx;
	s_grass_lamb_wy = ty;
	if (pdef) s_gl.cur_pdef = *pdef;

	/* Evaluate double-precision reference origin at (tx, ty, 0) to eliminate 32-bit float cancellation */
	float E[3], S[3], N[3];
	torus_frame_double((double)tx, (double)ty, 0.0, pdef, s_gl.cur_origin, E, S, N);

	double u0 = 6.28318530717958647692 * ((double)tx / (double)ND_SEVEN_WORLD_DIM_W);
	double v0 = 6.28318530717958647692 * ((double)ty / (double)ND_SEVEN_WORLD_DIM_H);
	s_gl.cur_ref_trig[0] = (float)cos(u0);
	s_gl.cur_ref_trig[1] = (float)sin(u0);
	s_gl.cur_ref_trig[2] = (float)cos(v0);
	s_gl.cur_ref_trig[3] = (float)sin(v0);

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
	s_gl.cur_near = near_z;
	s_gl.cur_far = far_z;
	s_gl.cur_tan_half = tanf(fov_rad * 0.5f);
	s_gl.cur_aspect = aspect;
	ar_mat4_multiply(s_gl.cur_mvp, proj, modelview);

	if (!s_gl.initialized) init_pipelines();
	(void)check_pipelines_ready();

	s_gl.stage_w = stage_w;
	s_gl.stage_h = stage_h;
	s_gl.frame_open = true;
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
	else
		s_gl.csm_valid = false;
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
	if (terrain_sea_lvl > -900.0f) {
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

	/* Queue volumetric cloud shell pass for this chunk */
	if (s_gl.cloud_count < MAX_WATER_QUEUE) {
		s_gl.cloud_queue[s_gl.cloud_count].ox = ox;
		s_gl.cloud_queue[s_gl.cloud_count].oy = oy;
		s_gl.cloud_queue[s_gl.cloud_count].span_w = span_w;
		s_gl.cloud_queue[s_gl.cloud_count].span_h = span_h;
		s_gl.cloud_queue[s_gl.cloud_count].level = level;
		s_gl.cloud_queue[s_gl.cloud_count].sea_level = terrain_sea_lvl;
		s_gl.cloud_queue[s_gl.cloud_count].height_tex = slot->height_tex;
		s_gl.cloud_queue[s_gl.cloud_count].substrate_tex = slot->substrate_tex;
		s_gl.cloud_queue[s_gl.cloud_count].rel_center[0] = rel_center[0];
		s_gl.cloud_queue[s_gl.cloud_count].rel_center[1] = rel_center[1];
		s_gl.cloud_queue[s_gl.cloud_count].rel_center[2] = rel_center[2];
		s_gl.cloud_queue[s_gl.cloud_count].major_r = eff_pdef->major_r;
		s_gl.cloud_queue[s_gl.cloud_count].minor_r = eff_pdef->minor_r;
		s_gl.cloud_count++;
	}

	/* Queue the water surface of this chunk for the submerged-object pass */
	if (terrain_sea_lvl > -900.0f && s_gl.water_count < MAX_WATER_QUEUE) {
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

void nd_seven_gl_flush_clouds(void)
{
	if (!s_gl.initialized || !s_gl.pipelines_ready || !s_gl.cloud_pipe.prog || s_gl.cloud_count == 0 ||
	    s_gl.cur_dist <= 480.0f) {
		s_gl.cloud_count = 0;
		return;
	}

	glUseProgram(s_gl.cloud_pipe.prog);

	/* Keep hardware depth test (GL_LEQUAL) enabled so the solid front ring of the torus
	 * properly occludes cloud shells on the far side of the torus at orbital min-zoom! */
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_FALSE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);

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
		/* Place proxy shell above the highest mountain peaks (up to +1,981 wu, eff_shell_z = 2,150 wu)
		 * while keeping it strictly below 58% of the camera's altitude so it can never approach
		 * the camera's near clipping plane AND never get Z-clipped by coarse terrain triangles! */
		float deck_base = 420.0f;
		float deck_span = 2400.0f;
		float max_safe_z = s_gl.cur_lamb_light[2] + s_gl.cur_dist * 0.58f;
		float eff_shell_z = fminf(2150.0f, fmaxf(24.0f, max_safe_z));
		glUniform4f(s_gl.cloud_pipe.uCloudDeck, deck_base, deck_span, s_gl.cur_dist, eff_shell_z);
	}

	glUniform3f(s_gl.cloud_pipe.uSkyColor,
	            0.55f * (1.0f - space_t) + 0.012f * space_t,
	            0.70f * (1.0f - space_t) + 0.016f * space_t,
	            0.85f * (1.0f - space_t) + 0.035f * space_t);
	glUniform1f(s_gl.cloud_pipe.uAtmoIntensity, fminf(1.0f, fmaxf(0.0f, (s_gl.cur_dist - 1200.0f) / 8000.0f)));

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
		glUniform2f(s_gl.cloud_pipe.uTorusRadii, chk->major_r, chk->minor_r);
		glUniform2f(s_gl.cloud_pipe.uChunkOrigin, (float)chk->ox, (float)chk->oy);
		glUniform2f(s_gl.cloud_pipe.uChunkRelOrigin,
		            (float)((double)chk->ox - (double)s_gl.cur_tx),
		            (float)((double)chk->oy - (double)s_gl.cur_ty));
		glUniform2f(s_gl.cloud_pipe.uChunkSpan, (float)chk->span_w, (float)chk->span_h);
		glUniform1f(s_gl.cloud_pipe.uSeaLevel, chk->sea_level);

		glDrawElements(GL_TRIANGLES, c_indices, GL_UNSIGNED_SHORT, 0);
	}

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
	if ((!s_gl.atlas_tex || s_gl.atlas_ver != cur_ver) && atlas_rgba) {
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
		s_gl.atlas_ver = cur_ver;
	}

	static nd_seven_gl_bb_vert_t bb_verts[MAX_BB_VERTS];
	bool emit_shadows = (s_gl.cur_dist < 16000.0f);

	glUseProgram(s_gl.bb_pipe.prog);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, s_gl.atlas_tex);
	glUniform1i(s_gl.bb_pipe.uAtlasTex, 0);
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

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(nd_seven_gl_bb_vert_t), 0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(nd_seven_gl_bb_vert_t), (size_t)12);
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(nd_seven_gl_bb_vert_t), (size_t)20);
	glEnableVertexAttribArray(3);
	glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(nd_seven_gl_bb_vert_t), (size_t)36);
	glEnableVertexAttribArray(4);
	glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(nd_seven_gl_bb_vert_t), (size_t)48);
	glEnableVertexAttribArray(5);
	glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(nd_seven_gl_bb_vert_t), (size_t)64);

	/* Sprites are drawn in the same depth buffer as terrain and water:
	 *   pass 0 (core)   opaque pixels (alpha >= 0.5): depth write + stencil mark, so the water pass can refract and
	 *                   absorb exactly the pixels that stand below the surface;
	 *   pass 1 (fringe) anti-aliased edges of the same sprites (alpha < 0.5), blended, no depth write;
	 *   pass 2 (other)  ground contact shadows, glow auras and spray particles, blended, no depth write.
	 * Core and "other" quads are collected in separate arrays so each pass draws its quads in submission (painter) order. */
	static nd_seven_gl_bb_vert_t other_verts[MAX_BB_OTHER_VERTS];
	uint32_t core_n = 0, other_n = 0;
	for (uint32_t i = 0; i <= count; i++) {
		bool last = (i == count);
		bool is_other = false, want_shadow = false;
		uint32_t need_core = 0, need_other = 0;
		if (!last) {
			is_other = (batch[i].style == ND_BB_STYLE_GLOW) || (batch[i].style == ND_BB_STYLE_PARTICLE);
			want_shadow = emit_shadows && !is_other &&
			              (batch[i].style != ND_BB_STYLE_GRASS) &&
			              (batch[i].width >= 8.0f && batch[i].height >= 8.0f);
			need_core = is_other ? 0u : 6u;
			need_other = (is_other ? 6u : 0u) + (want_shadow ? 6u : 0u);
		}
		if (last || core_n + need_core > MAX_BB_VERTS || other_n + need_other > MAX_BB_OTHER_VERTS) {
			if (core_n > 0) {
				glBufferData(GL_ARRAY_BUFFER, bb_verts, core_n * sizeof(nd_seven_gl_bb_vert_t), GL_DYNAMIC_DRAW);
				/* pass 0: core */
				glUniform1i(s_gl.bb_pipe.uPass, 0);
				glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
				glDisable(GL_BLEND);
				glDepthMask(GL_TRUE);
				glEnable(GL_STENCIL_TEST);
				glStencilMask(0xFF);
				glStencilFunc(GL_ALWAYS, 1, 0xFF);
				glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
				glDrawArrays(GL_TRIANGLES, 0, (GLsizei)core_n);
				glDisable(GL_STENCIL_TEST);
				/* pass 1: anti-aliased fringe */
				glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
				glUniform1i(s_gl.bb_pipe.uPass, 1);
				glEnable(GL_BLEND);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				glDepthMask(GL_FALSE);
				glDrawArrays(GL_TRIANGLES, 0, (GLsizei)core_n);
			}
			if (other_n > 0) {
				glBufferData(GL_ARRAY_BUFFER, other_verts,
				             other_n * sizeof(nd_seven_gl_bb_vert_t), GL_DYNAMIC_DRAW);
				glUniform1i(s_gl.bb_pipe.uPass, 2);
				glEnable(GL_BLEND);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				glDepthMask(GL_FALSE);
				glDrawArrays(GL_TRIANGLES, 0, (GLsizei)other_n);
			}
			core_n = 0;
			other_n = 0;
		}
		if (last) break;
		if (want_shadow) {
			eval_billboard_shadow_quad_with_origin(&batch[i], &s_gl.cur_pdef, s_gl.cur_origin,
			                                       &other_verts[other_n]);
			other_n += 6u;
		}
		if (is_other) {
			eval_billboard_quad_with_origin(&batch[i], s_gl.cur_pitch_deg, &s_gl.cur_pdef, s_gl.cur_origin,
			                                &other_verts[other_n]);
			other_n += 6u;
		} else {
			eval_billboard_quad_with_origin(&batch[i], s_gl.cur_pitch_deg, &s_gl.cur_pdef, s_gl.cur_origin,
			                                &bb_verts[core_n]);
			core_n += 6u;
			if (bb_sea_level > -900.0f && batch[i].z < bb_sea_level + 1.0f)
				s_gl.sprites_in_water = true;
		}
	}

	glDisableVertexAttribArray(1);
	glDisableVertexAttribArray(2);
	glDisableVertexAttribArray(3);
	glDisableVertexAttribArray(4);
	glDisableVertexAttribArray(5);
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
	float stiff = 0.86f + 0.28f * ((float)((h >> 16u) & 0xFFu) / 255.0f);

	float wind_proj = wx * wDirX + wy * wDirY;
	float cross_proj = wx * (-wDirY) + wy * wDirX;
	float gust_wave = sinf(anim_time * ND_GRASS_WAVE_FREQ_RAD - wind_proj * 0.035f + phase * 0.16f) * (ND_GRASS_WAVE_AMP * (0.60f + 0.40f * gust));
	float flutter = sinf(anim_time * ND_GRASS_FLUTTER_FREQ_RAD - wind_proj * 0.055f + cross_proj * 0.025f + phase * 0.20f) * (ND_GRASS_FLUTTER_AMP * (0.50f + 0.50f * catspaw));
	float wind_mag = (ND_GRASS_WIND_BASE_LEAN + ND_GRASS_WIND_GUST_SCALE * (gust - 0.70f > 0.0f ? gust - 0.70f : 0.0f) + ND_GRASS_WIND_CATSPAW_SCALE * catspaw + gust_wave + flutter) * stiff;
	if (wind_mag < 0.0f) wind_mag = 0.0f;
	float sx = wDirX * wind_mag + (-wDirY) * (flutter * 0.12f);
	float sy = wDirY * wind_mag + ( wDirX) * (flutter * 0.12f);
	float dz = (wind_mag * wind_mag) * 0.06f;
	float rustle = 0.05f * catspaw;

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
	if (maxFront <= (0.510f - 0.125f * humidity) && (!out_high_cirrus || (synFront + 0.176f) <= (0.42f - 0.10f * humidity))) {
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
		float wCir = gl_smoothstep(0.32f, 0.08f, 0.00000380f * fw);
		float pIrotX = 0.8f * pZonalX - 0.6f * pZonalY;
		float pIrotY = 0.6f * pZonalX + 0.8f * pZonalY;
		float cir_v = 0.5f, cir_gx = 0.0f, cir_gy = 0.0f;
		if (wCir > 0.001f) {
			gl_chaff_grad_noise2d(pIrotX * 0.00000380f - prevDirX * (phase1 * 1.45f),
			                      pIrotY * 0.00000380f - prevDirY * (phase1 * 1.45f),
			                      0x43495252u, &cir_v, &cir_gx, &cir_gy);
		}
		float cirStreak = 1.0f - 2.0f * fabsf(cir_v - 0.5f);
		highCirrus = gl_smoothstep(0.42f - 0.10f * humidity, 0.70f - 0.06f * humidity, synFront + (cirStreak - 0.45f) * 0.32f * wCir) *
		             ((1.0f - wCir) * 0.65f + wCir * gl_smoothstep(0.24f, 0.76f, cirStreak));
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

	float synRegime = gl_smoothstep(0.510f - 0.125f * humidity, 0.685f - 0.075f * humidity, synFront);
	float macroCoverage = synRegime;

	/* Multi-regime weather hierarchy: Continental Overcast Shields vs Frontal Broken Decks vs Fair Cumulus */
	float overcastShield = gl_smoothstep(0.62f, 0.86f, synRegime);
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
		macroCoverage = (1.0f - wMeso0) * synRegime + wMeso0 * structuredCov;
	} else {
		float orbitArm = gl_smoothstep(0.32f, 0.70f, r0);
		macroCoverage = synRegime * ((1.0f - overcastShield) * (0.72f + 0.28f * orbitArm) + overcastShield);
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
		float altoPosX = 0.60f * synQueryX + 0.80f * synQueryY + synWarp0X * 0.18f + 42000.0f;
		float altoPosY = -0.80f * synQueryX + 0.60f * synQueryY + synWarp0Y * 0.18f - 31000.0f;
		float alto_v = 0.55f, alto_gx = 0.0f, alto_gy = 0.0f;
		if (wAlto > 0.001f) {
			gl_chaff_grad_noise2d(altoPosX * 0.00001600f - atmoDirX * altoPhase,
			                      altoPosY * 0.00001600f - atmoDirY * altoPhase,
			                      0x414C544Fu, &alto_v, &alto_gx, &alto_gy);
		}
		float altoRibs = (1.0f - wAlto) * 0.62f + wAlto * gl_smoothstep(0.30f, 0.72f, 1.0f - 2.0f * fabsf(alto_v - 0.5f));
		altoShelf = altoShelfProf * gl_smoothstep(0.14f, 0.56f, contFront - 0.36f) * altoRibs * 0.84f;
	}
	float deepCore  = gl_smoothstep(0.62f, 0.92f, coreStrength);
	float towerTopH = 0.24f + (0.86f - 0.24f) * (deepCore * (0.45f + 0.55f * updraft));
	float towerProf = gl_smoothstep(0.12f, 0.24f, hNorm) * (1.0f - gl_smoothstep(towerTopH - 0.10f, towerTopH, hNorm)) * deepCore;
	float deck2Cov  = fmaxf(macroCoverage * towerProf, altoShelf);

	/* 3. Deck 3: downwind anvil plumes & jet-stream cirrus (reuses single synCloud evaluation at synQueryXY) */
	float anvilProf = gl_smoothstep(0.72f, 0.81f, hNorm) * (1.0f - gl_smoothstep(0.92f, 1.0f, hNorm));
	float anvilCov  = gl_smoothstep(0.38f, 0.78f, macroCoverage) * (0.55f + 0.45f * updraft) * 0.82f;
	float deck3Cov  = fmaxf(anvilCov, highCirrus * 0.78f) * anvilProf;

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
	/* Solar ecliptic longitude (180s = 3m orbital day/night cycle, 23.44 deg axial obliquity,
	 * +1.20 rad initial phase so the day/night terminator is visible across the torus ring from t=0) */
	float lambda_sun = TWO_PI_F * (t_sec / ND_ASTRO_SOLAR_DAY_SEC) + 1.20f;
	float obliq = 23.43928f * (PI_F / 180.0f);
	float sx = cosf(lambda_sun);
	float sy = sinf(lambda_sun);
	float sz = sinf(obliq) * sy;
	float slen = sqrtf(sx * sx + sy * sy + sz * sz + 1e-8f);
	if (out_sun_dir) {
		out_sun_dir[0] = sx / slen;
		out_sun_dir[1] = sy / slen;
		out_sun_dir[2] = sz / slen;
	}

	/* Lunar orbital longitude (22,356.86s lunar tidal day, 5.145 deg orbital inclination) */
	float lambda_moon = TWO_PI_F * (t_sec / ND_ASTRO_LUNAR_DAY_SEC) + 0.785398f;
	float inc = (23.43928f + 5.145f) * (PI_F / 180.0f);
	float mx = cosf(lambda_moon) * cosf(inc);
	float my = sinf(lambda_moon) * cosf(inc);
	float mz = sinf(lambda_moon) * sinf(inc);
	float mlen = sqrtf(mx * mx + my * my + mz * mz + 1e-8f);
	if (out_moon_dir) {
		out_moon_dir[0] = mx / mlen;
		out_moon_dir[1] = my / mlen;
		out_moon_dir[2] = mz / mlen;
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
			float init_spd = walking ? 38.0f : 8.0f;
			float energy = fminf(1.0f, 0.35f + 0.65f * fminf(1.0f, water_depth / 2.5f)) * (walking ? 1.0f : 0.42f);
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
			float energy = fminf(1.0f, 0.35f + 0.65f * fminf(1.0f, water_depth / 2.5f)) * (walking ? 1.0f : 0.42f);

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



