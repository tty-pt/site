import { Layout } from "../../../ssr/ui.tsx";

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

export default function IndexList({
  module,
  body,
  title,
}: {
  module: string;
  body: string | null;
  title?: string;
}) {
  const items = parseBody(body);

  const buttons = items.map((item) => (
    <a key={item.id} href={`/${module}/${item.id}/`} className="btn">
      {item.title || item.id}
    </a>
  ));

  return (
    <Layout user={null} title={title || module} path={`/${module}`}>
      <div className="center">
        {buttons.length > 0 ? (
          <div className="btn-row">{buttons}</div>
        ) : (
          <p>No items yet.</p>
        )}
        <a href={`/${module}/add`} className="btn">
          Add
        </a>
      </div>
    </Layout>
  );
}
