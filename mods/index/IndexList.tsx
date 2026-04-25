import { Handlers, PageProps } from "$fresh/server.ts";
import { EmptyState, Layout, OwnerActions, moduleItemPath, modulePath } from "@/ssr/ui.tsx";
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

function IndexItemLinks({ module, items }: {
  module: string;
  items: IndexItem[];
}) {
  if (items.length === 0) {
    return <EmptyState message="No items yet." />;
  }

  return (
    <>
      {items.map((item) => (
        <a
          key={item.id}
          href={`${moduleItemPath(module, item.id)}/`}
          className="btn"
        >
          {item.title || item.id}
        </a>
      ))}
    </>
  );
}

function IndexList({ body, module, user }: IndexData) {
  const items = parseBody(body);
  const menuItems = user
    ? <OwnerActions actions={[{ href: `/${module}/add`, icon: "➕", label: "add" }]} />
    : undefined;

  return (
    <Layout
      user={user}
      title={module}
      path={modulePath(module)}
      icon="🏠"
      menuItems={menuItems}
    >
      <div className="center">
        <IndexItemLinks module={module} items={items} />
      </div>
    </Layout>
  );
}

export default function Index({ data }: PageProps<IndexData>) {
  return <IndexList module={data.module} body={data.body} user={data.user} />;
}
