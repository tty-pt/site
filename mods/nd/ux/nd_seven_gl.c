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
#include <math.h>
#include <string.h>
#include <stdlib.h>

#define PI_F 3.14159265358979323846f
#define TWO_PI_F 6.28318530717958647692f

/* 32x32 quad grid topology = 33x33 vertices = 1,089 vertices */
#define GL_GRID_QUADS 32
#define GL_GRID_VERTS (GL_GRID_QUADS + 1)
#define GL_TOTAL_VERTS (GL_GRID_VERTS * GL_GRID_VERTS)
#define GL_TOTAL_INDICES (GL_GRID_QUADS * GL_GRID_QUADS * 6)

#define MAX_CHUNK_TEXTURES 128
#define MAX_WATER_QUEUE 128
#define MAX_BB_VERTS 12288

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
	"vec2 getGrad(ivec2 p, ivec2 maxC) {\n"
	"    float hL = getH(p - ivec2(1, 0), maxC);\n"
	"    float hR = getH(p + ivec2(1, 0), maxC);\n"
	"    float hD = getH(p - ivec2(0, 1), maxC);\n"
	"    float hU = getH(p + ivec2(0, 1), maxC);\n"
	"    float dx = (p.x <= 0) ? (hR - getH(p, maxC)) : ((p.x >= maxC.x) ? (getH(p, maxC) - hL) : (hR - hL) * 0.5);\n"
	"    float dy = (p.y <= 0) ? (hU - getH(p, maxC)) : ((p.y >= maxC.y) ? (getH(p, maxC) - hD) : (hU - hD) * 0.5);\n"
	"    return vec2(dx, dy);\n"
	"}\n"
	"float sampleHeightSmooth(vec2 uv, out vec2 out_grad) {\n"
	"    vec2 texSize = vec2(textureSize(uHeightTex, 0));\n"
	"    vec2 maxCoordF = max(vec2(1.0), texSize - 1.0);\n"
	"    vec2 pixelCoord = uv * maxCoordF;\n"
	"    ivec2 iCoord = ivec2(floor(pixelCoord));\n"
	"    vec2 f = fract(pixelCoord);\n"
	"    vec2 w = f * f * (3.0 - 2.0 * f);\n"
	"    ivec2 maxCoord = ivec2(maxCoordF);\n"
	"    ivec2 c00 = clamp(iCoord, ivec2(0), maxCoord);\n"
	"    ivec2 c10 = clamp(iCoord + ivec2(1, 0), ivec2(0), maxCoord);\n"
	"    ivec2 c01 = clamp(iCoord + ivec2(0, 1), ivec2(0), maxCoord);\n"
	"    ivec2 c11 = clamp(iCoord + ivec2(1, 1), ivec2(0), maxCoord);\n"
	"    float h00 = getH(c00, maxCoord);\n"
	"    float h10 = getH(c10, maxCoord);\n"
	"    float h01 = getH(c01, maxCoord);\n"
	"    float h11 = getH(c11, maxCoord);\n"
	"    float b0 = mix(h00, h10, w.x);\n"
	"    float b1 = mix(h01, h11, w.x);\n"
	"    float height = mix(b0, b1, w.y);\n"
	"    vec2 g00 = getGrad(c00, maxCoord);\n"
	"    vec2 g10 = getGrad(c10, maxCoord);\n"
	"    vec2 g01 = getGrad(c01, maxCoord);\n"
	"    vec2 g11 = getGrad(c11, maxCoord);\n"
	"    vec2 gx0 = mix(g00, g10, w.x);\n"
	"    vec2 gx1 = mix(g01, g11, w.x);\n"
	"    vec2 smoothGrad = mix(gx0, gx1, w.y);\n"
	"    out_grad.x = smoothGrad.x * (maxCoordF.x / uChunkSpan.x);\n"
	"    out_grad.y = smoothGrad.y * (maxCoordF.y / uChunkSpan.y);\n"
	"    return height;\n"
	"}\n"
	"void main() {\n"
	"    vUV = a_grid_uv;\n"
	"    float scale = (uHeightScale != 0.0) ? uHeightScale : 1.0;\n"
	"    float effectiveScale = scale * 0.0625;\n"
	"    vec2 grad;\n"
	"    float raw_h = sampleHeightSmooth(a_grid_uv, grad);\n"
	"    float wz = raw_h * effectiveScale;\n"
	"    vZ = wz;\n"
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
	"    float dRv = r0 * dcv + wz * cv;\n"
	"    vec3 worldPos = uPlanetCenter + vec3(Rv0 * dcu + dRv * cu, Rv0 * dsu + dRv * su, r0 * dsv + wz * sv);\n"
	"    vec3 torusN = vec3(cv * cu, cv * su, sv);\n"
	"    vec3 tangentU = vec3(-su, cu, 0.0);\n"
	"    vec3 tangentV = vec3(-sv * cu, -sv * su, cv);\n"
	"    vec3 N = normalize(torusN - grad.x * effectiveScale * tangentU - grad.y * effectiveScale * tangentV);\n"
	"    vec3 L = normalize(vec3(-0.577, -0.577, 0.816));\n"
	"    float diff = max(0.0, dot(N, L));\n"
	"    vLighting = 0.45 + 0.55 * diff;\n"
	"    vWorldPos = worldXY;\n"
	"    vWaterDepth = (uSeaLevel > -900.0) ? max(0.0, uSeaLevel - wz) : 0.0;\n"
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
	"uniform sampler2D uSubstrateTex;\n"
	"uniform float uSeaLevel;\n"
	"uniform float uFogStart;\n"
	"uniform float uFogEnd;\n"
	"uniform float uAnimTime;\n"
	"uniform vec3 uSkyColor;\n"
	"uniform float uAtmoIntensity;\n"
	"out vec4 FragColor;\n"
	"uint chaff_rotl32(uint x, int r) {\n"
	"    return (x << uint(r)) | (x >> uint(32 - r));\n"
	"}\n"
	"uint chaff_hash3(ivec3 p, uint seed) {\n"
	"    uint v1 = (uint(p.y) & 0xFFFFu) | ((uint(p.x) & 0xFFFFu) << 16u);\n"
	"    uint v2 = (uint(p.z) & 0xFFFFu);\n"
	"    uint h = seed + 0x165667B1u + 6u;\n"
	"    h += v1 * 0xC2B2AE3Du;\n"
	"    h = chaff_rotl32(h, 17) * 0x27D4EB2Fu;\n"
	"    h += v2 * 0x85EBCA77u;\n"
	"    h = chaff_rotl32(h, 13) * 0xC2B2AE3Du;\n"
	"    h ^= h >> 15u;\n"
	"    h *= 0x85EBCA77u;\n"
	"    h ^= h >> 16u;\n"
	"    return h;\n"
	"}\n"
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
	"float chaff_fast_noise2d(vec2 p) {\n"
	"    vec2 ip = floor(p);\n"
	"    vec2 fp = fract(p);\n"
	"    vec2 u = fp * fp * (3.0 - 2.0 * fp);\n"
	"    ivec2 c = ivec2(ip);\n"
	"    float n00 = float(chaff_hash3(ivec3(c.x, c.y, 0), 0xA341316Cu) >> 24u);\n"
	"    float n10 = float(chaff_hash3(ivec3(c.x + 1, c.y, 0), 0xA341316Cu) >> 24u);\n"
	"    float n01 = float(chaff_hash3(ivec3(c.x, c.y + 1, 0), 0xA341316Cu) >> 24u);\n"
	"    float n11 = float(chaff_hash3(ivec3(c.x + 1, c.y + 1, 0), 0xA341316Cu) >> 24u);\n"
	"    return mix(mix(n00, n10, u.x), mix(n01, n11, u.x), u.y) * (1.0 / 255.0);\n"
	"}\n"
	"float eval_micro_detail(vec2 pos) {\n"
	"    float meso  = chaff_fast_noise2d(pos * 0.35);\n"
	"    float micro = chaff_fast_noise2d(pos * 1.60);\n"
	"    float sub   = chaff_fast_noise2d(pos * 5.00);\n"
	"    return (meso - 0.5) * 0.45 + (micro - 0.5) * 0.35 + (sub - 0.5) * 0.20;\n"
	"}\n"
	"void main() {\n"
	"    vec2 sampleUV = vUV;\n"
	"    if (vWaterDepth > 0.0) {\n"
	"        float depthFactor = clamp(vWaterDepth / 16.0, 0.0, 1.0);\n"
	"        vec2 pWave1 = vWorldPos * 0.18 + vec2(uAnimTime * 0.8, -uAnimTime * 0.6);\n"
	"        vec2 pWave2 = vWorldPos * 0.38 + vec2(-uAnimTime * 1.1, uAnimTime * 0.9);\n"
	"        vec2 refractOffset = vec2(\n"
	"            sin(pWave1.x + pWave1.y) * 0.6 + cos(pWave2.x) * 0.4,\n"
	"            cos(pWave1.x - pWave1.y) * 0.6 + sin(pWave2.y) * 0.4\n"
	"        ) * (0.012 * depthFactor);\n"
	"        sampleUV = clamp(vUV + refractOffset, 0.0, 1.0);\n"
	"    }\n"
	"    vec4 baseCol = texture(uSubstrateTex, sampleUV);\n"
	"    float effLighting = vLighting;\n"
	"    if (vWaterDepth <= 0.0) {\n"
	"        float slope = 1.0 - max(0.0, dot(normalize(vNormal), normalize(vTorusNormal)));\n"
	"        float elevAboveWater = (uSeaLevel > -900.0) ? max(0.0, vZ - uSeaLevel) : max(0.0, vZ);\n"
	"        float rockFactor = smoothstep(0.18, 0.40, slope);\n"
	"        if (rockFactor > 0.0) {\n"
	"            float strata = sin(vZ * 1.8) * 0.14 + cos(vZ * 0.7 + vWorldPos.x * 0.03) * 0.08;\n"
	"            vec3 rockCol = vec3(0.38, 0.36, 0.34) * (0.85 + strata);\n"
	"            baseCol.rgb = mix(baseCol.rgb, rockCol, rockFactor);\n"
	"        }\n"
	"        float distFade = clamp(1.0 - (vDistance - 600.0) / 1800.0, 0.0, 1.0);\n"
	"        if (distFade > 0.0) {\n"
	"            float h0 = eval_micro_detail(vWorldPos);\n"
	"            const float delta = 0.25;\n"
	"            float hX = eval_micro_detail(vWorldPos + vec2(delta, 0.0));\n"
	"            float hY = eval_micro_detail(vWorldPos + vec2(0.0, delta));\n"
	"            vec2 microGrad = vec2(hX - h0, hY - h0) * (distFade / delta);\n"
	"            vec3 N_frag = normalize(vNormal - microGrad.x * 0.40 * vTangentU - microGrad.y * 0.40 * vTangentV);\n"
	"            vec3 L_frag = normalize(vec3(-0.577, -0.577, 0.816));\n"
	"            vec3 viewDir = normalize(vViewDir);\n"
	"            vec3 H_half = normalize(L_frag + viewDir);\n"
	"            float perFragDiff = max(0.0, dot(N_frag, L_frag));\n"
	"            float ao = clamp(1.0 - slope * 0.30 + h0 * 0.25, 0.60, 1.0);\n"
	"            effLighting = (vLighting > 0.32) ? (0.40 * ao + 0.60 * perFragDiff * ao) : (vLighting * ao);\n"
	"            float soilFactor = smoothstep(0.08, 0.24, slope) * (1.0 - rockFactor);\n"
	"            if (soilFactor > 0.0) {\n"
	"                float clod = chaff_fast_noise2d(vWorldPos * 2.2);\n"
	"                float soilMoisture = clamp(1.0 - elevAboveWater / 12.0, 0.0, 1.0);\n"
	"                vec3 soilCol = vec3(0.40, 0.30, 0.20) * (0.80 + 0.40 * clod) * (1.0 - soilMoisture * 0.35);\n"
	"                baseCol.rgb = mix(baseCol.rgb, soilCol, soilFactor * distFade);\n"
	"            }\n"
	"            bool isSandBiome = (baseCol.r > 0.55 && baseCol.g > 0.45 && baseCol.b < 0.45);\n"
	"            float shoreFactor = smoothstep(6.0, 0.5, elevAboveWater) * (1.0 - rockFactor);\n"
	"            if (isSandBiome || shoreFactor > 0.0) {\n"
	"                float sandRipple = sin(vWorldPos.x * 2.2 + sin(vWorldPos.y * 1.1) * 1.5) * 0.5 + 0.5;\n"
	"                float sandGrain = chaff_fast_noise2d(vWorldPos * 5.0);\n"
	"                float wetSand = smoothstep(2.5, 0.0, elevAboveWater);\n"
	"                vec3 drySandCol = vec3(0.74, 0.66, 0.48) * (0.88 + 0.24 * sandRipple);\n"
	"                vec3 wetSandCol = vec3(0.52, 0.44, 0.32) * (0.90 + 0.20 * sandGrain);\n"
	"                vec3 sandCol = mix(drySandCol, wetSandCol, wetSand);\n"
	"                float sandBlend = isSandBiome ? 1.0 : shoreFactor;\n"
	"                baseCol.rgb = mix(baseCol.rgb, sandCol, sandBlend * distFade);\n"
	"                float sandSpecAngle = max(0.0, dot(N_frag, H_half));\n"
	"                float sandSpec = pow(sandSpecAngle, 32.0) * (0.35 * wetSand + 0.08 * (1.0 - wetSand)) * sandBlend * distFade;\n"
	"                baseCol.rgb += vec3(sandSpec * 0.9, sandSpec * 0.95, sandSpec * 1.0);\n"
	"            }\n"
	"            if (rockFactor < 0.65 && !isSandBiome && shoreFactor < 0.7 && baseCol.g > baseCol.r * 1.02) {\n"
	"                float thatch = chaff_fast_noise2d(vWorldPos * 0.18);\n"
	"                vec3 lush = baseCol.rgb * vec3(0.92, 1.08, 0.90);\n"
	"                vec3 dryT = baseCol.rgb * vec3(1.10, 1.02, 0.75);\n"
	"                baseCol.rgb = mix(lush, dryT, smoothstep(0.38, 0.72, thatch) * distFade);\n"
	"            }\n"
	"            float microShade = clamp(1.0 + h0 * 0.35 * distFade, 0.72, 1.28);\n"
	"            baseCol.rgb *= microShade;\n"
	"            if (rockFactor > 0.3) {\n"
	"                float specAngle = max(0.0, dot(N_frag, H_half));\n"
	"                float spec = pow(specAngle, 20.0) * (rockFactor * 0.22 * distFade);\n"
	"                baseCol.rgb += vec3(spec);\n"
	"            }\n"
	"        }\n"
	"    }\n"
	"    vec3 lit = baseCol.rgb * effLighting;\n"
	"    if (vWaterDepth > 0.0) {\n"
	"        float d = clamp(vWaterDepth, 0.0, 32.0);\n"
	"        float distFade = clamp(1.0 - (vDistance - 600.0) / 1800.0, 0.0, 1.0);\n"
	"        float caustic = 0.0;\n"
	"        if (distFade > 0.01) {\n"
	"            float c1 = chaff_sample_3d(vec3(vWorldPos * 2.0, uAnimTime * 3.5), 3u, 0x1337u);\n"
	"            float c2 = chaff_sample_3d(vec3((vWorldPos + vec2(37.0, -23.0)) * 2.0, uAnimTime * 2.5), 3u, 0xBEEFu);\n"
	"            float diff = abs(c1 - c2);\n"
	"            caustic = pow(clamp(1.0 - diff * 6.0, 0.0, 1.0), 3.0) * distFade;\n"
	"        }\n"
	"        float causticCoherence = exp(-0.16 * d);\n"
	"        vec3 seabedCaustic = vec3(0.40, 0.75, 0.95) * (caustic * 0.55 * causticCoherence);\n"
	"        vec3 viewDir = normalize(vViewDir);\n"
	"        float cosView = max(0.25, dot(vec3(0.0, 0.0, 1.0), viewDir));\n"
	"        float opticalPath = d / cosView;\n"
	"        vec3 seabedAbsorption = exp(-vec3(0.065, 0.020, 0.005) * opticalPath);\n"
	"        vec3 seabedColor = (lit + seabedCaustic) * seabedAbsorption;\n"
	"        vec3 deepOceanScattered = vec3(0.012, 0.08, 0.30) * (1.0 - exp(-vec3(0.018, 0.030, 0.055) * opticalPath));\n"
	"        lit = seabedColor + deepOceanScattered;\n"
	"    }\n"
	"    float fogFactor = clamp((vDistance - uFogStart) / max(1.0, uFogEnd - uFogStart), 0.0, 1.0);\n"
	"    vec3 sky = (length(uSkyColor) > 0.001) ? uSkyColor : vec3(0.55, 0.70, 0.85);\n"
	"    vec3 viewDirNorm = normalize(vViewDir);\n"
	"    float limb = pow(clamp(1.0 - max(0.0, dot(vec3(0.0, 0.0, 1.0), viewDirNorm)), 0.0, 1.0), 3.0);\n"
	"    vec3 surfaceLit = mix(lit, vec3(0.35, 0.65, 0.95), limb * 0.45 * uAtmoIntensity);\n"
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
	"float getH(ivec2 p, ivec2 maxC) {\n"
	"    return float(texelFetch(uHeightTex, clamp(p, ivec2(0), maxC), 0).r);\n"
	"}\n"
	"float sampleHeightSmooth(vec2 uv, out float minNeighborH) {\n"
	"    vec2 texSize = vec2(textureSize(uHeightTex, 0));\n"
	"    vec2 maxCoordF = max(vec2(1.0), texSize - 1.0);\n"
	"    vec2 pixelCoord = uv * maxCoordF;\n"
	"    ivec2 iCoord = ivec2(floor(pixelCoord));\n"
	"    vec2 f = fract(pixelCoord);\n"
	"    vec2 w = f * f * (3.0 - 2.0 * f);\n"
	"    ivec2 maxCoord = ivec2(maxCoordF);\n"
	"    ivec2 c00 = clamp(iCoord, ivec2(0), maxCoord);\n"
	"    ivec2 c10 = clamp(iCoord + ivec2(1, 0), ivec2(0), maxCoord);\n"
	"    ivec2 c01 = clamp(iCoord + ivec2(0, 1), ivec2(0), maxCoord);\n"
	"    ivec2 c11 = clamp(iCoord + ivec2(1, 1), ivec2(0), maxCoord);\n"
	"    float h00 = getH(c00, maxCoord);\n"
	"    float h10 = getH(c10, maxCoord);\n"
	"    float h01 = getH(c01, maxCoord);\n"
	"    float h11 = getH(c11, maxCoord);\n"
	"    float hm10 = getH(clamp(iCoord + ivec2(-1, 0), ivec2(0), maxCoord), maxCoord);\n"
	"    float h0m1 = getH(clamp(iCoord + ivec2(0, -1), ivec2(0), maxCoord), maxCoord);\n"
	"    float hm1m1 = getH(clamp(iCoord + ivec2(-1, -1), ivec2(0), maxCoord), maxCoord);\n"
	"    minNeighborH = min(min(min(h00, h10), min(h01, h11)), min(min(hm10, h0m1), hm1m1));\n"
	"    return mix(mix(h00, h10, w.x), mix(h01, h11, w.x), w.y);\n"
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
	"    bool nearWater = (depth > 0.0) || (minNeighborWz < uSeaLevel);\n"
	"    float effWz = nearWater ? uSeaLevel : (min(wz, uSeaLevel) - 2.0);\n"
	"    float Rv0 = R + r0 * cv0;\n"
	"    float dRv = r0 * dcv + effWz * cv;\n"
	"    vec3 worldPos = uPlanetCenter + vec3(Rv0 * dcu + dRv * cu, Rv0 * dsu + dRv * su, r0 * dsv + effWz * sv);\n"
	"    vTorusNormal = vec3(cv * cu, cv * su, sv);\n"
	"    vWorldPos = worldXY;\n"
	"    vDistance = length(worldPos - uCameraPos);\n"
	"    vViewDir = uCameraPos - worldPos;\n"
	"    gl_Position = uMVP * vec4(worldPos, 1.0);\n"
	"}\n";

