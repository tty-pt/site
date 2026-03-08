import { Handlers, PageProps } from "$fresh/server.ts";
import IndexList from "@/index/IndexList.tsx";
import type { State } from "#/routes/_middleware.ts";

interface SongIndexData {
  user: string | null;
  body: string | null;
}

/**
 * Song listing page - /song
 * 
 * Displays list of all songs. Supports POST for list updates/filtering.
 */
export const handler: Handlers<SongIndexData, State> = {
  async GET(req, ctx) {
    return ctx.render({ 
      user: ctx.state.user,
      body: null,
    });
  },
  
  async POST(req, ctx) {
    const body = await req.text();
    return ctx.render({ 
      user: ctx.state.user,
      body,
    });
  },
};

export default function SongIndex({ data }: PageProps<SongIndexData>) {
  return <IndexList module="song" body={data.body} />;
}
