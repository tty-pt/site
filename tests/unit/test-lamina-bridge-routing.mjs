// lam_canvas_flush must paint the canvas whose size matches the buffer, so a
// page with two [data-bud-canvas] elements (stage + mini-map) keeps both.
import assert from 'node:assert/strict';

const calls = [];
function makeCanvas(id, w, h, fit = false) {
	const ctx = { putImageData(img) { calls.push({ id, w: img.width, h: img.height }); } };
	return {
		id, width: w, height: h,
		hasAttribute(name) { return fit && name === 'data-bud-canvas-fit'; },
		getContext(kind) { return kind === '2d' ? ctx : null; }
	};
}
const stage = makeCanvas('stage', 640, 480);
const mini = makeCanvas('mini', 224, 224);
const canvases = [stage, mini];

globalThis.document = { querySelectorAll: () => canvases, querySelector: () => canvases[0] };
globalThis.requestAnimationFrame = () => {};
globalThis.ImageData = class { constructor(d, w, h) { this.data = d; this.width = w; this.height = h; } };

const { createLaminaBridge, SHADERS } = await import('../../external/liblamina/htdocs/lamina-bridge.js');
const memory = { buffer: new ArrayBuffer(1920 * 1080 * 4 + 16) };
const bridge = createLaminaBridge(null, () => ({ memory }));
bridge.mount();
const { lam_canvas_flush } = bridge.makeLaminaImports();

lam_canvas_flush(0, 640, 480);
lam_canvas_flush(0, 224, 224);
lam_canvas_flush(0, 640, 480);
assert.deepEqual(calls.map((c) => c.id), ['stage', 'mini', 'stage']);

// A buffer matching no canvas falls back to the first one.
lam_canvas_flush(0, 100, 100);
assert.equal(calls.at(-1).id, 'stage');

console.log('PASS: lamina bridge routes each flush to the canvas of matching size');

// A canvas marked data-bud-canvas-fit follows the frame size: the full-screen
// stage is resized to the flushed w x h, the fixed-size mini-map keeps its own.
calls.length = 0;
const fitStage = makeCanvas('fit-stage', 640, 480, true);
const fitMini = makeCanvas('fit-mini', 224, 224);
canvases.splice(0, canvases.length, fitStage, fitMini);
const bridge2 = createLaminaBridge(null, () => ({ memory }));
bridge2.mount();
const flush2 = bridge2.makeLaminaImports().lam_canvas_flush;
flush2(0, 1456, 836);
assert.equal(calls.at(-1).id, 'fit-stage');
assert.deepEqual([fitStage.width, fitStage.height], [1456, 836]);
assert.deepEqual([calls.at(-1).w, calls.at(-1).h], [1456, 836]);
flush2(0, 224, 224);
assert.equal(calls.at(-1).id, 'fit-mini');
flush2(0, 1456, 836);
assert.equal(calls.at(-1).id, 'fit-stage');           // exact match now, no resize
flush2(0, 1920, 1080);
assert.deepEqual([fitStage.width, fitStage.height], [1920, 1080]);
assert.deepEqual([fitMini.width, fitMini.height], [224, 224]);

console.log('PASS: a fit canvas is resized to the flushed frame; fixed canvases are untouched');

// WebGL2 water and terrain shader contract verification
assert(SHADERS && SHADERS.FS_WATER_3D, 'FS_WATER_3D must be defined');
assert(SHADERS.VS_WATER_3D, 'VS_WATER_3D must be defined');
assert(SHADERS.FS_TERRAIN_3D, 'FS_TERRAIN_3D must be defined');
assert(SHADERS.VS_TERRAIN_3D, 'VS_TERRAIN_3D must be defined');

