/*
 * nd_seven_gl.c — Pure-C WebGL2 3D Terrain, Water, and Billboard Renderer.
 * Part of the NeverDark Seven 2.5D diorama platform.
 *
 * Implements the 3D rendering pipeline entirely in pure C over <ttypt/webgl.h>.
 * Dual-compiles for native SSR (safe no-op stubs) and WebAssembly (WebGL2).
 */

#include "nd_seven_gl.h"
#include "nd_seven_atlas.h"
#include <ttypt/webgl.h>
#include <ttypt/areole.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

#define PI_F 3.14159265358979323846f
#define TWO_PI_F 6.28318530717958647692f

/* Level 1+ standard grid: 32x32 quads = 33x33 vertices = 1,089 vertices (2,048 triangles) */
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

/* Level 0 close-zoom 2x subdivision grid: 96x96 quads = 97x97 vertices = 9,409 vertices (18,432 triangles, 4x normal zoom) */
#define GL_GRID_QUADS_Z2 96
#define GL_GRID_VERTS_Z2 (GL_GRID_QUADS_Z2 + 1)
#define GL_TOTAL_VERTS_Z2 (GL_GRID_VERTS_Z2 * GL_GRID_VERTS_Z2)
#define GL_TOTAL_INDICES_Z2 (GL_GRID_QUADS_Z2 * GL_GRID_QUADS_Z2 * 6)

/* Level 0 ultra-close-zoom 3x subdivision grid: 144x144 quads = 145x145 vertices = 21,025 vertices (41,472 triangles, 9x normal zoom) */
#define GL_GRID_QUADS_Z3 144
#define GL_GRID_VERTS_Z3 (GL_GRID_QUADS_Z3 + 1)
#define GL_TOTAL_VERTS_Z3 (GL_GRID_VERTS_Z3 * GL_GRID_VERTS_Z3)
#define GL_TOTAL_INDICES_Z3 (GL_GRID_QUADS_Z3 * GL_GRID_QUADS_Z3 * 6)

