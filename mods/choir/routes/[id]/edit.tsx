import { Handlers, PageProps } from "$fresh/server.ts";
import { Layout } from "@/ssr/ui.tsx";
import type { State } from "#/routes/_middleware.ts";

interface ChoirEditData {
  user: string | null;
  id: string;
  title: string;
  formats: string[];
}

export const handler: Handlers<ChoirEditData, State> = {
  async POST(req, ctx) {
    const body = await req.text();
    const params = new URLSearchParams(body);
    const id = params.get("id") ?? ctx.params.id;
    const title = params.get("title") ?? "";
    const formatStr = params.get("format") ?? "";
    const formats = formatStr.split("\n").map((f) => f.trim()).filter(Boolean);

    return ctx.render({ user: ctx.state.user, id, title, formats });
  },
};

export default function ChoirEdit({ data }: PageProps<ChoirEditData>) {
  const { user, id, title, formats } = data;

  return (
    <Layout user={user} title={`Edit ${title}`} path={`/choir/${id}/edit`} icon="🎶">
      <div className="center">
        <form
          method="POST"
          action={`/api/choir/${id}/edit`}
          encType="multipart/form-data"
          className="flex flex-col gap-4 w-full max-w-lg"
        >
          <label>
            Choir Name:
            <input
              type="text"
              name="title"
              defaultValue={title}
              required
              className="w-full"
            />
          </label>
          <label>
            Song Formats (one per line):
            <textarea
              name="format"
              rows={10}
              defaultValue={formats.join("\n")}
              className="w-full font-mono"
            />
          </label>
          <div className="flex gap-2">
            <button type="submit" className="btn btn-primary">
              Save Changes
            </button>
            <a href={`/choir/${id}`} className="btn btn-secondary">
              Cancel
            </a>
          </div>
        </form>
      </div>
    </Layout>
  );
}
