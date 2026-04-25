import { Handlers, PageProps } from "$fresh/server.ts";
import { EmptyState, Layout, ItemMenu, ErrorPage, PageSection, moduleItemPath, modulePath, readPostedJson } from "@/ssr/ui.tsx";
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

interface SongbookSong {
  chordId: string;
  transpose: number;
  format: string;
  chordTitle: string;
  chordData: string;
  originalKey: number;
}

interface SbData {
  user: string | null;
  songbook: Songbook | null;
  error?: string;
  status?: number;
}

interface Songbook {
  id: string;
  title: string;
  owner: string;
  choir: string;
  songs: SongbookSong[];
}

interface SongbookPayload {
  title?: string;
  owner?: string;
  choir?: string;
  songs?: Song[];
}

function toSongbook(id: string, payload: SongbookPayload): Songbook {
  return {
    id,
    title: payload.title || "",
    owner: payload.owner || "",
    choir: payload.choir || "",
    songs: (payload.songs || []).map((song) => ({
      chordId: song.chordId || "",
      transpose: song.transpose || 0,
      format: song.format || "",
      chordTitle: song.chordTitle || "",
      chordData: song.chordData || "",
      originalKey: song.originalKey || 0,
    })),
  };
}

function LinkedChoir({ choirId }: { choirId: string }) {
  if (!choirId) return null;
  return (
    <div className="flex justify-end text-xs text-muted">
      <a href={moduleItemPath("choir", choirId)} className="text-muted">
        {choirId}
      </a>
    </div>
  );
}

function SongbookSongs({
  songbookId,
  songs,
  isOwner,
}: {
  songbookId: string;
  songs: SongbookSong[];
  isOwner: boolean;
}) {
  return (
    <PageSection title="Songs">
      {songs.length === 0 ? (
        <EmptyState message="No songs yet." />
      ) : (
        <div className="flex flex-col gap-1">
          {songs.map((song, index) => (
            <SongItem
              key={index}
              chordId={song.chordId}
              transpose={song.transpose}
              format={song.format}
              chordTitle={song.chordTitle}
              chordData={song.chordData}
              originalKey={song.originalKey}
              isOwner={isOwner}
              songbookId={songbookId}
              index={index}
            />
          ))}
        </div>
      )}
    </PageSection>
  );
}

export const handler: Handlers<SbData, State> = {
  async POST(req, ctx) {
    const result = await readPostedJson<SongbookPayload>(req);
    if (!result.ok) {
      return ctx.render({ user: ctx.state.user, songbook: null, error: result.error, status: result.status }, { status: result.status });
    }

    return ctx.render({
      user: ctx.state.user,
      songbook: toSongbook(ctx.params.id, result.data),
    });
  },
};

export default function SbDetail({ data }: PageProps<SbData>) {
  if (data.error || !data.songbook) {
    return <ErrorPage status={data.status ?? 404} message={data.error ?? "Songbook not found"} user={data.user} path={modulePath("songbook")} />;
  }

  const songbook = data.songbook;
  const isOwner = data.user === songbook.owner;

  return (
    <Layout
      user={data.user}
      title={`songbook: ${songbook.title}`}
      path={moduleItemPath("songbook", songbook.id)}
      icon="📖"
      menuItems={<ItemMenu module="songbook" id={songbook.id} isOwner={isOwner} />}
    >
      <div className="flex flex-col gap-1">
        <LinkedChoir choirId={songbook.choir} />
        <SongbookSongs songbookId={songbook.id} songs={songbook.songs} isOwner={isOwner} />
      </div>
    </Layout>
  );
}