#define MAX_CHUNK_TEXTURES 192
#define MAX_WATER_QUEUE 192
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
	"vec2 chaff_grad_dir(int cx, int cy, uint seed) {\n"
	"    uint h = chaff_hash3(ivec3(cx, cy, 0), seed);\n"
	"    float gx = float(int(h & 0xFFFFu) - 32768) * (1.0 / 32768.0);\n"
	"    float gy = float(int(h >> 16u) - 32768) * (1.0 / 32768.0);\n"
	"    vec2 g = vec2(gx, gy);\n"
	"    return g * inversesqrt(dot(g, g) + 1e-6);\n"
	"}\n"
	"vec3 chaff_grad_noise2d(vec2 p, uint seed) {\n"
	"    vec2 ip = floor(p);\n"
	"    vec2 fp = p - ip;\n"
	"    vec2 u = fp * fp * fp * (fp * (fp * 6.0 - 15.0) + 10.0);\n"
	"    vec2 du = 30.0 * fp * fp * (fp * (fp - 2.0) + 1.0);\n"
	"    ivec2 c = ivec2(ip);\n"
	"    vec2 g00 = chaff_grad_dir(c.x,     c.y,     seed);\n"
	"    vec2 g10 = chaff_grad_dir(c.x + 1, c.y,     seed);\n"
	"    vec2 g01 = chaff_grad_dir(c.x,     c.y + 1, seed);\n"
	"    vec2 g11 = chaff_grad_dir(c.x + 1, c.y + 1, seed);\n"
	"    float v00 = dot(g00, fp);\n"
	"    float v10 = dot(g10, fp - vec2(1.0, 0.0));\n"
	"    float v01 = dot(g01, fp - vec2(0.0, 1.0));\n"
	"    float v11 = dot(g11, fp - vec2(1.0, 1.0));\n"
	"    float k0 = v10 - v00;\n"
	"    float k1 = v01 - v00;\n"
	"    float k2 = v11 - v01 - k0;\n"
	"    float val = 0.5 + 0.70710678 * (v00 + k0 * u.x + k1 * u.y + k2 * u.x * u.y);\n"
	"    vec2 gInterp = g00 + (g10 - g00) * u.x + (g01 - g00) * u.y + (g11 - g01 - g10 + g00) * (u.x * u.y);\n"
	"    vec2 grad = 0.70710678 * (gInterp + du * vec2(k0 + k2 * u.y, k1 + k2 * u.x));\n"
	"    return vec3(clamp(val, 0.0, 1.0), grad.x, grad.y);\n"
	"}\n"
	"float sampleHeightSmooth(vec2 uv, out vec2 out_grad) {\n"
	"    vec2 texSize = vec2(textureSize(uHeightTex, 0));\n"
	"    vec2 maxCoordF = max(vec2(1.0), texSize - 1.0);\n"
	"    vec2 uvc = clamp(uv, 0.0, 1.0);\n"
	"    vec2 pixelCoord = uvc * maxCoordF;\n"
	"    ivec2 iCoord = ivec2(floor(pixelCoord));\n"
	"    vec2 f = pixelCoord - vec2(iCoord);\n"
	"    vec2 w = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);\n"
	"    if (uvc.x <= 0.0001 || uvc.x >= 0.9999) w.y = f.y;\n"
	"    if (uvc.y <= 0.0001 || uvc.y >= 0.9999) w.x = f.x;\n"
	"    ivec2 maxCoord = ivec2(maxCoordF);\n"
	"    float h00 = getH(iCoord, maxCoord);\n"
	"    float h10 = getH(iCoord + ivec2(1, 0), maxCoord);\n"
	"    float h01 = getH(iCoord + ivec2(0, 1), maxCoord);\n"
	"    float h11 = getH(iCoord + ivec2(1, 1), maxCoord);\n"
	"    float hM0 = getH(iCoord + ivec2(-1, 0), maxCoord);\n"
	"    float h20 = getH(iCoord + ivec2(2, 0), maxCoord);\n"
	"    float hM1 = getH(iCoord + ivec2(-1, 1), maxCoord);\n"
	"    float h21 = getH(iCoord + ivec2(2, 1), maxCoord);\n"
	"    float h0M = getH(iCoord + ivec2(0, -1), maxCoord);\n"
	"    float h1M = getH(iCoord + ivec2(1, -1), maxCoord);\n"
	"    float h02 = getH(iCoord + ivec2(0, 2), maxCoord);\n"
	"    float h12 = getH(iCoord + ivec2(1, 2), maxCoord);\n"
	"    float b0 = mix(h00, h10, w.x);\n"
	"    float b1 = mix(h01, h11, w.x);\n"
	"    float height = mix(b0, b1, w.y);\n"
	"    float cx0 = (iCoord.x <= 0) ? 1.0 : 0.5;\n"
	"    float cx1 = (iCoord.x + 1 >= maxCoord.x) ? 1.0 : 0.5;\n"
	"    float cy0 = (iCoord.y <= 0) ? 1.0 : 0.5;\n"
	"    float cy1 = (iCoord.y + 1 >= maxCoord.y) ? 1.0 : 0.5;\n"
	"    vec2 g00 = vec2((h10 - hM0) * cx0, (h01 - h0M) * cy0);\n"
	"    vec2 g10 = vec2((h20 - h00) * cx1, (h11 - h1M) * cy0);\n"
	"    vec2 g01 = vec2((h11 - hM1) * cx0, (h02 - h00) * cy1);\n"
	"    vec2 g11 = vec2((h21 - h01) * cx1, (h12 - h10) * cy1);\n"
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
	"    vec2 worldXY = vec2(uChunkOrigin.x + a_grid_uv.x * uChunkSpan.x, uChunkOrigin.y + a_grid_uv.y * uChunkSpan.y);\n"
	"    vec2 relXY = vec2(uChunkRelOrigin.x + a_grid_uv.x * uChunkSpan.x, uChunkRelOrigin.y + a_grid_uv.y * uChunkSpan.y);\n"
	"    float edgeFade = clamp(min(min(a_grid_uv.x, 1.0 - a_grid_uv.x), min(a_grid_uv.y, 1.0 - a_grid_uv.y)) * 12.0, 0.0, 1.0);\n"
	"    if (uChunkSpan.x <= 160.5 && edgeFade > 0.001) {\n"
	"        float camHoriz = length(relXY);\n"
	"        float shoreClear = (uSeaLevel <= -900.0) ? 1.0 : smoothstep(uSeaLevel + 0.5, uSeaLevel + 3.0, wz);\n"
	"        float closeW = clamp((220.0 - camHoriz) / 140.0, 0.0, 1.0) * edgeFade * shoreClear;\n"
	"        if (closeW > 0.001) {\n"
	"            vec3 g1 = chaff_grad_noise2d(worldXY * 0.22, 0xA341316Cu);\n"
	"            vec3 g2 = chaff_grad_noise2d(worldXY * 0.68, 0xC8013EA4u);\n"
	"            float disp = ((g1.x - 0.5) * 1.15 + (g2.x - 0.5) * 0.45) * closeW;\n"
	"            wz += disp;\n"
	"            grad += (g1.yz * (0.22 * 1.15) + g2.yz * (0.68 * 0.45)) * (closeW / effectiveScale);\n"
	"        }\n"
	"    }\n"
	"    vZ = wz;\n"
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
	"out vec4 FragColor;\n"
	"float getH(ivec2 p, ivec2 maxC) {\n"
	"    return float(texelFetch(uHeightTex, clamp(p, ivec2(0), maxC), 0).r);\n"
	"}\n"
	"float sampleHeightSmooth(vec2 uv, out vec2 out_grad) {\n"
	"    vec2 texSize = vec2(textureSize(uHeightTex, 0));\n"
	"    vec2 maxCoordF = max(vec2(1.0), texSize - 1.0);\n"
	"    vec2 uvc = clamp(uv, 0.0, 1.0);\n"
	"    vec2 pixelCoord = uvc * maxCoordF;\n"
	"    ivec2 iCoord = ivec2(floor(pixelCoord));\n"
	"    vec2 f = pixelCoord - vec2(iCoord);\n"
	"    vec2 w = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);\n"
	"    if (uvc.x <= 0.0001 || uvc.x >= 0.9999) w.y = f.y;\n"
	"    if (uvc.y <= 0.0001 || uvc.y >= 0.9999) w.x = f.x;\n"
	"    ivec2 maxCoord = ivec2(maxCoordF);\n"
	"    float h00 = getH(iCoord, maxCoord);\n"
	"    float h10 = getH(iCoord + ivec2(1, 0), maxCoord);\n"
	"    float h01 = getH(iCoord + ivec2(0, 1), maxCoord);\n"
	"    float h11 = getH(iCoord + ivec2(1, 1), maxCoord);\n"
	"    float hM0 = getH(iCoord + ivec2(-1, 0), maxCoord);\n"
	"    float h20 = getH(iCoord + ivec2(2, 0), maxCoord);\n"
	"    float hM1 = getH(iCoord + ivec2(-1, 1), maxCoord);\n"
	"    float h21 = getH(iCoord + ivec2(2, 1), maxCoord);\n"
	"    float h0M = getH(iCoord + ivec2(0, -1), maxCoord);\n"
	"    float h1M = getH(iCoord + ivec2(1, -1), maxCoord);\n"
	"    float h02 = getH(iCoord + ivec2(0, 2), maxCoord);\n"
	"    float h12 = getH(iCoord + ivec2(1, 2), maxCoord);\n"
	"    float b0 = mix(h00, h10, w.x);\n"
	"    float b1 = mix(h01, h11, w.x);\n"
	"    float height = mix(b0, b1, w.y);\n"
	"    float cx0 = (iCoord.x <= 0) ? 1.0 : 0.5;\n"
	"    float cx1 = (iCoord.x + 1 >= maxCoord.x) ? 1.0 : 0.5;\n"
	"    float cy0 = (iCoord.y <= 0) ? 1.0 : 0.5;\n"
	"    float cy1 = (iCoord.y + 1 >= maxCoord.y) ? 1.0 : 0.5;\n"
	"    vec2 g00 = vec2((h10 - hM0) * cx0, (h01 - h0M) * cy0);\n"
	"    vec2 g10 = vec2((h20 - h00) * cx1, (h11 - h1M) * cy0);\n"
	"    vec2 g01 = vec2((h11 - hM1) * cx0, (h02 - h00) * cy1);\n"
	"    vec2 g11 = vec2((h21 - h01) * cx1, (h12 - h10) * cy1);\n"
	"    vec2 gx0 = mix(g00, g10, w.x);\n"
	"    vec2 gx1 = mix(g01, g11, w.x);\n"
	"    vec2 smoothGrad = mix(gx0, gx1, w.y);\n"
	"    out_grad.x = smoothGrad.x * (maxCoordF.x / max(1.0, uChunkSpan.x));\n"
	"    out_grad.y = smoothGrad.y * (maxCoordF.y / max(1.0, uChunkSpan.y));\n"
	"    return height;\n"
	"}\n"
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
	"vec2 chaff_grad_dir(int cx, int cy, uint seed) {\n"
	"    uint h = chaff_hash3(ivec3(cx, cy, 0), seed);\n"
	"    float gx = float(int(h & 0xFFFFu) - 32768) * (1.0 / 32768.0);\n"
	"    float gy = float(int(h >> 16u) - 32768) * (1.0 / 32768.0);\n"
	"    vec2 g = vec2(gx, gy);\n"
	"    return g * inversesqrt(dot(g, g) + 1e-6);\n"
	"}\n"
	"vec3 chaff_grad_noise2d(vec2 p, uint seed) {\n"
	"    vec2 ip = floor(p);\n"
	"    vec2 fp = p - ip;\n"
	"    vec2 u = fp * fp * fp * (fp * (fp * 6.0 - 15.0) + 10.0);\n"
	"    vec2 du = 30.0 * fp * fp * (fp * (fp - 2.0) + 1.0);\n"
	"    ivec2 c = ivec2(ip);\n"
	"    vec2 g00 = chaff_grad_dir(c.x,     c.y,     seed);\n"
	"    vec2 g10 = chaff_grad_dir(c.x + 1, c.y,     seed);\n"
	"    vec2 g01 = chaff_grad_dir(c.x,     c.y + 1, seed);\n"
	"    vec2 g11 = chaff_grad_dir(c.x + 1, c.y + 1, seed);\n"
	"    float v00 = dot(g00, fp);\n"
	"    float v10 = dot(g10, fp - vec2(1.0, 0.0));\n"
	"    float v01 = dot(g01, fp - vec2(0.0, 1.0));\n"
	"    float v11 = dot(g11, fp - vec2(1.0, 1.0));\n"
	"    float k0 = v10 - v00;\n"
	"    float k1 = v01 - v00;\n"
	"    float k2 = v11 - v01 - k0;\n"
	"    float val = 0.5 + 0.70710678 * (v00 + k0 * u.x + k1 * u.y + k2 * u.x * u.y);\n"
	"    vec2 gInterp = g00 + (g10 - g00) * u.x + (g01 - g00) * u.y + (g11 - g01 - g10 + g00) * (u.x * u.y);\n"
	"    vec2 grad = 0.70710678 * (gInterp + du * vec2(k0 + k2 * u.y, k1 + k2 * u.x));\n"
	"    return vec3(clamp(val, 0.0, 1.0), grad.x, grad.y);\n"
	"}\n"
	"vec4 eval_zoom_terrain_detail(vec2 pos, float dist, float fw, out vec4 out_octaves) {\n"
	"    const float freqs[4] = float[4](0.16, 0.85, 4.20, 18.00);\n"
	"    const float amps[4]  = float[4](0.28, 0.24, 0.18, 0.12);\n"
	"    const float dFar[4]  = float[4](6500.0, 2200.0, 780.0, 240.0);\n"
	"    const float dNear[4] = float[4](1600.0,  550.0, 180.0,  38.0);\n"
	"    const uint  seeds[4] = uint[4](0xA341316Cu, 0xC8013EA4u, 0x3891053Bu, 0x85EBCA6Bu);\n"
	"    const mat2  rot  = mat2(0.8, -0.6, 0.6, 0.8);\n"
	"    const mat2  irot = mat2(0.8,  0.6, -0.6, 0.8);\n"
	"    float h = 0.0;\n"
	"    vec2 grad = vec2(0.0);\n"
	"    float cavity = 0.0;\n"
	"    vec2 pRot = pos;\n"
	"    mat2 curInvRot = mat2(1.0, 0.0, 0.0, 1.0);\n"
	"    float oct[4] = float[4](0.5, 0.5, 0.5, 0.5);\n"
	"    for (int i = 0; i < 4; i++) {\n"
	"        float wDist = clamp((dFar[i] - dist) / max(1.0, dFar[i] - dNear[i]), 0.0, 1.0);\n"
	"        float wNyq  = smoothstep(0.55, 0.18, freqs[i] * fw);\n"
	"        float w = wDist * wNyq;\n"
	"        if (w > 0.001) {\n"
	"            vec3 g = chaff_grad_noise2d(pRot * freqs[i], seeds[i]);\n"
	"            oct[i] = mix(0.5, g.x, w);\n"
	"            float signedVal = g.x - 0.5;\n"
	"            vec2 worldG = curInvRot * g.yz;\n"
	"            h += signedVal * amps[i] * w;\n"
	"            grad += worldG * (freqs[i] * amps[i] * w);\n"
	"            if (i >= 1) {\n"
	"                cavity += max(0.0, 0.48 - g.x) * (amps[i] * 2.4 * w);\n"
	"            }\n"
	"            pRot += g.yz * (0.08 * w);\n"
	"        }\n"
	"        pRot = rot * pRot;\n"
	"        curInvRot = curInvRot * irot;\n"
	"    }\n"
	"    out_octaves = vec4(oct[0], oct[1], oct[2], oct[3]);\n"
	"    return vec4(h, grad.x, grad.y, clamp(cavity, 0.0, 0.45));\n"
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
	"void main() {\n"
	"    float fragZ = vZ;\n"
	"    vec3 fragNormal = normalize(vNormal);\n"
	"    vec3 torusN = normalize(vTorusNormal);\n"
	"    vec3 tangentU = normalize(vTangentU);\n"
	"    vec3 tangentV = normalize(vTangentV);\n"
	"    float fragWaterDepth = vWaterDepth;\n"
	"    if (vDistance < 2400.0) {\n"
	"        float scale = (uHeightScale != 0.0) ? uHeightScale : 1.0;\n"
	"        float effectiveScale = scale * 0.0625;\n"
	"        vec2 fragGrad;\n"
	"        float raw_h = sampleHeightSmooth(vUV, fragGrad);\n"
	"        fragZ = raw_h * effectiveScale;\n"
	"        fragWaterDepth = (uSeaLevel > -900.0) ? (uSeaLevel - fragZ) : -999.0;\n"
	"        fragNormal = normalize(torusN - fragGrad.x * effectiveScale * tangentU - fragGrad.y * effectiveScale * tangentV);\n"
	"    }\n"
	"    float fw = max(1e-4, max(length(dFdx(vWorldPos)), length(dFdy(vWorldPos))));\n"
	"    vec4 octaves = vec4(0.5);\n"
	"    vec4 detail = (fragWaterDepth <= 14.0) ? eval_zoom_terrain_detail(vWorldPos, vDistance, fw, octaves) : vec4(0.0);\n"
	"    vec2 edgeMask = 4.0 * vUV * (1.0 - vUV);\n"
	"    vec2 sampleUV = vUV + detail.yz * (0.010 * edgeMask);\n"
	"    if (fragWaterDepth > 0.0) {\n"
	"        float depthFactor = clamp(fragWaterDepth / 10.0, 0.0, 1.0);\n"
	"        float tRef = mod(uAnimTime, 256.0);\n"
	"        vec3 wRef1 = chaff_grad_noise2d(vWorldPos * 0.22 + vec2(tRef * 0.55, -tRef * 0.45), 0x57415631u);\n"
	"        vec3 wRef2 = chaff_grad_noise2d(vWorldPos * 0.52 + vec2(-tRef * 0.80, tRef * 0.65), 0x57415632u);\n"
	"        vec2 refractOffset = (wRef1.yz * 0.65 + wRef2.yz * 0.35) * (0.015 * depthFactor * edgeMask);\n"
	"        sampleUV = clamp(vUV + refractOffset, 0.0, 1.0);\n"
	"    }\n"
	"    vec4 baseCol = sampleSubstrateSmooth(sampleUV);\n"
	"    vec3 L_frag = normalize(vec3(-0.577, -0.577, 0.816));\n"
	"    float baseDiff = max(0.0, dot(fragNormal, L_frag));\n"
	"    float effLighting = (vDistance < 2400.0) ? (0.45 + 0.55 * baseDiff) : vLighting;\n"
	"    if (fragWaterDepth <= 0.25) {\n"
	"        float slope = 1.0 - max(0.0, dot(fragNormal, torusN));\n"
	"        float elevAboveWater = (uSeaLevel > -900.0) ? max(0.0, fragZ - uSeaLevel) : max(0.0, fragZ);\n"
	"        float h0 = detail.x;\n"
	"        vec2 microGrad = detail.yz;\n"
	"        float microCavity = detail.w;\n"
	"        float nMeso  = octaves.x;\n"
	"        float nStd   = octaves.y;\n"
	"        float nClose = octaves.z;\n"
	"        float nUltra = octaves.w;\n"
	"        float macroFade = clamp((7500.0 - vDistance) / 5000.0, 0.0, 1.0);\n"
	"        float distFade  = clamp(1.0 - (vDistance - 600.0) / 1800.0, 0.0, 1.0);\n"
	"        float closeZoom = clamp((380.0 - vDistance) / 260.0, 0.0, 1.0);\n"
	"        float ultraZoom = clamp((160.0 - vDistance) / 120.0, 0.0, 1.0);\n"
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
	"                baseCol.rgb = mix(baseCol.rgb, screeCol, talusFactor * 0.65 * distFade);\n"
	"            }\n"
	"            baseCol.rgb = mix(baseCol.rgb, rockCol, rockFactor);\n"
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
	"                baseCol.rgb = mix(baseCol.rgb, loamCol, soilFactor * distFade);\n"
	"            }\n"
	"            bool isSandBiome = (baseCol.r > 0.55 && baseCol.g > 0.45 && baseCol.b < 0.45);\n"
	"            float shoreFactor = smoothstep(6.0, 0.4, elevAboveWater) * (1.0 - rockFactor);\n"
	"            float sandSparkle = 0.0;\n"
	"            float wetSand = 0.0;\n"
	"            float sandBlend = 0.0;\n"
	"            if ((isSandBiome || shoreFactor > 0.0) && distFade > 0.0) {\n"
	"                float duneWave = nMeso * 0.45 + nStd * 0.55;\n"
	"                wetSand = smoothstep(2.6, 0.0, elevAboveWater);\n"
	"                vec3 drySandCol = vec3(0.75, 0.67, 0.49) * (0.88 + 0.22 * duneWave + 0.16 * (nClose - 0.5) * closeZoom + 0.10 * (nUltra - 0.5) * ultraZoom);\n"
	"                vec3 wetSandCol = vec3(0.50, 0.42, 0.30) * (0.90 + 0.18 * nStd + 0.12 * (nClose - 0.5) * closeZoom);\n"
	"                vec3 sandCol = mix(drySandCol, wetSandCol, wetSand);\n"
	"                sandBlend = isSandBiome ? 1.0 : shoreFactor;\n"
	"                baseCol.rgb = mix(baseCol.rgb, sandCol, sandBlend * distFade);\n"
	"                sandSparkle = smoothstep(0.68, 0.88, nClose * 0.45 + nUltra * 0.55) * closeZoom;\n"
	"            }\n"
	"            if (rockFactor < 0.65 && !isSandBiome && shoreFactor < 0.7 && baseCol.g > baseCol.r * 1.02) {\n"
	"                vec3 deepMoss = baseCol.rgb * vec3(0.78, 0.96, 0.76);\n"
	"                vec3 lushTurf = baseCol.rgb * vec3(0.92, 1.09, 0.88);\n"
	"                vec3 warmClover = baseCol.rgb * vec3(1.12, 1.04, 0.74);\n"
	"                vec3 vegCol = mix(deepMoss, lushTurf, smoothstep(0.25, 0.72, nMeso));\n"
	"                vegCol = mix(vegCol, warmClover, smoothstep(0.38, 0.76, nStd) * 0.70);\n"
	"                float bladeRelief = (nStd - 0.5) * 0.16 * distFade\n"
	"                                  + (nClose - 0.5) * 0.20 * closeZoom\n"
	"                                  + (nUltra - 0.5) * 0.14 * ultraZoom;\n"
	"                vegCol *= clamp(1.0 + bladeRelief, 0.76, 1.26);\n"
	"                baseCol.rgb = mix(baseCol.rgb, vegCol, macroFade);\n"
	"            }\n"
	"            float normStrength = 0.24 + 0.22 * distFade + 0.16 * closeZoom;\n"
	"            vec3 N_frag = normalize(fragNormal - microGrad.x * normStrength * tangentU - microGrad.y * normStrength * tangentV);\n"
	"            vec3 viewDir = normalize(vViewDir);\n"
	"            vec3 H_half = normalize(L_frag + viewDir);\n"
	"            float perFragDiff = max(0.0, dot(N_frag, L_frag));\n"
	"            float cavityAO = clamp(1.0 - slope * 0.26 + h0 * 0.26 - microCavity * 0.42 - rockCrevice * 0.34, 0.54, 1.05);\n"
	"            float fragLit = 0.40 * cavityAO + 0.60 * perFragDiff * cavityAO;\n"
	"            effLighting = mix(effLighting, fragLit, macroFade);\n"
	"            float microShade = clamp(1.0 + h0 * (0.30 + 0.16 * closeZoom) * macroFade, 0.72, 1.28);\n"
	"            baseCol.rgb *= microShade;\n"
	"            if (sandBlend > 0.0 && distFade > 0.0) {\n"
	"                float sandSpecAngle = max(0.0, dot(N_frag, H_half));\n"
	"                float wetFilmSpec = pow(sandSpecAngle, 36.0) * (0.38 * wetSand + 0.07 * (1.0 - wetSand));\n"
	"                float quartzGlint = pow(sandSpecAngle, 64.0) * sandSparkle * 0.45;\n"
	"                float sandSpec = (wetFilmSpec + quartzGlint) * sandBlend * distFade;\n"
	"                baseCol.rgb += vec3(sandSpec * 0.94, sandSpec * 0.97, sandSpec * 1.0);\n"
	"            }\n"
	"            if (rockFactor > 0.25 && distFade > 0.0) {\n"
	"                float specAngle = max(0.0, dot(N_frag, H_half));\n"
	"                float spec = pow(specAngle, 24.0) * (rockFactor * (0.20 + 0.12 * closeZoom) * (1.0 - rockCrevice) * distFade);\n"
	"                baseCol.rgb += vec3(spec);\n"
	"            }\n"
	"        }\n"
	"    } else {\n"
	"        /* Submerged seabed substrate: sandy littoral shallows, pebbles, and benthic rock relief */\n"
	"        float slope = 1.0 - max(0.0, dot(fragNormal, torusN));\n"
	"        float distFade  = clamp(1.0 - (vDistance - 900.0) / 2400.0, 0.0, 1.0);\n"
	"        float closeZoom = clamp((380.0 - vDistance) / 260.0, 0.0, 1.0);\n"
	"        float ultraZoom = clamp((160.0 - vDistance) / 120.0, 0.0, 1.0);\n"
	"        float nMeso  = octaves.x;\n"
	"        float nStd   = octaves.y;\n"
	"        float nClose = octaves.z;\n"
	"        float nUltra = octaves.w;\n"
	"        float shallowSandMask = smoothstep(12.0, 1.5, fragWaterDepth) * (1.0 - smoothstep(0.08, 0.22, slope));\n"
	"        float sandRipple = (nStd - 0.5) * 0.22 + (nClose - 0.5) * 0.18 * closeZoom + (nUltra - 0.5) * 0.12 * ultraZoom;\n"
	"        vec3 littoralSand = vec3(0.68, 0.60, 0.44) * clamp(0.92 + sandRipple, 0.72, 1.22);\n"
	"        float pebble = smoothstep(0.64, 0.85, nClose * 0.5 + nUltra * 0.5) * closeZoom;\n"
	"        littoralSand = mix(littoralSand, vec3(0.45, 0.43, 0.38), pebble * 0.45);\n"
	"        vec3 benthicSubstrate = mix(baseCol.rgb, vec3(0.42, 0.46, 0.40), 0.35);\n"
	"        baseCol.rgb = mix(benthicSubstrate, littoralSand, shallowSandMask * (0.45 + 0.55 * distFade));\n"
	"        if (distFade > 0.01 && fragWaterDepth < 14.0) {\n"
	"            vec3 N_sub = normalize(fragNormal - detail.y * (0.30 * distFade) * tangentU - detail.z * (0.30 * distFade) * tangentV);\n"
	"            float subDiff = max(0.0, dot(N_sub, L_frag));\n"
	"            float subCavity = clamp(1.0 + detail.x * 0.28 - detail.w * 0.38, 0.62, 1.18);\n"
	"            effLighting = mix(effLighting, (0.45 + 0.55 * subDiff) * subCavity, distFade);\n"
	"        }\n"
	"    }\n"
	"    vec3 lit = baseCol.rgb * effLighting;\n"
	"    if (fragWaterDepth > 0.0) {\n"
	"        float d = clamp(fragWaterDepth, 0.0, 48.0);\n"
	"        float wetBlend = smoothstep(0.0, 0.22, fragWaterDepth);\n"
	"        float distFade = clamp(1.0 - (vDistance - 900.0) / 2500.0, 0.0, 1.0);\n"
	"        float closeZoom = clamp((420.0 - vDistance) / 300.0, 0.0, 1.0);\n"
	"        float caustic = 0.0;\n"
	"        if (distFade > 0.01 && d < 18.0) {\n"
	"            float tC = mod(uAnimTime, 256.0);\n"
	"            float c1 = chaff_sample_3d(vec3(vWorldPos * 1.85, tC * 3.2), 3u, 0x1337u);\n"
	"            float c2 = chaff_sample_3d(vec3((vWorldPos + vec2(37.0, -23.0)) * 1.85, tC * 2.4), 3u, 0xBEEFu);\n"
	"            float diff1 = abs(c1 - c2);\n"
	"            float caustMeso = pow(clamp(1.0 - diff1 * 6.0, 0.0, 1.0), 3.0);\n"
	"            float caustFine = 0.0;\n"
	"            if (closeZoom > 0.01) {\n"
	"                float c3 = chaff_sample_3d(vec3(vWorldPos * 5.2, tC * 4.6), 2u, 0xCA57u);\n"
	"                float c4 = chaff_sample_3d(vec3((vWorldPos + vec2(-19.0, 41.0)) * 5.2, tC * 3.8), 2u, 0xCA58u);\n"
	"                caustFine = pow(clamp(1.0 - abs(c3 - c4) * 6.0, 0.0, 1.0), 3.0) * closeZoom;\n"
	"            }\n"
	"            caustic = (caustMeso + caustFine * 0.65) * distFade;\n"
	"        }\n"
	"        float causticFocus = smoothstep(0.0, 0.65, d) * exp(-0.14 * d);\n"
	"        vec3 seabedCaustic = vec3(0.48, 0.84, 1.0) * (caustic * 0.62 * causticFocus);\n"
	"        vec3 viewDir = normalize(vViewDir);\n"
	"        float cosView = max(0.28, dot(torusN, viewDir));\n"
	"        /* Non-linear shallow optical path: keeps 0..3 unit wading shallows crystal-clear (>85% transmission)\n"
	"         * before ramping into rich turquoise-cerulean shelf (3..10) and deep midnight sapphire (12+). */\n"
	"        float shallowFactor = smoothstep(0.0, 10.0, d);\n"
	"        float effectiveOpticalPath = (d * (0.22 + 0.78 * shallowFactor)) / cosView;\n"
	"        vec3 seabedAbsorption = exp(-vec3(0.110, 0.032, 0.010) * effectiveOpticalPath);\n"
	"        vec3 seabedColor = (lit + seabedCaustic) * seabedAbsorption;\n"
	"        float deepTransition = smoothstep(2.5, 16.0, d);\n"
	"        vec3 scatterTint = mix(vec3(0.018, 0.165, 0.285), vec3(0.010, 0.065, 0.240), deepTransition);\n"
	"        vec3 deepOceanScattered = scatterTint * (1.0 - exp(-vec3(0.035, 0.055, 0.085) * effectiveOpticalPath));\n"
	"        lit = mix(lit, seabedColor + deepOceanScattered, wetBlend);\n"
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
	"    vec2 w = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);\n"
	"    if (uvc.x <= 0.0001 || uvc.x >= 0.9999) w.y = f.y;\n"
	"    if (uvc.y <= 0.0001 || uvc.y >= 0.9999) w.x = f.x;\n"
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
	"    bool nearWater = (depth > -1.5) || (minNeighborWz < uSeaLevel + 1.5);\n"
	"    float effWz = nearWater ? uSeaLevel : (min(wz, uSeaLevel) - 2.0);\n"
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
	"uniform vec2 uChunkSpan;\n"
	"uniform float uHeightScale;\n"
	"uniform float uSeaLevel;\n"
	"uniform float uFogStart;\n"
	"uniform float uFogEnd;\n"
	"uniform float uAnimTime;\n"
	"uniform vec3 uSkyColor;\n"
	"uniform float uAtmoIntensity;\n"
	"out vec4 FragColor;\n"
	"float getH(ivec2 p, ivec2 maxC) {\n"
	"    return float(texelFetch(uHeightTex, clamp(p, ivec2(0), maxC), 0).r);\n"
	"}\n"
	"float sampleHeightSmooth(vec2 uv, out vec2 out_grad) {\n"
	"    vec2 texSize = vec2(textureSize(uHeightTex, 0));\n"
	"    vec2 maxCoordF = max(vec2(1.0), texSize - 1.0);\n"
	"    vec2 uvc = clamp(uv, 0.0, 1.0);\n"
	"    vec2 pixelCoord = uvc * maxCoordF;\n"
	"    ivec2 iCoord = ivec2(floor(pixelCoord));\n"
	"    vec2 f = pixelCoord - vec2(iCoord);\n"
	"    vec2 w = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);\n"
	"    if (uvc.x <= 0.0001 || uvc.x >= 0.9999) w.y = f.y;\n"
	"    if (uvc.y <= 0.0001 || uvc.y >= 0.9999) w.x = f.x;\n"
	"    ivec2 maxCoord = ivec2(maxCoordF);\n"
	"    float h00 = getH(iCoord, maxCoord);\n"
	"    float h10 = getH(iCoord + ivec2(1, 0), maxCoord);\n"
	"    float h01 = getH(iCoord + ivec2(0, 1), maxCoord);\n"
	"    float h11 = getH(iCoord + ivec2(1, 1), maxCoord);\n"
	"    float b0 = mix(h00, h10, w.x);\n"
	"    float b1 = mix(h01, h11, w.x);\n"
	"    float height = mix(b0, b1, w.y);\n"
	"    float dx = mix(h10 - h00, h11 - h01, w.y);\n"
	"    float dy = b1 - b0;\n"
	"    out_grad.x = dx * (maxCoordF.x / max(1.0, uChunkSpan.x));\n"
	"    out_grad.y = dy * (maxCoordF.y / max(1.0, uChunkSpan.y));\n"
	"    return height;\n"
	"}\n"
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
	"vec2 chaff_grad_dir(int cx, int cy, uint seed) {\n"
	"    uint h = chaff_hash3(ivec3(cx, cy, 0), seed);\n"
	"    float gx = float(int(h & 0xFFFFu) - 32768) * (1.0 / 32768.0);\n"
	"    float gy = float(int(h >> 16u) - 32768) * (1.0 / 32768.0);\n"
	"    vec2 g = vec2(gx, gy);\n"
	"    return g * inversesqrt(dot(g, g) + 1e-6);\n"
	"}\n"
	"vec3 chaff_grad_noise2d(vec2 p, uint seed) {\n"
	"    vec2 ip = floor(p);\n"
	"    vec2 fp = p - ip;\n"
	"    vec2 u = fp * fp * fp * (fp * (fp * 6.0 - 15.0) + 10.0);\n"
	"    vec2 du = 30.0 * fp * fp * (fp * (fp - 2.0) + 1.0);\n"
	"    ivec2 c = ivec2(ip);\n"
	"    vec2 g00 = chaff_grad_dir(c.x,     c.y,     seed);\n"
	"    vec2 g10 = chaff_grad_dir(c.x + 1, c.y,     seed);\n"
	"    vec2 g01 = chaff_grad_dir(c.x,     c.y + 1, seed);\n"
	"    vec2 g11 = chaff_grad_dir(c.x + 1, c.y + 1, seed);\n"
	"    float v00 = dot(g00, fp);\n"
	"    float v10 = dot(g10, fp - vec2(1.0, 0.0));\n"
	"    float v01 = dot(g01, fp - vec2(0.0, 1.0));\n"
	"    float v11 = dot(g11, fp - vec2(1.0, 1.0));\n"
	"    float k0 = v10 - v00;\n"
	"    float k1 = v01 - v00;\n"
	"    float k2 = v11 - v01 - k0;\n"
	"    float val = 0.5 + 0.70710678 * (v00 + k0 * u.x + k1 * u.y + k2 * u.x * u.y);\n"
	"    vec2 gInterp = g00 + (g10 - g00) * u.x + (g01 - g00) * u.y + (g11 - g01 - g10 + g00) * (u.x * u.y);\n"
	"    vec2 grad = 0.70710678 * (gInterp + du * vec2(k0 + k2 * u.y, k1 + k2 * u.x));\n"
	"    return vec3(clamp(val, 0.0, 1.0), grad.x, grad.y);\n"
	"}\n"
	"vec4 eval_zoom_water_waves(vec2 pos, float anim_time, float dist, float fw) {\n"
	"    const float freqs[6] = float[6](0.024, 0.072, 0.220, 0.680, 2.150, 6.800);\n"
	"    const float amps[6]  = float[6](1.450, 0.680, 0.280, 0.115, 0.045, 0.018);\n"
	"    const float spds[6]  = float[6](0.180, 0.340, 0.620, 1.050, 1.750, 2.800);\n"
	"    const float dFar[6]  = float[6](65000.0, 18000.0, 4800.0, 1600.0, 520.0, 200.0);\n"
	"    const float dNear[6] = float[6](14000.0,  3600.0, 1100.0,  360.0, 130.0,  42.0);\n"
	"    const uint  seeds[6] = uint[6](0x4F434530u, 0x5357454Cu, 0x57415645u, 0x52495050u, 0x46494E45u, 0x53505241u);\n"
	"    const mat2  rot  = mat2(0.8, -0.6, 0.6, 0.8);\n"
	"    const mat2  irot = mat2(0.8,  0.6, -0.6, 0.8);\n"
	"    const vec2  windDirs[6] = vec2[6](\n"
	"        vec2( 0.7071, -0.7071),\n"
	"        vec2( 0.8660, -0.5000),\n"
	"        vec2( 0.5000, -0.8660),\n"
	"        vec2( 0.9397, -0.3420),\n"
	"        vec2( 0.3420, -0.9397),\n"
	"        vec2( 0.7660,  0.6428)\n"
	"    );\n"
	"    float h = 0.0;\n"
	"    vec2 grad = vec2(0.0);\n"
	"    float crest = 0.0;\n"
	"    vec2 pRot = pos;\n"
	"    mat2 curInvRot = mat2(1.0, 0.0, 0.0, 1.0);\n"
	"    for (int i = 0; i < 6; i++) {\n"
	"        float wDist = clamp((dFar[i] - dist) / max(1.0, dFar[i] - dNear[i]), (i == 0) ? 0.35 : 0.0, 1.0);\n"
	"        float wNyq  = smoothstep(0.55, 0.16, freqs[i] * fw);\n"
	"        float w = wDist * wNyq;\n"
	"        if (w > 0.001) {\n"
	"            float phase = mod(anim_time * spds[i], 1024.0);\n"
	"            vec2 q = pRot * freqs[i] - windDirs[i] * phase;\n"
	"            vec3 g = chaff_grad_noise2d(q, seeds[i]);\n"
	"            float signedVal = g.x - 0.5;\n"
	"            /* Trochoidal Gerstner-like crest sharpening on primary wave trains */\n"
	"            float ridge = 0.5 - abs(signedVal);\n"
	"            float shaped = mix(signedVal, ridge - 0.18, 0.35);\n"
	"            float dShape = mix(1.0, (signedVal >= 0.0) ? -0.70 : 0.70, 0.35);\n"
	"            vec2 worldG = curInvRot * g.yz;\n"
	"            h += shaped * amps[i] * w;\n"
	"            grad += worldG * (dShape * freqs[i] * amps[i] * w);\n"
	"            crest += max(0.0, ridge - 0.22) * (w * ((i <= 2) ? 1.4 : 0.8));\n"
	"            pRot += g.yz * (0.09 * w);\n"
	"        }\n"
	"        pRot = rot * pRot;\n"
	"        curInvRot = curInvRot * irot;\n"
	"    }\n"
	"    return vec4(h, grad.x, grad.y, clamp(crest, 0.0, 1.0));\n"
	"}\n"
	"void main() {\n"
	"    float fragWaterDepth = vWaterDepth;\n"
	"    vec2 shoreGrad = vec2(0.0);\n"
	"    if (vDistance < 2800.0) {\n"
	"        float scale = (uHeightScale != 0.0) ? uHeightScale : 1.0;\n"
	"        float effectiveScale = scale * 0.0625;\n"
	"        float raw_h = sampleHeightSmooth(vUV, shoreGrad);\n"
	"        float fragZ = raw_h * effectiveScale;\n"
	"        fragWaterDepth = uSeaLevel - fragZ;\n"
	"        shoreGrad *= effectiveScale;\n"
	"    }\n"
	"    if (fragWaterDepth <= 0.0) {\n"
	"        discard;\n"
	"    }\n"
	"    vec3 viewDir = normalize(vViewDir);\n"
	"    vec3 sunDir = normalize(vec3(-0.577, -0.577, 0.816));\n"
	"    vec3 torusN = normalize(vTorusNormal);\n"
	"    vec3 tangentU = normalize(vTangentU);\n"
	"    vec3 tangentV = normalize(vTangentV);\n"
	"    float fw = max(1e-4, max(length(dFdx(vWorldPos)), length(dFdy(vWorldPos))));\n"
	"    /* Damp wave amplitude gently in ankle-deep surf zone (0..1.5 units) */\n"
	"    float shoreWaveDamp = smoothstep(0.0, 1.5, fragWaterDepth);\n"
	"    vec4 waves = eval_zoom_water_waves(vWorldPos, uAnimTime, vDistance, fw);\n"
	"    vec2 waveGrad = waves.yz * (0.35 + 0.65 * shoreWaveDamp);\n"
	"    vec3 waveNormal = normalize(torusN - waveGrad.x * tangentU - waveGrad.y * tangentV);\n"
	"    float NdotV = max(0.0, dot(waveNormal, viewDir));\n"
	"    float NdotL = max(0.0, dot(waveNormal, sunDir));\n"
	"    vec3 halfVec = normalize(sunDir + viewDir);\n"
	"    float NdotH = max(0.0, dot(waveNormal, halfVec));\n"
	"    /* Dual-lobe specular: crisp sun micro-glint + zoom-adaptive macro ocean glitter sheen */\n"
	"    float closeWeight = clamp(1.0 - (vDistance - 350.0) / 1400.0, 0.0, 1.0);\n"
	"    float specSharp = pow(NdotH, 180.0) * (0.45 + 0.75 * closeWeight);\n"
	"    float specBroad = pow(NdotH, 28.0)  * (0.28 + 0.22 * (1.0 - closeWeight));\n"
	"    float specSun = (specSharp + specBroad) * smoothstep(0.0, 0.25, fragWaterDepth);\n"
	"    /* Schlick Fresnel for air-water interface (R0 = 0.02): transparent at nadir/diorama pitch,\n"
	"     * mirror-reflective at grazing wave slopes and planetary limb */\n"
	"    float fresnel = 0.02 + 0.98 * pow(clamp(1.0 - NdotV, 0.0, 1.0), 5.0);\n"
	"    vec3 sky = (length(uSkyColor) > 0.001) ? uSkyColor : vec3(0.55, 0.72, 0.88);\n"
	"    vec3 sunColor = vec3(1.0, 0.98, 0.92);\n"
	"    /* Subsurface wave-crest turquoise back-scattering & wave facet shading */\n"
	"    float crestGlow = clamp(waves.w * 0.65 + max(0.0, NdotL - 0.52) * 1.15, 0.0, 1.0) * smoothstep(0.2, 3.5, fragWaterDepth);\n"
	"    vec3 sssTint = vec3(0.025, 0.21, 0.30) * crestGlow;\n"
	"    /* Organic multi-band shoreward swash foam & crisp waterline meniscus */\n"
	"    float tSwash = mod(uAnimTime * 1.35, 256.0);\n"
	"    vec3 swashNoise = chaff_grad_noise2d(vWorldPos * 0.35 + vec2(tSwash * 0.4, -tSwash * 0.3), 0xF0A157u);\n"
	"    float swashPhase = sin(fragWaterDepth * 3.6 - tSwash * 2.2 + (swashNoise.x - 0.5) * 3.2);\n"
	"    float swashBand = smoothstep(2.4, 0.15, fragWaterDepth) * smoothstep(0.25, 0.82, swashPhase * 0.5 + 0.5) * smoothstep(0.35, 0.72, swashNoise.x);\n"
	"    float meniscus = smoothstep(0.38, 0.0, fragWaterDepth);\n"
	"    float whitecap = smoothstep(0.62, 0.92, waves.w) * smoothstep(2.0, 8.0, fragWaterDepth) * 0.38;\n"
	"    float foam = clamp(meniscus * 0.78 + swashBand * 0.58 + whitecap, 0.0, 0.92);\n"
	"    vec3 foamColor = vec3(0.95, 0.98, 1.0);\n"
	"    /* Pure air-water surface compositing: seabed absorption & scattering were already applied\n"
	"     * once in s_fs_terrain on the framebuffer, so s_fs_water adds ONLY surface reflection, SSS,\n"
	"     * specular sun glints, and shore foam without double-extinguishing the seabed! */\n"
	"    float skyReflWeight = fresnel * (0.35 + 0.65 * shoreWaveDamp);\n"
	"    float sssWeight     = crestGlow * 0.28;\n"
	"    float specWeight    = clamp(specSun, 0.0, 0.96);\n"
	"    float foamWeight    = foam;\n"
	"    float totalWeight   = skyReflWeight + sssWeight + specWeight + foamWeight;\n"
	"    vec3 surfaceNum = sky * skyReflWeight + sssTint + sunColor * specSun + foamColor * foamWeight;\n"
	"    vec3 surfaceRgb = (totalWeight > 1e-4) ? clamp(surfaceNum / max(totalWeight, 1e-4), 0.0, 1.0) : sky;\n"
	"    float limb = pow(clamp(1.0 - max(0.0, dot(torusN, viewDir)), 0.0, 1.0), 3.0);\n"
	"    surfaceRgb = mix(surfaceRgb, vec3(0.35, 0.70, 1.0), limb * 0.55 * uAtmoIntensity);\n"
	"    float edgeFade = smoothstep(0.0, 0.08, fragWaterDepth);\n"
	"    float alpha = clamp(1.0 - (1.0 - skyReflWeight) * (1.0 - sssWeight) * (1.0 - specWeight) * (1.0 - foamWeight) + limb * 0.35 * uAtmoIntensity, 0.0, 0.96) * edgeFade;\n"
	"    float fogFactor = clamp((vDistance - uFogStart) / max(1.0, uFogEnd - uFogStart), 0.0, 1.0);\n"
	"    FragColor = vec4(mix(surfaceRgb, sky, fogFactor * uAtmoIntensity), alpha);\n"
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

	float uc = u < 0.0f ? 0.0f : (u > 1.0f ? 1.0f : u);
	float vc = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);

	float px = uc * max_xf;
	float py = vc * max_yf;

	int ix = (int)floorf(px);
	int iy = (int)floorf(py);

	float fx = px - (float)ix;
	float fy = py - (float)iy;

	float wx = fx * fx * fx * (fx * (fx * 6.0f - 15.0f) + 10.0f);
	float wy = fy * fy * fy * (fy * (fy * 6.0f - 15.0f) + 10.0f);
	if (uc <= 0.0001f || uc >= 0.9999f) wy = fy;
	if (vc <= 0.0001f || vc >= 0.9999f) wx = fx;

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
	GLuint height_tex;
} gl_water_chunk_t;

