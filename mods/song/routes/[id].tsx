import { Handlers, PageProps } from "$fresh/server.ts";
import SongView from "#/islands/SongView.tsx";
import type { State } from "#/routes/_middleware.ts";
import { ErrorPage } from "@/ssr/ui.tsx";

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
  categories: string | null;
  author: string | null;
  error?: string;
  status?: number;
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
    const text = await req.text();
    const id = ctx.params.id;
    const url = new URL(req.url);

    const params = new URLSearchParams(text);
    const error = params.get("error");
    if (error) {
      const status = Number(params.get("status") ?? "500");
      return ctx.render(
        { user: ctx.state.user, path: url.pathname, id, data: "", title: null, yt: null, audio: null, pdf: null, transpose: 0, useBemol: false, useLatin: false, showMedia: false, originalKey: 0, owner: false, categories: null, author: null, error, status },
        { status },
      );
    }

    const body = JSON.parse(text);
    const transpose = parseInt(url.searchParams.get("t") || "0", 10);
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
  if (data.error) {
    return <ErrorPage status={data.status ?? 500} message={data.error} user={data.user} path={data.path} />;
  }
  return (<SongView { ...data } />);
}
