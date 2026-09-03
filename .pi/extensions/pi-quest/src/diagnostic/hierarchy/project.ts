import { existsSync } from "node:fs";
import { dirname, join, resolve } from "node:path";

export function findProjectRoot(startDir?: string): string {
  let current = resolve(
    startDir || (typeof process !== "undefined" ? process.cwd() : "."),
  );
  while (current.includes("/.pi/extensions/")) {
    current = current.slice(0, current.indexOf("/.pi/extensions/"));
  }

  const piIdx = current.indexOf("/.pi/");
  if (piIdx !== -1) {
    return current.slice(0, piIdx);
  }
  if (current.endsWith("/.pi")) {
    return dirname(current);
  }

  while (true) {
    const hasPi = existsSync(join(current, ".pi"));
    const hasDocs = existsSync(join(current, "docs"));
    const hasGit = existsSync(join(current, ".git"));

    if ((hasPi && hasDocs) || (hasGit && (hasPi || hasDocs)) || hasPi) {
      return current;
    }

    const parent = dirname(current);
    if (parent === current) {
      break;
    }
    current = parent;
  }

  return resolve(
    startDir || (typeof process !== "undefined" ? process.cwd() : "."),
  );
}

export function findExtensionDir(
  projectRoot: string,
  explicitDir?: string,
): string {
  if (explicitDir) {
    return resolve(explicitDir);
  }
  const standardExtPath = resolve(projectRoot, ".pi/extensions/pi-quest");
  if (existsSync(standardExtPath)) {
    return standardExtPath;
  }
  return resolve(
    dirname(dirname(
      import.meta.url
        ? new URL(import.meta.url).pathname
        : (typeof process !== "undefined" ? process.cwd() : "."),
    )),
  );
}