static struct {
	bool initialized;
	gl_terrain_pipe_t terrain_pipe;
	gl_water_pipe_t water_pipe;
	gl_billboard_pipe_t bb_pipe;

	GLuint grid_vbo;
	GLuint grid_ibo;
	GLuint grid_vbo_hi;
	GLuint grid_ibo_hi;
	GLuint grid_vbo_z2;
	GLuint grid_ibo_z2;
	GLuint grid_vbo_z3;
	GLuint grid_ibo_z3;

	GLuint bb_vbo;
	GLuint atlas_tex;

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
	static float grid_uv_scratch[GL_TOTAL_VERTS_Z3 * 2];
	static uint16_t indices_scratch[GL_TOTAL_INDICES_Z3];

	create_static_grid(GL_GRID_QUADS_LO, grid_uv_scratch, indices_scratch, &s_gl.grid_vbo, &s_gl.grid_ibo);
	create_static_grid(GL_GRID_QUADS_HI, grid_uv_scratch, indices_scratch, &s_gl.grid_vbo_hi, &s_gl.grid_ibo_hi);
	create_static_grid(GL_GRID_QUADS_Z2, grid_uv_scratch, indices_scratch, &s_gl.grid_vbo_z2, &s_gl.grid_ibo_z2);
	create_static_grid(GL_GRID_QUADS_Z3, grid_uv_scratch, indices_scratch, &s_gl.grid_vbo_z3, &s_gl.grid_ibo_z3);

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

static void select_grid_buffers(uint8_t level, int32_t ox, int32_t oy, int32_t span_w, int32_t span_h,
                                GLuint *out_vbo, GLuint *out_ibo, GLsizei *out_indices)
{
	if (level != 0) {
		*out_vbo = s_gl.grid_vbo;
		*out_ibo = s_gl.grid_ibo;
		*out_indices = GL_TOTAL_INDICES;
		return;
	}

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
	if (quads >= GL_GRID_QUADS_Z3) {
		*out_vbo = s_gl.grid_vbo_z3;
		*out_ibo = s_gl.grid_ibo_z3;
		*out_indices = GL_TOTAL_INDICES_Z3;
	} else if (quads >= GL_GRID_QUADS_Z2) {
		*out_vbo = s_gl.grid_vbo_z2;
		*out_ibo = s_gl.grid_ibo_z2;
		*out_indices = GL_TOTAL_INDICES_Z2;
	} else {
		*out_vbo = s_gl.grid_vbo_hi;
		*out_ibo = s_gl.grid_ibo_hi;
		*out_indices = GL_TOTAL_INDICES_HI;
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

	GLuint active_vbo = 0, active_ibo = 0;
	GLsizei active_indices = 0;
	select_grid_buffers(level, ox, oy, span_w, span_h, &active_vbo, &active_ibo, &active_indices);

	glBindBuffer(GL_ARRAY_BUFFER, active_vbo);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, 0);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, active_ibo);
	glDrawElements(GL_TRIANGLES, active_indices, GL_UNSIGNED_SHORT, 0);

