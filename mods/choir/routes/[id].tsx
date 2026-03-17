import { Handlers, PageProps } from "$fresh/server.ts";
import { Layout } from "@/ssr/ui.tsx";
import type { State } from "#/routes/_middleware.ts";

interface ChoirSong {
  id: string;
  title: string;
  preferredKey: number;
  format: string;
}

interface ChoirData {
  user: string | null;
  choir: Choir | null;
  songs: ChoirSong[];
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

    return ctx.render({
      user: ctx.state.user,
      choir,
      songs,
    });
  },
};

export default function ChoirDetail({ data }: PageProps<ChoirData>) {
  if (!data.choir) {
    return (
      <Layout user={data.user} title="Choir Not Found" path="/choir">
        <div className="center">
          <h1>Choir Not Found</h1>
          <a href="/choir" className="btn">Back to Choirs</a>
        </div>
      </Layout>
    );
  }

  const choir = data.choir;
  const isOwner = data.user === choir.owner;

  const menuItems = (
    <div className="flex flex-col gap-2">
      <a
        href={`/choir/${choir.id}/edit`}
        className="btn"
      >
        📝 Edit
      </a>
      <a
        href={`/choir/${choir.id}/delete`}
        className="btn"
      >
        🗑 Delete
      </a>
      <a
        href={`/songbook/new?choir=${choir.id}`}
        className="btn"
      >
        + Add Songbook
      </a>
    </div>
  );

  return (
    <Layout user={data.user} title={choir.title} path={`/choir/${choir.id}`} menuItems={menuItems}>
      <div className="center">
        <p>Owner: {choir.owner || "Unknown"}</p>
        <p>Songbooks: {choir.counter}</p>

        <h3 style={{ marginTop: "2rem" }}>Repertoire</h3>
        {data.songs.length === 0 ? (
          <p style={{ color: "#666" }}>No songs in repertoire yet.</p>
        ) : (
          <ul style={{ listStyle: "none", padding: 0, textAlign: "left", maxWidth: "500px", margin: "0 auto" }}>
            {data.songs.map((song) => (
              <li key={song.id} style={{ padding: "0.5rem", borderBottom: "1px solid #eee", display: "flex", justifyContent: "space-between", alignItems: "center" }}>
                <a href={`/choir/${choir.id}/song/${song.id}`} style={{ flex: 1 }}>
                  {song.title || song.id}
                </a>
                <span style={{ color: "#666", marginRight: "1rem" }}>
                  {song.preferredKey !== 0 ? KEY_NAMES[song.preferredKey % 12] : "original"}
                </span>
                {isOwner && (
                  <button
                    className="btn"
                    style={{ padding: "0.25rem 0.5rem", fontSize: "0.8rem", backgroundColor: "#ff6b6b" }}
                    onClick={async () => {
                      if (!confirm(`Remove "${song.title || song.id}" from repertoire?`)) return;
                      await fetch(`/api/choir/${choir.id}/song/${song.id}`, { method: "DELETE" });
                      window.location.reload();
                    }}
                  >
                    Remove
                  </button>
                )}
              </li>
            ))}
          </ul>
        )}

        {isOwner && (
          <div style={{ marginTop: "1rem" }}>
            <details>
              <summary style={{ cursor: "pointer", color: "#0066cc" }}>Add song to repertoire</summary>
              <form
                method="POST"
                action={`/api/choir/${choir.id}/songs`}
                onSubmit={(e) => {
                  e.preventDefault();
                  const form = e.target as HTMLFormElement;
                  const fd = new FormData(form);
                  fetch(`/api/choir/${choir.id}/songs`, {
                    method: "POST",
                    body: fd,
                  }).then(() => window.location.reload());
                }}
                style={{ marginTop: "0.5rem" }}
              >
                <input type="text" name="song_id" placeholder="Song ID" required style={{ padding: "0.5rem", marginRight: "0.5rem" }} />
                <input type="text" name="format" placeholder="Format (e.g., any)" defaultValue="any" style={{ padding: "0.5rem", marginRight: "0.5rem" }} />
                <button type="submit" className="btn">Add</button>
              </form>
            </details>
          </div>
        )}

        <h3 style={{ marginTop: "2rem" }}>Song Formats</h3>
        <pre style={{ backgroundColor: "#f5f5f5", padding: "1rem", borderRadius: "4px", textAlign: "left" }}>
          {choir.formats.join("\n")}
        </pre>

        <a href="/choir/" className="btn">
          Back to Choirs
        </a>
      </div>
    </Layout>
  );
}