// 1. Canonical chaff_glsl procedural noise routines must be present in FS_WATER_3D and FS_TERRAIN_3D
assert(SHADERS.FS_WATER_3D.includes('uint chaff_rotl32('), 'FS_WATER_3D must implement chaff_rotl32');
assert(SHADERS.FS_WATER_3D.includes('uint chaff_hash2('), 'FS_WATER_3D must implement chaff_hash2');
assert(SHADERS.FS_WATER_3D.includes('uint chaff_hash3('), 'FS_WATER_3D must implement chaff_hash3');
assert(SHADERS.FS_WATER_3D.includes('float chaff_sample_octave('), 'FS_WATER_3D must implement chaff_sample_octave');
assert(SHADERS.FS_WATER_3D.includes('float chaff_sample_3d('), 'FS_WATER_3D must implement chaff_sample_3d');
assert(SHADERS.FS_WATER_3D.includes('vec4 chaff_sample_grad_3d('), 'FS_WATER_3D must implement chaff_sample_grad_3d');
assert(SHADERS.FS_WATER_3D.includes('vec3 chaff_sample_grad_2d('), 'FS_WATER_3D must implement chaff_sample_grad_2d');

assert(SHADERS.FS_TERRAIN_3D.includes('float chaff_sample_3d('), 'FS_TERRAIN_3D must implement chaff_sample_3d');

// 2. Uniform and varying contracts
assert(SHADERS.FS_WATER_3D.includes('uniform float uAnimTime;'), 'FS_WATER_3D must declare uAnimTime uniform');
assert(SHADERS.FS_WATER_3D.includes('in vec2 vWorldPos;'), 'FS_WATER_3D must receive vWorldPos');
assert(SHADERS.VS_WATER_3D.includes('out vec2 vWorldPos;'), 'VS_WATER_3D must export vWorldPos');

assert(SHADERS.FS_TERRAIN_3D.includes('uniform float uAnimTime;'), 'FS_TERRAIN_3D must declare uAnimTime uniform');
assert(SHADERS.FS_TERRAIN_3D.includes('in vec2 vWorldPos;'), 'FS_TERRAIN_3D must receive vWorldPos');
assert(SHADERS.VS_TERRAIN_3D.includes('out vec2 vWorldPos;'), 'VS_TERRAIN_3D must export vWorldPos');
assert(SHADERS.VS_TERRAIN_3D.includes('out float vWaterDepth;'), 'VS_TERRAIN_3D must export vWaterDepth');

// 3. Verification of 3D seabed caustics and volumetric optical extinction (R1, R3, R4)
assert(SHADERS.FS_TERRAIN_3D.includes('caustic'), 'FS_TERRAIN_3D must cast caustics on the seabed');
assert(SHADERS.FS_TERRAIN_3D.includes('pow('), 'FS_TERRAIN_3D must calculate sharpened caustic highlights on seabed');
assert(SHADERS.FS_TERRAIN_3D.includes('causticCoherence'), 'FS_TERRAIN_3D must diffuse caustics with water depth');
assert(SHADERS.FS_TERRAIN_3D.includes('opticalPath'), 'FS_TERRAIN_3D must calculate view-dependent optical ray path length');
assert(SHADERS.FS_TERRAIN_3D.includes('seabedAbsorption'), 'FS_TERRAIN_3D must calculate Beer-Lambert seabed absorption');
assert(SHADERS.FS_TERRAIN_3D.includes('deepOceanScattered'), 'FS_TERRAIN_3D must accumulate volumetric deep ocean in-scattering');

// 4. Verification of 3D water surface wave hierarchy and surface optics (R2, R3, R4)
assert(SHADERS.FS_WATER_3D.includes('chaff_sample_3d('), 'FS_WATER_3D must evaluate 3D procedural water noise');
assert(SHADERS.FS_WATER_3D.includes('chaff_rotate_golden('), 'FS_WATER_3D must implement golden ratio rotation');
assert(SHADERS.FS_WATER_3D.includes('wMacro'), 'FS_WATER_3D must synthesize macro ocean swells');
assert(SHADERS.FS_WATER_3D.includes('wMid'), 'FS_WATER_3D must synthesize mid-frequency wind chop');
assert(SHADERS.FS_WATER_3D.includes('wCap'), 'FS_WATER_3D must synthesize capillary surface wavelets');
assert(SHADERS.FS_WATER_3D.includes('nyqCap'), 'FS_WATER_3D must apply Nyquist distance filtering to capillary ripples');
assert(SHADERS.FS_WATER_3D.includes('nyqMid'), 'FS_WATER_3D must apply Nyquist distance filtering to mid-frequency wind chop');
assert(SHADERS.FS_WATER_3D.includes('slantPath'), 'FS_WATER_3D must evaluate slant optical path length L');
assert(SHADERS.FS_WATER_3D.includes('waveGrad'), 'FS_WATER_3D must calculate physical wave gradient normals');
assert(SHADERS.FS_WATER_3D.includes('fresnel'), 'FS_WATER_3D must calculate Fresnel sky reflection');
assert(SHADERS.FS_WATER_3D.includes('specSun'), 'FS_WATER_3D must calculate specular sun highlights');
assert(SHADERS.FS_WATER_3D.includes('diffuseSun'), 'FS_WATER_3D must calculate diffuse sun lighting on wave facets');
assert(SHADERS.FS_WATER_3D.includes('foam'), 'FS_WATER_3D must synthesize shoreline foam');
assert(!SHADERS.FS_WATER_3D.includes('hC'), 'FS_WATER_3D must not reference undeclared identifier hC');
assert(!SHADERS.FS_WATER_3D.includes('seabedAbsorption'), 'FS_WATER_3D must never tint the water surface with terrain depth in 3D');

