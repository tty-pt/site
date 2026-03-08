import { defineConfig } from "$fresh/server.ts";

// This Fresh app runs on port 3000 and is proxied by NDC (port 8080)
// The proxy.c module forwards requests: Client → NDC:8080 → Fresh:3000
// User authentication and module info come via X-Remote-User and X-Modules headers
export default defineConfig({
  port: 3000,
});
