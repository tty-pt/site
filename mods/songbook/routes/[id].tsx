import { Handlers, PageProps } from "$fresh/server.ts";
import { Layout, ItemMenu } from "@/ssr/ui.tsx";
import type { State } from "#/routes/_middleware.ts";
import SongItem from "#/islands/SongItem.tsx";

interface Song {
  chordId?: string;
  transpose?: number;
  format?: string;
  chordTitle?: string;
  chordData?: string;
  originalKey?: number;
}

interface SbData {
  user: string | null;
  songbook: Songbook | null;
}

interface Songbook {
  id: string;
  title: string;
  owner: string;
  choir: string;
  songs: Song[];
}

export const handler: Handlers<SbData, State> = {
  async POST(req, ctx) {
    const body = await req.json();

    const songbook: Songbook = {
      id: ctx.params.id,
      title: body.title || "",
      owner: body.owner || "",
      choir: body.choir || "",
      songs: body.songs || [],
    };

    return ctx.render({
      user: ctx.state.user,
      songbook,
    });
  },
};

export default function SbDetail({ data }: PageProps<SbData>) {
  if (!data.songbook) {
    return (
      <Layout user={data.user} title="Songbook Not Found" path="/songbook" icon="📖">
        <div className="center">
          <h1>Songbook Not Found</h1>
        </div>
      </Layout>
    );
  }

  const songbook = data.songbook as Songbook & { songs: Array<Song & { chordTitle: string; chordData: string; originalKey: number }> };
  const isOwner = data.user === songbook.owner;

  return (
    <Layout user={data.user} title={`songbook: ${songbook.title}`} path={`/songbook/${songbook.id}`} icon="📖"
      menuItems={<ItemMenu module="songbook" id={songbook.id} isOwner={isOwner} />}>
      <div className="flex flex-col gap-1">
        {songbook.choir && (
          <div className="flex justify-end text-xs text-muted">
            <a href={`/choir/${songbook.choir}`} className="text-muted">
              {songbook.choir}
            </a>
          </div>
        )}

        {songbook.songs.map((song, i) => (
          <SongItem
            key={i}
            chordId={song.chordId || ""}
            transpose={song.transpose || 0}
            format={song.format || ""}
            chordTitle={song.chordTitle || ""}
            chordData={song.chordData || ""}
            originalKey={song.originalKey || 0}
            isOwner={isOwner}
            songbookId={songbook.id}
            index={i}
          />
        ))}

      </div>
    </Layout>
  );
}