static const char *s_fs_water =
	"#version 300 es\n"
	"precision highp float;\n"
	"precision highp int;\n"
	"precision highp sampler2D;\n"
	"in vec2 vUV;\n"
	"in float vWaterDepth;\n"
	"in float vDistance;\n"
	"in vec2 vWorldPos;\n"
	"in vec3 vViewDir;\n"
	"in vec3 vTorusNormal;\n"
	"uniform float uFogStart;\n"
	"uniform float uFogEnd;\n"
	"uniform float uAnimTime;\n"
	"uniform vec3 uSkyColor;\n"
	"uniform float uAtmoIntensity;\n"
	"out vec4 FragColor;\n"
	"uint chaff_rotl32(uint x, int r) {\n"
	"    return (x << uint(r)) | (x >> uint(32 - r));\n"
	"}\n"
	"uint chaff_hash2(ivec2 p, uint seed) {\n"
	"    uint val = (uint(p.y) & 0xFFFFu) | ((uint(p.x) & 0xFFFFu) << 16u);\n"
	"    uint h = seed + 0x165667B1u + 4u;\n"
	"    h += val * 0xC2B2AE3Du;\n"
	"    h = chaff_rotl32(h, 17) * 0x27D4EB2Fu;\n"
	"    h ^= h >> 15u;\n"
	"    h *= 0x85EBCA77u;\n"
	"    h ^= h >> 13u;\n"
	"    h *= 0xC2B2AE3Du;\n"
	"    h ^= h >> 16u;\n"
	"    return h;\n"
	"}\n"
	"uint chaff_hash3(ivec3 p, uint seed) {\n"
	"    uint v1 = (uint(p.y) & 0xFFFFu) | ((uint(p.x) & 0xFFFFu) << 16u);\n"
	"    uint v2 = (uint(p.z) & 0xFFFFu);\n"
	"    uint h = seed + 0x165667B1u + 6u;\n"
	"    h += v1 * 0xC2B2AE3Du;\n"
	"    h = chaff_rotl32(h, 17) * 0x27D4EB2Fu;\n"
	"    h += v2 * 0x85EBCA77u;\n"
	"    h = chaff_rotl32(h, 13) * 0xC2B2AE3Du;\n"
	"    h ^= h >> 15u;\n"
	"    h *= 0x85EBCA77u;\n"
	"    h ^= h >> 16u;\n"
	"    return h;\n"
	"}\n"
	"float chaff_sample_octave(vec2 world_pos, uint shift, uint vshift, uint seed) {\n"
	"    float cell_size = float(1u << shift);\n"
	"    vec2 p = world_pos / cell_size;\n"
	"    ivec2 i0 = ivec2(floor(p));\n"
	"    vec2 f = fract(p);\n"
	"    vec2 u = f * f * (3.0 - 2.0 * f);\n"
	"    int step_px = int(1u << shift);\n"
	"    ivec2 cell = i0 * step_px;\n"
	"    uint v00 = chaff_hash2(cell, seed) >> vshift;\n"
	"    uint v10 = chaff_hash2(cell + ivec2(step_px, 0), seed) >> vshift;\n"
	"    uint v01 = chaff_hash2(cell + ivec2(0, step_px), seed) >> vshift;\n"
	"    uint v11 = chaff_hash2(cell + ivec2(step_px, step_px), seed) >> vshift;\n"
	"    float b0 = mix(float(v00), float(v10), u.x);\n"
	"    float b1 = mix(float(v01), float(v11), u.x);\n"
	"    return mix(b0, b1, u.y);\n"
	"}\n"
	"vec3 chaff_sample_grad_2d(vec2 world_pos, uint shift, uint vshift, uint seed) {\n"
	"    float cell_size = float(1u << shift);\n"
	"    vec2 p = world_pos / cell_size;\n"
	"    ivec2 i0 = ivec2(floor(p));\n"
	"    vec2 f = fract(p);\n"
	"    vec2 u = f * f * (3.0 - 2.0 * f);\n"
	"    vec2 du = 6.0 * f * (1.0 - f);\n"
	"    int step_px = int(1u << shift);\n"
	"    ivec2 cell = i0 * step_px;\n"
	"    float v00 = float(chaff_hash2(cell, seed) >> vshift);\n"
	"    float v10 = float(chaff_hash2(cell + ivec2(step_px, 0), seed) >> vshift);\n"
	"    float v01 = float(chaff_hash2(cell + ivec2(0, step_px), seed) >> vshift);\n"
	"    float v11 = float(chaff_hash2(cell + ivec2(step_px, step_px), seed) >> vshift);\n"
	"    float b0 = mix(v00, v10, u.x);\n"
	"    float b1 = mix(v01, v11, u.x);\n"
	"    float val = mix(b0, b1, u.y) / 255.0;\n"
	"    float dH_dx = mix(v10 - v00, v11 - v01, u.y);\n"
	"    float dH_dy = b1 - b0;\n"
	"    vec2 grad = (vec2(dH_dx, dH_dy) * du) / (255.0 * cell_size);\n"
	"    return vec3(val, grad.x, grad.y);\n"
	"}\n"
	"float chaff_sample_3d(vec3 pos, uint shift, uint seed) {\n"
	"    float cell_size = float(1u << shift);\n"
	"    vec3 p = pos / cell_size;\n"
	"    ivec3 i0 = ivec3(floor(p));\n"
	"    vec3 f = fract(p);\n"
	"    vec3 u = f * f * (3.0 - 2.0 * f);\n"
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
	"    return mix(b0, b1, u.z) / 255.0;\n"
	"}\n"
	"vec4 chaff_sample_grad_3d(vec3 pos, uint shift, uint seed) {\n"
	"    float cell_size = float(1u << shift);\n"
	"    vec3 p = pos / cell_size;\n"
	"    ivec3 i0 = ivec3(floor(p));\n"
	"    vec3 f = fract(p);\n"
	"    vec3 u = f * f * (3.0 - 2.0 * f);\n"
	"    vec3 du = 6.0 * f * (1.0 - f);\n"
	"    int step_px = int(1u << shift);\n"
	"    ivec3 cell = i0 * step_px;\n"
	"    float v000 = float(chaff_hash3(cell + ivec3(0, 0, 0), seed) >> 24u);\n"
	"    float v100 = float(chaff_hash3(cell + ivec3(step_px, 0, 0), seed) >> 24u);\n"
	"    float v010 = float(chaff_hash3(cell + ivec3(0, step_px, 0), seed) >> 24u);\n"
	"    float v110 = float(chaff_hash3(cell + ivec3(step_px, step_px, 0), seed) >> 24u);\n"
	"    float v001 = float(chaff_hash3(cell + ivec3(0, 0, step_px), seed) >> 24u);\n"
	"    float v101 = float(chaff_hash3(cell + ivec3(step_px, 0, step_px), seed) >> 24u);\n"
	"    float v011 = float(chaff_hash3(cell + ivec3(0, step_px, step_px), seed) >> 24u);\n"
	"    float v111 = float(chaff_hash3(cell + ivec3(step_px, step_px, step_px), seed) >> 24u);\n"
	"    float b00 = mix(v000, v100, u.x);\n"
	"    float b10 = mix(v010, v110, u.x);\n"
	"    float b01 = mix(v001, v101, u.x);\n"
	"    float b11 = mix(v111, v111, u.x);\n"
	"    b11 = mix(v011, v111, u.x);\n"
	"    float b0 = mix(b00, b10, u.y);\n"
	"    float b1 = mix(b01, b11, u.y);\n"
	"    float val = mix(b0, b1, u.z) / 255.0;\n"
	"    float dx00 = v100 - v000;\n"
	"    float dx10 = v110 - v010;\n"
	"    float dx01 = v101 - v001;\n"
	"    float dx11 = v111 - v011;\n"
	"    float dx0 = mix(dx00, dx10, u.y);\n"
	"    float dx1 = mix(dx01, dx11, u.y);\n"
	"    float dN_dx = mix(dx0, dx1, u.z);\n"
	"    float dy0 = b10 - b00;\n"
	"    float dy1 = b11 - b01;\n"
	"    float dN_dy = mix(dy0, dy1, u.z);\n"
	"    float dN_dz = b1 - b0;\n"
	"    vec3 grad = (vec3(dN_dx, dN_dy, dN_dz) * du) / (255.0 * cell_size);\n"
	"    return vec4(val, grad.x, grad.y, grad.z);\n"
	"}\n"
	"vec2 chaff_rotate_golden(vec2 p) {\n"
	"    return vec2(0.8506508 * p.x - 0.5257311 * p.y, 0.5257311 * p.x + 0.8506508 * p.y);\n"
	"}\n"
	"void main() {\n"
	"    if (vWaterDepth <= 0.0) {\n"
	"        discard;\n"
	"    }\n"
	"    vec3 viewDir = normalize(vViewDir);\n"
	"    vec3 sunDir = normalize(vec3(-0.577, -0.577, 0.816));\n"
	"    vec2 wind = vec2(0.7071, -0.7071);\n"
	"    vec2 pMacro = vWorldPos * 0.035 - wind * (uAnimTime * 0.8);\n"
	"    vec2 pMid   = chaff_rotate_golden(vWorldPos * 0.12) + vec2(0.5, 0.866) * (uAnimTime * 1.6);\n"
	"    vec2 pCap   = vWorldPos * 0.45 - wind * (uAnimTime * 3.5);\n"
	"    vec4 sMacro = chaff_sample_grad_3d(vec3(pMacro * 8.0, uAnimTime * 1.2), 6u, 0x5357u);\n"
	"    float wMacro = sMacro.x;\n"
	"    vec4 sMid = chaff_sample_grad_3d(vec3(pMid * 8.0, uAnimTime * 2.2), 4u, 0xCAFEu);\n"
	"    float wMid = sMid.x;\n"
	"    vec3 sCap = chaff_sample_grad_2d(pCap, 2u, 24u, 0xC491u);\n"
	"    float wCap = sCap.x;\n"
	"    vec2 pRipple = vWorldPos * 1.10 - wind * (uAnimTime * 4.8);\n"
	"    vec3 sRip = chaff_sample_grad_2d(pRipple, 1u, 24u, 0x9E37u);\n"
	"    float wRip = sRip.x;\n"
	"    float nyqRip   = clamp(1.0 - (vDistance - 250.0) / 450.0, 0.0, 1.0);\n"
	"    float nyqCap   = clamp(1.0 - (vDistance - 400.0) / 800.0, 0.0, 1.0);\n"
	"    float nyqMid   = clamp(1.0 - (vDistance - 1200.0) / 3300.0, 0.0, 1.0);\n"
	"    float nyqMacro = clamp(1.0 - (vDistance - 8000.0) / 32000.0, 0.25, 1.0);\n"
	"    vec2 gradMacro = sMacro.yz * (4.32 * nyqMacro);\n"
	"    vec2 gradMid   = sMid.yz * (4.48 * nyqMid);\n"
	"    vec2 gradCap   = sCap.yz * (0.42 * nyqCap);\n"
	"    vec2 gradRip   = sRip.yz * (0.36 * nyqRip);\n"
	"    vec2 waveGrad  = gradMacro + gradMid + gradCap + gradRip;\n"
	"    vec3 localWaveNormal = vec3(-waveGrad.x, -waveGrad.y, 1.0);\n"
	"    vec3 waveNormal = normalize(localWaveNormal + vTorusNormal * 0.7);\n"
	"    float diffuseSun = clamp(dot(waveNormal, sunDir), 0.0, 1.0);\n"
	"    vec3 halfVec = normalize(sunDir + viewDir);\n"
	"    float specExp = mix(24.0, 192.0, nyqCap);\n"
	"    float specStrength = mix(0.45, 1.0, nyqMid);\n"
	"    float specSun = pow(clamp(dot(waveNormal, halfVec), 0.0, 1.0), specExp) * specStrength;\n"
	"    vec3 nUp = normalize(mix(vec3(0.0, 0.0, 1.0), vTorusNormal, clamp(vDistance / 8000.0, 0.0, 1.0)));\n"
	"    float slantPath = vWaterDepth / max(0.25, dot(viewDir, nUp));\n"
	"    float c1 = chaff_sample_octave(vWorldPos * 0.5 + vec2(uAnimTime * 0.2), 3u, 24u, 0xCA11u);\n"
	"    float c2 = chaff_sample_octave(vWorldPos * 0.5 - vec2(uAnimTime * 0.15), 3u, 24u, 0xCA22u);\n"
	"    float caustic = pow(min(c1, c2) / 255.0, 2.0) * 2.5 * smoothstep(4.0, 0.5, vWaterDepth);\n"
	"    vec3 cSeabed = vec3(0.22, 0.30, 0.26) * (1.0 + caustic);\n"
	"    vec3 mu = vec3(0.22, 0.07, 0.015);\n"
	"    vec3 cTransmitted = cSeabed * exp(-mu * slantPath);\n"
	"    vec3 cOcean = vec3(0.012, 0.08, 0.30);\n"
	"    float sigma = 0.08;\n"
	"    vec3 cInscatter = cOcean * (1.0 - exp(-sigma * slantPath));\n"
	"    vec3 fluidBody = cTransmitted + cInscatter;\n"
	"    float fresnel = 0.04 + 0.96 * pow(clamp(1.0 - dot(waveNormal, viewDir), 0.0, 1.0), 5.0);\n"
	"    vec3 sky = (length(uSkyColor) > 0.001) ? uSkyColor : vec3(0.55, 0.72, 0.88);\n"
	"    vec3 sunColor = vec3(1.0, 0.98, 0.92);\n"
	"    vec3 surfaceRgb = mix(fluidBody, sky, fresnel) + sunColor * specSun;\n"
	"    surfaceRgb += vec3(0.04, 0.12, 0.18) * (diffuseSun * 0.20 * fresnel);\n"
	"    float shoreNoise = chaff_sample_octave(vWorldPos + vec2(0.7071, -0.7071) * (uAnimTime * 3.5), 2u, 24u, 0xF0A1u) / 255.0;\n"
	"    float foam = smoothstep(2.5, 0.3, vWaterDepth) * step(0.52, shoreNoise + (wMid - 0.5) * 0.3);\n"
	"    vec3 foamColor = vec3(0.94, 0.97, 1.0);\n"
	"    vec3 fluidRgb = mix(surfaceRgb, foamColor, foam * 0.85);\n"
	"    float limb = pow(clamp(1.0 - max(0.0, dot(waveNormal, viewDir)), 0.0, 1.0), 3.0);\n"
	"    fluidRgb = mix(fluidRgb, vec3(0.35, 0.70, 1.0), limb * 0.55 * uAtmoIntensity);\n"
	"    float fluidAlpha = 1.0 - exp(-0.10 * slantPath);\n"
	"    float alpha = clamp(fresnel + fluidAlpha * 0.82 + specSun * 0.92 + foam * 0.85 + limb * 0.4, fresnel, 0.98);\n"
	"    float fogFactor = clamp((vDistance - uFogStart) / max(1.0, uFogEnd - uFogStart), 0.0, 1.0);\n"
	"    FragColor = vec4(mix(fluidRgb, sky, fogFactor * uAtmoIntensity), alpha);\n"
	"}\n";

