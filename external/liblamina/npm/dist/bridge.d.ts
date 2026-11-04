export function createLaminaBridge(root?: any, getWasm?: () => any): {
  mount(): void;
  makeLaminaImports(): {
    lam_canvas_flush(buf_ptr: number, width: number, height: number): void;
    lam_fill(): void;
    lam_tile_draw(): void;
    lam_flush(): void;
  };
  destroy(): void;
};
