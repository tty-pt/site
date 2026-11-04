const VS_QUAD = `attribute vec2 a_pos;
attribute vec2 a_uv;
varying vec2 v_uv;
void main() {
	v_uv = a_uv;
	gl_Position = vec4(a_pos, 0.0, 1.0);
}`;

const FS_QUAD = `precision mediump float;
varying vec2 v_uv;
uniform sampler2D u_tex;
void main() {
	gl_FragColor = texture2D(u_tex, v_uv);
}`;

function initGlPipeline(gl) {
  try {
    const vs = gl.createShader(gl.VERTEX_SHADER);
    gl.shaderSource(vs, VS_QUAD);
    gl.compileShader(vs);

    const fs = gl.createShader(gl.FRAGMENT_SHADER);
    gl.shaderSource(fs, FS_QUAD);
    gl.compileShader(fs);

    const prog = gl.createProgram();
    gl.attachShader(prog, vs);
    gl.attachShader(prog, fs);
    gl.linkProgram(prog);

    if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) {
      return null;
    }

    gl.useProgram(prog);

    const verts = new Float32Array([
      -1.0, -1.0, 0.0, 1.0,
       1.0, -1.0, 1.0, 1.0,
      -1.0,  1.0, 0.0, 0.0,
      -1.0,  1.0, 0.0, 0.0,
       1.0, -1.0, 1.0, 1.0,
       1.0,  1.0, 1.0, 0.0
    ]);

    const vbo = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
    gl.bufferData(gl.ARRAY_BUFFER, verts, gl.STATIC_DRAW);

    const aPos = gl.getAttribLocation(prog, "a_pos");
    const aUv = gl.getAttribLocation(prog, "a_uv");
    gl.enableVertexAttribArray(aPos);
    gl.vertexAttribPointer(aPos, 2, gl.FLOAT, false, 16, 0);
    gl.enableVertexAttribArray(aUv);
    gl.vertexAttribPointer(aUv, 2, gl.FLOAT, false, 16, 8);

    const tex = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, tex);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);

    return { prog, vbo, tex };
  } catch (e) {
    return null;
  }
}

export function createLaminaRenderer(canvasEl) {
  let gl = null;
  let glPipe = null;
  let canvas2d = null;

  if (canvasEl && typeof canvasEl.getContext === "function") {
    try {
      const opts = { preserveDrawingBuffer: true, alpha: true, antialias: true };
      gl = canvasEl.getContext("webgl2", opts) || canvasEl.getContext("webgl", opts);
      if (gl) {
        glPipe = initGlPipeline(gl);
      }
    } catch (e) {
      gl = null;
      glPipe = null;
    }

    if (!gl || !glPipe) {
      gl = null;
      glPipe = null;
      try {
        canvas2d = canvasEl.getContext("2d");
      } catch (e) {
        canvas2d = null;
      }
    }
  }

  return {
    isWebGL() {
      return Boolean(gl && glPipe);
    },

    render(rgbaBytes, width, height) {
      if (!canvasEl) return;

      if (gl && glPipe) {
        gl.viewport(0, 0, canvasEl.width || width, canvasEl.height || height);
        gl.useProgram(glPipe.prog);
        gl.bindTexture(gl.TEXTURE_2D, glPipe.tex);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, width, height, 0, gl.RGBA, gl.UNSIGNED_BYTE, rgbaBytes);
        gl.drawArrays(gl.TRIANGLES, 0, 6);
        return;
      }

      if (canvas2d) {
        const clamped = rgbaBytes instanceof Uint8ClampedArray
          ? rgbaBytes
          : new Uint8ClampedArray(rgbaBytes.buffer, rgbaBytes.byteOffset, rgbaBytes.byteLength);
        const imgData = typeof ImageData !== "undefined"
          ? new ImageData(clamped, width, height)
          : { data: clamped, width, height };
        canvas2d.putImageData(imgData, 0, 0);
      }
    },

    destroy() {
      if (gl && glPipe) {
        try {
          gl.deleteTexture(glPipe.tex);
          gl.deleteBuffer(glPipe.vbo);
          gl.deleteProgram(glPipe.prog);
        } catch (e) {}
      }
      gl = null;
      glPipe = null;
      canvas2d = null;
    }
  };
}

function getExport(exports, names) {
  if (!exports) return null;
  for (const name of names) {
    if (typeof exports[name] === "function") return exports[name];
  }
  return null;
}

export function createLaminaBridge(root, getWasm) {
  const renderers = new WeakMap();
  const kicked = new WeakSet();

  function allCanvases() {
    if (!root) {
      if (typeof document !== "undefined") {
        return Array.from(document.querySelectorAll("[data-bud-canvas]"));
      }
      return [];
    }
    const list = root.querySelectorAll ? root.querySelectorAll("[data-bud-canvas]") : [];
    if (list && list.length) return Array.from(list);
    return root.tagName === "CANVAS" ? [root] : [];
  }

  function pickCanvas(width, height) {
    const list = allCanvases();
    let first = null, fit = null;
    for (const c of list) {
      if (!first) first = c;
      if (c.width === width && c.height === height) return c;
      if (!fit && c.hasAttribute && c.hasAttribute("data-bud-canvas-fit")) fit = c;
    }
    if (fit) {
      fit.width = width;
      fit.height = height;
      return fit;
    }
    return first;
  }

  function getRenderer(canvasEl) {
    let r = renderers.get(canvasEl);
    if (!r) {
      r = createLaminaRenderer(canvasEl);
      renderers.set(canvasEl, r);
    }
    return r;
  }

  function kickRender() {
    const fn = getExport(getWasm(), ["wasm_canvas_render"]);
    if (fn && typeof requestAnimationFrame === "function") {
      requestAnimationFrame(() => fn());
    }
  }

  return {
    mount() {
      for (const c of allCanvases()) {
        getRenderer(c);
        if (!kicked.has(c)) {
          kicked.add(c);
          kickRender();
        }
      }
    },

    makeLaminaImports() {
      return {
        lam_canvas_flush(buf_ptr, width, height) {
          const canvasEl = pickCanvas(width, height);
          if (!canvasEl) return;
          const renderer = getRenderer(canvasEl);
          if (!kicked.has(canvasEl)) {
            kicked.add(canvasEl);
            kickRender();
          }
          const exports = getWasm();
          if (!exports || !exports.memory) return;
          const uptr = buf_ptr >>> 0;
          const byteLen = width * height * 4;
          if (uptr + byteLen > exports.memory.buffer.byteLength) return;

          const bytes = new Uint8Array(exports.memory.buffer, uptr, byteLen);
          renderer.render(bytes, width, height);
        },
        lam_fill() {},
        lam_tile_draw() {},
        lam_flush() {}
      };
    },

    destroy() {
      // Cleaned automatically via WeakMap
    }
  };
}

if (typeof window !== "undefined") {
  window.__wasm_capability_registry = window.__wasm_capability_registry || {};
  window.__wasm_capability_registry.lamina = (root, getWasm) => {
    const b = createLaminaBridge(root, getWasm);
    return {
      mount() { b.mount(); },
      destroy() { b.destroy(); },
      makeImports() { return b.makeLaminaImports(); }
    };
  };
}