static const char *s_vs_billboard =
	"#version 300 es\n"
	"precision highp float;\n"
	"layout (location = 0) in vec3 a_pos;\n"
	"layout (location = 1) in vec2 a_uv;\n"
	"layout (location = 2) in vec4 a_tint;\n"
	"layout (location = 3) in vec3 a_local_pos;\n"
	"uniform mat4 uMVP;\n"
	"uniform vec3 uCameraPos;\n"
	"out vec2 vUV;\n"
	"out vec4 vTint;\n"
	"out vec3 vWorldPos;\n"
	"out vec3 vLocalPos;\n"
	"void main() {\n"
	"    vUV = a_uv;\n"
	"    vTint = a_tint;\n"
	"    vWorldPos = a_pos;\n"
	"    vLocalPos = a_local_pos;\n"
	"    vec3 toCam = uCameraPos - a_pos;\n"
	"    float dCam = length(toCam);\n"
	"    vec3 dirToCam = (dCam > 0.001) ? (toCam / dCam) : vec3(0.0, 0.0, 1.0);\n"
	"    float bias = min(24.0, dCam * 0.08);\n"
	"    gl_Position = uMVP * vec4(a_pos + dirToCam * bias, 1.0);\n"
	"}\n";

static const char *s_fs_billboard =
	"#version 300 es\n"
	"precision highp float;\n"
	"precision highp sampler2D;\n"
	"in vec2 vUV;\n"
	"in vec4 vTint;\n"
	"in vec3 vWorldPos;\n"
	"in vec3 vLocalPos;\n"
	"uniform sampler2D uAtlasTex;\n"
	"uniform float uSeaLevel;\n"
	"uniform float uAnimTime;\n"
	"uniform vec3 uCameraPos;\n"
	"uniform float uFogStart;\n"
	"uniform float uFogEnd;\n"
	"uniform vec3 uSkyColor;\n"
	"uniform float uAtmoIntensity;\n"
	"out vec4 FragColor;\n"
	"uint chaff_rotl32(uint x, int r) {\n"
	"    return (x << uint(r)) | (x >> uint(32 - r));\n"
	"}\n"
	"uint chaff_hash3(ivec3 p, uint seed) {\n"
	"    uint v1 = (uint(p.y) & 0xFFFFu) | ((uint(p.x) & 0xFFFFu) << 16u);\n"
	"    uint v2 = (uint(p.z) & 0xFFFFu);\n"
	"    uint h = seed + 0x165667B1u + 6u;\n"
	"    h += v1 * 0xC2B2AE3Du;\n"
	"    h = chaff_rotl32(h, 17) * 0x27D4EB2Fu;\n"
	"    h += v2 * 0x85EBCA77u;\n"
	"    h = chaff_rotl32(h, 13) * 0xC2B2AE3Du;\n"
	"    h ^= h >> 15u;\n"
	"    h *= 0x85EBCA77u;\n"
	"    h ^= h >> 16u;\n"
	"    return h;\n"
	"}\n"
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
	"void main() {\n"
	"    float d = 0.0;\n"
	"    bool isSubmerged = false;\n"
	"    if (uSeaLevel > -900.0 && vLocalPos.z < uSeaLevel - 0.25) {\n"
	"        d = (uSeaLevel - 0.25) - vLocalPos.z;\n"
	"        isSubmerged = true;\n"
	"    }\n"
	"    vec2 sampleUV = vUV;\n"
	"    if (isSubmerged) {\n"
	"        float distortStrength = clamp(d * 0.25, 0.0, 1.0);\n"
	"        vec2 waveOffset = vec2(\n"
	"            sin(vLocalPos.y * 0.35 + uAnimTime * 3.5),\n"
	"            cos(vLocalPos.x * 0.35 + uAnimTime * 2.8)\n"
	"        ) * (vec2(1.2 / 512.0, 1.2 / 328.0) * distortStrength);\n"
	"        sampleUV = vUV + waveOffset;\n"
	"    }\n"
	"    vec4 baseCol = texture(uAtlasTex, vUV);\n"
	"    if (baseCol.a < 0.1) discard;\n"
	"    vec4 subSample = isSubmerged ? texture(uAtlasTex, sampleUV) : baseCol;\n"
	"    if (subSample.a < 0.1) subSample = baseCol;\n"
	"    vec4 col = subSample * vTint;\n"
	"    if (col.a <= 0.001) discard;\n"
	"    vec3 lit = col.rgb;\n"
	"    if (isSubmerged) {\n"
	"        float c1 = chaff_sample_3d(vec3(vLocalPos.xy * 2.0, uAnimTime * 3.5), 3u, 0x1337u);\n"
	"        float c2 = chaff_sample_3d(vec3((vLocalPos.xy + vec2(37.0, -23.0)) * 2.0, uAnimTime * 2.5), 3u, 0xBEEFu);\n"
	"        float diff = abs(c1 - c2);\n"
	"        float caustic = pow(clamp(1.0 - diff * 6.0, 0.0, 1.0), 3.0);\n"
	"        float causticCoherence = exp(-0.12 * d);\n"
	"        vec3 submergedCaustic = vec3(0.55, 0.90, 1.0) * (caustic * 0.60 * causticCoherence);\n"
	"        vec3 viewDir = normalize(uCameraPos - vWorldPos);\n"
	"        float cosView = max(0.25, dot(vec3(0.0, 0.0, 1.0), viewDir));\n"
	"        float shallowFactor = smoothstep(0.0, 16.0, d);\n"
	"        float effectiveOpticalPath = (d * (0.25 + 0.75 * shallowFactor)) / cosView;\n"
	"        float submergedBlend = clamp(0.22 + 0.14 * smoothstep(0.0, 8.0, d), 0.0, 0.50);\n"
	"        vec3 shallowWash = vec3(0.25, 0.85, 0.92);\n"
	"        vec3 tintedSurface = mix(lit + submergedCaustic, shallowWash * (0.75 + 0.25 * lit), submergedBlend);\n"
	"        vec3 waterAbsorption = exp(-vec3(0.020, 0.008, 0.002) * effectiveOpticalPath);\n"
	"        vec3 waterColor = tintedSurface * waterAbsorption;\n"
	"        vec3 deepOceanScattered = vec3(0.005, 0.035, 0.12) * shallowFactor *\n"
	"                                  (1.0 - exp(-vec3(0.010, 0.018, 0.030) * effectiveOpticalPath));\n"
	"        lit = waterColor + deepOceanScattered;\n"
	"        float meniscus = smoothstep(0.0, 0.35, d) * smoothstep(1.8, 0.35, d);\n"
	"        vec3 meniscusColor = vec3(0.95, 0.98, 1.0);\n"
	"        lit = mix(lit, meniscusColor, meniscus * 0.70);\n"
	"    }\n"
	"    FragColor = vec4(lit, col.a);\n"
	"}\n";

