/**
 * Development server orchestrator
 * 
 * Manages all development processes:
 * - Deno SSR server with HMR
 * - Tailwind CSS watcher
 * - Client JS bundle watcher
 * - LiveReload server
 * - ndc HTTP server
 * 
 * Usage:
 *   deno run --allow-all scripts/dev-server.ts
 */

const COLORS = {
  reset: "\x1b[0m",
  bright: "\x1b[1m",
  dim: "\x1b[2m",
  cyan: "\x1b[36m",
  green: "\x1b[32m",
  yellow: "\x1b[33m",
  red: "\x1b[31m",
  blue: "\x1b[34m",
  magenta: "\x1b[35m",
};

type ProcessInfo = {
  name: string;
  process: Deno.ChildProcess;
  color: string;
};

const processes: ProcessInfo[] = [];

/**
 * Colorized log with process name
 */
function log(message: string, color: string = COLORS.reset): void {
  console.log(`${color}${message}${COLORS.reset}`);
}

/**
 * Kill all existing dev processes
 */
async function killExistingProcesses(): Promise<void> {
  log("🧹 Cleaning up existing processes...", COLORS.dim);
  
  const killCommands = [
    ["pkill", "-f", "deno.*server.ts"],
    ["pkill", "-f", "tailwindcss"],
    ["pkill", "-f", "livereload"],
    ["pkill", "-x", "ndc"],
  ];
  
  for (const args of killCommands) {
    try {
      const cmd = new Deno.Command(args[0], {
        args: args.slice(1),
        stdout: "null",
        stderr: "null",
      });
      await cmd.output();
    } catch {
      // Ignore errors (process may not exist)
    }
  }
  
  // Give processes time to die
  await new Promise(resolve => setTimeout(resolve, 500));
}

/**
 * Check if a port is available
 */
async function checkPort(port: number): Promise<boolean> {
  try {
    const listener = Deno.listen({ port });
    listener.close();
    return true;
  } catch {
    return false;
  }
}

/**
 * Wait for Deno SSR server to be ready
 */
async function waitForSSR(maxAttempts = 10): Promise<boolean> {
  for (let i = 0; i < maxAttempts; i++) {
    try {
      const response = await fetch("http://localhost:3000", { 
        method: "HEAD",
        signal: AbortSignal.timeout(1000)
      });
      if (response.ok || response.status === 404) {
        return true; // Server is responding
      }
    } catch {
      // Not ready yet
    }
    await new Promise(resolve => setTimeout(resolve, 500));
  }
  return false;
}

/**
 * Spawn a background process
 */
function spawn(
  name: string,
  cmd: string,
  args: string[],
  color: string,
  options: Deno.CommandOptions = {}
): Deno.ChildProcess {
  log(`🚀 Starting ${name}...`, color);
  
  const command = new Deno.Command(cmd, {
    args,
    stdout: "piped",
    stderr: "piped",
    ...options,
  });
  
  const process = command.spawn();
  
  // Pipe output with color-coded prefix
  (async () => {
    const decoder = new TextDecoder();
    for await (const chunk of process.stdout) {
      const text = decoder.decode(chunk);
      text.split('\n').forEach(line => {
        if (line.trim()) {
          console.log(`${color}[${name}]${COLORS.reset} ${line}`);
        }
      });
    }
  })();
  
  (async () => {
    const decoder = new TextDecoder();
    for await (const chunk of process.stderr) {
      const text = decoder.decode(chunk);
      text.split('\n').forEach(line => {
        if (line.trim()) {
          console.error(`${color}[${name}]${COLORS.reset} ${COLORS.red}${line}${COLORS.reset}`);
        }
      });
    }
  })();
  
  processes.push({ name, process, color });
  return process;
}

/**
 * Cleanup all spawned processes
 */
async function cleanup(): Promise<void> {
  log("\n👋 Shutting down dev server...", COLORS.yellow);
  
  for (const { name, process, color } of processes) {
    try {
      log(`   Stopping ${name}...`, color);
      process.kill("SIGTERM");
      await process.status;
    } catch {
      // Process may have already exited
    }
  }
  
  log("✓ All processes stopped", COLORS.green);
  Deno.exit(0);
}

/**
 * Main dev server startup
 */
