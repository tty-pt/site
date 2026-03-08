/**
 * Watch and bundle client-side TypeScript files
 * 
 * Monitors mods/*/client.ts files for changes and automatically bundles them
 * to htdocs/js/*.js for browser consumption.
 * 
 * Features:
 * - Auto-discovers modules with client.ts files
 * - Watches for file changes using Deno.watchFs
 * - Debounces rapid changes to prevent redundant builds
 * - Handles errors gracefully without crashing
 * - Parallel bundling for multiple modules
 * 
 * Usage:
 *   deno run --allow-read --allow-write --allow-run scripts/watch-bundle.ts
 */

import { join } from "@std/path";

interface ModuleInfo {
  name: string;
  sourcePath: string;
  outputPath: string;
}

const DEBOUNCE_DELAY = 200; // ms
const bundleTimers = new Map<string, number>();

/**
 * Discover all modules with client.ts files
 */
async function discoverModules(): Promise<ModuleInfo[]> {
  const modules: ModuleInfo[] = [];
  const modsDir = join(Deno.cwd(), "mods");
  
  try {
    for await (const entry of Deno.readDir(modsDir)) {
      if (entry.isDirectory) {
        const clientPath = join(modsDir, entry.name, "client.ts");
        try {
          await Deno.stat(clientPath);
          modules.push({
            name: entry.name,
            sourcePath: clientPath,
            outputPath: join(Deno.cwd(), "htdocs", "js", `${entry.name}.js`)
          });
        } catch {
          // No client.ts in this module, skip
        }
      }
    }
  } catch (err) {
    console.error("Error discovering modules:", err);
  }
  
  return modules;
}

/**
 * Bundle a single module's client.ts file
 */
async function bundleModule(module: ModuleInfo): Promise<boolean> {
  const startTime = Date.now();
  
  try {
    console.log(`📦 Bundling ${module.name}...`);
    
    const command = new Deno.Command("deno", {
      args: [
        "bundle",
        module.sourcePath,
        module.outputPath
      ],
      stdout: "piped",
      stderr: "piped"
    });
    
    const { success, stderr } = await command.output();
    
    if (success) {
      const elapsed = Date.now() - startTime;
      const stat = await Deno.stat(module.outputPath);
      const sizeKB = (stat.size / 1024).toFixed(1);
      console.log(`✓ Bundled ${module.name}.js (${sizeKB} KB) in ${elapsed}ms`);
      return true;
    } else {
      const errorText = new TextDecoder().decode(stderr);
      console.error(`✗ Bundle failed for ${module.name}:`);
      console.error(errorText);
      return false;
    }
  } catch (err) {
    console.error(`✗ Error bundling ${module.name}:`, err.message);
    return false;
  }
}

/**
 * Debounced bundle function to prevent redundant builds
 */
function debouncedBundle(module: ModuleInfo): void {
  const existingTimer = bundleTimers.get(module.name);
  if (existingTimer !== undefined) {
    clearTimeout(existingTimer);
  }
  
  const timer = setTimeout(() => {
    bundleModule(module);
    bundleTimers.delete(module.name);
  }, DEBOUNCE_DELAY);
  
  bundleTimers.set(module.name, timer as unknown as number);
}

/**
 * Main watch loop
 */
async function watch(): Promise<void> {
  console.log("🔍 Discovering modules with client.ts files...");
  const modules = await discoverModules();
  
  if (modules.length === 0) {
    console.log("⚠️  No modules with client.ts files found");
    console.log("   Create a mods/{module}/client.ts file to get started");
    return;
  }
  
  console.log(`📚 Found ${modules.length} module(s) with client code:`);
  modules.forEach(m => console.log(`   - ${m.name}`));
  
  // Initial build of all modules
  console.log("\n🏗️  Building initial bundles...");
  const results = await Promise.all(modules.map(bundleModule));
  const successCount = results.filter(Boolean).length;
  console.log(`✓ Initial build complete: ${successCount}/${modules.length} succeeded\n`);
  
  // Start watching
  console.log("👀 Watching for changes (Ctrl+C to stop)...\n");
  
  const modsDir = join(Deno.cwd(), "mods");
  const watcher = Deno.watchFs(modsDir);
  
  for await (const event of watcher) {
    // Filter for modify/create events on client.ts files
    if (event.kind === "modify" || event.kind === "create") {
      for (const path of event.paths) {
        if (path.endsWith("client.ts")) {
          // Find which module this belongs to
          const module = modules.find(m => m.sourcePath === path);
          if (module) {
            debouncedBundle(module);
          }
        }
      }
    }
  }
}

// Handle Ctrl+C gracefully
Deno.addSignalListener("SIGINT", () => {
  console.log("\n👋 Stopping bundle watcher...");
  Deno.exit(0);
});

// Start watching
if (import.meta.main) {
  await watch();
}
