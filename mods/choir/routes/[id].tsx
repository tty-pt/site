import { Handlers, PageProps } from "$fresh/server.ts";
import { Layout, ItemMenu } from "@/ssr/ui.tsx";
import type { State } from "#/routes/_middleware.ts";

interface ChoirSong {
  id: string;
  title: string;
  preferredKey: number;
  originalKey: number;
  format: string;
}

interface SongEntry {
  id: string;
  title: string;
}

interface ChoirData {
  user: string | null;
  choir: Choir | null;
  songs: ChoirSong[];
  allSongs: SongEntry[];
  songbooks: SongEntry[];
  error?: string;
}

interface Choir {
  id: string;
  title: string;
  owner: string;
  counter: string;
  formats: string[];
}

const KEY_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];

export const handler: Handlers<ChoirData, State> = {
  async POST(req, ctx) {
    const body = await req.json();

    const formats = body.formats ? body.formats.split("\n").filter((f: string) => f.length > 0) : [];

    const choir: Choir = {
      id: ctx.params.id,
      title: body.title || "",
      owner: body.owner || "",
      counter: body.counter || "0",
      formats,
    };

    const songs: ChoirSong[] = body.songs || [];
    const allSongs: SongEntry[] = (body.allSongs || []).sort(
      (a: SongEntry, b: SongEntry) => a.title.localeCompare(b.title),
    );
    const songbooks: SongEntry[] = body.songbooks || [];

    return ctx.render({
      user: ctx.state.user,
      choir,
      songs,
      allSongs,
      songbooks,
    });
  },
};

export default function ChoirDetail({ data }: PageProps<ChoirData>) {
  if (!data.choir) {
    return (
      <Layout user={data.user} title="Choir Not Found" path="/choir" icon="🎶">
        <div className="center">
          <h1>Choir Not Found</h1>
        </div>
      </Layout>
    );
  }

  const choir = data.choir;
  const isOwner = data.user === choir.owner;

  const menuItems = (
    <>
      <ItemMenu module="choir" id={choir.id} isOwner={isOwner} />
      {isOwner && <a href={`/songbook/add?choir=${choir.id}`} className="btn"><span>➕</span><label>add songbook</label></a>}
    </>
  );

  return (
    <Layout user={data.user} title={choir.title} path={`/choir/${choir.id}`} icon="🎶" menuItems={menuItems}>
      <div className="center">
        {choir.owner && (
          <div className="flex justify-end text-xs text-muted w-full">
            <a href={`/${choir.owner}/`} className="text-muted">{choir.owner}</a>
          </div>
        )}

        <h3>Songbooks</h3>
        {data.songbooks.length === 0 ? (
          <p className="text-muted">No songbooks yet.</p>
        ) : (
          <div className="center">
            {data.songbooks.map((sb) => (
              <a key={sb.id} href={`/songbook/${sb.id}`} className="btn">{sb.title || sb.id}</a>
            ))}
          </div>
        )}

        <h3>Repertoire</h3>
        {data.songs.length === 0 ? (
          <p className="text-muted">No songs in repertoire yet.</p>
        ) : (
          <ul className="list-none p-0 text-left w-full max-w-lg mx-auto">
            {data.songs.map((song) => (
              <li key={song.id} className="p-2 border-b border-muted flex justify-between items-center">
                <a href={`/choir/${choir.id}/song/${song.id}`} className="flex-1">
                  {song.title || song.id}
                </a>
                <span className="text-muted mr-4">
                  {KEY_NAMES[(song.preferredKey || song.originalKey) % 12]}
                </span>
                {isOwner && (
                  <form method="POST" action={`/api/choir/${choir.id}/song/${song.id}/remove`} className="inline">
                    <button
                      type="submit"
                      className="btn btn-danger py-1 px-2 text-xs"
                    >
                      Remove
                    </button>
                  </form>
                )}
              </li>
            ))}
          </ul>
        )}

        {isOwner && (
          <div className="w-full max-w-lg">
            <details>
              <summary className="cursor-pointer text-blue-600">Add song to repertoire</summary>
              <form
                method="POST"
                action={`/api/choir/${choir.id}/songs`}
              >
                <datalist id="choir-songs">
                  {data.allSongs.map((s) => (
                    <option key={s.id} value={`${s.title} [${s.id}]`} />
                  ))}
                </datalist>
                <div className="btn-row">
                  <input list="choir-songs" name="song_id" placeholder="Search song..." required />
                  <button type="submit" className="btn">Add</button>
                </div>
              </form>
            </details>
          </div>
        )}

        <h3>Song Formats</h3>
        <pre className="bg-surface p-4 rounded text-left w-full max-w-lg">
          {choir.formats.join("\n")}
        </pre>
      </div>
    </Layout>
  );
}