	/* Queue water pass for this chunk if world has water */
	if (sea_level > -900.0f && s_gl.water_count < MAX_WATER_QUEUE) {
		s_gl.water_queue[s_gl.water_count].ox = ox;
		s_gl.water_queue[s_gl.water_count].oy = oy;
		s_gl.water_queue[s_gl.water_count].span_w = span_w;
		s_gl.water_queue[s_gl.water_count].span_h = span_h;
		s_gl.water_queue[s_gl.water_count].level = level;
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
	s_gl.water_count = 0;
}

void nd_seven_gl_flush_billboards(const lam_billboard_gpu_t *batch,
                                  uint32_t count,
                                  const uint32_t *atlas_rgba)
{
	if (!batch || count == 0) return;
	if (!s_gl.initialized || !s_gl.bb_pipe.prog) return;

	if (!s_gl.atlas_tex && atlas_rgba) {
		s_gl.atlas_tex = glCreateTexture();
		glBindTexture(GL_TEXTURE_2D, s_gl.atlas_tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ND_SEVEN_ATLAS_W, ND_SEVEN_ATLAS_H_PX, 0,
		             GL_RGBA, GL_UNSIGNED_BYTE, atlas_rgba);
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

uint32_t nd_seven_gl_grid_quads_for_zoom(uint8_t level, float cam_dist)
{
	if (level > 0)
		return GL_GRID_QUADS_LO;
	if (cam_dist > 0.0f && cam_dist <= 125.0f)
		return GL_GRID_QUADS_Z3; /* 144x144 quads = 41,472 triangles (9x normal zoom) */
	if (cam_dist > 0.0f && cam_dist <= 230.0f)
		return GL_GRID_QUADS_Z2; /* 96x96 quads = 18,432 triangles (4x normal zoom) */
	return GL_GRID_QUADS_HI;         /* 48x48 quads = 4,608 triangles (normal 1.0x zoom) */
}

uint32_t nd_seven_gl_grid_quads_for_level(uint8_t level)
{
	return nd_seven_gl_grid_quads_for_zoom(level, ND_SEVEN_CAM_DIST);
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
	static const float freqs[4] = { 0.16f, 0.85f, 4.20f, 18.00f };
	static const float amps[4]  = { 0.28f, 0.24f, 0.18f, 0.12f  };
	static const float dFar[4]  = { 6500.0f, 2200.0f, 780.0f, 240.0f };
	static const float dNear[4] = { 1600.0f,  550.0f, 180.0f,  38.0f };
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

		float wNyq = gl_smoothstep(0.55f, 0.18f, freqs[i] * fw);
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
	static const float windDirs[6][2] = {
		{  0.7071f, -0.7071f },
		{  0.8660f, -0.5000f },
		{  0.5000f, -0.8660f },
		{  0.9397f, -0.3420f },
		{  0.3420f, -0.9397f },
		{  0.7660f,  0.6428f }
	};

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
		float w = wDist * wNyq;
		if (w > 0.001f) {
			float raw_phase = anim_time * spds[i];
			float phase = raw_phase - 1024.0f * floorf(raw_phase / 1024.0f);
			float qx = prx * freqs[i] - windDirs[i][0] * phase;
			float qy = pry * freqs[i] - windDirs[i][1] * phase;
			float val = 0.0f, ogx = 0.0f, ogy = 0.0f;
			gl_chaff_grad_noise2d(qx, qy, seeds[i], &val, &ogx, &ogy);
			float signedVal = val - 0.5f;
			float ridge = 0.5f - fabsf(signedVal);
			float shaped = signedVal * 0.65f + (ridge - 0.18f) * 0.35f;
			float dShape = 0.65f + ((signedVal >= 0.0f) ? -0.70f : 0.70f) * 0.35f;
			float wgx = ir00 * ogx + ir10 * ogy;
			float wgy = ir01 * ogx + ir11 * ogy;
			h  += shaped * amps[i] * w;
			gx += wgx * (dShape * freqs[i] * amps[i] * w);
			gy += wgy * (dShape * freqs[i] * amps[i] * w);
			if (ridge > 0.22f) {
				crest += (ridge - 0.22f) * (w * ((i <= 2) ? 1.4f : 0.8f));
			}
			prx += ogx * (0.09f * w);
			pry += ogy * (0.09f * w);
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