const char *nd_seven_gl_vs_terrain(void) { return s_vs_terrain; }
const char *nd_seven_gl_fs_terrain(void) { return s_fs_terrain; }
const char *nd_seven_gl_vs_water(void) { return s_vs_water; }
const char *nd_seven_gl_fs_water(void) { return s_fs_water; }
const char *nd_seven_gl_vs_billboard(void) { return s_vs_billboard; }
const char *nd_seven_gl_fs_billboard(void) { return s_fs_billboard; }

/* --- Pure-C Torus Geometry & Camera Math --- */

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
		float far_z = (d * 20.0f > 200000.0f) ? (d * 20.0f) : 200000.0f;

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
	float far_z = (d * 20.0f > 200000.0f) ? (d * 20.0f) : 200000.0f;

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

	float tint[4] = {
		(float)((b->tint_rgba >> 24) & 0xFF) / 255.0f,
		(float)((b->tint_rgba >> 16) & 0xFF) / 255.0f,
		(float)((b->tint_rgba >> 8) & 0xFF) / 255.0f,
		(float)(b->tint_rgba & 0xFF) / 255.0f
	};

	struct { float dx, dy, u, v, loc_z; } c[4] = {
		{ -half_w, 0.0f, b->u0, b->v1, b->z },       /* BL (0) */
		{  half_w, 0.0f, b->u1, b->v1, b->z },       /* BR (1) */
		{ -half_w, h,    b->u0, b->v0, b->z + h },   /* TL (2) */
		{  half_w, h,    b->u1, b->v0, b->z + h }    /* TR (3) */
	};

	/* Triangle 1: BL, BR, TL (indices 0, 1, 2) */
	/* Triangle 2: BR, TR, TL (indices 1, 3, 2) */
	int idx[6] = { 0, 1, 2, 1, 3, 2 };

	for (int i = 0; i < 6; i++) {
		int k = idx[i];
		out_verts[i].world_pos[0] = base_pos[0] + c[k].dx * right[0] + c[k].dy * up[0];
		out_verts[i].world_pos[1] = base_pos[1] + c[k].dx * right[1] + c[k].dy * up[1];
		out_verts[i].world_pos[2] = base_pos[2] + c[k].dx * right[2] + c[k].dy * up[2];

		out_verts[i].uv[0] = c[k].u;
		out_verts[i].uv[1] = c[k].v;

		out_verts[i].tint[0] = tint[0];
		out_verts[i].tint[1] = tint[1];
		out_verts[i].tint[2] = tint[2];
		out_verts[i].tint[3] = tint[3];

		out_verts[i].local_pos[0] = b->x;
		out_verts[i].local_pos[1] = b->y;
		out_verts[i].local_pos[2] = c[k].loc_z;
	}
}

