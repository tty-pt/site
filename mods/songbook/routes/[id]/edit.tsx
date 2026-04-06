import { Handlers, PageProps } from "$fresh/server.ts";
import { Layout } from "@/ssr/ui.tsx";
import type { State } from "#/routes/_middleware.ts";

const repoRoot = Deno.cwd();

interface Song {
  chordId: string;
  transpose: number;
  format: string;
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
  error?: string;
}

interface Songbook {
  id: string;
  title: string;
  choir: string;
  songs: Song[];
}

function urlDecode(str: string): string {
  return str.replace(/%([0-9A-Fa-f]{2})/g, (_, hex) =>
    String.fromCharCode(parseInt(hex, 16))
  ).replace(/%0A/g, "\n");
}

function parseBody(body: string | null): { title: string; choir: string; songs: Song[] } {
  if (!body) return { title: "", choir: "", songs: [] };

  const params = new URLSearchParams(body);
  const title = urlDecode(params.get("title") || "");
  const choir = urlDecode(params.get("choir") || "");
  
  const songsStr = params.get("songs") || "";
  const songs: Song[] = songsStr.split("\n").filter(line => line.trim()).map(line => {
    const parts = line.split(":");
    return {
      chordId: parts[0] || "",
      transpose: parseInt(parts[1] || "0", 10),
      format: parts[2] || "any",
    };
  });

  return { title, choir, songs };
}

async function getAllChords(): Promise<Chord[]> {
  const chordsPath = `${repoRoot}/items/song/items`;
  const chords: Chord[] = [];

  try {
    for await (const entry of Deno.readDir(chordsPath)) {
      if (!entry.isDirectory || entry.name.startsWith(".")) continue;

      const chordPath = `${chordsPath}/${entry.name}`;
      let title = entry.name;
      try {
        title = (await Deno.readTextFile(`${chordPath}/title`)).trim();
      } catch {
        // No title file
      }

      let type = "";
      try {
        type = (await Deno.readTextFile(`${chordPath}/type`)).trim();
      } catch {
        // No type file
      }

      chords.push({ id: entry.name, title, type });
    }
  } catch {
    // No chords directory
  }

  return chords.sort((a, b) => a.title.localeCompare(b.title));
}

export const handler: Handlers<SbEditData, State> = {
  async POST(req, ctx) {
    const body = await req.text();
    const { title, choir, songs } = parseBody(body);
    const id = ctx.params.id;

    return ctx.render({
      user: ctx.state.user,
      songbook: { id, title, choir, songs },
      allChords: [],
    });
  },

  async GET(req, ctx) {
    const body = await req.text();
    const { title, choir, songs } = parseBody(body);
    const id = ctx.params.id;

    if (!title && !songs.length) {
      return ctx.renderNotFound();
    }

    const allChords = await getAllChords();

    return ctx.render({
      user: ctx.state.user,
      songbook: { id, title, choir, songs },
      allChords,
    });
  },
};

export default function SbEdit({ data }: PageProps<SbEditData>) {
  if (!data.songbook) {
    return (
      <Layout user={data.user} title="Songbook Not Found" path="/songbook">
        <div className="center">
          <h1>Songbook Not Found</h1>
          <a href="/songbook">← Back to Songbooks</a>
        </div>
      </Layout>
    );
  }

  const songbook = data.songbook;
  const allChords = data.allChords;

  return (
    <Layout user={data.user} title={`Edit ${songbook.title}`} path={`/songbook/${songbook.id}/edit`}>
      <div className="center">
        <h1>Edit {songbook.title}</h1>
        <form
          method="POST"
          action={`/songbook/${songbook.id}/edit`}
          encType="multipart/form-data"
          style={{ display: "flex", flexDirection: "column", gap: "0.5rem" }}
        >
          {songbook.songs.map((song, i) => (
            <div
              key={i}
              style={{ display: "flex", gap: "0.5rem", alignItems: "center" }}
            >
              <label style={{ flex: "0 0 150px" }}>
                {i + 1}. Format:
                <input
                  type="text"
                  name={`fmt_${i}`}
                  defaultValue={song.format}
                  style={{ width: "100%", padding: "0.25rem", marginTop: "0.25rem" }}
                />
              </label>
              <label style={{ flex: 1 }}>
                Song:
                <select
                  name={`song_${i}`}
                  defaultValue={song.chordId}
                  style={{ width: "100%", padding: "0.25rem", marginTop: "0.25rem" }}
                >
                  <option value="">(Empty)</option>
                  {allChords.map((chord) => (
                    <option key={chord.id} value={chord.id}>
                      {chord.title}{chord.type ? ` [${chord.type}]` : ""}
                    </option>
                  ))}
                </select>
              </label>
              <label style={{ flex: "0 0 80px" }}>
                Transpose:
                <select
                  name={`t_${i}`}
                  defaultValue={`${song.transpose}`}
                  style={{ width: "100%", padding: "0.25rem", marginTop: "0.25rem" }}
                >
                  {[-11, -10, -9, -8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11].map((t) => (
                    <option key={t} value={`${t}`}>{t > 0 ? "+" : ""}{t}</option>
                  ))}
                </select>
              </label>
            </div>
          ))}
          <input
            type="hidden"
            name="amount"
            value={`${songbook.songs.length}`}
          />
          <div style={{ marginTop: "1rem", display: "flex", gap: "0.5rem" }}>
            <button
              type="submit"
              style={{ padding: "0.5rem 1rem", backgroundColor: "#28a745", color: "white", border: "none", borderRadius: "4px", cursor: "pointer" }}
            >
              Save Changes
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
