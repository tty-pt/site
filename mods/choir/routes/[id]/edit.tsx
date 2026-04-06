import { Handlers, PageProps } from "$fresh/server.ts";
import { Layout } from "@/ssr/ui.tsx";
import type { State } from "#/routes/_middleware.ts";

interface ChoirEditData {
  user: string | null;
  choir: Choir | null;
  error?: string;
}

interface Choir {
  id: string;
  title: string;
  owner: string;
  counter: string;
  formats: string[];
}

async function getChoir(id: string): Promise<Choir | null> {
  const choirPath = `${Deno.cwd()}/items/choir/items/${id}`;

  try {
    const title = await Deno.readTextFile(`${choirPath}/title`);
    let owner = "";
    try {
      owner = (await Deno.readTextFile(`${choirPath}/.owner`)).trim();
    } catch {
      // No owner file
    }

    let counter = "0";
    try {
      counter = (await Deno.readTextFile(`${choirPath}/counter`)).trim();
    } catch {
      // No counter file
    }

    let formats: string[] = [];
    try {
      const formatText = await Deno.readTextFile(`${choirPath}/format`);
      formats = formatText.trim().split("\n").filter((f) => f.length > 0);
    } catch {
      formats = [
        "entrada",
        "aleluia",
        "ofertorio",
        "santo",
        "comunhao",
        "acao_de_gracas",
        "saida",
        "any",
      ];
    }

    return { id, title: title.trim(), owner, counter, formats };
  } catch {
    return null;
  }
}

export const handler: Handlers<ChoirEditData, State> = {
  async GET(req, ctx) {
    const id = ctx.params.id;
    const choir = await getChoir(id);

    if (!choir) {
      return ctx.renderNotFound();
    }

    if (ctx.state.user !== choir.owner) {
      return ctx.render({
        user: ctx.state.user,
        choir: null,
        error: "permission_denied",
      });
    }

    return ctx.render({
      user: ctx.state.user,
      choir,
    });
  },
};

export default function ChoirEdit({ data }: PageProps<ChoirEditData>) {
  if (data.error === "permission_denied") {
    return (
      <Layout user={data.user} title="Permission Denied" path="/choir">
        <div className="center">
          <h1>Permission Denied</h1>
          <p>You don't own this choir.</p>
          <a href="/choir">← Back to Choirs</a>
        </div>
      </Layout>
    );
  }

  if (!data.choir) {
    return (
      <Layout user={data.user} title="Choir Not Found" path="/choir">
        <div className="center">
          <h1>Choir Not Found</h1>
          <a href="/choir">← Back to Choirs</a>
        </div>
      </Layout>
    );
  }

  const choir = data.choir;

  return (
    <Layout user={data.user} title={`Edit ${choir.title}`} path={`/choir/${choir.id}/edit`}>
      <div className="center">
        <h1>Edit {choir.title}</h1>
        <form
          method="POST"
          action={`/api/choir/${choir.id}/edit`}
          encType="multipart/form-data"
          style={{ display: "flex", flexDirection: "column", gap: "1rem", maxWidth: "500px" }}
        >
          <label>
            Choir Name:
            <input
              type="text"
              name="title"
              defaultValue={choir.title}
              required
              style={{ display: "block", marginTop: "0.25rem", width: "100%", padding: "0.5rem" }}
            />
          </label>
          <label>
            Song Formats (one per line):
            <textarea
              name="format"
              rows={10}
              defaultValue={choir.formats.join("\n")}
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
              href={`/choir/${choir.id}`}
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
