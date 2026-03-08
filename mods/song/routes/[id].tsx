import { Handlers, PageProps } from "$fresh/server.ts";
import ChordDetail from "@/song/ChordDetail.tsx";
import TransposeControls from "#/islands/TransposeControls.tsx";
import TransposeForm from "#/islands/TransposeForm.tsx";
import { dirname, fromFileUrl, resolve } from "@std/path";
import type { State } from "#/routes/_middleware.ts";

const moduleDir = dirname(fromFileUrl(import.meta.url));
const repoRoot = resolve(moduleDir, "../..");

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

interface SongDetailData {
  user: string | null;
  path: string;
  id: string;
  data: string;
  title: string | null;
  transpose: number;
  useBemol: boolean;
  useLatin: boolean;
}

/**
 * Song detail page with transpose controls - /song/:id
 * 
 * Displays song chord chart with interactive transpose controls (island).
 * Supports query parameters for server-side transpose via C backend:
 *   - t=N  : transpose by N semitones (-11 to +11)
 *   - b=1  : use flat notation (♭) instead of sharps
 *   - l=1  : use Latin notation (Do/Re/Mi)
 * 
 * Flow:
 *   GET /song/:id?t=2&b=1
 *     → C backend intercepts, transposes using libtransp.so
 *     → C backend POSTs transposed data to Fresh
 *     → Fresh renders with transposed chords
 */
export const handler: Handlers<SongDetailData, State> = {
  async GET(req, ctx) {
    const id = ctx.params.id;
    const url = new URL(req.url);
    
    // Parse query params (t=transpose, b=bemol/flats, l=latin)
    const transpose = parseInt(url.searchParams.get("t") || "0", 10);
    const useBemol = url.searchParams.get("b") === "1";
    const useLatin = url.searchParams.get("l") === "1";
    
    const songData = await getSongData(id);
    
    if (!songData) {
      return ctx.renderNotFound();
    }
    
    return ctx.render({
      user: ctx.state.user,
      path: url.pathname,
      id,
      data: songData.data,
      title: songData.title,
      transpose,
      useBemol,
      useLatin,
    });
  },
  
  async POST(req, ctx) {
    // C backend sends transposed chord data in POST body
    // This happens when query params trigger server-side transposition
    const transposedData = await req.text();
    const id = ctx.params.id;
    const url = new URL(req.url);
    
    // Parse query params
    const transpose = parseInt(url.searchParams.get("t") || "0", 10);
    const useBemol = url.searchParams.get("b") === "1";
    const useLatin = url.searchParams.get("l") === "1";
    
    // Still need title from filesystem
    const songData = await getSongData(id);
    
    return ctx.render({
      user: ctx.state.user,
      path: url.pathname,
      id,
      data: transposedData,
      title: songData?.title || null,
      transpose,
      useBemol,
      useLatin,
    });
  },
};

export default function SongDetail({ data }: PageProps<SongDetailData>) {
  return (
    <ChordDetail 
      user={data.user}
      path={data.path}
      id={data.id}
      data={data.data}
      title={data.title}
      transpose={data.transpose}
      useBemol={data.useBemol}
      useLatin={data.useLatin}
      transposeForm={
        <TransposeForm
          id={data.id}
          transpose={data.transpose}
          useBemol={data.useBemol}
          useLatin={data.useLatin}
        />
      }
      customTransposeControls={
        <TransposeControls 
          initialTranspose={data.transpose}
          useBemol={data.useBemol}
          useLatin={data.useLatin}
        />
      }
    />
  );
}