// 5. Verification of 3D billboard water submersion and optics (SQ1 / R1)
assert(SHADERS.VS_BILLBOARD_3D.includes('out vec3 vWorldPos;'), 'VS_BILLBOARD_3D must export vWorldPos');
assert(SHADERS.FS_BILLBOARD_3D.includes('uniform float uSeaLevel;'), 'FS_BILLBOARD_3D must declare uSeaLevel uniform');
assert(SHADERS.FS_BILLBOARD_3D.includes('uniform float uAnimTime;'), 'FS_BILLBOARD_3D must declare uAnimTime uniform');
assert(SHADERS.FS_BILLBOARD_3D.includes('uniform vec3 uCameraPos;'), 'FS_BILLBOARD_3D must declare uCameraPos uniform');
assert(SHADERS.FS_BILLBOARD_3D.includes('waterAbsorption'), 'FS_BILLBOARD_3D must implement Beer-Lambert absorption for submerged fragments');
assert(SHADERS.FS_BILLBOARD_3D.includes('deepOceanScattered'), 'FS_BILLBOARD_3D must implement deep ocean in-scattering');
assert(SHADERS.FS_BILLBOARD_3D.includes('submergedCaustic'), 'FS_BILLBOARD_3D must cast caustics on submerged billboard surfaces');
assert(SHADERS.FS_BILLBOARD_3D.includes('meniscus'), 'FS_BILLBOARD_3D must implement waterline meniscus highlight');
assert(SHADERS.FS_BILLBOARD_3D.includes('shallowWash'), 'FS_BILLBOARD_3D must implement Caribbean turquoise baseline wash');
assert(SHADERS.FS_BILLBOARD_3D.includes('vec3(0.25, 0.85, 0.92)'), 'FS_BILLBOARD_3D must match C_SHALLOW turquoise palette');
assert(SHADERS.FS_BILLBOARD_3D.includes('waveOffset'), 'FS_BILLBOARD_3D must implement subsurface wave refraction displacement');
assert(SHADERS.FS_BILLBOARD_3D.includes('waveElevation'), 'FS_BILLBOARD_3D must undulate waterline with surface wave chop');
assert(SHADERS.FS_BILLBOARD_3D.includes('smoothstep(1.6, 0.0, d)'), 'FS_BILLBOARD_3D must calibrate 1.6-unit meniscus highlight');

// 6. Verification of 3D submerged terrain seabed wave refraction
assert(SHADERS.FS_TERRAIN_3D.includes('uniform float uSeaLevel;'), 'FS_TERRAIN_3D must declare uSeaLevel uniform');
assert(SHADERS.FS_TERRAIN_3D.includes('refractOffset'), 'FS_TERRAIN_3D must calculate wave refraction displacement for submerged seabed');
assert(SHADERS.FS_TERRAIN_3D.includes('depthFactor'), 'FS_TERRAIN_3D must scale seabed refraction with water depth');

console.log('PASS: WebGL2 water, terrain, and billboard 3D shaders integrate canonical chaff_glsl procedural noise routines and decoupled physical optics');
