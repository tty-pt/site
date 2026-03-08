#!/usr/bin/env -S deno run --allow-read --allow-write
/**
 * Setup Routes - Hard Link Module Routes and Islands to Fresh
 * 
 * This script auto-discovers modules with routes/ and islands/ directories
 * and creates hard links so Fresh can discover them as regular files.
 * 
 * Usage:
 *   deno run --allow-read --allow-write scripts/setup-routes.ts
 * 
 * Flow:
 *   1. Scan mods/ for directories containing routes/ or islands/ subdirectories
 *   2. Remove existing hard links and symlinks in routes/ and islands/
 *   3. Create fresh hard links for each file:
 *      - Each file in mods/<module>/routes/ -> routes/<module>/<file>
 *      - Each file in mods/<module>/islands/ -> islands/<file>
 *   4. Verify hard links created correctly (same inode)
 *   5. Report what was linked
 */

import { resolve, dirname, fromFileUrl, relative, join, basename } from "@std/path";
import { walk } from "jsr:@std/fs@^1.0.0/walk";

const scriptDir = dirname(fromFileUrl(import.meta.url));
const repoRoot = resolve(scriptDir, "..");
const modsDir = resolve(repoRoot, "mods");
const routesDir = resolve(repoRoot, "routes");
const islandsDir = resolve(repoRoot, "islands");

// Core routes that should never be deleted
const CORE_ROUTES = [
  "_app.tsx",
  "index.tsx",
  "_middleware.ts",
  "[...slug].tsx",
];

interface ModuleInfo {
  name: string;
  hasRoutes: boolean;
  hasIslands: boolean;
}

interface HardLinkInfo {
  source: string;
  destination: string;
  type: "route" | "island";
}

/**
 * Check if a path exists
 */
async function exists(path: string): Promise<boolean> {
  try {
    await Deno.stat(path);
    return true;
  } catch {
    return false;
  }
}

/**
 * Discover modules that have routes/ or (_islands)/ directories
 */
async function discoverModules(): Promise<ModuleInfo[]> {
  const modules: ModuleInfo[] = [];
  
  try {
    for await (const entry of Deno.readDir(modsDir)) {
      if (!entry.isDirectory) continue;
      
      const moduleName = entry.name;
      const moduleRoutesPath = resolve(modsDir, moduleName, "routes");
      const moduleIslandsPath = resolve(modsDir, moduleName, "islands");
      
      const hasRoutes = await exists(moduleRoutesPath);
      const hasIslands = await exists(moduleIslandsPath);
      
      if (hasRoutes || hasIslands) {
        modules.push({
          name: moduleName,
          hasRoutes,
          hasIslands,
        });
      }
    }
  } catch (e) {
    console.error("Failed to scan mods directory:", e);
    Deno.exit(1);
  }
  
  return modules;
}

/**
 * Clean up old symlinks and hard links
 */
async function cleanupOldLinks(): Promise<void> {
  // Clean routes subdirectories (module routes)
  try {
    for await (const entry of Deno.readDir(routesDir)) {
      if (!entry.isDirectory) continue;
      if (CORE_ROUTES.includes(entry.name)) continue;
      
      const dirPath = resolve(routesDir, entry.name);
      try {
        // Check if it's a symlink first
        const stat = await Deno.lstat(dirPath);
        if (stat.isSymlink) {
          await Deno.remove(dirPath);
          console.log(`  Removed old symlink: routes/${entry.name}`);
        } else if (stat.isDirectory) {
          // Remove directory and all its contents (hard links)
          await Deno.remove(dirPath, { recursive: true });
          console.log(`  Removed old hard links: routes/${entry.name}/`);
        }
      } catch (e) {
        console.error(`  Failed to remove routes/${entry.name}:`, e);
      }
    }
  } catch {
    // routes/ directory doesn't exist or can't be read
  }
  
  // Clean islands (individual files)
  try {
    for await (const entry of Deno.readDir(islandsDir)) {
      if (!entry.isFile) continue;
      if (!entry.name.endsWith(".tsx") && !entry.name.endsWith(".ts")) continue;
      
      const filePath = resolve(islandsDir, entry.name);
      try {
        await Deno.remove(filePath);
        console.log(`  Removed old link: islands/${entry.name}`);
      } catch (e) {
        console.error(`  Failed to remove islands/${entry.name}:`, e);
      }
    }
  } catch {
    // islands/ directory doesn't exist or can't be read
  }
}

/**
 * Create hard links for all route files in a module
 */
