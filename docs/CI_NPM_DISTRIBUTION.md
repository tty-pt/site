# CI Automation & Universal C Distribution via NPM

## 1. Overview & Vision

The `tty.pt` continuous integration system (`~/ci`) builds, signs, and publishes native packages across operating systems:
- **Debian / Ubuntu** (`.deb` via APT)
- **Alpine Linux** (`.apk` via APK repository)
- **Fedora / RHEL / CentOS** (`.rpm` via YUM/DNF)
- **Arch Linux** (`.pkg.tar.zst` via PACMAN & AUR)
- **macOS** (Homebrew formulas via `tty-pt/homebrew-tap`)
- **OpenBSD** (ports & tarballs)
- **Windows** (MSYS2 / winget)

Now, with colocated `npm/` adapters across our pure-C libraries (`libcorm`, `libjoint`, `libislet`, `libhyle`, `liblemma`, `libsepal`, `libtransp`), we extend this distribution pipeline to **NPM**.

NPM serves a dual role:
1. **JavaScript / TypeScript Runtime**: Delivering native Node-API addons and precompiled WebAssembly reactors for Node.js, Next.js, and browser applications.
2. **Universal C Header & Library Distribution**: Allowing C and C++ developers to install libraries (`npm install @ttypt/corm`) and compile against headers in `node_modules/@ttypt/corm/include` with **zero system privileges, zero OS fragmentation, and deterministic SemVer version locking**.

---

## 2. CI Pipeline Integration (`~/ci`)

### 2.1 Reusable Action: `~/ci/package/npm-action.yml`

In `~/ci`, a composite GitHub Action automatically detects whether a repository houses an `npm/` directory and executes the packaging pipeline:

```yaml
name: 'package-npm'
description: 'Build native addons, compile WASM reactors, and publish npm packages'
inputs:
  npm_token:
    description: 'NPM registry authentication token'
    required: true
  path:
    description: 'Subdirectory containing package.json'
    default: 'npm'

runs:
  using: 'composite'
  steps:
    - name: Setup Node.js
      uses: actions/setup-node@v4
      with:
        node-version: 22
        registry-url: 'https://registry.npmjs.org'

    - name: Build Native Addon & WASM Reactor
      shell: bash
      run: |
        cd ${{ inputs.path }}
        npm run build

    - name: Run Test Suite
      shell: bash
      run: |
        cd ${{ inputs.path }}
        npm test

    - name: Publish to NPM
      if: startsWith(github.ref, 'refs/tags/v')
      shell: bash
      env:
        NODE_AUTH_TOKEN: ${{ inputs.npm_token }}
      run: |
        cd ${{ inputs.path }}
        npm publish --access public
```

### 2.2 Synchronized Multi-Format Release Matrix

When a version tag (`v0.2.0`) is pushed to a repository (e.g. `github.com/tty-pt/libcorm`), GitHub Actions launches a matrix release:

```text
               ┌───────────────────┐
               │  Release Tag v*   │
               └─────────┬─────────┘
                         │
        ┌────────────────┼────────────────┐
        ▼                ▼                ▼
 [OS Native Matrix] [WASM Matrix]  [NPM Package Matrix]
   - APT (.deb)        - wasm32       - @ttypt/corm
   - RPM (.rpm)                       - Headers (include/)
   - APK (.apk)                       - Native Addon (.node)
   - Brew (macOS)                     - Reactor (.wasm)
   - Pacman (Arch)
```

---

## 3. Distributing C Headers & Sources via NPM

### 3.1 The NPM C-Package Standard
In every colocated `npm/package.json`, the `"files"` field declares both runtime artifacts and C development assets:

```json
{
  "name": "@ttypt/corm",
  "version": "0.2.0",
  "main": "dist/index.js",
  "types": "dist/index.d.ts",
  "files": [
    "dist/",
    "include/",
    "corm.pc",
    "README.md",
    "LICENSE"
  ]
}
```