void nd_seven_gl_eval_billboard_quad(const lam_billboard_gpu_t *b,
                                     float pitch_deg,
                                     const nd_planet_def_t *pdef,
                                     nd_seven_gl_bb_vert_t out_verts[6])
{
	eval_billboard_quad_with_origin(b, pitch_deg, pdef, NULL, out_verts);
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

	float px = u * max_xf;
	float py = v * max_yf;

	int ix = (int)floorf(px);
	int iy = (int)floorf(py);

	float fx = px - (float)ix;
	float fy = py - (float)iy;

	float wx = fx * fx * (3.0f - 2.0f * fx);
	float wy = fy * fy * (3.0f - 2.0f * fy);

	int w = (int)tex_w;
	int h = (int)tex_h;

	float h00 = get_h16(height_i16, ix,     iy,     w, h);
	float h10 = get_h16(height_i16, ix + 1, iy,     w, h);
	float h01 = get_h16(height_i16, ix,     iy + 1, w, h);
	float h11 = get_h16(height_i16, ix + 1, iy + 1, w, h);

	float b0 = h00 + (h10 - h00) * wx;
	float b1 = h01 + (h11 - h01) * wx;
	float raw_h = b0 + (b1 - b0) * wy;

	if (out_grad_x || out_grad_y) {
		float dx0 = h10 - h00;
		float dx1 = h11 - h01;
		float dy0 = h01 - h00;
		float dy1 = h11 - h10;
		if (out_grad_x) *out_grad_x = (dx0 + (dx1 - dx0) * wy) * (max_xf / (span_x > 0.0f ? span_x : 160.0f));
		if (out_grad_y) *out_grad_y = (dy0 + (dy1 - dy0) * wx) * (max_yf / (span_y > 0.0f ? span_y : 128.0f));
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
	GLint aGridUV;
} gl_terrain_pipe_t;

typedef struct {
	GLuint prog;
	GLint uMVP;
	GLint uHeightTex;
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
	GLint aGridUV;
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
	GLint aPos;
	GLint aUV;
	GLint aTint;
	GLint aLocalPos;
} gl_billboard_pipe_t;

typedef struct {
	uint16_t world;
	uint8_t level;
	uint8_t active;
	uint32_t rev;
	int32_t ox, oy;
	int32_t span_w, span_h;
	uint32_t last_touch;
	GLuint height_tex;
	GLuint substrate_tex;
} gl_chunk_tex_t;

typedef struct {
	int32_t ox, oy;
	int32_t span_w, span_h;
	float sea_level;
	GLuint height_tex;
} gl_water_chunk_t;

static struct {
	bool initialized;
	gl_terrain_pipe_t terrain_pipe;
	gl_water_pipe_t water_pipe;
	gl_billboard_pipe_t bb_pipe;

	GLuint grid_vbo;
	GLuint grid_ibo;

	GLuint bb_vbo;
	GLuint atlas_tex;
	uint32_t atlas_ver;

	gl_chunk_tex_t chunk_cache[MAX_CHUNK_TEXTURES];
	uint32_t frame_count;

	gl_water_chunk_t water_queue[MAX_WATER_QUEUE];
	uint32_t water_count;

	float cur_mvp[16];
	float cur_eye[3];
	float cur_target[3];
	float cur_up[3];
	double cur_origin[3];
	float cur_tx;
	float cur_ty;
	float cur_ref_trig[4];
	float cur_dist;
	float cur_pitch_deg;
	float cur_anim_time;
	float cur_sea_level;
	nd_planet_def_t cur_pdef;
} s_gl;

static GLuint compile_shader(GLenum type, const char *src)
{
	GLuint sh = glCreateShader(type);
	if (!sh) return 0;
	glShaderSource(sh, src, strlen(src));
	glCompileShader(sh);
	GLint status = 0;
	status = glGetShaderParameter(sh, GL_COMPILE_STATUS);
	if (!status) {
		glDeleteShader(sh);
		return 0;
	}
	return sh;
}

static GLuint create_program(const char *vs_src, const char *fs_src)
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
	GLint status = glGetProgramParameter(prog, GL_LINK_STATUS);
	glDeleteShader(vs);
	glDeleteShader(fs);
	if (!status) {
		glDeleteProgram(prog);
		return 0;
	}
	return prog;
}

