import { Handlers, PageProps } from "$fresh/server.ts";
import SongView from "#/islands/SongView.tsx";
import type { State } from "#/routes/_middleware.ts";
import { ErrorPage, readPostedJson } from "@/ssr/ui.tsx";

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

interface SongDetailPayload {
  data?: string;
  title?: string | null;
  yt?: string | null;
  audio?: string | null;
  pdf?: string | null;
  originalKey?: number;
  owner?: boolean;
  categories?: string | null;
  author?: string | null;
}

interface SongDetailFlags {
  transpose: number;
  useBemol: boolean;
  useLatin: boolean;
  showMedia: boolean;
}

const EMPTY_SONG_DETAIL: Omit<SongDetailData, "user" | "path" | "id"> = {
  data: "",
  title: null,
  yt: null,
  audio: null,
  pdf: null,
  transpose: 0,
  useBemol: false,
  useLatin: false,
  showMedia: false,
  originalKey: 0,
  owner: false,
  categories: null,
  author: null,
};

function songFlagsFromUrl(url: URL): SongDetailFlags {
  return {
    transpose: parseInt(url.searchParams.get("t") || "0", 10),
    useBemol: url.searchParams.get("b") === "1",
    useLatin: url.searchParams.get("l") === "1",
    showMedia: url.searchParams.get("m") === "1",
  };
}

function toSongDetailData(
  payload: SongDetailPayload,
  base: { user: string | null; path: string; id: string },
  flags: SongDetailFlags,
): SongDetailData {
  return {
    ...base,
    data: payload.data ?? "",
    title: payload.title ?? null,
    yt: payload.yt ?? null,
    audio: payload.audio ?? null,
    pdf: payload.pdf ?? null,
    transpose: flags.transpose,
    useBemol: flags.useBemol,
    useLatin: flags.useLatin,
    showMedia: flags.showMedia,
    originalKey: payload.originalKey ?? 0,
    owner: payload.owner === true,
    categories: payload.categories ?? null,
    author: payload.author ?? null,
  };
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
    const id = ctx.params.id;
    const url = new URL(req.url);
    const base = { user: ctx.state.user, path: url.pathname, id };
    const result = await readPostedJson<SongDetailPayload>(req);
    if (!result.ok) {
      return ctx.render(
        { ...base, ...EMPTY_SONG_DETAIL, error: result.error, status: result.status },
        { status: result.status },
      );
    }

    return ctx.render(toSongDetailData(result.data, base, songFlagsFromUrl(url)));
  },
};

export default function SongDetail({ data }: PageProps<SongDetailData>) {
  if (data.error) {
    return <ErrorPage status={data.status ?? 500} message={data.error} user={data.user} path={data.path} />;
  }
  return (<SongView { ...data } />);
}
