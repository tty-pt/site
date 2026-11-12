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
#include <nd/field.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

#define PI_F 3.14159265358979323846f
#define TWO_PI_F 6.28318530717958647692f

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
#define MAX_BB_VERTS 24576

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
	"    for (int i = 0; i < 4; i++) {\n"
	"        float wDist = clamp((dFar[i] - dist) / max(1.0, dFar[i] - dNear[i]), 0.0, 1.0);\n"
	"        float wNyq  = smoothstep(0.92, 0.28, freqs[i] * fw);\n"
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
	"/* High-Definition Macro & Orbital Procedural Noise Image (atmosphere -> low orbit -> space):\n"
	" * Replaces high-frequency polygon subdivision at altitude with a crisp, Nyquist band-limited\n"
	" * 4-octave continental/orogenic noise image directly in world coordinates. */\n"
	"vec4 eval_macro_planetary_detail(vec2 pos, float dist, float fw, out vec4 out_macro_oct) {\n"
	"    float highAltWeight = smoothstep(1400.0, 4200.0, dist);\n"
	"    if (highAltWeight <= 0.001) {\n"
	"        out_macro_oct = vec4(0.5);\n"
	"        return vec4(0.0);\n"
	"    }\n"
	"    const float freqs[4] = float[4](0.00065, 0.00240, 0.00920, 0.03600);\n"
	"    const float amps[4]  = float[4](3.20, 1.85, 0.95, 0.48);\n"
	"    const uint  seeds[4] = uint[4](0x504C414Eu, 0x4F524231u, 0x41544D4Fu, 0x4D45534Fu);\n"
	"    const mat2  rot  = mat2(0.8, -0.6, 0.6, 0.8);\n"
	"    const mat2  irot = mat2(0.8,  0.6, -0.6, 0.8);\n"
	"    float h = 0.0;\n"
	"    vec2 grad = vec2(0.0);\n"
	"    float ridgeAcc = 0.0;\n"
	"    vec2 pRot = pos;\n"
	"    mat2 curInvRot = mat2(1.0, 0.0, 0.0, 1.0);\n"
	"    float oct[4] = float[4](0.5, 0.5, 0.5, 0.5);\n"
	"    for (int i = 0; i < 4; i++) {\n"
	"        float wNyq = smoothstep(0.62, 0.16, freqs[i] * fw) * highAltWeight;\n"
	"        if (wNyq > 0.001) {\n"
	"            vec3 g = chaff_grad_noise2d(pRot * freqs[i], seeds[i]);\n"
	"            oct[i] = mix(0.5, g.x, wNyq);\n"
	"            float signedVal = g.x - 0.5;\n"
	"            float ridge = 0.5 - abs(signedVal);\n"
	"            float shaped = mix(signedVal, ridge - 0.16, 0.42);\n"
	"            vec2 worldG = curInvRot * g.yz;\n"
	"            h += shaped * amps[i] * wNyq;\n"
	"            grad += worldG * (freqs[i] * amps[i] * wNyq);\n"
	"            ridgeAcc += max(0.0, ridge - 0.18) * (0.65 * wNyq);\n"
	"            pRot += g.yz * (0.14 * wNyq);\n"
	"        }\n"
	"        pRot = rot * pRot;\n"
	"        curInvRot = curInvRot * irot;\n"
	"    }\n"
	"    out_macro_oct = vec4(oct[0], oct[1], oct[2], oct[3]);\n"
	"    return vec4(h, grad.x, grad.y, clamp(ridgeAcc, 0.0, 0.65));\n"
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
	"    float scale = (uHeightScale != 0.0) ? uHeightScale : 1.0;\n"
	"    float effectiveScale = scale * 0.0625;\n"
	"    vec2 fragGrad;\n"
	"    float raw_h = sampleHeightSmooth(vUV, fragGrad);\n"
	"    float fragZ = raw_h * effectiveScale;\n"
	"    vec3 torusN = normalize(vTorusNormal);\n"
	"    vec3 tangentU = normalize(vTangentU);\n"
	"    vec3 tangentV = normalize(vTangentV);\n"
	"    vec3 fragNormal = normalize(torusN - fragGrad.x * effectiveScale * tangentU - fragGrad.y * effectiveScale * tangentV);\n"
	"    float fw = clamp(max(length(dFdx(vWorldPos)), length(dFdy(vWorldPos))), 1e-4, max(uChunkSpan.x, uChunkSpan.y) * 0.25);\n"
	"    vec4 macroOct = vec4(0.5);\n"
	"    vec4 macroDetail = eval_macro_planetary_detail(vWorldPos, vDistance, fw, macroOct);\n"
	"    /* Refine coarse macro heightmap & normals with high-definition procedural noise image at altitude */\n"
	"    float rawMacroDepth = (uSeaLevel > -900.0) ? (uSeaLevel - fragZ) : -999.0;\n"
	"    float coastDetailEnv = (uSeaLevel > -900.0) ? (0.35 + 0.65 * smoothstep(0.0, 12.0, abs(rawMacroDepth))) : 1.0;\n"
	"    fragZ += macroDetail.x * coastDetailEnv;\n"
	"    fragNormal = normalize(fragNormal - macroDetail.y * 0.65 * tangentU - macroDetail.z * 0.65 * tangentV);\n"
	"    float rawDepth = (uSeaLevel > -900.0) ? (uSeaLevel - fragZ) : -999.0;\n"
	"    vec3 shoreRelief = (uSeaLevel > -900.0) ? eval_shore_micro_relief(vWorldPos, rawDepth, fw) : vec3(0.0);\n"
	"    fragZ += shoreRelief.x;\n"
	"    fragNormal = normalize(fragNormal - shoreRelief.y * tangentU - shoreRelief.z * tangentV);\n"
	"    float fragWaterDepth = (uSeaLevel > -900.0) ? (uSeaLevel - fragZ) : -999.0;\n"
	"    vec4 octaves = vec4(0.5);\n"
	"    vec4 detail = (fragWaterDepth <= 18.0) ? eval_zoom_terrain_detail(vWorldPos, vDistance, fw, octaves) : vec4(0.0);\n"
	"    vec2 edgeMask = 4.0 * vUV * (1.0 - vUV);\n"
	"    vec2 macroUVWarp = vec2(macroOct.z - 0.5, macroOct.w - 0.5) * (0.018 * edgeMask * smoothstep(2000.0, 8000.0, vDistance));\n"
	"    vec2 sampleUV = clamp(vUV + detail.yz * (0.010 * edgeMask) + macroUVWarp, 0.0, 1.0);\n"
	"    vec2 waveRefGrad = vec2(0.0);\n"
	"    if (fragWaterDepth > 0.0) {\n"
	"        float depthFactor = clamp(fragWaterDepth / 10.0, 0.0, 1.0);\n"
	"        float tRef = mod(uAnimTime, 256.0);\n"
	"        vec3 wRef1 = chaff_grad_noise2d(vWorldPos * 0.22 + vec2(tRef * 0.55, -tRef * 0.45), 0x57415631u);\n"
	"        vec3 wRef2 = chaff_grad_noise2d(vWorldPos * 0.52 + vec2(-tRef * 0.80, tRef * 0.65), 0x57415632u);\n"
	"        waveRefGrad = wRef1.yz * 0.65 + wRef2.yz * 0.35;\n"
	"        vec2 refractOffset = waveRefGrad * (0.012 * depthFactor * edgeMask);\n"
	"        sampleUV = clamp(vUV + refractOffset, 0.0, 1.0);\n"
	"    }\n"
	"    vec4 baseCol = sampleSubstrateSmooth(sampleUV);\n"
	"    vec3 rawBiome = sampleSubstrateSmooth(vUV).rgb;\n"
	"    /* Continuous biome water/seabed weights derived from uSubstrateTex (populated via ndw_biome_base_rgb) */\n"
	"    float maxBio = max(rawBiome.r, max(rawBiome.g, rawBiome.b));\n"
	"    float minBio = min(rawBiome.r, min(rawBiome.g, rawBiome.b));\n"
	"    float wVolcanic = smoothstep(0.32, 0.14, maxBio);\n"
	"    float wPolar = smoothstep(0.52, 0.72, minBio) * smoothstep(0.18, 0.04, max(rawBiome.r, rawBiome.g) - rawBiome.b);\n"
	"    float wTropical = smoothstep(0.46, 0.68, rawBiome.r) * smoothstep(0.38, 0.58, rawBiome.g) * (1.0 - wPolar);\n"
	"    float wTemperate = clamp(1.0 - (wVolcanic + wPolar + wTropical), 0.0, 1.0);\n"
	"    vec3 L_frag = normalize(-0.577 * tangentU - 0.577 * tangentV + 0.816 * torusN);\n"
	"    float baseDiff = max(0.0, dot(fragNormal, L_frag));\n"
	"    float effLighting = 0.45 + 0.55 * baseDiff;\n"
	"    vec3 dryCol = baseCol.rgb;\n"
	"    float dryLighting = effLighting;\n"
	"    float slope = 1.0 - max(0.0, dot(fragNormal, torusN));\n"
	"    float elevAboveWater = (uSeaLevel > -900.0) ? max(0.0, fragZ - uSeaLevel) : max(0.0, fragZ);\n"
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
	"            bool isSandBiome = (dryCol.r > 0.55 && dryCol.g > 0.45 && dryCol.b < 0.45);\n"
	"            float shoreFactor = smoothstep(6.0, 0.4, elevAboveWater) * (1.0 - rockFactor);\n"
	"            float sandSparkle = 0.0;\n"
	"            float wetSand = 0.0;\n"
	"            float sandBlend = 0.0;\n"
	"            if ((isSandBiome || shoreFactor > 0.0) && distFade > 0.0) {\n"
	"                float duneWave = nMeso * 0.45 + nStd * 0.55;\n"
	"                wetSand = smoothstep(2.6, 0.0, elevAboveWater);\n"
	"                vec3 drySandCol = vec3(0.75, 0.67, 0.49) * (0.88 + 0.22 * duneWave + 0.16 * (nClose - 0.5) * closeZoom + 0.10 * (nUltra - 0.5) * ultraZoom);\n"
	"                vec3 wetSandCol = vec3(0.54, 0.46, 0.34) * (0.90 + 0.18 * nStd + 0.12 * (nClose - 0.5) * closeZoom);\n"
	"                vec3 sandCol = mix(drySandCol, wetSandCol, wetSand);\n"
	"                sandBlend = isSandBiome ? 1.0 : shoreFactor;\n"
	"                dryCol = mix(dryCol, sandCol, sandBlend * distFade);\n"
	"                sandSparkle = smoothstep(0.68, 0.88, nClose * 0.45 + nUltra * 0.55) * closeZoom;\n"
	"            }\n"
	"            if (rockFactor < 0.65 && !isSandBiome && shoreFactor < 0.7 && fragWaterDepth < 0.35 && dryCol.g > dryCol.r * 1.02) {\n"
	"                vec3 deepMoss = dryCol * vec3(0.78, 0.96, 0.76);\n"
	"                vec3 lushTurf = dryCol * vec3(0.92, 1.09, 0.88);\n"
	"                vec3 warmClover = dryCol * vec3(1.12, 1.04, 0.74);\n"
	"                vec3 vegCol = mix(deepMoss, lushTurf, smoothstep(0.25, 0.72, nMeso));\n"
	"                vegCol = mix(vegCol, warmClover, smoothstep(0.38, 0.76, nStd) * 0.70);\n"
	"                float bladeRelief = (nStd - 0.5) * 0.16 * distFade + (nClose - 0.5) * 0.20 * closeZoom + (nUltra - 0.5) * 0.14 * ultraZoom;\n"
	"                vegCol *= clamp(1.0 + bladeRelief, 0.76, 1.26);\n"
	"                dryCol = mix(dryCol, vegCol, macroFade * smoothstep(0.35, -0.10, fragWaterDepth));\n"
	"            }\n"
	"            float normStrength = 0.24 + 0.22 * distFade + 0.16 * closeZoom;\n"
	"            vec3 N_frag = normalize(fragNormal - microGrad.x * normStrength * tangentU - microGrad.y * normStrength * tangentV);\n"
	"            vec3 viewDir = normalize(vViewDir);\n"
	"            vec3 H_half = normalize(L_frag + viewDir);\n"
	"            float perFragDiff = max(0.0, dot(N_frag, L_frag));\n"
	"            float cavityAO = clamp(1.0 - slope * 0.26 + h0 * 0.26 - microCavity * 0.42 - rockCrevice * 0.34, 0.54, 1.05);\n"
	"            float fragLit = 0.40 * cavityAO + 0.60 * perFragDiff * cavityAO;\n"
	"            dryLighting = mix(dryLighting, fragLit, macroFade);\n"
	"            float microShade = clamp(1.0 + h0 * (0.30 + 0.16 * closeZoom) * macroFade, 0.72, 1.28);\n"
	"            dryCol *= microShade;\n"
	"            /* Capillary wet-shore darkening and sub-aerial wet specular sheen just above waterline */\n"
	"            float shoreWetMoisture = smoothstep(-0.38, 0.15, fragWaterDepth);\n"
	"            dryCol *= (1.0 - shoreWetMoisture * 0.20);\n"
	"            float subAerialMask = smoothstep(0.25, -0.02, fragWaterDepth);\n"
	"            float wetSpecAngle = max(0.0, dot(N_frag, H_half));\n"
	"            float wetFilmSpec = pow(wetSpecAngle, 36.0) * (0.32 * wetSand + 0.24 * shoreWetMoisture) * subAerialMask;\n"
	"            float quartzGlint = pow(wetSpecAngle, 64.0) * sandSparkle * 0.42 * subAerialMask;\n"
	"            dryCol += vec3((wetFilmSpec + quartzGlint) * (0.35 + 0.65 * sandBlend) * distFade);\n"
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
	"            subLighting = mix(subLighting, (0.45 + 0.55 * subDiff) * subCavity, distFade);\n"
	"        }\n"
	"    }\n"
	"    /* Gradual transition from detailed shore substrate into deeper benthic sediment (0.35 .. 5.5 units) */\n"
	"    float submergedBlend = smoothstep(0.35, 5.5, fragWaterDepth);\n"
	"    /* High-definition orbital/atmospheric procedural albedo modulation on dry land & ocean basins */\n"
	"    float orbitDetailWeight = smoothstep(1800.0, 6500.0, vDistance);\n"
	"    if (orbitDetailWeight > 0.001) {\n"
	"        float contTex = (macroOct.x - 0.5) * 0.16 + (macroOct.y - 0.5) * 0.18 + (macroOct.z - 0.5) * 0.14 + (macroOct.w - 0.5) * 0.10;\n"
	"        float ridgeCrest = macroDetail.w * orbitDetailWeight;\n"
	"        vec3 ridgeTint = mix(dryCol * (1.0 + contTex), vec3(0.52, 0.50, 0.48), clamp(ridgeCrest * 0.55, 0.0, 0.45));\n"
	"        float alpineSnow = smoothstep(48.0, 110.0, elevAboveWater + macroDetail.x * 6.0) * smoothstep(0.28, 0.65, macroOct.y * 0.6 + macroOct.z * 0.4);\n"
	"        ridgeTint = mix(ridgeTint, vec3(0.90, 0.93, 0.97), alpineSnow * 0.65 * orbitDetailWeight);\n"
	"        dryCol = mix(dryCol, ridgeTint, orbitDetailWeight * 0.85);\n"
	"        subCol *= clamp(1.0 + contTex * 0.75 * orbitDetailWeight, 0.78, 1.22);\n"
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
	"    /* 3. Physical Volumetric Water Extinction & Biome-Aware Optics (Crystal-Clear Shallows) */\n"
	"    if (fragWaterDepth > 0.0) {\n"
	"        float d = clamp(fragWaterDepth, 0.0, 64.0);\n"
	"        float wetBlend = smoothstep(0.0, 0.45, fragWaterDepth);\n"
	"        float distFade = clamp(1.0 - (vDistance - 900.0) / 2500.0, 0.0, 1.0);\n"
	"        float closeZoom = clamp((420.0 - vDistance) / 300.0, 0.0, 1.0);\n"
	"        /* Subsurface wave-refracted caustic light web projected onto seabed before Beer-Lambert absorption */\n"
	"        float caustic = 0.0;\n"
	"        if (distFade > 0.01 && d < 16.0) {\n"
	"            float tC = mod(uAnimTime, 256.0);\n"
	"            vec2 cUV = vWorldPos + waveRefGrad * 1.6;\n"
	"            float c1 = chaff_sample_3d(vec3(cUV * 1.85, tC * 2.4), 3u, 0x1337u);\n"
	"            float c2 = chaff_sample_3d(vec3((cUV + vec2(37.0, -23.0)) * 1.85, tC * 1.9), 3u, 0xBEEFu);\n"
	"            float caustMeso = pow(clamp(1.0 - abs(c1 - c2) * 5.6, 0.0, 1.0), 3.0);\n"
	"            float caustFine = 0.0;\n"
	"            if (closeZoom > 0.01) {\n"
	"                float c3 = chaff_sample_3d(vec3(cUV * 4.8, tC * 3.6), 2u, 0xCA57u);\n"
	"                float c4 = chaff_sample_3d(vec3((cUV + vec2(-19.0, 41.0)) * 4.8, tC * 3.0), 2u, 0xCA58u);\n"
	"                caustFine = pow(clamp(1.0 - abs(c3 - c4) * 5.6, 0.0, 1.0), 3.0) * closeZoom;\n"
	"            }\n"
	"            caustic = (caustMeso + caustFine * 0.65) * distFade;\n"
	"        }\n"
	"        float causticFocus = smoothstep(0.10, 1.10, d) * exp(-0.15 * d);\n"
	"        lit *= (1.0 + caustic * causticFocus * 0.38);\n"
	"        vec3 viewDir = normalize(vViewDir);\n"
	"        float cosView = max(0.35, dot(torusN, viewDir));\n"
	"        float opticalDepth = d / cosView;\n"
	"        /* Non-linear shallow optical path: crystal-clear wading shallows (d < 4), smooth shelf ramp (4..16) */\n"
	"        float shallowFactor = smoothstep(0.40, 15.0, d);\n"
	"        float effectiveOpticalDepth = opticalDepth * (0.14 + 0.86 * shallowFactor);\n"
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
	"        /* Depth-staged in-scattered water body color */\n"
	"        float tShelf = smoothstep(2.5, 9.5, d);\n"
	"        float tOuter = smoothstep(9.5, 20.0, d);\n"
	"        float tAbyss = smoothstep(20.0, 42.0, d);\n"
	"        vec3 inscatterCol = mix(C_SHALLOW, C_SHELF, tShelf);\n"
	"        inscatterCol = mix(inscatterCol, C_OUTER, tOuter);\n"
	"        inscatterCol = mix(inscatterCol, C_ABYSS, tAbyss);\n"
	"        float bodyScatterWeight = shallowFactor * (1.0 - exp(-0.072 * effectiveOpticalDepth));\n"
	"        vec3 waterBody = mix(transmittedBed, inscatterCol, bodyScatterWeight);\n"
	"        lit = mix(lit, waterBody, wetBlend);\n"
	"    }\n"
	"    float fogFactor = clamp((vDistance - uFogStart) / max(1.0, uFogEnd - uFogStart), 0.0, 1.0);\n"
	"    vec3 sky = (length(uSkyColor) > 0.001) ? uSkyColor : vec3(0.55, 0.70, 0.85);\n"
	"    vec3 viewDirNorm = normalize(vViewDir);\n"
	"    float limb = pow(clamp(1.0 - max(0.0, dot(torusN, viewDirNorm)), 0.0, 1.0), 3.0);\n"
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
	"    /* Raise effWz slightly above uSeaLevel near shore to prevent linearly interpolated terrain triangles from depth-clipping per-fragment water */\n"
	"    bool nearWater = (depth > -3.0) || (minNeighborWz < uSeaLevel + 3.0);\n"
	"    float effWz = nearWater ? (max(uSeaLevel, wz) + 0.12) : (min(wz, uSeaLevel) - 2.0);\n"
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
	"uniform float uFogStart;\n"
	"uniform float uFogEnd;\n"
	"uniform float uAnimTime;\n"
	"uniform vec3 uSkyColor;\n"
	"uniform float uAtmoIntensity;\n"
	"uniform vec4 uLambLight;\n"
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
	"/* Multi-octave moving recursive wind field: synoptic wind -> meso gusts (cat's paws) -> micro eddies.\n"
	" * Uses spatial domain warping + constant-direction phase advection so temporal velocity is invariant over time. */\n"
	"vec4 eval_recursive_wind(vec2 worldPos, float anim_time) {\n"
	"    const mat2 rot  = mat2(0.8, -0.6, 0.6, 0.8);\n"
	"    const mat2 irot = mat2(0.8,  0.6, -0.6, 0.8);\n"
	"    const vec2 dir0 = vec2(0.741, -0.671);\n"
	"    const vec2 dir1 = vec2(0.822, -0.569);\n"
	"    const vec2 dir2 = vec2(0.640, -0.768);\n"
	"    float t0 = mod(anim_time * 0.08, 1024.0);\n"
	"    vec2 p0 = worldPos * 0.0055 - dir0 * t0;\n"
	"    vec3 w0 = chaff_grad_noise2d(p0, 0x57494E44u);\n"
	"    vec2 baseWind = normalize(dir0 + vec2(-w0.z, w0.y) * 0.28);\n"
	"    float t1 = mod(anim_time * 0.22, 1024.0);\n"
	"    vec2 p1 = rot * (worldPos * 0.028 + w0.yz * 0.65) - dir1 * t1;\n"
	"    vec3 w1 = chaff_grad_noise2d(p1, 0x47555354u);\n"
	"    vec2 mesoGrad = irot * w1.yz;\n"
	"    vec2 localWind1 = normalize(baseWind + vec2(-mesoGrad.y, mesoGrad.x) * 0.22);\n"
	"    float t2 = mod(anim_time * 0.48, 1024.0);\n"
	"    vec2 p2 = rot * (rot * (worldPos * 0.120) + w0.yz * 0.45 + w1.yz * 0.55) - dir2 * t2;\n"
	"    vec3 w2 = chaff_grad_noise2d(p2, 0x45444459u);\n"
	"    vec2 microGrad = (irot * irot) * w2.yz;\n"
	"    vec2 windDir = normalize(localWind1 + vec2(-microGrad.y, microGrad.x) * 0.16);\n"
	"    float gustEnv = clamp(0.42 + 0.45 * w0.x + 0.55 * (w1.x - 0.38) + 0.30 * (w2.x - 0.42), 0.25, 1.35);\n"
	"    float catspaw = smoothstep(0.38, 0.76, w1.x * 0.62 + w2.x * 0.38);\n"
	"    return vec4(windDir.x, windDir.y, gustEnv, catspaw);\n"
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
	"vec4 eval_zoom_water_waves(vec2 pos, float anim_time, float dist, float fw) {\n"
	"    vec4 wind = eval_recursive_wind(pos, anim_time);\n"
	"    vec2 wDir = wind.xy;\n"
	"    vec2 wPerp = vec2(-wDir.y, wDir.x);\n"
	"    float gust = wind.z;\n"
	"    const float freqs[6] = float[6](0.024, 0.072, 0.220, 0.680, 2.150, 6.800);\n"
	"    const float amps[6]  = float[6](1.450, 0.680, 0.280, 0.115, 0.045, 0.018);\n"
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
	"        float wDist = clamp((dFar[i] - dist) / max(1.0, dFar[i] - dNear[i]), (i == 0) ? 0.35 : 0.0, 1.0);\n"
	"        float wNyq  = smoothstep(0.55, 0.16, freqs[i] * fw);\n"
	"        float gustScale = (i >= 3) ? (0.75 + 0.45 * gust) : (0.85 + 0.20 * gust);\n"
	"        float w = wDist * wNyq * gustScale;\n"
	"        if (w > 0.001) {\n"
	"            float phase = mod(anim_time * spds[i], 1024.0);\n"
	"            /* Spatial wind-streamline warp (never multiplied by anim_time, so wave speed never accelerates) */\n"
	"            vec2 windWarp = (wDir * (gust - 0.65) + wPerp * (wind.w - 0.5)) * 0.45;\n"
	"            vec2 q = pRot * freqs[i] + windWarp - dirs[i] * phase;\n"
	"            vec3 g = chaff_grad_noise2d(q, seeds[i]);\n"
	"            float signedVal = g.x - 0.5;\n"
	"            /* Trochoidal Gerstner-like crest sharpening without derivative cancellation */\n"
	"            float ridge = 0.5 - abs(signedVal);\n"
	"            float shaped = mix(signedVal, ridge - 0.18, 0.35);\n"
	"            float dShape = 0.85 + 0.45 * ridge;\n"
	"            vec2 worldG = curInvRot * g.yz;\n"
	"            h += shaped * amps[i] * w;\n"
	"            grad += worldG * (dShape * freqs[i] * amps[i] * w);\n"
	"            crest += max(0.0, ridge - 0.22) * (w * ((i <= 2) ? 1.4 : 0.8));\n"
	"            pRot += g.yz * (0.11 * w);\n"
	"        }\n"
	"        pRot = rot * pRot;\n"
	"        curInvRot = curInvRot * irot;\n"
	"    }\n"
	"    return vec4(h, grad.x, grad.y, clamp(crest, 0.0, 1.0));\n"
	"}\n"
	"/* High-frequency wind-driven capillary micro-facets for full-surface specular shimmer */\n"
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
	"void main() {\n"
	"    float scale = (uHeightScale != 0.0) ? uHeightScale : 1.0;\n"
	"    float effectiveScale = scale * 0.0625;\n"
	"    vec2 shoreGrad = vec2(0.0);\n"
	"    float raw_h = sampleHeightSmooth(vUV, shoreGrad);\n"
	"    float fragZ = raw_h * effectiveScale;\n"
	"    shoreGrad *= effectiveScale;\n"
	"    float fw = clamp(max(length(dFdx(vWorldPos)), length(dFdy(vWorldPos))), 1e-4, max(uChunkSpan.x, uChunkSpan.y) * 0.25);\n"
	"    float rawDepth = uSeaLevel - fragZ;\n"
	"    vec3 shoreRelief = eval_shore_micro_relief(vWorldPos, rawDepth, fw);\n"
	"    fragZ += shoreRelief.x;\n"
	"    shoreGrad += shoreRelief.yz;\n"
	"    float fragWaterDepth = uSeaLevel - fragZ;\n"
	"    if (fragWaterDepth <= 0.0) {\n"
	"        discard;\n"
	"    }\n"
	"    vec3 viewDir = normalize(vViewDir);\n"
	"    vec3 torusN = normalize(vTorusNormal);\n"
	"    vec3 tangentU = normalize(vTangentU);\n"
	"    vec3 tangentV = normalize(vTangentV);\n"
	"    vec3 sunDir = normalize(-0.577 * tangentU - 0.577 * tangentV + 0.816 * torusN);\n"
	"    /* Biome-aware water optics derived from uSubstrateTex (populated via ndw_biome_base_rgb) */\n"
	"    vec3 rawBiome = texture(uSubstrateTex, vUV).rgb;\n"
	"    float maxBio = max(rawBiome.r, max(rawBiome.g, rawBiome.b));\n"
	"    float minBio = min(rawBiome.r, min(rawBiome.g, rawBiome.b));\n"
	"    float wVolcanic = smoothstep(0.32, 0.14, maxBio);\n"
	"    float wPolar = smoothstep(0.52, 0.72, minBio) * smoothstep(0.18, 0.04, max(rawBiome.r, rawBiome.g) - rawBiome.b);\n"
	"    float wTropical = smoothstep(0.46, 0.68, rawBiome.r) * smoothstep(0.38, 0.58, rawBiome.g) * (1.0 - wPolar);\n"
	"    float wTemperate = clamp(1.0 - (wVolcanic + wPolar + wTropical), 0.0, 1.0);\n"
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
	"    /* Evaluate recursive moving wind field & height-model basin/fetch profile */\n"
	"    vec4 wind = eval_recursive_wind(vWorldPos, uAnimTime);\n"
	"    vec2 basin = eval_water_fetch_and_basin(vUV, wind.xy, shoreGrad, fragWaterDepth, effectiveScale);\n"
	"    float openSeaFactor = basin.x;\n"
	"    float windFetch = basin.y;\n"
	"    /* Scale gravity waves by depth, upwind fetch, and lake-vs-ocean basin exposure */\n"
	"    float shoreWaveDamp = smoothstep(0.0, 1.8, fragWaterDepth);\n"
	"    float basinWaveScale = (0.38 + 0.62 * openSeaFactor) * windFetch * (0.30 + 0.70 * shoreWaveDamp);\n"
	"    vec4 waves = eval_zoom_water_waves(vWorldPos, uAnimTime, vDistance, fw);\n"
	"    vec2 capGrad = eval_wind_capillary_facets(vWorldPos, uAnimTime, wind, vDistance, fw) * smoothstep(0.0, 0.35, fragWaterDepth);\n"
	"    vec2 waveGrad = waves.yz * basinWaveScale + capGrad * 0.45;\n"
	"    vec2 specGrad = waves.yz * (basinWaveScale * 1.45 + 0.35) + capGrad * 1.35;\n"
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
	"    float specSun     = (specMicro + specWavelet + specBroad) * smoothstep(0.0, 0.25, fragWaterDepth);\n"
	"    /* Schlick Fresnel for air-water interface (R0 = 0.02) + per-wavelet facet tilt contrast */\n"
	"    float flatNdotV = max(0.0, dot(torusN, viewDir));\n"
	"    float facetTilt = clamp((flatNdotV - dot(specNormal, viewDir)) * 2.8, -0.18, 0.32);\n"
	"    float fresnel = clamp(0.02 + 0.98 * pow(clamp(1.0 - NdotV, 0.0, 1.0), 5.0) + max(0.0, facetTilt) * 0.22, 0.015, 0.65);\n"
	"    vec3 sky = (length(uSkyColor) > 0.001) ? uSkyColor : vec3(0.55, 0.72, 0.88);\n"
	"    vec3 sunColor = vec3(1.0, 0.98, 0.92);\n"
	"    /* 3D wave slope hillshading: sunward wave faces tilt luminous, leeward troughs deepen */\n"
	"    float waveSlopeDiff = dot(specNormal, sunDir) - dot(torusN, sunDir);\n"
	"    float hillshadeLit = max(0.0, waveSlopeDiff);\n"
	"    float hillshadeShd = max(0.0, -waveSlopeDiff);\n"
	"    vec3 waveShade = C_SHELF * (1.0 + hillshadeLit * 0.55 - hillshadeShd * 0.40);\n"
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
	"    float foam = clamp(crestLap + whitecap, 0.0, 0.38) * foamDistFade;\n"
	"    vec3 foamColor = vec3(0.92, 0.96, 1.0);\n"
	"    /* Pure air-water surface compositing: shallow water stays glass-clear except for wavelet specularity & Fresnel */\n"
	"    float shallowGlassMask = smoothstep(0.0, 4.5, fragWaterDepth);\n"
	"    float skyReflWeight = fresnel * (0.18 + 0.82 * shallowGlassMask);\n"
	"    float sssWeight     = crestGlow * 0.16;\n"
	"    float specWeight    = clamp(specSun, 0.0, 0.92);\n"
	"    float foamWeight    = foam;\n"
	"    float totalWeight   = skyReflWeight + sssWeight + specWeight + foamWeight;\n"
	"    vec3 reflSkyCol = mix(sky * 0.92, sky * 1.08, clamp(0.5 + facetTilt * 2.2, 0.0, 1.0));\n"
	"    vec3 surfaceNum = reflSkyCol * skyReflWeight + sssTint + sunColor * specSun + foamColor * foamWeight;\n"
	"    vec3 surfaceRgb = (totalWeight > 1e-4) ? clamp(surfaceNum / max(totalWeight, 1e-4), 0.0, 1.0) : reflSkyCol;\n"
	"    if (uLambLight.w > 0.001) {\n"
	"        vec2 dXY = uLambLight.xy - vWorldPos;\n"
	"        float d2 = dot(dXY, dXY);\n"
	"        float atten = (1.0 / (1.0 + d2 * 0.00055)) * smoothstep(220.0 * 220.0, 28.0 * 28.0, d2);\n"
	"        surfaceRgb += vec3(1.06, 0.99, 0.84) * (uLambLight.w * 0.16 * atten);\n"
	"    }\n"
	"    float limb = pow(clamp(1.0 - max(0.0, dot(torusN, viewDir)), 0.0, 1.0), 3.0);\n"
	"    surfaceRgb = mix(surfaceRgb, vec3(0.35, 0.70, 1.0), limb * 0.55 * uAtmoIntensity);\n"
	"    float edgeFade = smoothstep(0.0, 0.14, fragWaterDepth);\n"
	"    float alpha = clamp(1.0 - (1.0 - skyReflWeight) * (1.0 - sssWeight) * (1.0 - specWeight) * (1.0 - foamWeight) + limb * 0.35 * uAtmoIntensity, 0.0, 0.94) * edgeFade;\n"
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
	"vec4 fetchBoundedTexel(ivec2 tc, ivec4 iBounds, ivec2 maxTex) {\n"
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
	"    vec2 filterW = clamp(fw * 0.90, vec2(0.26), vec2(0.50));\n"
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
	"    const mat2 rot  = mat2(0.8, -0.6, 0.6, 0.8);\n"
	"    const mat2 irot = mat2(0.8,  0.6, -0.6, 0.8);\n"
	"    const vec2 dir0 = vec2(0.741, -0.671);\n"
	"    const vec2 dir1 = vec2(0.822, -0.569);\n"
	"    const vec2 dir2 = vec2(0.640, -0.768);\n"
	"    float t0 = mod(anim_time * 0.08, 1024.0);\n"
	"    vec2 p0 = worldPos * 0.0055 - dir0 * t0;\n"
	"    vec3 w0 = chaff_grad_noise2d(p0, 0x57494E44u);\n"
	"    vec2 baseWind = normalize(dir0 + vec2(-w0.z, w0.y) * 0.28);\n"
	"    float t1 = mod(anim_time * 0.22, 1024.0);\n"
	"    vec2 p1 = rot * (worldPos * 0.028 + w0.yz * 0.65) - dir1 * t1;\n"
	"    vec3 w1 = chaff_grad_noise2d(p1, 0x47555354u);\n"
	"    vec2 mesoGrad = irot * w1.yz;\n"
	"    vec2 localWind1 = normalize(baseWind + vec2(-mesoGrad.y, mesoGrad.x) * 0.22);\n"
	"    float t2 = mod(anim_time * 0.48, 1024.0);\n"
	"    vec2 p2 = rot * (rot * (worldPos * 0.120) + w0.yz * 0.45 + w1.yz * 0.55) - dir2 * t2;\n"
	"    vec3 w2 = chaff_grad_noise2d(p2, 0x45444459u);\n"
	"    vec2 microGrad = (irot * irot) * w2.yz;\n"
	"    vec2 windDir = normalize(localWind1 + vec2(-microGrad.y, microGrad.x) * 0.16);\n"
	"    float gustEnv = clamp(0.42 + 0.45 * w0.x + 0.55 * (w1.x - 0.38) + 0.30 * (w2.x - 0.42), 0.25, 1.35);\n"
	"    float catspaw = smoothstep(0.38, 0.76, w1.x * 0.62 + w2.x * 0.38);\n"
	"    return vec4(windDir.x, windDir.y, gustEnv, catspaw);\n"
	"}\n"
	"vec4 eval_zoom_water_waves(vec2 pos, vec4 wind, float anim_time, float dist, float fw) {\n"
	"    vec2 wDir = wind.xy;\n"
	"    vec2 wPerp = vec2(-wDir.y, wDir.x);\n"
	"    float gust = wind.z;\n"
	"    const float freqs[6] = float[6](0.024, 0.072, 0.220, 0.680, 2.150, 6.800);\n"
	"    const float amps[6]  = float[6](1.450, 0.680, 0.280, 0.115, 0.045, 0.018);\n"
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
	"        float wDist = clamp((dFar[i] - dist) / max(1.0, dFar[i] - dNear[i]), (i == 0) ? 0.35 : 0.0, 1.0);\n"
	"        float wNyq  = smoothstep(0.55, 0.16, freqs[i] * fw);\n"
	"        float gustScale = (i >= 3) ? (0.75 + 0.45 * gust) : (0.85 + 0.20 * gust);\n"
	"        float w = wDist * wNyq * gustScale;\n"
	"        if (w > 0.001) {\n"
	"            float phase = mod(anim_time * spds[i], 1024.0);\n"
	"            vec2 windWarp = (wDir * (gust - 0.65) + wPerp * (wind.w - 0.5)) * 0.45;\n"
	"            vec2 q = pRot * freqs[i] + windWarp - dirs[i] * phase;\n"
	"            vec3 g = chaff_grad_noise2d(q, seeds[i]);\n"
	"            float signedVal = g.x - 0.5;\n"
	"            float ridge = 0.5 - abs(signedVal);\n"
	"            float shaped = mix(signedVal, ridge - 0.18, 0.35);\n"
	"            float dShape = 0.85 + 0.45 * ridge;\n"
	"            vec2 worldG = curInvRot * g.yz;\n"
	"            h += shaped * amps[i] * w;\n"
	"            grad += worldG * (dShape * freqs[i] * amps[i] * w);\n"
	"            crest += max(0.0, ridge - 0.22) * (w * ((i <= 2) ? 1.4 : 0.8));\n"
	"            pRot += g.yz * (0.11 * w);\n"
	"        }\n"
	"        pRot = rot * pRot;\n"
	"        curInvRot = curInvRot * irot;\n"
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
	"    vec2 wCoord2 = vec2(dot(pos, prevW) * 1.35 + dot(pos, prevP) * 0.35, dot(pos, prevP) * 0.85 - dot(pos, prevW) * 0.25) * f2 + gustShift * 1.4 - vec2(t2, -0.22 * t2);\n"
	"    vec3 c1 = (nyq1 > 0.001) ? chaff_grad_noise2d(wCoord1, 0xCA9111A1u) : vec3(0.5, 0.0, 0.0);\n"
	"    vec3 c2 = (nyq2 > 0.001) ? chaff_grad_noise2d(wCoord2 + c1.yz * 0.25, 0xCA9111A2u) : vec3(0.5, 0.0, 0.0);\n"
	"    vec2 localG = c1.yz * (0.14 * nyq1) + c2.yz * (0.11 * nyq2);\n"
	"    vec2 worldCapGrad = (wDir * localG.x * 1.40 + wPerp * localG.y * 0.80) * rippleEnv;\n"
	"    return worldCapGrad;\n"
	"}\n"
	"void main() {\n"
	"    float vDist = length(uCameraPos - vWorldPos);\n"
	"    float fogFactor = clamp((vDist - uFogStart) / max(1.0, uFogEnd - uFogStart), 0.0, 1.0);\n"
	"    vec3 sky = (length(uSkyColor) > 0.001) ? uSkyColor : vec3(0.55, 0.70, 0.85);\n"
	"    /* Exact horizontal water-plane (Z = uSeaLevel) intersection of the camera ray through this billboard pixel:\n"
	"     * matches vWorldPos of s_vs_water / s_fs_water at the exact same screen pixel! */\n"
	"    vec2 waterSurfPos = vec2(vLocalPos.x, vLocalPos.y + (uSeaLevel - vQuadUV.z) * 0.7002075);\n"
	"    float waterFw = max(1e-4, max(length(dFdx(waterSurfPos)), length(dFdy(waterSurfPos))));\n"
	"    float d = 0.0;\n"
	"    bool isSubmerged = false;\n"
	"    vec4 wind = vec4(0.7071, -0.7071, 1.0, 0.5);\n"
	"    vec4 waves = vec4(0.0);\n"
	"    vec2 capGrad = vec2(0.0);\n"
	"    vec2 waveRefGrad = vec2(0.0);\n"
	"    float waveSurfZ = uSeaLevel;\n"
	"    /* Flora (style 1..2) has anti-submersion; non-flora submerges when its ground base (vQuadUV.z) is in water */\n"
	"    bool canSubmerge = (vQuadUV.w < 0.5 || vQuadUV.w > 2.5);\n"
	"    if (uSeaLevel > -900.0 && canSubmerge && vQuadUV.z < uSeaLevel + 0.85) {\n"
	"        wind = eval_recursive_wind(waterSurfPos, uAnimTime);\n"
	"        waves = eval_zoom_water_waves(waterSurfPos, wind, uAnimTime, vDist, waterFw);\n"
	"        waveSurfZ = uSeaLevel + waves.x * 0.35 * smoothstep(2600.0, 400.0, vDist);\n"
	"        if (vQuadUV.z < waveSurfZ + 0.35 && vLocalPos.z < waveSurfZ) {\n"
	"            d = waveSurfZ - vLocalPos.z;\n"
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
	"        float shadowAlpha = clamp((penumbra * 0.36 * dapple + umbra) * vTint.a * waterAtten * distAtten * (1.0 - fogFactor * uAtmoIntensity), 0.0, 0.48);\n"
	"        if (shadowAlpha <= 0.005) discard;\n"
	"        vec3 shadowRgb = mix(vec3(0.05, 0.07, 0.10), sky * 0.16, 0.45);\n"
	"        FragColor = vec4(shadowRgb, shadowAlpha);\n"
	"        return;\n"
	"    }\n"
	"    /* 2. Sub-Texel Anti-Aliased, Snell-Refracted & Outline-Softened Sprite Sampling */\n"
	"    float style = vQuadUV.w;\n"
	"    bool isLambGlow = (style > 3.5);\n"
	"    vec2 sampleUV = vUV;\n"
	"    if (isSubmerged) {\n"
	"        vec2 atlasSize = max(vec2(1.0), vec2(textureSize(uAtlasTex, 0)));\n"
	"        float depthRamp = smoothstep(0.0, 1.4, d) * (0.48 + 0.52 * smoothstep(1.0, 7.0, d));\n"
	"        float refractDistFade = clamp(1.0 - (vDist - 800.0) / 2400.0, 0.0, 1.0);\n"
	"        /* Physical Snell's law air->water (n1/n2 = 1/1.333) wave-slope ray deflection + vertical foreshortening */\n"
	"        vec2 snellTexelShift = vec2(\n"
	"            -waveRefGrad.x * (3.20 * depthRamp),\n"
	"            -waveRefGrad.y * (2.20 * depthRamp) - clamp(d * 0.10, 0.0, 0.75) * smoothstep(0.0, 1.0, d)\n"
	"        ) * refractDistFade;\n"
	"        sampleUV = vUV + snellTexelShift / atlasSize;\n"
	"        if (vSpriteBounds.z > vSpriteBounds.x && vSpriteBounds.w > vSpriteBounds.y) {\n"
	"            vec2 halfTexel = 0.5 / atlasSize;\n"
	"            sampleUV = clamp(sampleUV, vSpriteBounds.xy + halfTexel, vSpriteBounds.zw - halfTexel);\n"
	"        }\n"
	"    }\n"
	"    vec4 baseCol = sampleAtlasSmooth(vUV, vSpriteBounds, style);\n"
	"    vec4 subSample = isSubmerged ? sampleAtlasSmooth(sampleUV, vSpriteBounds, style) : baseCol;\n"
	"    float effSampleAlpha = isSubmerged ? mix(baseCol.a, subSample.a, smoothstep(0.0, 0.65, d)) : baseCol.a;\n"
	"    float edgeAlpha = smoothstep(0.12, 0.46, effSampleAlpha);\n"
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
	"    vec3 g2 = (distFade > 0.01) ? chaff_grad_noise2d(surfPos * 0.78 + vec2(17.3, -29.1), 0xC8013EA4u) : vec3(0.5, 0.0, 0.0);\n"
	"    float microH = (g1.x - 0.5) * 0.26 + (g2.x - 0.5) * 0.16 * distFade;\n"
	"    vec2 microGrad = g1.yz * 0.22 + g2.yz * (0.32 * distFade);\n"
	"    float microCavity = clamp(max(0.0, 0.48 - g1.x) * 0.42 + max(0.0, 0.48 - g2.x) * 0.32 * distFade, 0.0, 0.34);\n"
	"    bool isFoliage = !isLambGlow && ((style > 0.5 && style < 2.5) || (albedo.g > max(albedo.r, albedo.b) * 1.06));\n"
	"    bool isMineral = !isLambGlow && (style > 2.5);\n"
	"    if (isFoliage && distFade > 0.01) {\n"
	"        /* Harmonize foliage palette with terrain vegetation (deepMoss / lushTurf / warmClover) */\n"
	"        vec3 warmFoliage = albedo * vec3(1.06, 1.02, 0.84);\n"
	"        vec3 deepLeaf    = albedo * vec3(0.84, 0.96, 0.80);\n"
	"        albedo = mix(deepLeaf, warmFoliage, smoothstep(0.28, 0.72, g1.x));\n"
	"        float leafRelief = (g1.x - 0.5) * 0.16 * distFade + (g2.x - 0.5) * 0.14 * closeZoom;\n"
	"        albedo *= clamp(1.0 + leafRelief, 0.80, 1.22);\n"
	"    } else if (isMineral && distFade > 0.01) {\n"
	"        /* Harmonize mineral sprites with terrain slate/granite strata & fracture crevices */\n"
	"        vec3 slateWarm   = vec3(0.42, 0.39, 0.35);\n"
	"        vec3 graniteCool = vec3(0.34, 0.36, 0.38);\n"
	"        vec3 rockBase    = mix(slateWarm, graniteCool, clamp(g1.x, 0.0, 1.0)) * (0.75 + 0.65 * rawLuma);\n"
	"        albedo = mix(albedo, rockBase, 0.38 * distFade);\n"
	"        float rockCrevice = smoothstep(0.11, 0.015, abs(g2.x - 0.5)) * distFade;\n"
	"        albedo *= (1.0 - rockCrevice * 0.28);\n"
	"    }\n"
	"    vec3 proxyN = normalize(vec3(qx * 0.65, 0.55 - 0.15 * qy, 0.22 + 0.68 * qy));\n"
	"    vec3 N_bb = normalize(proxyN - vec3(microGrad.x * 0.38, 0.0, microGrad.y * 0.38));\n"
	"    vec3 L_sun = normalize(vec3(-0.577, -0.577, 0.816));\n"
	"    float sunDiff = max(0.0, dot(N_bb, L_sun));\n"
	"    float cavityAO = clamp(1.0 + microH * 0.28 - microCavity, 0.68, 1.15);\n"
	"    float hAboveGround = max(0.0, vLocalPos.z - vQuadUV.z);\n"
	"    float contactAO = isLambGlow ? 1.0 : mix(0.68, 1.0, smoothstep(0.0, 5.5, hAboveGround));\n"
	"    float hemiSky = 0.5 + 0.5 * N_bb.z;\n"
	"    vec3 ambCol = mix(vec3(0.50, 0.55, 0.62), vec3(0.68, 0.74, 0.82), hemiSky);\n"
	"    vec3 sunCol = vec3(1.06, 1.02, 0.95);\n"
	"    vec3 lightTerm = (ambCol * 0.52 + sunCol * (0.58 * sunDiff)) * (cavityAO * contactAO);\n"
	"    vec3 lit = albedo * mix(vec3(1.0), lightTerm, isLambGlow ? 0.32 : 0.78);\n"
	"    if (!isLambGlow && uSeaLevel > -900.0 && !isSubmerged) {\n"
	"        /* Capillary wet-rim darkening just above the waterline (matches terrain shoreWetMoisture) */\n"
	"        float heightAboveWater = vLocalPos.z - waveSurfZ;\n"
	"        float wetRim = smoothstep(0.45, 0.0, heightAboveWater);\n"
	"        lit *= (1.0 - wetRim * 0.18);\n"
	"    }\n"
	"    if (isLambGlow) {\n"
	"        /* Subtle self-luminance: slightly brighter than the environment with a soft warm rim */\n"
	"        float rimGlow = smoothstep(0.85, 0.25, baseCol.a) * edgeAlpha;\n"
	"        vec3 bodyLit = clamp(lit * 1.16 + vec3(0.11, 0.10, 0.07) + vec3(0.14, 0.12, 0.08) * rimGlow, 0.0, 1.0);\n"
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
	"    /* 5. Physical Volumetric Water Column + Exact s_fs_water Surface Interface Over Submerged Parts */\n"
	"    if (isSubmerged) {\n"
	"        float dClamped = clamp(d, 0.0, 64.0);\n"
	"        float baseWaterDepth = max(dClamped, uSeaLevel - vQuadUV.z);\n"
	"        float baseSubFade = clamp((waveSurfZ + 0.35 - vQuadUV.z) / 0.50, 0.0, 1.0);\n"
	"        float wetBlend = smoothstep(0.0, 0.20, dClamped) * baseSubFade * (isLambGlow ? edgeAlpha : 1.0);\n"
	"        float caustDistFade = clamp(1.0 - (vDist - 900.0) / 2500.0, 0.0, 1.0);\n"
	"        float caustCloseZoom = clamp((420.0 - vDist) / 300.0, 0.0, 1.0);\n"
	"        float caustic = 0.0;\n"
	"        if (caustDistFade > 0.01 && dClamped < 18.0) {\n"
	"            float tC = mod(uAnimTime, 256.0);\n"
	"            vec2 cUV = waterSurfPos + waveRefGrad * 1.6;\n"
	"            float c1 = chaff_sample_3d(vec3(cUV * 1.85, tC * 2.4), 3u, 0x1337u);\n"
	"            float c2 = chaff_sample_3d(vec3((cUV + vec2(37.0, -23.0)) * 1.85, tC * 1.9), 3u, 0xBEEFu);\n"
	"            float caustMeso = pow(clamp(1.0 - abs(c1 - c2) * 5.6, 0.0, 1.0), 3.0);\n"
	"            float caustFine = 0.0;\n"
	"            if (caustCloseZoom > 0.01) {\n"
	"                float c3 = chaff_sample_3d(vec3(cUV * 4.8, tC * 3.6), 2u, 0xCA57u);\n"
	"                float c4 = chaff_sample_3d(vec3((cUV + vec2(-19.0, 41.0)) * 4.8, tC * 3.0), 2u, 0xCA58u);\n"
	"                caustFine = pow(clamp(1.0 - abs(c3 - c4) * 5.6, 0.0, 1.0), 3.0) * caustCloseZoom;\n"
	"            }\n"
	"            caustic = (caustMeso + caustFine * 0.65) * caustDistFade;\n"
	"        }\n"
	"        float causticFocus = smoothstep(0.05, 0.85, dClamped) * exp(-0.12 * dClamped);\n"
	"        vec3 wetLit = lit * mix(1.0, isLambGlow ? 0.88 : 0.80, smoothstep(0.0, 0.50, dClamped));\n"
	"        vec3 caustLit = wetLit * (1.0 + caustic * causticFocus * 0.52);\n"
	"        /* 5a. Underwater column Beer-Lambert spectral absorption & volumetric water body in-scattering */\n"
	"        float cosView = 0.5736;\n"
	"        float opticalDepth = dClamped / cosView;\n"
	"        float shallowFactor = smoothstep(0.0, 6.0, baseWaterDepth);\n"
	"        float effectiveOpticalDepth = opticalDepth * (0.48 + 0.52 * shallowFactor);\n"
	"        vec3 seabedAbsorption = exp(-vec3(0.125, 0.045, 0.014) * effectiveOpticalDepth);\n"
	"        vec3 transmittedSpr = caustLit * seabedAbsorption;\n"
	"        vec3 C_SHALLOW = vec3(0.13, 0.46, 0.52);\n"
	"        vec3 C_SHELF   = vec3(0.09, 0.34, 0.46);\n"
	"        vec3 C_OUTER   = vec3(0.05, 0.22, 0.38);\n"
	"        vec3 C_ABYSS   = vec3(0.024, 0.10, 0.24);\n"
	"        vec3 inscatterCol = mix(C_SHALLOW, C_SHELF, smoothstep(1.5, 6.5, baseWaterDepth));\n"
	"        inscatterCol = mix(inscatterCol, C_OUTER, smoothstep(6.5, 15.0, baseWaterDepth));\n"
	"        inscatterCol = mix(inscatterCol, C_ABYSS, smoothstep(15.0, 32.0, baseWaterDepth));\n"
	"        float bodyScatterWeight = clamp(0.24 * smoothstep(0.0, 0.70, dClamped) + (0.42 + 0.58 * shallowFactor) * (1.0 - exp(-0.15 * effectiveOpticalDepth)), 0.0, 0.92);\n"
	"        vec3 waterColumn = mix(transmittedSpr, inscatterCol, bodyScatterWeight);\n"
	"        /* 5b. Exact s_fs_water Air-Water Surface Interface composited ON TOP of the water column */\n"
	"        float shoreWaveDamp = smoothstep(0.0, 1.8, baseWaterDepth);\n"
	"        float openSeaFactor = smoothstep(2.5, 12.0, baseWaterDepth);\n"
	"        float basinWaveScale = (0.38 + 0.62 * openSeaFactor) * (0.55 + 0.45 * shoreWaveDamp);\n"
	"        vec2 waveGrad = waves.yz * basinWaveScale + capGrad * 0.45;\n"
	"        vec2 specGrad = waves.yz * (basinWaveScale * 1.45 + 0.35) + capGrad * 1.35;\n"
	"        vec3 surfViewDir = vec3(0.0, 0.573576, 0.819152);\n"
	"        vec3 waveNormal  = normalize(vec3(-waveGrad.x, -waveGrad.y, 1.0));\n"
	"        vec3 specNormal  = normalize(vec3(-specGrad.x, -specGrad.y, 1.0));\n"
	"        float NdotV = max(0.0, dot(waveNormal, surfViewDir));\n"
	"        vec3 flatRefl = reflect(-surfViewDir, vec3(0.0, 0.0, 1.0));\n"
	"        vec3 glintSunDir = normalize(flatRefl - vec3(0.14, 0.04, -0.08));\n"
	"        vec3 halfVec = normalize(glintSunDir + surfViewDir);\n"
	"        float NdotH_cap  = max(0.0, dot(specNormal, halfVec));\n"
	"        float NdotH_wave = max(0.0, dot(waveNormal, halfVec));\n"
	"        float closeWeight = clamp(1.0 - (vDist - 350.0) / 1400.0, 0.0, 1.0);\n"
	"        float specMicro   = pow(smoothstep(0.9942, 0.9997, NdotH_cap), 2.5) * (0.42 + 0.52 * wind.w) * (0.50 + 0.50 * closeWeight);\n"
	"        float specWavelet = pow(smoothstep(0.9840, 0.9982, NdotH_wave), 2.0) * (0.22 + 0.18 * wind.z);\n"
	"        float specBroad   = smoothstep(0.9620, 0.9935, NdotH_wave) * (0.09 + 0.05 * (1.0 - closeWeight));\n"
	"        float specSun     = (specMicro + specWavelet + specBroad) * smoothstep(0.0, 0.20, dClamped);\n"
	"        float flatNdotV = surfViewDir.z;\n"
	"        float facetTilt = clamp((flatNdotV - dot(specNormal, surfViewDir)) * 2.8, -0.18, 0.32);\n"
	"        float fresnel = clamp(0.16 + 0.84 * pow(clamp(1.0 - NdotV, 0.0, 1.0), 3.0) + max(0.0, facetTilt) * 0.34, 0.14, 0.72);\n"
	"        float waveSlopeDiff = dot(specNormal, L_sun) - L_sun.z;\n"
	"        float hillshadeLit  = max(0.0, waveSlopeDiff);\n"
	"        float hillshadeShd  = max(0.0, -waveSlopeDiff);\n"
	"        vec3 waveShade = C_SHELF * (1.0 + hillshadeLit * 0.55 - hillshadeShd * 0.40);\n"
	"        float crestGlow = clamp(waves.w * 0.60 + hillshadeLit * 1.40, 0.0, 1.0) * smoothstep(0.25, 4.0, baseWaterDepth);\n"
	"        vec3 sssTint = mix(C_SHALLOW, waveShade, 0.55) * (crestGlow * 0.28);\n"
	"        float shallowGlassMask = smoothstep(0.0, 2.5, baseWaterDepth);\n"
	"        float skyReflWeight = fresnel * (0.42 + 0.58 * shallowGlassMask);\n"
	"        float sssWeight     = crestGlow * 0.22;\n"
	"        float specWeight    = clamp(specSun, 0.0, 0.94);\n"
	"        float totalWeight   = skyReflWeight + sssWeight + specWeight;\n"
	"        vec3 reflSkyCol = mix(sky * 0.92, sky * 1.08, clamp(0.5 + facetTilt * 2.2, 0.0, 1.0));\n"
	"        vec3 sunColor   = vec3(1.0, 0.98, 0.92);\n"
	"        vec3 surfaceNum = reflSkyCol * skyReflWeight + sssTint + sunColor * specSun;\n"
	"        vec3 surfaceRgb = (totalWeight > 1e-4) ? clamp(surfaceNum / max(totalWeight, 1e-4), 0.0, 1.0) : reflSkyCol;\n"
	"        float surfAlpha = clamp(1.0 - (1.0 - skyReflWeight) * (1.0 - sssWeight) * (1.0 - specWeight), 0.0, 0.94) * smoothstep(0.0, 0.14, dClamped);\n"
	"        vec3 submergedComposite = mix(waterColumn, surfaceRgb, surfAlpha);\n"
	"        lit = mix(lit, submergedComposite, wetBlend);\n"
	"        if (isLambGlow && edgeAlpha < 0.99) {\n"
	"            outAlpha = clamp(edgeAlpha + auraAlpha * clamp(1.0 - dClamped * 0.35, 0.0, 1.0), 0.0, 1.0) * vTint.a;\n"
	"        }\n"
	"        /* Crisp air-water meniscus reflection/foam ring right along the live wave intersection */\n"
	"        float meniscus = smoothstep(0.0, 0.16, dClamped) * smoothstep(0.95, 0.16, dClamped) * clamp(1.0 - (vDist - 600.0) / 1800.0, 0.0, 1.0) * baseSubFade * (isLambGlow ? edgeAlpha : 1.0);\n"
	"        vec3 meniscusColor = mix(reflSkyCol, vec3(0.96, 0.99, 1.0), 0.74);\n"
	"        lit = mix(lit, meniscusColor, meniscus * 0.62);\n"
	"    }\n"
	"    /* 6. Atmospheric Distance Fog & Planetary Limb Scattering Parity with Terrain */\n"
	"    vec3 surfaceLit = mix(lit, vec3(0.35, 0.65, 0.95), 0.22 * uAtmoIntensity);\n"
	"    vec3 finalRgb = mix(surfaceLit, sky, fogFactor * uAtmoIntensity);\n"
	"    FragColor = vec4(finalRgb, outAlpha);\n"
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

		out_verts[i].local_pos[0] = b->x + c[k].dx;
		out_verts[i].local_pos[1] = b->y - c[k].dy / (sin_p > 0.1f ? sin_p : 0.819152f);
		out_verts[i].local_pos[2] = c[k].loc_z;

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
	GLint uLambLight;
	GLint aPos;
	GLint aUV;
	GLint aTint;
	GLint aLocalPos;
	GLint aQuadUV;
	GLint aSpriteBounds;
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
	float rel_center[3];
	float major_r;
	float minor_r;
	GLuint height_tex;
	GLuint substrate_tex;
} gl_water_chunk_t;