async function createRouteHardLinks(moduleName: string): Promise<HardLinkInfo[]> {
  const links: HardLinkInfo[] = [];
  const moduleRoutesPath = resolve(modsDir, moduleName, "routes");
  
  if (!await exists(moduleRoutesPath)) {
    return links;
  }
  
  // Walk all .ts and .tsx files recursively
  for await (const entry of walk(moduleRoutesPath, {
    exts: ["ts", "tsx"],
    includeFiles: true,
    includeDirs: false,
  })) {
    const sourcePath = entry.path;
    const relativePath = relative(moduleRoutesPath, sourcePath);
    const destinationPath = resolve(routesDir, moduleName, relativePath);
    
    // Create parent directory
    const destDir = dirname(destinationPath);
    await Deno.mkdir(destDir, { recursive: true });
    
    // Create hard link
    try {
      await Deno.link(sourcePath, destinationPath);
      links.push({
        source: sourcePath,
        destination: destinationPath,
        type: "route",
      });
    } catch (e) {
      console.error(`  ✗ Failed to hard link ${relativePath}:`, e);
    }
  }
  
  return links;
}

/**
 * Create hard links for all island files in a module
 */
async function createIslandHardLinks(moduleName: string): Promise<HardLinkInfo[]> {
  const links: HardLinkInfo[] = [];
  const moduleIslandsPath = resolve(modsDir, moduleName, "islands");
  
  if (!await exists(moduleIslandsPath)) {
    return links;
  }
  
  // Walk island files (non-recursive, just top level)
  for await (const entry of walk(moduleIslandsPath, {
    exts: ["ts", "tsx"],
    includeFiles: true,
    includeDirs: false,
    maxDepth: 1,
  })) {
    const sourcePath = entry.path;
    const fileName = basename(sourcePath);
    const destinationPath = resolve(islandsDir, fileName);
    
    // Create hard link
    try {
      await Deno.link(sourcePath, destinationPath);
      links.push({
        source: sourcePath,
        destination: destinationPath,
        type: "island",
      });
    } catch (e) {
      console.error(`  ✗ Failed to hard link ${fileName}:`, e);
    }
  }
  
  return links;
}

/**
 * Verify that hard links were created correctly by checking inodes
 */
async function verifyHardLinks(links: HardLinkInfo[]): Promise<boolean> {
  let allValid = true;
  
  for (const link of links) {
    try {
      const sourceStat = await Deno.stat(link.source);
      const destStat = await Deno.stat(link.destination);
      
      if (sourceStat.ino !== destStat.ino) {
        console.error(`  ✗ Inode mismatch: ${link.source} (${sourceStat.ino}) != ${link.destination} (${destStat.ino})`);
        allValid = false;
      }
      
      // Check link count (should be at least 2)
      if (sourceStat.nlink !== destStat.nlink) {
        console.error(`  ✗ Link count mismatch: ${link.source} (${sourceStat.nlink}) != ${link.destination} (${destStat.nlink})`);
        allValid = false;
      }
    } catch (e) {
      console.error(`  ✗ Failed to verify ${link.destination}:`, e);
      allValid = false;
    }
  }
  
  return allValid;
}

/**
 * Main execution
 */
async function main() {
  console.log("🔗 Setting up Fresh route and island hard links...\n");
  
  // Discover modules
  const modules = await discoverModules();
  
  if (modules.length === 0) {
    console.log("No modules with routes/ or (_islands)/ found in mods/");
    return;
  }
  
  console.log(`Found ${modules.length} module(s):\n`);
  modules.forEach(mod => {
    const parts = [];
    if (mod.hasRoutes) parts.push("routes");
    if (mod.hasIslands) parts.push("islands");
    console.log(`  - ${mod.name} (${parts.join(", ")})`);
  });
  console.log();
  
  // Clean existing links
  console.log("Cleaning old links...");
  await cleanupOldLinks();
  console.log();
  
  // Create hard links
  console.log("Creating hard links...");
  let totalRoutes = 0;
  let totalIslands = 0;
  const allLinks: HardLinkInfo[] = [];
  
  for (const mod of modules) {
    if (mod.hasRoutes) {
      const links = await createRouteHardLinks(mod.name);
      allLinks.push(...links);
      totalRoutes += links.length;
      if (links.length > 0) {
        console.log(`  ✓ ${mod.name}: ${links.length} route file(s) hard linked`);
      }
    }
    
    if (mod.hasIslands) {
      const links = await createIslandHardLinks(mod.name);
      allLinks.push(...links);
      totalIslands += links.length;
      if (links.length > 0) {
        console.log(`  ✓ ${mod.name}: ${links.length} island file(s) hard linked`);
      }
    }
  }
  console.log();
  
  // Verify hard links
  console.log("Verifying hard links...");
  const allValid = await verifyHardLinks(allLinks);
  
  if (!allValid) {
    console.error("\n❌ Some hard links failed verification!");
    Deno.exit(1);
  }
  console.log("  ✓ All hard links verified (inodes match)\n");
  
  console.log("✅ Setup complete!\n");
  console.log(`  Routes: ${totalRoutes} file(s) hard linked`);
  console.log(`  Islands: ${totalIslands} file(s) hard linked\n`);
  console.log("Run `deno task start` to start Fresh with module routes and islands.");
}

if (import.meta.main) {
  main();
}
