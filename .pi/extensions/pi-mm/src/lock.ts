import { open, unlink, stat, readFile, rename, mkdir } from "node:fs/promises";
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
    if (code === "ESRCH") {
      return false; // Confirmed: process does not exist
    }
    // Fail closed: EPERM or any ambiguous system error treats process as alive
    return true;
  }
}

async function tryRecoverStaleLock(lockFile: string, staleThresholdMs: number): Promise<void> {
  try {
    const st = await stat(lockFile);
    if (Date.now() - st.mtimeMs < staleThresholdMs) {
      return;
    }

    const content = await readFile(lockFile, "utf-8");
    let info: LockInfo | null = null;
    try {
      info = JSON.parse(content);
    } catch {
      // Treat malformed lock ownership as unknown, not dead. Fail closed!
      return;
    }

    if (!info || typeof info.pid !== "number" || info.pid <= 0 || isPidAlive(info.pid)) {
      return;
    }

    // Process is confirmed dead (ESRCH).
    // Atomically isolate this candidate lock to eliminate TOCTOU replacement races.
    const myPid = typeof process !== "undefined" ? process.pid : 0;
    const recoveryPath = `${lockFile}.recovery.${myPid}.${Date.now()}.${Math.random().toString(36).slice(2, 6)}`;
    try {
      await rename(lockFile, recoveryPath);
    } catch {
      return; // Competing process already won recovery or lock was unlinked
    }

    // Safely remove the isolated recovery file without ever overwriting another process's lock
    try {
      await unlink(recoveryPath);
    } catch {
      // Best-effort cleanup
    }
  } catch {
    // Stat failed or file absent
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

  // 1. In-process queue: ensures serialized execution within the same process
  let releaseInProcess: () => void = () => {};
  const currentLock = inProcessLocks.get(dir) ?? Promise.resolve();
  const lockAcquiredPromise = new Promise<void>((resolve) => {
    releaseInProcess = resolve;
  });
  inProcessLocks.set(dir, currentLock.then(() => lockAcquiredPromise, () => lockAcquiredPromise));
  await currentLock;

  // 2. Cross-process filesystem lock: unconditionally enforced inside try/finally
  const lockFile = join(dir, ".mm.lock");
  let fileLockAcquired = false;

  try {
    await mkdir(dir, { recursive: true });
    fileLockAcquired = await acquireFileLock(lockFile, Date.now(), timeoutMs, staleThresholdMs);
    if (!fileLockAcquired) {
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
