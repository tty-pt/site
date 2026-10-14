import { open, unlink, stat, readFile, mkdir } from "node:fs/promises";
import { join } from "node:path";

const inProcessLocks = new Map<string, Promise<void>>();

export interface LockOptions {
  timeoutMs?: number;
  staleThresholdMs?: number;
}

interface LockInfo {
  pid: number;
  createdAt: number;
}

export function isPidAlive(pid: number): boolean {
  if (!pid || pid <= 0) return false;
  try {
    process.kill(pid, 0);
    return true;
  } catch (err: unknown) {
    const code = (err as { code?: string })?.code;
    return code === "EPERM"; // EPERM means process exists (alive)
  }
}

async function tryRecoverStaleLock(lockFile: string, staleThresholdMs: number): Promise<void> {
  try {
    const content = await readFile(lockFile, "utf-8");
    const st = await stat(lockFile);
    if (!content.trim()) {
      // Empty lock file: wait if recent to avoid pruning mid-write
      if (Date.now() - st.mtimeMs > 1000) {
        await unlink(lockFile).catch(() => {});
      }
      return;
    }
    const info: LockInfo = JSON.parse(content);
    // If the process that created the lock is genuinely dead, safely prune it
    if (info.pid > 0 && !isPidAlive(info.pid)) {
      await unlink(lockFile).catch(() => {});
    } else if (Date.now() - st.mtimeMs > staleThresholdMs * 10 && !isPidAlive(info.pid)) {
      await unlink(lockFile).catch(() => {});
    }
  } catch {
    // If unparseable and older than threshold, prune corrupt lock file
    try {
      const st = await stat(lockFile);
      if (Date.now() - st.mtimeMs > 2000) {
        await unlink(lockFile).catch(() => {});
      }
    } catch {
      // Stat failed: file was already removed
    }
  }
}

async function acquireFileLock(
  lockFile: string,
  start: number,
  timeoutMs: number,
  staleThresholdMs: number,
): Promise<boolean> {
  while (true) {
    try {
      const handle = await open(lockFile, "wx");
      try {
        const payload = JSON.stringify({ pid: typeof process !== "undefined" ? process.pid : 0, createdAt: Date.now() });
        await handle.write(new TextEncoder().encode(payload));
      } finally {
        await handle.close();
      }
      return true;
    } catch (err: unknown) {
      const code = (err as { code?: string })?.code;
      if (code === "ENOENT" || code === "ENOTDIR" || code === "EACCES" || code === "EPERM") {
        return false; // Mock/virtual directories without disk access
      }
      if (code === "EEXIST") {
        await tryRecoverStaleLock(lockFile, staleThresholdMs);
      }
      if (Date.now() - start > timeoutMs) {
        throw new Error(`Failed to acquire memory lock on ${lockFile} after ${timeoutMs}ms (lock contention)`);
      }
      const backoff = 20 + Math.floor(Math.random() * 20);
      await new Promise((resolve) => setTimeout(resolve, backoff));
    }
  }
}

export async function withLock<T>(
  dir: string,
  fn: () => Promise<T>,
  opts: LockOptions = {},
): Promise<T> {
  const timeoutMs = opts.timeoutMs ?? 3000;
  const staleThresholdMs = opts.staleThresholdMs ?? 5000;

  // 1. In-process queue
  let releaseInProcess: () => void = () => {};
  const currentLock = inProcessLocks.get(dir) ?? Promise.resolve();
  const lockAcquiredPromise = new Promise<void>((resolve) => {
    releaseInProcess = resolve;
  });
  inProcessLocks.set(dir, currentLock.then(() => lockAcquiredPromise, () => lockAcquiredPromise));
  await currentLock;

  // 2. Cross-process filesystem lock
  const lockFile = join(dir, ".mm.lock");
  let fileLockAcquired = false;
  let dirOnDisk = false;
  try {
    await mkdir(dir, { recursive: true });
    dirOnDisk = true;
  } catch {
    // Mock directory
  }

  try {
    fileLockAcquired = await acquireFileLock(lockFile, Date.now(), timeoutMs, staleThresholdMs);
    if (!fileLockAcquired && dirOnDisk) {
      throw new Error(`Failed to acquire memory lock on ${dir}: filesystem locking required`);
    }
    return await fn();
  } finally {
    if (fileLockAcquired) {
      try { await unlink(lockFile); } catch { /* best-effort */ }
    }
    releaseInProcess();
  }
}
