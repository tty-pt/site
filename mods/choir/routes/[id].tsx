import { Handlers, PageProps } from "$fresh/server.ts";
import { Layout } from "@/ssr/ui.tsx";
import type { State } from "#/routes/_middleware.ts";
import { dirname, fromFileUrl, resolve } from "@std/path";

const moduleDir = dirname(fromFileUrl(import.meta.url));
const repoRoot = resolve(moduleDir, "../../../..");

interface ChoirData {
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
  const choirPath = `${repoRoot}/items/choir/items/${id}`;

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

export const handler: Handlers<ChoirData, State> = {
  async GET(req, ctx) {
    const id = ctx.params.id;
    const choir = await getChoir(id);

    if (!choir) {
      return ctx.renderNotFound();
    }

    return ctx.render({
      user: ctx.state.user,
      choir,
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

  return (
    <Layout user={data.user} title={choir.title} path={`/choir/${choir.id}`}>
      <div className="center">
        <h1>{choir.title}</h1>
        <p>Choir ID: {choir.id}</p>
        <p>Owner: {choir.owner || "Unknown"}</p>
        <p>Songbooks: {choir.counter}</p>

        {isOwner && (
          <div style={{ marginTop: "1rem", marginBottom: "1rem", display: "flex", gap: "0.5rem" }}>
            <a
              href={`/choir/${choir.id}/edit`}
              className="btn"
              style={{ backgroundColor: "#ffc107", color: "black" }}
            >
              Edit Choir
            </a>
            <a
              href={`/sb/new?choir=${choir.id}`}
              className="btn"
            >
              Create Songbook
            </a>
          </div>
        )}

        <h3 style={{ marginTop: "2rem" }}>Song Formats</h3>
        <pre style={{ backgroundColor: "#f5f5f5", padding: "1rem", borderRadius: "4px", textAlign: "left" }}>
          {choir.formats.join("\n")}
        </pre>

        <div style={{ marginTop: "2rem" }}>
          <a href="/choir">← Back to Choirs</a>
        </div>
      </div>
    </Layout>
  );
}
