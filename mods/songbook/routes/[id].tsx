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
  chordTitle?: string;
  chordData?: string;
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

async function getSongbook(id: string): Promise<Songbook | null> {
  const sbPath = `${repoRoot}/items/songbook/items/${id}`;

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

async function transposeChord(chordId: string, transpose: number): Promise<string> {
  if (!chordId || transpose === 0) {
    try {
      return await Deno.readTextFile(`${repoRoot}/items/chords/items/${chordId}/data.txt`);
    } catch {
      return "";
    }
  }

  try {
    const response = await fetch(`http://127.0.0.1:8080/api/chords/transpose?id=${chordId}&t=${transpose}`);
    if (response.ok) {
      const text = await response.text();
      const lines = text.split("\n");
      return lines.slice(2).join("\n");
    }
  } catch (e) {
    console.error(`Failed to transpose ${chordId}:`, e);
  }

  try {
    return await Deno.readTextFile(`${repoRoot}/items/chords/items/${chordId}/data.txt`);
  } catch {
    return "";
  }
}

async function getChordTitle(chordId: string): Promise<string> {
  try {
    return (await Deno.readTextFile(`${repoRoot}/items/chords/items/${chordId}/title`)).trim();
  } catch {
    return chordId;
  }
}

export const handler: Handlers<SbData, State> = {
  async GET(req, ctx) {
    const id = ctx.params.id;
    const songbook = await getSongbook(id);

    if (!songbook) {
      return ctx.renderNotFound();
    }

    const songsWithChords = await Promise.all(
      songbook.songs.map(async (song) => {
        if (!song.chordId) return { ...song, chordTitle: "", chordData: "" };
        const chordTitle = await getChordTitle(song.chordId);
        const chordData = await transposeChord(song.chordId, song.transpose);
        return { ...song, chordTitle, chordData };
      })
    );

    return ctx.render({
      user: ctx.state.user,
      songbook: { ...songbook, songs: songsWithChords as Song[] },
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

  const songbook = data.songbook as Songbook & { songs: Array<Song & { chordTitle: string; chordData: string }> };
  const isOwner = data.user === songbook.owner;

  return (
    <Layout user={data.user} title={songbook.title} path={`/songbook/${songbook.id}`}>
      <div style={{ padding: "2rem", maxWidth: "900px", margin: "0 auto" }}>
        <h1>{songbook.title}</h1>
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
          <div
            key={i}
            id={`${i}`}
            style={{ marginTop: song.chordId ? "2rem" : "1rem", borderTop: song.chordId ? "2px solid #ddd" : "none", paddingTop: song.chordId ? "1rem" : "0" }}
          >
            {song.chordId ? (
              <>
                <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center" }}>
                  <div>
                    <h4 style={{ margin: 0 }}>
                      <a href={`/chords/${song.chordId}`} target="_blank">{song.chordTitle}</a>
                    </h4>
                    <p style={{ margin: "0.25rem 0", fontSize: "0.9rem", color: "#666" }}>
                      {song.format} • Transpose: {song.transpose > 0 ? "+" : ""}{song.transpose}
                    </p>
                  </div>
                  {isOwner && (
                    <div style={{ display: "flex", gap: "0.5rem" }}>
                      <form
                        method="POST"
                        action={`/songbook/${songbook.id}/transpose`}
                        encType="multipart/form-data"
                        style={{ display: "inline" }}
                      >
                        <input type="hidden" name="n" value={`${i}`} />
                        <select
                          name="t"
                          onChange={(e) => {
                            (e.target as HTMLSelectElement).form?.submit();
                          }}
                          defaultValue={`${song.transpose}`}
                          style={{ padding: "0.25rem" }}
                        >
                          {[-11, -10, -9, -8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11].map((t) => (
                            <option key={t} value={`${t}`}>{t > 0 ? "+" : ""}{t}</option>
                          ))}
                        </select>
                      </form>
                      <form
                        method="POST"
                        action={`/songbook/${songbook.id}/randomize`}
                        encType="multipart/form-data"
                        style={{ display: "inline" }}
                      >
                        <input type="hidden" name="n" value={`${i}`} />
                        <button type="submit" style={{ padding: "0.25rem 0.5rem" }}>🎲</button>
                      </form>
                    </div>
                  )}
                </div>
                <pre
                  style={{
                    fontFamily: "monospace",
                    fontSize: "0.9rem",
                    whiteSpace: "pre-wrap",
                    backgroundColor: "#f9f9f9",
                    padding: "1rem",
                    borderRadius: "4px",
                    marginTop: "0.5rem",
                  }}
                >
                  {song.chordData || "(No chord data)"}
                </pre>
              </>
            ) : (
              <div style={{ padding: "1rem", backgroundColor: "#f5f5f5", borderRadius: "4px" }}>
                <h3>{song.format.toUpperCase()}</h3>
              </div>
            )}
          </div>
        ))}

        <div style={{ marginTop: "2rem" }}>
          <a href="/songbook">← Back to Songbooks</a>
        </div>
      </div>
    </Layout>
  );
}
