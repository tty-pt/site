export interface Octave {
  x: number;
  w: number;
}

export function getNoiseWasm(): any;

export function noisePoint1D(x: number, seed: number, shift: number): number;
export function noisePoint2D(x: number, y: number, seed: number, shift: number): number;
export function noisePoint3D(x: number, y: number, z: number, seed: number, shift: number): number;
export function noisePoint4D(x: number, y: number, z: number, w: number, seed: number, shift: number): number;
export function noisePointN(coords: number[], seed: number, shift: number): number;
export function noiseOctaves(
  dim: number,
  origin: number[],
  octaves: Octave[],
  seed: number,
  cy: number
): Uint32Array;
