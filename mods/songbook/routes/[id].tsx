import { Handlers, PageProps } from "$fresh/server.ts";
import { Layout } from "@/ssr/ui.tsx";
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
      <Layout user={data.user} title="Songbook Not Found" path="/songbook">
        <div className="center">
          <h1>Songbook Not Found</h1>
          <a href="/songbook" className="btn">Back to Songbooks</a>
        </div>
      </Layout>
    );
  }

  const songbook = data.songbook as Songbook & { songs: Array<Song & { chordTitle: string; chordData: string; originalKey: number }> };
  const isOwner = data.user === songbook.owner;

  return (
    <Layout user={data.user} title={songbook.title} path={`/songbook/${songbook.id}`}>
      <div style={{ padding: "2rem", maxWidth: "900px", margin: "0 auto" }}>
        <p style={{ color: "#666" }}>Choir: {songbook.choir}</p>

        {isOwner && (
          <div style={{ marginTop: "1rem", marginBottom: "2rem" }}>
            <a
              href={`/songbook/${songbook.id}/edit`}
              style={{
                padding: "0.5rem 1rem",
                backgroundColor: "#ffc107",
                color: "black",
                textDecoration: "none",
                borderRadius: "4px",
                marginRight: "0.5rem",
              }}
            >
              Edit Songbook
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

        <div style={{ marginTop: "2rem" }}>
          <a href="/songbook">← Back to Songbooks</a>
        </div>
      </div>
    </Layout>
  );
}