static void init_grid_buffers(void)
{
	float grid_uv[GL_TOTAL_VERTS * 2];
	for (int y = 0; y < GL_GRID_VERTS; y++) {
		for (int x = 0; x < GL_GRID_VERTS; x++) {
			int idx = (y * GL_GRID_VERTS + x) * 2;
			grid_uv[idx]     = (float)x / (float)GL_GRID_QUADS;
			grid_uv[idx + 1] = (float)y / (float)GL_GRID_QUADS;
		}
	}

	uint16_t indices[GL_TOTAL_INDICES];
	int i_idx = 0;
	for (int y = 0; y < GL_GRID_QUADS; y++) {
		for (int x = 0; x < GL_GRID_QUADS; x++) {
			uint16_t i0 = (uint16_t)(y * GL_GRID_VERTS + x);
			uint16_t i1 = (uint16_t)(i0 + 1);
			uint16_t i2 = (uint16_t)((y + 1) * GL_GRID_VERTS + x);
			uint16_t i3 = (uint16_t)(i2 + 1);

			indices[i_idx++] = i0;
			indices[i_idx++] = i1;
			indices[i_idx++] = i2;
			indices[i_idx++] = i1;
			indices[i_idx++] = i3;
			indices[i_idx++] = i2;
		}
	}

	s_gl.grid_vbo = glCreateBuffer();
	glBindBuffer(GL_ARRAY_BUFFER, s_gl.grid_vbo);
	glBufferData(GL_ARRAY_BUFFER, grid_uv, sizeof(grid_uv), GL_STATIC_DRAW);

	s_gl.grid_ibo = glCreateBuffer();
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_gl.grid_ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices, sizeof(indices), GL_STATIC_DRAW);

	s_gl.bb_vbo = glCreateBuffer();
	glBindBuffer(GL_ARRAY_BUFFER, s_gl.bb_vbo);
	glBufferData(GL_ARRAY_BUFFER, NULL, MAX_BB_VERTS * sizeof(nd_seven_gl_bb_vert_t), GL_DYNAMIC_DRAW);
}

