import { Handlers, PageProps } from "$fresh/server.ts";
import IndexAdd from "@/index/IndexAdd.tsx";
import { dirname, fromFileUrl, resolve } from "@std/path";
import type { State } from "../../_middleware.ts";

const moduleDir = dirname(fromFileUrl(import.meta.url));
const repoRoot = resolve(moduleDir, "../../..");

async function getSongData(id: string) {
  const base = `${repoRoot}/items/song/items/${id}`;
  
  try {
    const data = await Deno.readTextFile(`${base}/data.txt`);
    let title: string | null = null;
    
    try {
      title = await Deno.readTextFile(`${base}/title`);
    } catch {
      // Title is optional
    }
    
    return { data, title };
  } catch {
    return null;
  }
}

interface SongEditData {
  user: string | null;
  id: string;
  data: string;
  title: string | null;
}

/**
 * Edit song page - /song/:id/edit
 * 
 * Displays form to edit an existing song. POST is handled by C backend.
 */
export const handler: Handlers<SongEditData, State> = {
  async GET(req, ctx) {
    const id = ctx.params.id;
    const songData = await getSongData(id);
    
    if (!songData) {
      return ctx.renderNotFound();
    }
    
    return ctx.render({
      user: ctx.state.user,
      id,
      data: songData.data,
      title: songData.title,
    });
  },
};

export default function SongEdit({ data }: PageProps<SongEditData>) {
  // Reuse IndexAdd component (it may need edit mode support)
  return (
    <IndexAdd 
      user={data.user} 
      module="song"
    />
  );
}
