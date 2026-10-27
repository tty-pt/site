# Local LLM Inference & Pi Agent Integration (axil-auren)

`axil-auren` (`libauren`) provides ultra-fast in-process LLM inference, OpenAI-compatible streaming chat, and dense vector embeddings powered by `llama.cpp` and Vulkan/CPU, served as a native axil module on port `4242`.

---

## 1. Quickstart: Starting the Server

The turnkey launcher script starts `axil-auren` with automatic library paths and configuration discovery:

```bash
# Launch server on default port 4242:
/home/quirinpa/axil-auren/scripts/auren-server

# Or customize port:
AUREN_PORT=4242 /home/quirinpa/axil-auren/scripts/auren-server
```

Or run directly via `axil`:
```bash
LD_LIBRARY_PATH=/home/quirinpa/axil-auren/lib \
axil -A -d -p 4242 -m /home/quirinpa/axil-auren/lib/libaxil-auren
```

### Models & Sizing
Models are stored in `~/.models/`:
- **Generative Chat & System One**: `~/.models/qwen2.5-0.5b-instruct.gguf` (398 MB, Q4_K_M)
- **Dense Vector Embeddings (Fast & Lightweight)**: `~/.models/bge-small-en-v1.5.gguf` (37 MB, Q8_0, 384-dimensional)
- **Dense Vector Embeddings (More Accurate)**: `~/.models/bge-base-en-v1.5.gguf` (118 MB, Q8_0, 768-dimensional)

---

## 2. Configuration (`~/auren.json`)

The active multi-model configuration lives in `/home/quirinpa/auren.json` (and `~/.auren.json`):

```json
{
  "models": [
    {
      "id": "qwen",
      "path": "~/.models/qwen2.5-0.5b-instruct.gguf",
      "capabilities": ["chat", "judge"],
      "n_ctx": 131072,
      "n_gpu_layers": 0
    },
    {
      "id": "bge-small",
      "path": "~/.models/bge-small-en-v1.5.gguf",
      "capabilities": ["embed"],
      "n_ctx": 512,
      "n_gpu_layers": 0
    },
    {
      "id": "bge-base",
      "path": "~/.models/bge-base-en-v1.5.gguf",
      "capabilities": ["embed"],
      "n_ctx": 512,
      "n_gpu_layers": 0
    }
  ],
  "defaults": {
    "chat": "qwen",
    "judge": "qwen",
    "embed": "bge-small"
  },
  "max_resident": 3
}
```

- **`n_ctx: 131072`**: Extended context window utilizing $Q4\_0$ KV cache quantization.
- **CPU Batch Matrix Multiplication**: Uses all online CPU cores (`sysconf(_SC_NPROCESSORS_ONLN) = 22 threads`) during prompt ingestion.

---

## 3. Pi Coding Agent Integration

The Pi coding agent connects directly to `axil-auren` via its OpenAI-compatible endpoint.

### A. Provider Configuration (`~/.pi/agent/models.json`)
The `auren` provider definition:

```json
{
  "providers": {
    "auren": {
      "name": "axil-auren",
      "baseUrl": "http://localhost:4242/v1",
      "api": "openai-completions",
      "apiKey": "local",
      "models": [
        {
          "id": "qwen",
          "name": "Qwen 2.5 0.5B Instruct",
          "contextWindow": 131072,
          "maxTokens": 32768,
          "cost": {
            "input": 0,
            "output": 0,
            "cacheRead": 0,
            "cacheWrite": 0
          },
          "compat": {
            "supportsDeveloperRole": false,
            "supportsUsageInStreaming": false,
            "maxTokensField": "max_tokens"
          }
        }
      ]
    }
  }
}
```

### B. Session Continuity Extension (`~/.pi/agent/extensions/auren-prefix-cache.ts`)
The extension hooks into Pi's provider lifecycle to bind each interaction to the active workspace session ID:

```ts
export default function aurenPrefixCache(pi: any) {
  function getActiveSessionId(ctx: any): string {
    return ctx?.sessionManager?.getSessionId?.() || "default-session";
  }

  pi.on("before_provider_headers", (event: any, ctx: any) => {
    if (ctx?.model?.provider === "auren") {
      event.headers["x-session-id"] = getActiveSessionId(ctx);
    }
  });

  pi.on("before_provider_request", (event: any, ctx: any) => {
    if (ctx?.model?.provider === "auren") {
      if (event?.payload && typeof event.payload === "object") {
        event.payload.session_id = getActiveSessionId(ctx);
      }
    }
  });
}
```

### C. Running Pi with Local Model
```bash
# Interactive coding session:
pi --model auren/qwen

# Single prompt:
pi --model auren/qwen -p "Say 'hello from axil-auren!' and exit."
```

---

## 4. How Session Continuity & KV Prefix Caching Works

In agent coding workflows, system prompts, tool schemas, and workspace rules typically exceed 10,000–15,000 tokens. Evaluating this from scratch on every turn creates substantial delay on CPU.

`axil-auren` solves this via **Two-Sided KV Prefix Persistence**:
1. When Pi sends a request, the `x-session-id` header (or JSON `session_id`) identifies the session.
2. `axil-auren` allocates or retrieves an LRU session slot in `g_engine.sessions` owning a persistent context.
3. The conversation template is tokenized in full and compared against `cached_tokens`.
4. Downstream divergent cells are evicted via `llama_memory_seq_rm()`, and only the new turn suffix is evaluated starting explicitly at position $K$.
5. **Speedup**: Ingesting a 3,500+ token context drops from **30.5 seconds on Turn 1** to **2.0 seconds on Turn 2** (**15x+ speedup**), decoding only ~17 delta tokens.

---

## 5. Testing & Verification Commands

```bash
# 1. Run native engine test suite (standalone driver, no HTTP):
LD_LIBRARY_PATH=/home/quirinpa/axil-auren/lib \
/home/quirinpa/axil-auren/bin/engine-test ~/.models/qwen2.5-0.5b-instruct.gguf

# 2. Curl multi-turn chat test with session header:
curl -i -s http://localhost:4242/v1/chat/completions \
  -H "Content-Type: application/json" \
  -H "x-session-id: test-session-1" \
  -d '{"model":"qwen","messages":[{"role":"user","content":"Say: Hello!"}],"stream":false}'

# 3. Model listing:
curl -s http://localhost:4242/v1/models
```
