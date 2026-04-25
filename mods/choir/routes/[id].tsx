import { Handlers, PageProps } from "$fresh/server.ts";
import { Layout, ItemMenu, ErrorPage, PageSection, moduleItemPath, modulePath, readPostedJson } from "@/ssr/ui.tsx";
import type { State } from "#/routes/_middleware.ts";
import { KEY_NAMES_SHARP as KEY_NAMES } from "@/ssr/keys.ts";

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
  status?: number;
}

interface Choir {
  id: string;
  title: string;
  owner: string;
  counter: string;
  formats: string[];
}

function ChoirMenu({ choirId, isOwner }: { choirId: string; isOwner: boolean }) {
  return (
    <>
      <ItemMenu module="choir" id={choirId} isOwner={isOwner} />
      {isOwner && (
        <a href={`/songbook/add?choir=${choirId}`} className="btn">
          <span>➕</span>
          <label>add songbook</label>
        </a>
      )}
    </>
  );
}

function ChoirOwnerLink({ owner }: { owner: string }) {
  if (!owner) return null;
  return (
    <div className="flex justify-end text-xs text-muted w-full">
      <a href={`/${owner}/`} className="text-muted">{owner}</a>
    </div>
  );
}

function ChoirSongbooks({ songbooks }: { songbooks: SongEntry[] }) {
  return (
    <PageSection title="Songbooks">
      {songbooks.length === 0 ? (
        <p className="text-muted">No songbooks yet.</p>
      ) : (
        <div className="center">
          {songbooks.map((songbook) => (
            <a key={songbook.id} href={`/songbook/${songbook.id}`} className="btn">
              {songbook.title || songbook.id}
            </a>
          ))}
        </div>
      )}
    </PageSection>
  );
}

function ChoirRepertoire({
  choirId,
  songs,
  isOwner,
}: {
  choirId: string;
  songs: ChoirSong[];
  isOwner: boolean;
}) {
  return (
    <PageSection title="Repertoire">
      {songs.length === 0 ? (
        <p className="text-muted">No songs in repertoire yet.</p>
      ) : (
        <ul className="list-none p-0 text-left w-full max-w-lg mx-auto">
          {songs.map((song) => (
            <li key={song.id} className="p-2 border-b border-muted flex justify-between items-center">
              <a href={`/choir/${choirId}/song/${song.id}`} className="flex-1">
                {song.title || song.id}
              </a>
              <span className="text-muted mr-4">
                {KEY_NAMES[(song.preferredKey || song.originalKey) % 12]}
              </span>
              {isOwner && (
                <form method="POST" action={`/api/choir/${choirId}/song/${song.id}/remove`} className="inline">
                  <button type="submit" className="btn btn-danger py-1 px-2 text-xs">
                    Remove
                  </button>
                </form>
              )}
            </li>
          ))}
        </ul>
      )}
    </PageSection>
  );
}

function ChoirAddSongForm({
  choirId,
  allSongs,
}: {
  choirId: string;
  allSongs: SongEntry[];
}) {
  return (
    <div className="w-full max-w-lg">
      <details>
        <summary className="cursor-pointer text-blue-600">Add song to repertoire</summary>
        <form method="POST" action={`/api/choir/${choirId}/songs`}>
          <datalist id="choir-songs">
            {allSongs.map((song) => (
              <option key={song.id} value={`${song.title} [${song.id}]`} />
            ))}
          </datalist>
          <div className="btn-row">
            <input list="choir-songs" name="song_id" placeholder="Search song..." required />
            <button type="submit" className="btn">Add</button>
          </div>
        </form>
      </details>
    </div>
  );
}

function ChoirFormats({ formats }: { formats: string[] }) {
  return (
    <PageSection title="Song Formats">
      <pre className="bg-surface p-4 rounded text-left w-full max-w-lg">
        {formats.join("\n")}
      </pre>
    </PageSection>
  );
}


export const handler: Handlers<ChoirData, State> = {
  async POST(req, ctx) {
    const result = await readPostedJson<{
      title?: string;
      owner?: string;
      counter?: string;
      formats?: string;
      songs?: ChoirSong[];
      allSongs?: SongEntry[];
      songbooks?: SongEntry[];
    }>(req);
    if (!result.ok) {
      return ctx.render(
        { user: ctx.state.user, choir: null, songs: [], allSongs: [], songbooks: [], error: result.error, status: result.status },
        { status: result.status },
      );
    }

    const body = result.data;
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
  if (data.error || !data.choir) {
    return <ErrorPage status={data.status ?? 404} message={data.error ?? "Choir not found"} user={data.user} path={modulePath("choir")} />;
  }

  const choir = data.choir;
  const isOwner = data.user === choir.owner;

  return (
    <Layout
      user={data.user}
      title={choir.title}
      path={moduleItemPath("choir", choir.id)}
      icon="🎶"
      menuItems={<ChoirMenu choirId={choir.id} isOwner={isOwner} />}
    >
      <div className="center">
        <ChoirOwnerLink owner={choir.owner} />
        <ChoirSongbooks songbooks={data.songbooks} />
        <ChoirRepertoire choirId={choir.id} songs={data.songs} isOwner={isOwner} />
        {isOwner && <ChoirAddSongForm choirId={choir.id} allSongs={data.allSongs} />}
        <ChoirFormats formats={choir.formats} />
      </div>
    </Layout>
  );
}