### 3.2 Directory Layout After `npm install`
When a C developer or Node addon author runs `npm install @ttypt/corm`, their project gains:
```text
my-project/
├── node_modules/
│   └── @ttypt/
│       ├── corm/
│       │   ├── include/
│       │   │   └── ttypt/
│       │   │       ├── corm.h      <-- Canonical C API
│       │   │       └── cormr.h     <-- Recall Kernel API
│       │   ├── dist/
│       │   │   ├── index.js        <-- TypeScript/JS entry point
│       │   │   └── corm.wasm       <-- Pre-compiled WebAssembly reactor
│       │   └── corm.pc             <-- Pkg-config descriptor
│       ├── joint/
│       │   └── include/ttypt/joint.h
│       └── mk/
│           └── include.mk          <-- Turnkey build system
```

### 3.3 Consuming in Makefiles Without System Installation
A C developer writes their Makefile:
```makefile
# Include tty-pt build helper from node_modules
-include node_modules/@tty-pt/mk/include.mk

# Headers are automatically resolved by include.mk
CFLAGS += -I./node_modules/@ttypt/corm/include -I./node_modules/@ttypt/joint/include
LDLIBS += -lcorm -ljoint
```

**Benefits**:
1. **Zero Sudo / Root Access**: Works cleanly in unprivileged Docker containers, restricted enterprise workstations, and serverless environments.
2. **Deterministic Version Pinning**: Versioning is locked via `package-lock.json` or `pnpm-lock.yaml`, preventing system library version mismatch bugs across development machines.
3. **Multi-Language Interop**: A single `npm install` provides both the C development headers and the TypeScript types.

---

## 4. Multi-OS Prebuilt Binary Strategy (Zero-Compiler Node Runtime)

### 4.1 The Platform-Specific Optional Dependencies Model
To eliminate native compilation during `npm install` on end-user machines while maximizing raw C performance, the CI pipeline builds precompiled platform packages using the standard industry pattern (pioneered by `esbuild`, `@swc`, and `sharp`):

- `@ttypt/corm-linux-x64-gnu`
- `@ttypt/corm-linux-arm64-gnu`
- `@ttypt/corm-darwin-arm64` (Apple Silicon)
- `@ttypt/corm-darwin-x64` (Intel macOS)
- `@ttypt/corm-win32-x64-msvc`

The root `@ttypt/corm` package lists these in `optionalDependencies`:
```json
{
  "name": "@ttypt/corm",
  "optionalDependencies": {
    "@ttypt/corm-linux-x64-gnu": "0.2.0",
    "@ttypt/corm-darwin-arm64": "0.2.0"
  }
}
```

When npm installs `@ttypt/corm` on Ubuntu x86_64, it downloads **only** `@ttypt/corm-linux-x64-gnu`.

### 4.2 Infallible Runtime Tiering
```typescript
// dist/index.js
let nativeBinding = null;

// Tier 1: Try prebuilt platform package
try {
  nativeBinding = await import(`@ttypt/corm-${process.platform}-${process.arch}`);
} catch {}

// Tier 2: Try locally compiled build/Release/corm_node.node
if (!nativeBinding) {
  try {
    nativeBinding = require("../build/Release/corm_node.node");
  } catch {}
}

// Tier 3: Infallible WebAssembly Reactor (Browsers, FreeBSD, Edge)
if (!nativeBinding) {
  backend = new WasmBackend("dist/corm.wasm");
}
```

---

## 5. Build System `include.mk` Auto-Discovery

`external/mk/include.mk` automatically checks for `node_modules/@ttypt/*/include` in the project tree and appends them to `CFLAGS`:
```makefile
# NPM C-Dependency Auto-Discovery
NPM_TTYPT_INCLUDES := $(wildcard node_modules/@ttypt/*/include) \
                      $(wildcard ../node_modules/@ttypt/*/include) \
                      $(wildcard ../../node_modules/@ttypt/*/include)
ifneq ($(NPM_TTYPT_INCLUDES),)
CFLAGS += $(foreach inc,$(NPM_TTYPT_INCLUDES),-I$(inc))
endif
```
This guarantees that any C project in the monorepo or downstream immediately resolves headers installed via npm.
