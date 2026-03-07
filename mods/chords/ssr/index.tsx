import ChordList from "./components/ChordList.tsx";
import ChordAdd from "./components/ChordAdd.tsx";
import ChordDetail from "./components/ChordDetail.tsx";
import IndexList from "../../index/ssr/components/IndexList.tsx";
import { dirname, fromFileUrl, resolve } from "https://deno.land/std@0.208.0/path/mod.ts";

const moduleDir = dirname(fromFileUrl(import.meta.url));
const repoRoot = resolve(moduleDir, "../../../");

async function getChordData(id: string): Promise<{ data: string; title: string | null } | null> {
  try {
    const dataPath = `${repoRoot}/items/chords/items/${id}/data.txt`;
    const data = await Deno.readTextFile(dataPath);

    let title: string | null = null;
    try {
      const titlePath = `${repoRoot}/items/chords/items/${id}/title`;
      title = await Deno.readTextFile(titlePath);
    } catch {
      // title file is optional
    }

    return { data, title };
  } catch {
    return null;
  }
}

export const routes = ["/chords", "/chords/add", "/chords/:id"];

export async function render({ user, path, params, searchParams, body }: {
  user: string | null;
  path: string;
  params: Record<string, string>;
  searchParams: URLSearchParams;
  body?: string | null;
}) {
  if (path === "/chords/add") {
    return ChordAdd({ user, path });
  }

  if (path === "/chords" || path === "/chords/") {
    return IndexList({ module: "chords", body: body || null });
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
        const titlePath = `${repoRoot}/items/chords/items/${id}/title`;
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
