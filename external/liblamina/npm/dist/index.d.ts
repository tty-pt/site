export function getLaminaWasm(): any;
export function createLaminaRenderer(canvasEl: any): {
  isWebGL(): boolean;
  render(rgbaBytes: ArrayBufferView, width: number, height: number): void;
  destroy(): void;
};
export function createLaminaBridge(root?: any, getWasm?: () => any): any;

export function rgba(r: number, g: number, b: number, a?: number): number;
export function rgb(r: number, g: number, b: number): number;
export function colorR(c: number): number;
export function colorG(c: number): number;
export function colorB(c: number): number;
export function colorA(c: number): number;

export function attachCanvas(canvasEl: any): {
  isWebGL(): boolean;
  present(surface: LaminaSurface): void;
  destroy(): void;
};

export class LaminaSurface {
  width: number;
  height: number;
  wasmHandle: number;
  canvas?: any;
  constructor(width: number, height: number, options?: { canvas?: any });
  free(): void;
  clear(color?: number): this;
  fill(x: number, y: number, w: number, h: number, color: number): this;
  rect(x: number, y: number, w: number, h: number, color: number, thickness?: number): this;
  line(x0: number, y0: number, x1: number, y1: number, color: number, thickness?: number): this;
  paint(x: number, y: number, color: number): this;
  pick(x: number, y: number): number;
  getPixels(): Uint32Array;
  getClampedPixels(): Uint8ClampedArray;
  flush(targetCanvas?: any): this;
}

export class ViewportController {
  width: number;
  height: number;
  cameraX: number;
  cameraY: number;
  zoom: number;
  constructor(width?: number, height?: number);
  setCamera(x: number, y: number, zoom?: number): void;
  worldToScreen(worldX: number, worldY: number): { x: number; y: number };
  screenToWorld(screenX: number, screenY: number): { x: number; y: number };
}

export class FlexNode {
  wasmHandle: number;
  constructor(direction?: "row" | "column");
  setSize(w: number, h: number): this;
  setPadding(l: number, t: number, r: number, b: number): this;
  setFlex(grow?: number, shrink?: number): this;
  addChild(child: FlexNode): this;
  computeLayout(availableWidth: number, availableHeight: number, startX?: number, startY?: number): this;
  getBounds(): { x: number; y: number; width: number; height: number };
  free(): void;
}

export function version(): string;