static void init_pipelines(void)
{
	/* Terrain pipeline */
	s_gl.terrain_pipe.prog = create_program(s_vs_terrain, s_fs_terrain);
	if (s_gl.terrain_pipe.prog) {
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
		s_gl.terrain_pipe.aGridUV         = glGetAttribLocation(p, "a_grid_uv", 9);
	}

	/* Water pipeline */
	s_gl.water_pipe.prog = create_program(s_vs_water, s_fs_water);
	if (s_gl.water_pipe.prog) {
		GLuint p = s_gl.water_pipe.prog;
		s_gl.water_pipe.uMVP            = glGetUniformLocation(p, "uMVP", 4);
		s_gl.water_pipe.uHeightTex      = glGetUniformLocation(p, "uHeightTex", 10);
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
		s_gl.water_pipe.aGridUV         = glGetAttribLocation(p, "a_grid_uv", 9);
	}

	/* Billboard pipeline */
	s_gl.bb_pipe.prog = create_program(s_vs_billboard, s_fs_billboard);
	if (s_gl.bb_pipe.prog) {
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
		s_gl.bb_pipe.aPos           = glGetAttribLocation(p, "a_pos", 5);
		s_gl.bb_pipe.aUV            = glGetAttribLocation(p, "a_uv", 4);
		s_gl.bb_pipe.aTint          = glGetAttribLocation(p, "a_tint", 6);
		s_gl.bb_pipe.aLocalPos      = glGetAttribLocation(p, "a_local_pos", 11);
	}

	init_grid_buffers();
	s_gl.initialized = true;
}

