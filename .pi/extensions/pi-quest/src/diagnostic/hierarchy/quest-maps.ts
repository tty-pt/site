export interface QuestFileInfo {
	slug: string;
	path: string;
	parent: string | null;
	declaredSubquests: string[];
	questId?: string | null;
	initialPrompt?: string | null;
	mtime: number;
}

export function buildQuestMaps(allQuestInfos: QuestFileInfo[]): {
	questMap: Map<string, QuestFileInfo>;
	childrenMap: Map<string, Set<string>>;
	effectiveParentMap: Map<string, string>;
} {
	const questMap = new Map<string, QuestFileInfo>();
	for (const info of allQuestInfos) {
		if (!questMap.has(info.slug)) {
			questMap.set(info.slug, info);
		}
	}

	const childrenMap = new Map<string, Set<string>>();
	const effectiveParentMap = new Map<string, string>();

	for (const [slug, info] of questMap.entries()) {
		if (!childrenMap.has(slug)) {
			childrenMap.set(slug, new Set());
		}

		if (info.parent && questMap.has(info.parent)) {
			effectiveParentMap.set(slug, info.parent);
			if (!childrenMap.has(info.parent)) {
				childrenMap.set(info.parent, new Set());
			}
			childrenMap.get(info.parent)!.add(slug);
		}

		for (const sub of info.declaredSubquests) {
			if (sub && sub !== slug) {
				if (!childrenMap.has(slug)) {
					childrenMap.set(slug, new Set());
				}
				childrenMap.get(slug)!.add(sub);
				if (!effectiveParentMap.has(sub)) {
					effectiveParentMap.set(sub, slug);
				}
			}
		}
	}

	return { questMap, childrenMap, effectiveParentMap };
}

export function computeDescendantMaps(
	childrenMap: Map<string, Set<string>>,
	rootSlugs: string[],
): { rootSubquestsMap: Map<string, Set<string>>; subquestToRootMap: Map<string, string> } {
	const getDescendants = (root: string): Set<string> => {
		const descendants = new Set<string>();
		const queue = [root];
		while (queue.length > 0) {
			const curr = queue.shift()!;
			const directChildren = childrenMap.get(curr);
			if (directChildren) {
				for (const child of directChildren) {
					if (!descendants.has(child) && child !== root) {
						descendants.add(child);
						queue.push(child);
					}
				}
			}
		}
		return descendants;
	};

	const rootSubquestsMap = new Map<string, Set<string>>();
	const subquestToRootMap = new Map<string, string>();

	for (const root of rootSlugs) {
		const subs = getDescendants(root);
		rootSubquestsMap.set(root, subs);
		for (const sub of subs) {
			subquestToRootMap.set(sub, root);
		}
	}

	return { rootSubquestsMap, subquestToRootMap };
}
