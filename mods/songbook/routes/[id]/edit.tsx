import { Handlers, PageProps } from "$fresh/server.ts";
import { Layout } from "@/ssr/ui.tsx";
import type { State } from "#/routes/_middleware.ts";
import SongbookEditRow from "#/islands/SongbookEditRow.tsx";

const KEY_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];

interface Song {
  chordId: string;
  transpose: number;
  format: string;
  originalKey: number;
}

interface Chord {
  id: string;
  title: string;
  type: string;
}

interface SbEditData {
  user: string | null;
  songbook: Songbook | null;
  allChords: Chord[];
  allTypes: string[];
}

interface Songbook {
  id: string;
  title: string;
  choir: string;
  songs: Song[];
}

function parseBody(body: string | null): { title: string; choir: string; songs: Song[]; allChords: Chord[]; allTypes: string[] } {
  if (!body) return { title: "", choir: "", songs: [], allChords: [], allTypes: [] };

  const params = new URLSearchParams(body);
  const title = params.get("title") ?? "";
  const choir = params.get("choir") ?? "";

  const songsStr = params.get("songs") ?? "";
  const songs: Song[] = songsStr.split("\n").filter((l) => l.trim()).map((line) => {
    const parts = line.split(":");
    return {
      chordId: parts[0] ?? "",
      transpose: parseInt(parts[1] ?? "0", 10),
      format: parts[2] ?? "any",
      originalKey: parseInt(parts[3] ?? "0", 10),
    };
  });

  let allChords: Chord[] = [];
  const allChordsStr = params.get("allChords");
  if (allChordsStr) {
    try { allChords = JSON.parse(allChordsStr); } catch { allChords = []; }
  }

  let allTypes: string[] = [];
  const allTypesStr = params.get("allTypes");
  if (allTypesStr) {
    try { allTypes = JSON.parse(allTypesStr); } catch { allTypes = []; }
  }

  return { title, choir, songs, allChords, allTypes };
}

export const handler: Handlers<SbEditData, State> = {
  async POST(req, ctx) {
    const body = await req.text();
    const { title, choir, songs, allChords, allTypes } = parseBody(body);
    const id = ctx.params.id;

    return ctx.render({
      user: ctx.state.user,
      songbook: { id, title, choir, songs },
      allChords,
      allTypes,
    });
  },
};

export default function SbEdit({ data }: PageProps<SbEditData>) {
  if (!data.songbook) {
    return (
      <Layout user={data.user} title="Songbook Not Found" path="/songbook" icon="📖">
        <div className="center">
          <h1>Songbook Not Found</h1>
        </div>
      </Layout>
    );
  }

  const songbook = data.songbook;
  const allChords = data.allChords;
  const allTypes = data.allTypes;

  return (
    <Layout user={data.user} title={`Edit ${songbook.title}`} path={`/songbook/${songbook.id}/edit`} icon="📖">
      <div className="center">
        <h1>Edit {songbook.title}</h1>
        <form
          method="POST"
          action={`/songbook/${songbook.id}/edit`}
          encType="multipart/form-data"
          style={{ display: "flex", flexDirection: "column", gap: "0.5rem" }}
        >
          {songbook.songs.map((song, i) => (
            <SongbookEditRow
              key={i}
              index={i}
              chordId={song.chordId}
              transpose={song.transpose}
              format={song.format}
              originalKey={song.originalKey}
              allChords={allChords}
              allTypes={allTypes}
            />
          ))}
          <input
            type="hidden"
            name="amount"
            value={`${songbook.songs.length}`}
          />
          <div style={{ marginTop: "1rem", display: "flex", gap: "0.5rem" }}>
            <button
              type="submit"
              name="action"
              value="save"
              style={{ padding: "0.5rem 1rem", backgroundColor: "#28a745", color: "white", border: "none", borderRadius: "4px", cursor: "pointer" }}
            >
              Save Changes
            </button>
            <button
              type="submit"
              name="action"
              value="add_row"
              style={{ padding: "0.5rem 1rem", backgroundColor: "#007bff", color: "white", border: "none", borderRadius: "4px", cursor: "pointer" }}
            >
              + Add Row
            </button>
            <a
              href={`/songbook/${songbook.id}`}
              style={{ padding: "0.5rem 1rem", backgroundColor: "#6c757d", color: "white", textDecoration: "none", borderRadius: "4px", display: "inline-block" }}
            >
              Cancel
            </a>
          </div>
        </form>
      </div>
    </Layout>
  );
}
