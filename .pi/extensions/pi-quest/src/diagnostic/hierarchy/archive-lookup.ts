import { existsSync } from "node:fs";
import { readdir, stat } from "node:fs/promises";
import { resolve } from "node:path";

export async function findArchivedQuestFile(
	archiveDir: string,
	slug: string,
): Promise<{ path: string; mtime: number } | null> {
	const dirsToCheck = [archiveDir, resolve(archiveDir, "../archive")];
	const candidates: Array<{ path: string; mtime: number }> = [];

	for (const dir of dirsToCheck) {
		try {
			if (!existsSync(dir)) continue;
			const files = await readdir(dir);
			const prefix = `${slug}-`;
			const exact = `${slug}.md`;

			for (const f of files) {
				if (f === exact || (f.startsWith(prefix) && f.endsWith(".md"))) {
					const fPath = resolve(dir, f);
					try {
						const s = await stat(fPath);
						candidates.push({ path: fPath, mtime: s.mtimeMs });
					} catch {}
				}
			}
		} catch {}
	}

	if (candidates.length === 0) return null;
	candidates.sort((a, b) => b.mtime - a.mtime);
	return candidates[0];
}
