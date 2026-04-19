import { Handlers, PageProps } from "$fresh/server.ts";
import { Layout } from "@/ssr/ui.tsx";
import type { State } from "#/routes/_middleware.ts";

interface SongEditData {
  user: string | null;
  id: string;
  title: string;
  type: string;
  yt: string;
  audio: string;
  pdf: string;
  data: string;
  author: string;
}

function urlDecode(str: string): string {
  return str.replace(/%([0-9A-Fa-f]{2})/g, (_, hex) =>
    String.fromCharCode(parseInt(hex, 16))
  ).replace(/%0A/g, "\n");
}

function parseBody(body: string | null): SongEditData {
  if (!body) {
    return { user: null, id: "", title: "", type: "", yt: "", audio: "", pdf: "", data: "", author: "" };
  }

  const params = new URLSearchParams(body);
  return {
    user: null,
    id: "",
    title: urlDecode(params.get("title") || ""),
    type: urlDecode(params.get("type") || ""),
    yt: urlDecode(params.get("yt") || ""),
    audio: urlDecode(params.get("audio") || ""),
    pdf: urlDecode(params.get("pdf") || ""),
    data: urlDecode(params.get("data") || ""),
    author: urlDecode(params.get("author") || ""),
  };
}

export const handler: Handlers<SongEditData, State> = {
  async POST(req, ctx) {
    const body = await req.text();
    const data = parseBody(body);
    data.id = ctx.params.id;

    return ctx.render({
      ...data,
      user: ctx.state.user,
    });
  },
};

export default function SongEdit({ data }: PageProps<SongEditData>) {
  const { id, title, type, yt, audio, pdf, data: songData, author } = data;

  return (
    <Layout user={data.user} title={`Edit ${title || id}`} path={`/song/${id}/edit`} icon="🎸">
      <div className="center">
        <h1>Edit Song</h1>
        <form
          method="POST"
          action={`/song/${id}/edit`}
          encType="application/x-www-form-urlencoded"
          style={{ display: "flex", flexDirection: "column", gap: "1rem", maxWidth: "600px" }}
        >
          <label>
            Title:
            <input
              type="text"
              name="title"
              defaultValue={title}
              style={{ display: "block", marginTop: "0.25rem", width: "100%", padding: "0.5rem" }}
            />
          </label>

          <label>
            Author:
            <input
              type="text"
              name="author"
              defaultValue={author}
              style={{ display: "block", marginTop: "0.25rem", width: "100%", padding: "0.5rem" }}
            />
          </label>
          
          <label>
            Type (e.g., entrada, santo, comunhao):
            <textarea
              name="type"
              defaultValue={type}
              rows={3}
              style={{ display: "block", marginTop: "0.25rem", width: "100%", padding: "0.5rem", fontFamily: "monospace" }}
            />
          </label>
          
          <label>
            YouTube URL:
            <input
              type="text"
              name="yt"
              defaultValue={yt}
              style={{ display: "block", marginTop: "0.25rem", width: "100%", padding: "0.5rem" }}
            />
          </label>
          
          <label>
            Audio URL:
            <input
              type="text"
              name="audio"
              defaultValue={audio}
              style={{ display: "block", marginTop: "0.25rem", width: "100%", padding: "0.5rem" }}
            />
          </label>
          
          <label>
            PDF URL:
            <input
              type="text"
              name="pdf"
              defaultValue={pdf}
              style={{ display: "block", marginTop: "0.25rem", width: "100%", padding: "0.5rem" }}
            />
          </label>
          
          <label>
            Chord Data:
            <textarea
              name="data"
              defaultValue={songData}
              rows={20}
              style={{ display: "block", marginTop: "0.25rem", width: "100%", padding: "0.5rem", fontFamily: "monospace", whiteSpace: "pre" }}
            />
          </label>
          
          <div style={{ display: "flex", gap: "0.5rem" }}>
            <button
              type="submit"
              style={{ padding: "0.5rem 1rem", backgroundColor: "#28a745", color: "white", border: "none", borderRadius: "4px", cursor: "pointer" }}
            >
              Save Changes
            </button>
            <a
              href={`/song/${id}`}
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