static struct {
	bool initialized;
	gl_terrain_pipe_t terrain_pipe;
	gl_water_pipe_t water_pipe;
	gl_billboard_pipe_t bb_pipe;

	GLuint grid_vbo_macro;
	GLuint grid_ibo_macro;
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
	float cur_lamb_light[4];
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

	create_static_grid(GL_GRID_QUADS_MACRO, grid_uv_scratch, indices_scratch, &s_gl.grid_vbo_macro, &s_gl.grid_ibo_macro);
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
		s_gl.terrain_pipe.uLambLight      = glGetUniformLocation(p, "uLambLight", 10);
		s_gl.terrain_pipe.aGridUV         = glGetAttribLocation(p, "a_grid_uv", 9);
	}

	/* Water pipeline */
	s_gl.water_pipe.prog = create_program(s_vs_water, s_fs_water);
	if (s_gl.water_pipe.prog) {
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
		s_gl.bb_pipe.uLambLight     = glGetUniformLocation(p, "uLambLight", 10);
		s_gl.bb_pipe.aPos           = glGetAttribLocation(p, "a_pos", 5);
		s_gl.bb_pipe.aUV            = glGetAttribLocation(p, "a_uv", 4);
		s_gl.bb_pipe.aTint          = glGetAttribLocation(p, "a_tint", 6);
		s_gl.bb_pipe.aLocalPos      = glGetAttribLocation(p, "a_local_pos", 11);
		s_gl.bb_pipe.aQuadUV        = glGetAttribLocation(p, "a_quad_uv", 9);
		s_gl.bb_pipe.aSpriteBounds  = glGetAttribLocation(p, "a_sprite_bounds", 15);
	}

	init_grid_buffers();
	s_gl.initialized = true;
}

