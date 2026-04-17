import { Handlers, PageProps } from "$fresh/server.ts";
import SongView from "#/islands/SongView.tsx";
import type { State } from "#/routes/_middleware.ts";

interface SongDetailData {
  user: string | null;
  path: string;
  id: string;
  data: string;
  title: string | null;
  yt: string | null;
  audio: string | null;
  pdf: string | null;
  transpose: number;
  useBemol: boolean;
  useLatin: boolean;
  showMedia: boolean;
  originalKey: number;
  owner: boolean;
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
  async POST(req, ctx) {
    const body = await req.json();

    const id = ctx.params.id;
    const url = new URL(req.url);
    
    const transpose = parseInt(
      url.searchParams.get("t") || "0",
      10
    );

    const useBemol = url.searchParams.get("b") === "1";
    const useLatin = url.searchParams.get("l") === "1";
    const showMedia = url.searchParams.get("m") === "1";
    
    return ctx.render({
      user: ctx.state.user,
      path: url.pathname,
      ...body,
      id,
      transpose,
      useBemol,
      useLatin,
      showMedia,
    });
  },
};

export default function SongDetail({ data }: PageProps<SongDetailData>) {
  return (<SongView { ...data } />);
}