async function main(): Promise<void> {
  log(`${COLORS.bright}${COLORS.cyan}╔════════════════════════════════════════╗${COLORS.reset}`);
  log(`${COLORS.bright}${COLORS.cyan}║   Development Server with Hot-Reload  ║${COLORS.reset}`);
  log(`${COLORS.bright}${COLORS.cyan}╚════════════════════════════════════════╝${COLORS.reset}\n`);
  
  // Kill existing processes
  await killExistingProcesses();
  
  // Check required ports
  const portsToCheck = [
    { port: 3000, name: "Deno SSR" },
    { port: 8080, name: "ndc" },
    { port: 35729, name: "LiveReload" },
  ];
  
  for (const { port, name } of portsToCheck) {
    if (!await checkPort(port)) {
      log(`✗ Port ${port} is in use (needed for ${name})`, COLORS.red);
      log(`  Kill the process using this port and try again`, COLORS.dim);
      Deno.exit(1);
    }
  }
  
  // Set DEV_MODE environment variable
  Deno.env.set("DEV_MODE", "1");
  log("✓ DEV_MODE=1 set\n", COLORS.green);
  
  // Initial builds (synchronous)
  log("🏗️  Running initial builds...", COLORS.bright);
  
  // Build CSS
  try {
    const cssCmd = new Deno.Command("npm", {
      args: ["run", "build:css"],
      stdout: "piped",
      stderr: "piped",
    });
    const cssResult = await cssCmd.output();
    if (cssResult.success) {
      log("✓ CSS built", COLORS.green);
    } else {
      log("✗ CSS build failed", COLORS.red);
      const error = new TextDecoder().decode(cssResult.stderr);
      console.error(error);
    }
  } catch (err) {
    log(`✗ CSS build error: ${err.message}`, COLORS.red);
  }
  
  // Build client JS
  try {
    const jsCmd = new Deno.Command("deno", {
      args: ["run", "--allow-read", "--allow-write", "--allow-run", "scripts/watch-bundle.ts"],
      stdout: "piped",
      stderr: "piped",
    });
    
    // Run initial bundle (will exit after first build)
    // We'll start the watcher separately in background
  } catch (err) {
    log(`✗ JS build error: ${err.message}`, COLORS.red);
  }
  
  log("", COLORS.reset);
  
  // Start background processes
  log("🎬 Starting watchers...\n", COLORS.bright);
  
  // 1. Deno SSR with HMR
  spawn(
    "Deno SSR",
    "deno",
    ["run", "--allow-read", "--allow-net", "--allow-env", "--watch-hmr", "mods/ssr/server.ts"],
    COLORS.cyan,
    { cwd: Deno.cwd() }
  );
  
  // 2. Tailwind CSS watch
  spawn(
    "Tailwind",
    "npm",
    ["run", "dev:css"],
    COLORS.magenta
  );
  
  // 3. Client JS bundle watcher
  spawn(
    "Bundler",
    "deno",
    ["run", "--allow-read", "--allow-write", "--allow-run", "scripts/watch-bundle.ts"],
    COLORS.blue
  );
  
  // 4. LiveReload server
  spawn(
    "LiveReload",
    "npx",
    ["livereload", "htdocs/", "--wait", "200", "--exts", "css,js"],
    COLORS.yellow
  );
  
  // Wait for Deno SSR to be ready
  log("\n⏳ Waiting for SSR server...", COLORS.dim);
  const ssrReady = await waitForSSR();
  if (!ssrReady) {
    log("✗ SSR server failed to start", COLORS.red);
    await cleanup();
    Deno.exit(1);
  }
  log("✓ SSR server ready\n", COLORS.green);
  
  // 5. Start ndc (foreground - this blocks)
  log("🌐 Starting ndc HTTP server...\n", COLORS.bright);
  
  const ndcPath = Deno.env.get("NDC_PATH") || "ndc";
  const ndcCmd = new Deno.Command(ndcPath, {
    args: ["-C", Deno.cwd(), "-p", "8080", "-d"],
    stdout: "inherit",
    stderr: "inherit",
  });
  
  const ndcProcess = ndcCmd.spawn();
  
  // Print ready message
  setTimeout(() => {
    log(`\n${COLORS.bright}${COLORS.green}╔════════════════════════════════════════╗${COLORS.reset}`);
    log(`${COLORS.bright}${COLORS.green}║          Dev Server Ready! 🚀          ║${COLORS.reset}`);
    log(`${COLORS.bright}${COLORS.green}╚════════════════════════════════════════╝${COLORS.reset}`);
    log(`\n  ${COLORS.cyan}→ Application:${COLORS.reset}   http://localhost:8080`);
    log(`  ${COLORS.dim}→ SSR Server:    http://localhost:3000${COLORS.reset}`);
    log(`  ${COLORS.dim}→ LiveReload:    http://localhost:35729${COLORS.reset}`);
    log(`\n  ${COLORS.yellow}Press Ctrl+C to stop${COLORS.reset}\n`);
  }, 1000);
  
  // Wait for ndc to exit
  await ndcProcess.status;
  
  // If ndc exits, cleanup everything
  await cleanup();
}

// Handle Ctrl+C
Deno.addSignalListener("SIGINT", cleanup);
Deno.addSignalListener("SIGTERM", cleanup);

// Start the dev server
if (import.meta.main) {
  try {
    await main();
  } catch (err) {
    log(`\n✗ Fatal error: ${err.message}`, COLORS.red);
    await cleanup();
    Deno.exit(1);
  }
}
