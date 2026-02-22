import React from "https://esm.sh/react@18";
import PoemList from "./components/PoemList.tsx";
import PoemAdd from "./components/PoemAdd.tsx";
import PoemDetail from "./components/PoemDetail.tsx";
import { dirname, fromFileUrl, resolve } from "https://deno.land/std@0.208.0/path/mod.ts";

const moduleDir = dirname(fromFileUrl(import.meta.url));
const repoRoot = resolve(moduleDir, "../../../");

async function getPoems(): Promise<string[]> {
  try {
    const dir = await Deno.readDir(`${repoRoot}/items/poem/items`);
    const poems: string[] = [];
    for await (const entry of dir) {
      if (entry.isDirectory && !entry.name.startsWith(".")) {
        poems.push(entry.name);
      }
    }
    return poems.sort((a, b) => a.localeCompare(b));
  } catch {
    return [];
  }
}

async function getPoemContent(id: string): Promise<string | null> {
  try {
    const path = `${repoRoot}/items/poem/items/${id}/pt_PT.html`;
    return await Deno.readTextFile(path);
  } catch {
    return null;
  }
}

// Components moved to separate TSX files under ./components

export const routes = ["/poem", "/poem/add", "/poem/:id"];

export async function render({ user, path, params }: { user: string | null; path: string; params: Record<string, string> }) {
  if (path === "/poem/add") {
    return PoemAdd({ user, path });
  }

  if (path === "/poem" || path === "/poem/") {
    const poems = await getPoems();
    return PoemList({ user, path, poems });
  }

  const id = params.id;
  if (id) {
    const html = await getPoemContent(id);
    if (html) {
      return PoemDetail({ user, path, id, html });
    }
  }

  return null;
}
