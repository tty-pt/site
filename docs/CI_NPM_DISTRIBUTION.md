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

Now, with colocated `npm/` adapters across our pure-C libraries (`libstele`, `libbud`, `libcorm`, `libjoint`, `libislet`, `libhyle`, `libhyle-bud`, `liblemma`, `libsepal`, `liblamina`, `libxylem`, `libtransp`), we extend this distribution pipeline to **NPM**.

NPM serves a dual role:
1. **JavaScript / TypeScript Runtime**: Delivering native Node-API addons and precompiled WebAssembly reactors for Node.js, Next.js, and browser applications.
2. **Universal C Header & Library Distribution**: Allowing C and C++ developers to install libraries (`npm install @tty-pt/corm`) and compile against headers in `node_modules/@tty-pt/corm/include` with **zero system privileges, zero OS fragmentation, and deterministic SemVer version locking**.

---

## 2. CI Pipeline Integration (`~/ci`)

### 2.1 Centralized Workflow: `~/ci/.github/workflows/all-in-one.yml`

In `~/ci`, the reusable `all-in-one.yml` workflow automatically detects whether a repository houses an `npm/` or `package.json` directory and runs the dedicated `npm` job:

```yaml
  npm:
    if: (inputs.publish_npm || contains(format(',{0},', inputs.publish_to), ',npm,')) && startsWith(github.ref, 'refs/tags/')
    runs-on: ubuntu-latest
    needs: test
    steps:
      - name: Checkout repository
        uses: actions/checkout@v4

      - name: Set up Node.js
        uses: actions/setup-node@v4
        with:
          node-version: '22'
          registry-url: 'https://registry.npmjs.org'

      - name: Build, Test & Publish NPM package
        shell: bash
        env:
          NODE_AUTH_TOKEN: ${{ secrets.NPM_TOKEN }}
        run: |
          pkg_dir=""
          if [ -f "npm/package.json" ]; then
            pkg_dir="npm"
          elif [ -f "package.json" ]; then
            pkg_dir="."
          fi

          if [ -z "$pkg_dir" ]; then
            echo "ℹ️ No package.json found; skipping npm job."
            exit 0
          fi

          cd "$pkg_dir"
          TAG_VERSION="${GITHUB_REF_NAME#v}"
          if [ -n "$TAG_VERSION" ]; then
            npm version "$TAG_VERSION" --no-git-tag-version --allow-same-version
          fi

          npm install --ignore-scripts
          npm run build
          npm test
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
   - APT (.deb)        - wasm32       - @tty-pt/corm
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
  "name": "@tty-pt/corm",
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
When a C developer or Node addon author runs `npm install @tty-pt/corm`, their project gains:
```text
my-project/
├── node_modules/
│   └── @tty-pt/
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
CFLAGS += -I./node_modules/@tty-pt/corm/include -I./node_modules/@tty-pt/joint/include
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

- `@tty-pt/corm-linux-x64-gnu`
- `@tty-pt/corm-linux-arm64-gnu`
- `@tty-pt/corm-darwin-arm64` (Apple Silicon)
- `@tty-pt/corm-darwin-x64` (Intel macOS)
- `@tty-pt/corm-win32-x64-msvc`

The root `@tty-pt/corm` package lists these in `optionalDependencies`:
```json
{
  "name": "@tty-pt/corm",
  "optionalDependencies": {
    "@tty-pt/corm-linux-x64-gnu": "0.2.0",
    "@tty-pt/corm-darwin-arm64": "0.2.0"
  }
}
```

When npm installs `@tty-pt/corm` on Ubuntu x86_64, it downloads **only** `@tty-pt/corm-linux-x64-gnu`.

### 4.2 Infallible Runtime Tiering
```typescript
// dist/index.js
let nativeBinding = null;

// Tier 1: Try prebuilt platform package
try {
  nativeBinding = await import(`@tty-pt/corm-${process.platform}-${process.arch}`);
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

`external/mk/include.mk` automatically checks for `node_modules/@tty-pt/*/include` in the project tree and appends them to `CFLAGS`:
```makefile
# NPM C-Dependency Auto-Discovery
NPM_TTYPT_INCLUDES := $(wildcard node_modules/@tty-pt/*/include) \
                      $(wildcard ../node_modules/@tty-pt/*/include) \
                      $(wildcard ../../node_modules/@tty-pt/*/include)
ifneq ($(NPM_TTYPT_INCLUDES),)
CFLAGS += $(foreach inc,$(NPM_TTYPT_INCLUDES),-I$(inc))
endif
```
This guarantees that any C project in the monorepo or downstream immediately resolves headers installed via npm.
