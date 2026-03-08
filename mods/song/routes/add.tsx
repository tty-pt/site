import { Handlers, PageProps } from "$fresh/server.ts";
import IndexAdd from "@/index/IndexAdd.tsx";
import type { State } from "#/routes/_middleware.ts";

interface SongAddData {
  user: string | null;
}

/**
 * Add song form page - /song/add
 * 
 * Displays form to add a new song. POST is handled by C backend (mods/song/song.c).
 */
export const handler: Handlers<SongAddData, State> = {
  GET(req, ctx) {
    return ctx.render({ user: ctx.state.user });
  },
};

export default function SongAdd({ data }: PageProps<SongAddData>) {
  return <IndexAdd user={data.user} module="song" />;
}
