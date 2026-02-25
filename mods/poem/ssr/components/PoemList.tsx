import React from "https://esm.sh/react@18";
import { Layout } from "../../../ssr/ui.tsx";

export default function PoemList({ user, path, poems }: { user: string | null; path: string; poems: string[] }) {
  const buttons = poems.map((poem) => (
    <a key={poem} href={`/poem/${poem}/`} className="btn">
      {poem}
    </a>
  ));

  return (
    <Layout user={user} title="poem" path={path}>
      <div className="center">
        {buttons.length > 0 ? (
          <div className="btn-row">{buttons}</div>
        ) : (
          <p>No poems yet.</p>
        )}
        <a href="/poem/add" className="btn">
          Add Poem
        </a>
      </div>
    </Layout>
  );
}
