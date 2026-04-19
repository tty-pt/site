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
          className="flex flex-col gap-4 w-full max-w-2xl"
        >
          <label>
            Title:
            <input
              type="text"
              name="title"
              defaultValue={title}
              className="block mt-1 w-full"
            />
          </label>

          <label>
            Author:
            <input
              type="text"
              name="author"
              defaultValue={author}
              className="block mt-1 w-full"
            />
          </label>
          
          <label>
            Type (e.g., entrada, santo, comunhao):
            <textarea
              name="type"
              defaultValue={type}
              rows={3}
              className="block mt-1 w-full font-mono"
            />
          </label>
          
          <label>
            YouTube URL:
            <input
              type="text"
              name="yt"
              defaultValue={yt}
              className="block mt-1 w-full"
            />
          </label>
          
          <label>
            Audio URL:
            <input
              type="text"
              name="audio"
              defaultValue={audio}
              className="block mt-1 w-full"
            />
          </label>
          
          <label>
            PDF URL:
            <input
              type="text"
              name="pdf"
              defaultValue={pdf}
              className="block mt-1 w-full"
            />
          </label>
          
          <label>
            Chord Data:
            <textarea
              name="data"
              defaultValue={songData}
              rows={20}
              className="block mt-1 w-full font-mono whitespace-pre"
            />
          </label>
          
          <div className="flex gap-2">
            <button type="submit" className="btn btn-primary">
              Save Changes
            </button>
            <a href={`/song/${id}`} className="btn btn-secondary">
              Cancel
            </a>
          </div>
        </form>
      </div>
    </Layout>
  );
}
