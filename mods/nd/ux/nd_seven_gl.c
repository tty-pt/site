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
	"    float scale = (uHeightScale != 0.0) ? uHeightScale : 1.0;\n"
	"    float effectiveScale = scale * 0.0625;\n"
	"    vec2 fragGrad;\n"
	"    float raw_h = sampleHeightSmooth(vUV, fragGrad);\n"
	"    float fragZ = raw_h * effectiveScale;\n"
	"    vec3 torusN = normalize(vTorusNormal);\n"
	"    vec3 tangentU = normalize(vTangentU);\n"
	"    vec3 tangentV = normalize(vTangentV);\n"
	"    vec3 fragNormal = normalize(torusN - fragGrad.x * effectiveScale * tangentU - fragGrad.y * effectiveScale * tangentV);\n"
	"    float fw = max(1e-4, max(length(dFdx(vWorldPos)), length(dFdy(vWorldPos))));\n"
	"    float rawDepth = (uSeaLevel > -900.0) ? (uSeaLevel - fragZ) : -999.0;\n"
	"    vec3 shoreRelief = (uSeaLevel > -900.0) ? eval_shore_micro_relief(vWorldPos, rawDepth, fw) : vec3(0.0);\n"
	"    fragZ += shoreRelief.x;\n"
	"    fragNormal = normalize(fragNormal - shoreRelief.y * tangentU - shoreRelief.z * tangentV);\n"
	"    float fragWaterDepth = (uSeaLevel > -900.0) ? (uSeaLevel - fragZ) : -999.0;\n"
	"    vec4 octaves = vec4(0.5);\n"
	"    vec4 detail = (fragWaterDepth <= 18.0) ? eval_zoom_terrain_detail(vWorldPos, vDistance, fw, octaves) : vec4(0.0);\n"
	"    vec2 edgeMask = 4.0 * vUV * (1.0 - vUV);\n"
	"    vec2 sampleUV = vUV + detail.yz * (0.010 * edgeMask);\n"
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
	"    float macroFade = clamp((7500.0 - vDistance) / 5000.0, 0.0, 1.0);\n"
	"    float distFade  = clamp(1.0 - (vDistance - 600.0) / 1800.0, 0.0, 1.0);\n"
	"    float closeZoom = clamp((380.0 - vDistance) / 260.0, 0.0, 1.0);\n"
	"    float ultraZoom = clamp((160.0 - vDistance) / 120.0, 0.0, 1.0);\n"
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
	"    vec3 lit = mix(dryCol * dryLighting, subCol * subLighting, submergedBlend);\n"
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
	"/* Height-model basin & upwind fetch classifier: distinguishes quiet lakes/sheltered coves from deep open sea */\n"
	"vec2 eval_water_fetch_and_basin(vec2 uv, vec2 windDir, vec2 shoreGrad, float fragWaterDepth, float effectiveScale) {\n"
	"    vec2 texSize = vec2(textureSize(uHeightTex, 0));\n"
	"    vec2 maxCoordF = max(vec2(1.0), texSize - 1.0);\n"
	"    ivec2 maxC = ivec2(maxCoordF);\n"
	"    vec2 offshoreDir = (length(shoreGrad) > 1e-4) ? (-normalize(shoreGrad)) : windDir;\n"
	"    float d_up1 = uSeaLevel - getH(ivec2(clamp(uv - windDir * 0.10, 0.0, 1.0) * maxCoordF), maxC) * effectiveScale;\n"
	"    float d_up2 = uSeaLevel - getH(ivec2(clamp(uv - windDir * 0.24, 0.0, 1.0) * maxCoordF), maxC) * effectiveScale;\n"
	"    float d_off1 = uSeaLevel - getH(ivec2(clamp(uv + offshoreDir * 0.16, 0.0, 1.0) * maxCoordF), maxC) * effectiveScale;\n"
	"    float d_off2 = uSeaLevel - getH(ivec2(clamp(uv + offshoreDir * 0.34, 0.0, 1.0) * maxCoordF), maxC) * effectiveScale;\n"
	"    float d_c0 = uSeaLevel - getH(ivec2(vec2(0.5, 0.5) * maxCoordF), maxC) * effectiveScale;\n"
	"    float d_c1 = uSeaLevel - getH(ivec2(vec2(0.2, 0.2) * maxCoordF), maxC) * effectiveScale;\n"
	"    float d_c2 = uSeaLevel - getH(ivec2(vec2(0.8, 0.2) * maxCoordF), maxC) * effectiveScale;\n"
	"    float d_c3 = uSeaLevel - getH(ivec2(vec2(0.2, 0.8) * maxCoordF), maxC) * effectiveScale;\n"
	"    float d_c4 = uSeaLevel - getH(ivec2(vec2(0.8, 0.8) * maxCoordF), maxC) * effectiveScale;\n"
	"    float maxD = max(fragWaterDepth, max(max(d_off1, d_off2), max(d_c0, max(max(d_c1, d_c2), max(d_c3, d_c4)))));\n"
	"    float meanD = (max(0.0, d_off1) + max(0.0, d_off2) + max(0.0, d_c0) + max(0.0, d_up1) + max(0.0, d_up2)) * 0.2;\n"
	"    float openSeaFactor = smoothstep(14.0, 28.0, maxD) * smoothstep(7.0, 18.0, meanD);\n"
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
	"    float fw = max(1e-4, max(length(dFdx(vWorldPos)), length(dFdy(vWorldPos))));\n"
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
	float ay = b->anchor_y;
	float wz_off = b->water_z_off;

	float tint[4] = {
		(float)((b->tint_rgba >> 24) & 0xFF) / 255.0f,
		(float)((b->tint_rgba >> 16) & 0xFF) / 255.0f,
		(float)((b->tint_rgba >> 8) & 0xFF) / 255.0f,
		(float)(b->tint_rgba & 0xFF) / 255.0f
	};

	struct { float dx, dy, u, v, loc_z; } c[4] = {
		{ -half_w, -ay,     b->u0, b->v1, b->z - wz_off },       /* BL (0) */
		{  half_w, -ay,     b->u1, b->v1, b->z - wz_off },       /* BR (1) */
		{ -half_w, h - ay,  b->u0, b->v0, b->z + h - wz_off },   /* TL (2) */
		{  half_w, h - ay,  b->u1, b->v0, b->z + h - wz_off }    /* TR (3) */
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
	GLuint substrate_tex;
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


