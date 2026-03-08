import { Handlers, PageProps } from "$fresh/server.ts";
import { Layout } from "@/ssr/ui.tsx";
import type { State } from "#/routes/_middleware.ts";
import { dirname, fromFileUrl, resolve } from "@std/path";

const moduleDir = dirname(fromFileUrl(import.meta.url));
const repoRoot = resolve(moduleDir, "../../../..");

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
  owner: string;
  choir: string;
  songs: Song[];
}

async function getSongbook(id: string): Promise<Songbook | null> {
  const sbPath = `${repoRoot}/items/sb/items/${id}`;

  try {
    const title = await Deno.readTextFile(`${sbPath}/title`);
    let owner = "";
    try {
      owner = (await Deno.readTextFile(`${sbPath}/.owner`)).trim();
    } catch {
      // No owner file
    }

    let choir = "";
    try {
      choir = (await Deno.readTextFile(`${sbPath}/choir`)).trim();
    } catch {
      // No choir file
    }

    const dataText = await Deno.readTextFile(`${sbPath}/data.txt`);
    const songs = dataText.trim().split("\n").map((line) => {
      const parts = line.split(":");
      return {
        chordId: parts[0] || "",
        transpose: parseInt(parts[1] || "0", 10),
        format: parts[2] || "any",
      };
    });

    return { id, title: title.trim(), owner, choir, songs };
  } catch {
    return null;
  }
}

async function getAllChords(): Promise<Chord[]> {
  const chordsPath = `${repoRoot}/items/chords/items`;
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
  async GET(req, ctx) {
    const id = ctx.params.id;
    const songbook = await getSongbook(id);

    if (!songbook) {
      return ctx.renderNotFound();
    }

    if (ctx.state.user !== songbook.owner) {
      return ctx.render({
        user: ctx.state.user,
        songbook: null,
        allChords: [],
        error: "permission_denied",
      });
    }

    const allChords = await getAllChords();

    return ctx.render({
      user: ctx.state.user,
      songbook,
      allChords,
    });
  },
};

export default function SbEdit({ data }: PageProps<SbEditData>) {
  if (data.error === "permission_denied") {
    return (
      <Layout user={data.user} title="Permission Denied" path="/sb">
        <div className="center">
          <h1>Permission Denied</h1>
          <p>You don't own this songbook.</p>
          <a href="/sb">← Back to Songbooks</a>
        </div>
      </Layout>
    );
  }

  if (!data.songbook) {
    return (
      <Layout user={data.user} title="Songbook Not Found" path="/sb">
        <div className="center">
          <h1>Songbook Not Found</h1>
          <a href="/sb">← Back to Songbooks</a>
        </div>
      </Layout>
    );
  }

  const songbook = data.songbook;
  const allChords = data.allChords;

  return (
    <Layout user={data.user} title={`Edit ${songbook.title}`} path={`/sb/${songbook.id}/edit`}>
      <div className="center">
        <h1>Edit {songbook.title}</h1>
        <form
          method="POST"
          action={`/api/sb/${songbook.id}/edit`}
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
              href={`/sb/${songbook.id}`}
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
