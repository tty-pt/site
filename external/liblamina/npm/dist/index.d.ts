export declare class ViewportController {
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

export declare function version(): string;
