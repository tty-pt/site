import React from "https://esm.sh/react@18";
import ChordList from "./components/ChordList.tsx";
import ChordAdd from "./components/ChordAdd.tsx";
import ChordDetail from "./components/ChordDetail.tsx";
import { dirname, fromFileUrl, resolve } from "https://deno.land/std@0.208.0/path/mod.ts";
import { transpose, TRANSP_BEMOL, TRANSP_LATIN } from "../../../lib/transp/mod.ts";

const moduleDir = dirname(fromFileUrl(import.meta.url));
const repoRoot = resolve(moduleDir, "../../../");

async function getChords(): Promise<string[]> {
  try {
    const dir = await Deno.readDir(`${repoRoot}/items/chords/items`);
    const chords: string[] = [];
    for await (const entry of dir) {
      if (entry.isDirectory && !entry.name.startsWith(".")) {
        chords.push(entry.name);
      }
    }
    return chords.sort((a, b) => a.localeCompare(b));
  } catch {
    return [];
  }
}

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

export async function render({ user, path, params, searchParams }: { user: string | null; path: string; params: Record<string, string>; searchParams: URLSearchParams }) {
  if (path === "/chords/add") {
    return ChordAdd({ user, path });
  }

  if (path === "/chords" || path === "/chords/") {
    const chords = await getChords();
    return ChordList({ user, path, chords });
  }

  const id = params.id;
  if (id) {
    // Parse query parameters from searchParams
    const transposeAmount = parseInt(searchParams.get("t") || "0");
    const useBemol = searchParams.get("b") === "1";
    const useLatin = searchParams.get("l") === "1";
    
    // Fetch chord data
    const chordData = await getChordData(id);
    if (!chordData) {
      return null;
    }
    
    // Apply transposition if needed
    let data = chordData.data;
    if (transposeAmount !== 0 || useBemol || useLatin) {
      let flags = 0;
      if (useBemol) flags |= TRANSP_BEMOL;
      if (useLatin) flags |= TRANSP_LATIN;
      
      const transposed = transpose(chordData.data, transposeAmount, flags);
      if (transposed) {
        data = transposed;
      }
    }
    
    return ChordDetail({ 
      user, 
      path, 
      id, 
      data, 
      title: chordData.title,
      transpose: transposeAmount,
      useBemol,
      useLatin
    });
  }

  return null;
}