void nd_seven_gl_set_lamb_light(float wx, float wy, float wz, float intensity)
{
	s_gl.cur_lamb_light[0] = wx;
	s_gl.cur_lamb_light[1] = wy;
	s_gl.cur_lamb_light[2] = wz;
	s_gl.cur_lamb_light[3] = (intensity > 0.0f) ? intensity : 0.0f;
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

void nd_seven_gl_begin_frame(uint32_t stage_w, uint32_t stage_h,
                             float tx, float ty, float tz,
                             float dist, float pitch_deg, float fov_deg,
                             float anim_time,
                             const nd_planet_def_t *pdef)
{
	s_gl.frame_count++;
	s_gl.water_count = 0;
	s_gl.cur_sea_level = pdef ? (float)nd_field_water_level(pdef->world_id) : -999.0f;
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
	} else if (quads >= GL_GRID_QUADS_HI) {
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
	if (!s_gl.initialized || !s_gl.terrain_pipe.prog) return;
	const nd_planet_def_t *eff_pdef = pdef ? pdef : &s_gl.cur_pdef;

	float terrain_sea_lvl = (sea_level > -900.0f) ? sea_level : (float)nd_field_water_level(world);
	if (terrain_sea_lvl > -900.0f) {
		bool contains_focal = (s_gl.cur_tx >= (float)ox && s_gl.cur_tx <= (float)(ox + span_w) &&
		                       s_gl.cur_ty >= (float)oy && s_gl.cur_ty <= (float)(oy + span_h));
		if (s_gl.cur_sea_level <= -900.0f || contains_focal) {
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

	glUniform1f(s_gl.terrain_pipe.uFogStart, s_gl.cur_dist * 1.5f);
	glUniform1f(s_gl.terrain_pipe.uFogEnd, s_gl.cur_dist * 4.0f);
	glUniform1f(s_gl.terrain_pipe.uAnimTime, s_gl.cur_anim_time);

	float space_t = fminf(1.0f, fmaxf(0.0f, (s_gl.cur_dist - 12000.0f) / 40000.0f));
	glUniform3f(s_gl.terrain_pipe.uSkyColor,
	            0.55f * (1.0f - space_t) + 0.012f * space_t,
	            0.70f * (1.0f - space_t) + 0.016f * space_t,
	            0.85f * (1.0f - space_t) + 0.035f * space_t);
	glUniform1f(s_gl.terrain_pipe.uAtmoIntensity, fminf(1.0f, fmaxf(0.0f, (s_gl.cur_dist - 1200.0f) / 8000.0f)));
	glUniform4f(s_gl.terrain_pipe.uLambLight,
	            s_gl.cur_lamb_light[0], s_gl.cur_lamb_light[1],
	            s_gl.cur_lamb_light[2], s_gl.cur_lamb_light[3]);

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
		s_gl.water_queue[s_gl.water_count].rel_center[0] = rel_center[0];
		s_gl.water_queue[s_gl.water_count].rel_center[1] = rel_center[1];
		s_gl.water_queue[s_gl.water_count].rel_center[2] = rel_center[2];
		s_gl.water_queue[s_gl.water_count].major_r = eff_pdef->major_r;
		s_gl.water_queue[s_gl.water_count].minor_r = eff_pdef->minor_r;
		s_gl.water_queue[s_gl.water_count].height_tex = slot->height_tex;
		s_gl.water_queue[s_gl.water_count].substrate_tex = slot->substrate_tex;
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
	glUniform4f(s_gl.water_pipe.uLambLight,
	            s_gl.cur_lamb_light[0], s_gl.cur_lamb_light[1],
	            s_gl.cur_lamb_light[2], s_gl.cur_lamb_light[3]);

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
	uint32_t out_verts_count = 0;
	bool emit_shadows = (s_gl.cur_dist < 16000.0f);

	for (uint32_t i = 0; i < count; i++) {
		bool want_shadow = emit_shadows && (batch[i].style != ND_BB_STYLE_GLOW) &&
		                   (batch[i].width >= 8.0f && batch[i].height >= 8.0f);
		uint32_t needed = want_shadow ? 12u : 6u;
		if (out_verts_count + needed > MAX_BB_VERTS)
			break;
		if (want_shadow) {
			eval_billboard_shadow_quad_with_origin(&batch[i], &s_gl.cur_pdef,
			                                       s_gl.cur_origin, &bb_verts[out_verts_count]);
			out_verts_count += 6u;
		}
		eval_billboard_quad_with_origin(&batch[i], s_gl.cur_pitch_deg, &s_gl.cur_pdef,
		                                s_gl.cur_origin, &bb_verts[out_verts_count]);
		out_verts_count += 6u;
	}
	if (out_verts_count == 0)
		return;

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
	glUniform4f(s_gl.bb_pipe.uLambLight,
	            s_gl.cur_lamb_light[0], s_gl.cur_lamb_light[1],
	            s_gl.cur_lamb_light[2], s_gl.cur_lamb_light[3]);

	glBindBuffer(GL_ARRAY_BUFFER, s_gl.bb_vbo);
	glBufferData(GL_ARRAY_BUFFER, bb_verts, out_verts_count * sizeof(nd_seven_gl_bb_vert_t), GL_DYNAMIC_DRAW);

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

	glDrawArrays(GL_TRIANGLES, 0, (GLsizei)out_verts_count);

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
	if (cam_dist > 0.0f && cam_dist <= 220.0f)
		return GL_GRID_QUADS_Z3;    /* 144x144 quads = 41,472 triangles (9x normal zoom) */
	if (cam_dist > 0.0f && cam_dist <= 340.0f)
		return GL_GRID_QUADS_Z2;    /* 96x96 quads = 18,432 triangles (4x normal zoom) */
	return GL_GRID_QUADS_HI;            /* 48x48 quads = 4,608 triangles (mid-near zoom) */
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
	float raw_t0 = anim_time * 0.08f;
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

	float raw_t1 = anim_time * 0.22f;
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

	float raw_t2 = anim_time * 0.48f;
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