void nd_seven_gl_begin_frame(uint32_t stage_w, uint32_t stage_h,
                             float tx, float ty, float tz,
                             float dist, float pitch_deg, float fov_deg,
                             float anim_time,
                             const nd_planet_def_t *pdef)
{
	s_gl.frame_count++;
	s_gl.water_count = 0;
	s_gl.cur_sea_level = -999.0f;
	s_gl.cur_dist = (dist > 1.0f) ? dist : ND_SEVEN_CAM_DIST;
	s_gl.cur_pitch_deg = pitch_deg;
	s_gl.cur_anim_time = anim_time;
	s_gl.cur_tx = tx;
	s_gl.cur_ty = ty;
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
	float far_z = (d * 20.0f > 200000.0f) ? (d * 20.0f) : 200000.0f;
	ar_mat4_perspective(proj, fov_rad, aspect, near_z, far_z);
	ar_mat4_multiply(s_gl.cur_mvp, proj, modelview);

	if (!s_gl.initialized) init_pipelines();

	glViewport(0, 0, (GLsizei)stage_w, (GLsizei)stage_h);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_TRUE);
	glDisable(GL_CULL_FACE);

	float space_t = fminf(1.0f, fmaxf(0.0f, (s_gl.cur_dist - 12000.0f) / 40000.0f));
	float sky_r = 0.55f * (1.0f - space_t) + 0.012f * space_t;
	float sky_g = 0.70f * (1.0f - space_t) + 0.016f * space_t;
	float sky_b = 0.85f * (1.0f - space_t) + 0.035f * space_t;

	glClearColor(sky_r, sky_g, sky_b, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
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
	if (!s_gl.initialized || !s_gl.terrain_pipe.prog) return;
	const nd_planet_def_t *eff_pdef = pdef ? pdef : &s_gl.cur_pdef;

	if (sea_level > -900.0f) {
		s_gl.cur_sea_level = sea_level;
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
	                     slot->world != world || slot->ox != ox || slot->oy != oy);

	if (needs_upload) {
		slot->active = 1;
		slot->world = world;
		slot->level = level;
		slot->rev = rev;
		slot->ox = ox;
		slot->oy = oy;
		slot->span_w = span_w;
		slot->span_h = span_h;

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
	glUniform1f(s_gl.terrain_pipe.uSeaLevel, sea_level);
	glUniform3fv(s_gl.terrain_pipe.uCameraPos, 1, s_gl.cur_eye);
	glUniform3fv(s_gl.terrain_pipe.uPlanetCenter, 1, rel_center);
	glUniform2f(s_gl.terrain_pipe.uTorusRadii, eff_pdef->major_r, eff_pdef->minor_r);
	glUniform2f(s_gl.terrain_pipe.uWorldDim, ND_SEVEN_WORLD_DIM_W, ND_SEVEN_WORLD_DIM_H);

	glUniform1f(s_gl.terrain_pipe.uFogStart, s_gl.cur_dist * 1.5f);
	glUniform1f(s_gl.terrain_pipe.uFogEnd, s_gl.cur_dist * 4.0f);
	glUniform1f(s_gl.terrain_pipe.uAnimTime, s_gl.cur_anim_time);

	float space_t = fminf(1.0f, fmaxf(0.0f, (s_gl.cur_dist - 12000.0f) / 40000.0f));
	glUniform3f(s_gl.terrain_pipe.uSkyColor,
	            0.55f * (1.0f - space_t) + 0.012f * space_t,
	            0.70f * (1.0f - space_t) + 0.016f * space_t,
	            0.85f * (1.0f - space_t) + 0.035f * space_t);
	glUniform1f(s_gl.terrain_pipe.uAtmoIntensity, fminf(1.0f, fmaxf(0.0f, (s_gl.cur_dist - 1200.0f) / 8000.0f)));

	glBindBuffer(GL_ARRAY_BUFFER, s_gl.grid_vbo);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, 0);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_gl.grid_ibo);
	glDrawElements(GL_TRIANGLES, GL_TOTAL_INDICES, GL_UNSIGNED_SHORT, 0);

	/* Queue water pass for this chunk if world has water */
	if (sea_level > -900.0f && s_gl.water_count < MAX_WATER_QUEUE) {
		s_gl.water_queue[s_gl.water_count].ox = ox;
		s_gl.water_queue[s_gl.water_count].oy = oy;
		s_gl.water_queue[s_gl.water_count].span_w = span_w;
		s_gl.water_queue[s_gl.water_count].span_h = span_h;
		s_gl.water_queue[s_gl.water_count].sea_level = sea_level;
		s_gl.water_queue[s_gl.water_count].height_tex = slot->height_tex;
		s_gl.water_count++;
	}
}

void nd_seven_gl_flush_water(void)
{
	if (!s_gl.initialized || !s_gl.water_pipe.prog || s_gl.water_count == 0) {
		s_gl.water_count = 0;
		return;
	}

	glUseProgram(s_gl.water_pipe.prog);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_FALSE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);

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

	glBindBuffer(GL_ARRAY_BUFFER, s_gl.grid_vbo);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_gl.grid_ibo);

	for (uint32_t i = 0; i < s_gl.water_count; i++) {
		const gl_water_chunk_t *chk = &s_gl.water_queue[i];

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, chk->height_tex);
		glUniform1i(s_gl.water_pipe.uHeightTex, 0);

		glUniform2f(s_gl.water_pipe.uChunkOrigin, (float)chk->ox, (float)chk->oy);
		glUniform2f(s_gl.water_pipe.uChunkRelOrigin,
		            (float)((double)chk->ox - (double)s_gl.cur_tx),
		            (float)((double)chk->oy - (double)s_gl.cur_ty));
		glUniform2f(s_gl.water_pipe.uChunkSpan, (float)chk->span_w, (float)chk->span_h);
		glUniform1f(s_gl.water_pipe.uSeaLevel, chk->sea_level);

		glDrawElements(GL_TRIANGLES, GL_TOTAL_INDICES, GL_UNSIGNED_SHORT, 0);
	}

	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDepthMask(GL_TRUE);
	s_gl.water_count = 0;
}

void nd_seven_gl_flush_billboards(const lam_billboard_gpu_t *batch,
                                  uint32_t count,
                                  const uint32_t *atlas_rgba)
{
	if (!batch || count == 0) return;
	if (!s_gl.initialized || !s_gl.bb_pipe.prog) return;

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
	uint32_t n_quads = (count * 6 > MAX_BB_VERTS) ? (MAX_BB_VERTS / 6) : count;

	for (uint32_t i = 0; i < n_quads; i++) {
		eval_billboard_quad_with_origin(&batch[i], s_gl.cur_pitch_deg, &s_gl.cur_pdef,
		                                s_gl.cur_origin, &bb_verts[i * 6]);
	}

	glUseProgram(s_gl.bb_pipe.prog);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	/* Disable depth writes across billboards so overlapping sprites strictly follow back-to-front painter's order without Z-fighting */
	glDepthMask(GL_FALSE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, s_gl.atlas_tex);
	glUniform1i(s_gl.bb_pipe.uAtlasTex, 0);

	glUniformMatrix4fv(s_gl.bb_pipe.uMVP, 1, GL_FALSE, s_gl.cur_mvp);
	glUniform1f(s_gl.bb_pipe.uSeaLevel, s_gl.cur_sea_level);
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

	glBindBuffer(GL_ARRAY_BUFFER, s_gl.bb_vbo);
	glBufferData(GL_ARRAY_BUFFER, bb_verts, n_quads * 6 * sizeof(nd_seven_gl_bb_vert_t), GL_DYNAMIC_DRAW);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(nd_seven_gl_bb_vert_t), 0);

	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(nd_seven_gl_bb_vert_t), (size_t)12);

	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(nd_seven_gl_bb_vert_t), (size_t)20);

	glEnableVertexAttribArray(3);
	glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(nd_seven_gl_bb_vert_t), (size_t)36);

	glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(n_quads * 6));

	glDisableVertexAttribArray(1);
	glDisableVertexAttribArray(2);
	glDisableVertexAttribArray(3);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDepthMask(GL_TRUE);
}
