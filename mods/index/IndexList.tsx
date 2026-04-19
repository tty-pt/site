import { Handlers, PageProps } from "$fresh/server.ts";
import { Layout, OwnerActions } from "@/ssr/ui.tsx";
import type { State } from "#/routes/_middleware.ts";

interface IndexData {
  user: string | null;
  body: string;
  module: string;
}

export const handler: Handlers<IndexData, State> = {
  async POST(req, ctx) {
    const splits = req.url.split('/').filter(Boolean);
    const module = splits.pop()!;
    const body = await req.text();
    return ctx.render({ 
      user: ctx.state.user,
      body,
      module,
    });
  },
};

interface IndexItem {
  id: string;
  title: string;
}

function parseBody(body: string | null): IndexItem[] {
  if (!body) return [];

  const items: IndexItem[] = [];
  const lines = body.trim().split("\r\n");

  for (const line of lines) {
    const spaceIdx = line.indexOf(" ");
    if (spaceIdx > 0) {
      items.push({
        id: line.slice(0, spaceIdx),
        title: line.slice(spaceIdx + 1),
      });
    }
  }

  return items;
}

function IndexList({
  body,
  module,
  user,
}: IndexData) {
  const items = parseBody(body);

  const buttons = items.map((item) => (
    <a
      key={item.id}
      href={`/${module}/${item.id}/`}
      className="btn"
    >
      {item.title || item.id}
    </a>
  ));

  return (
    <Layout
      user={user}
      title={module}
      path={`/${module}`}
      icon="🏠"
      menuItems={user
        ? <OwnerActions actions={[{ href: `/${module}/add`, icon: "➕", label: "add" }]} />
        : undefined}
    >
      <div className="center">
        {buttons.length > 0 ? (
          buttons
        ) : (
          <p>No items yet.</p>
        )}
      </div>
    </Layout>
  );
}

export default function Index({ data }: PageProps<IndexData>) {
  return <IndexList module={data.module} body={data.body} user={data.user} />;
}
