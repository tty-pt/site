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
        <h1>Edit {title}</h1>
        <form
          method="POST"
          action={`/api/choir/${id}/edit`}
          encType="multipart/form-data"
          style={{ display: "flex", flexDirection: "column", gap: "1rem", maxWidth: "500px" }}
        >
          <label>
            Choir Name:
            <input
              type="text"
              name="title"
              defaultValue={title}
              required
              style={{ display: "block", marginTop: "0.25rem", width: "100%", padding: "0.5rem" }}
            />
          </label>
          <label>
            Song Formats (one per line):
            <textarea
              name="format"
              rows={10}
              defaultValue={formats.join("\n")}
              style={{ display: "block", marginTop: "0.25rem", width: "100%", padding: "0.5rem", fontFamily: "monospace" }}
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
              href={`/choir/${id}`}
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
