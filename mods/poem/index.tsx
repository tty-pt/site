import IndexList from "../index/IndexList.tsx";
import PoemAdd from "./PoemAdd.tsx";
import PoemDetail from "./PoemDetail.tsx";
import { dirname, fromFileUrl, resolve } from "jsr:@std/path";

const moduleDir = dirname(fromFileUrl(import.meta.url));
const repoRoot = resolve(moduleDir, "../..");

async function getPoemContent(id: string): Promise<string | null> {
  try {
    const path = `${repoRoot}/items/poem/items/${id}/pt_PT.html`;
    return await Deno.readTextFile(path);
  } catch {
    return null;
  }
}

async function poemExists(id: string): Promise<boolean> {
  try {
    const path = `${repoRoot}/items/poem/items/${id}`;
    const stat = await Deno.stat(path);
    return stat.isDirectory;
  } catch {
    return false;
  }
}

export const routes = ["/poem", "/poem/add", "/poem/:id"];

export async function render({ user, path, params, body }: {
  user: string | null;
  path: string;
  params: Record<string, string>;
  body?: string | null;
}) {
  if (path === "/poem/add") {
    return PoemAdd({ user, path });
  }

  if (path === "/poem" || path === "/poem/") {
    return IndexList({ module: "poem", body: body || null });
  }

  const id = params.id;
  if (!id)
    return null;

  const exists = await poemExists(id);
  if (!exists) {
    return null;
  }

  const html = (await getPoemContent(id)) ?? "";
  return PoemDetail({ user, path, id, html });
}
