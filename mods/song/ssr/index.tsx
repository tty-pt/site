import ChordDetail from "./components/ChordDetail.tsx";
import IndexAdd from "@/index/ssr/components/IndexAdd.tsx";
import IndexList from "@/index/ssr/components/IndexList.tsx";
import {
  dirname,
  fromFileUrl,
  resolve,
} from "https://deno.land/std@0.208.0/path/mod.ts";

const moduleDir = dirname(fromFileUrl(import.meta.url));
const repoRoot = resolve(moduleDir, "../../../");

async function getChordData(id: string): Promise<{
  data: string;
  title: string | null
} | null> {
  const base = `${repoRoot}/items/song/items/${id}`;

  try {
    const dataPath = `${base}/data.txt`;
    const data = await Deno.readTextFile(dataPath);

    let title: string | null = null;
    try {
      const titlePath = `${base}/title`;
      title = await Deno.readTextFile(titlePath);
    } catch {
      // title file is optional
    }

    return { data, title };
  } catch {
    return null;
  }
}

export const routes = [
  "/song",
  "/song/add",
  "/song/:id",
  "/song/:id/edit",
];

export async function render({ user, path, params, searchParams, body }: {
  user: string | null;
  path: string;
  params: Record<string, string>;
  searchParams: URLSearchParams;
  body?: string | null;
}) {
  if (path === "/song/add") {
    return IndexAdd({ user, module: "song" });
  }

  if (path === "/song" || path === "/song/") {
    return IndexList({ module: "song", body: body || null });
  }

  const id = params.id;
  if (id) {
    // Parse query parameters from searchParams
    const transposeAmount = parseInt(searchParams.get("t") || "0");
    const useBemol = searchParams.get("b") === "1";
    const useLatin = searchParams.get("l") === "1";
    
    // Use body if provided (already transposed by C), else read file
    let data: string;
    let title: string | null = null;
    
    if (body) {
      // Pre-transposed data from C module
      data = body;
      // Still need to fetch title
      try {
        const titlePath = `${repoRoot}/items/song/items/${id}/title`;
        title = await Deno.readTextFile(titlePath);
      } catch {
        // title file is optional
      }
    } else {
      // No transposition needed, read file directly
      const chordData = await getChordData(id);
      if (!chordData) {
        return null;
      }
      data = chordData.data;
      title = chordData.title;
    }
    
    return ChordDetail({ 
      user, 
      path, 
      id, 
      data, 
      title,
      transpose: transposeAmount,
      useBemol,
      useLatin
    });
  }

  return null;
}
