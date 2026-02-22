import React from "https://esm.sh/react@18";
import { Layout } from "../ui.tsx";

export default function PoemList({ user, path, poems }: { user: string | null; path: string; poems: string[] }) {
  const buttons = poems.map((poem) => (
    <a key={poem} href={`/poem/${poem}/`} className="btn wsnw h">
      {poem}
    </a>
  ));

  return (
    <Layout user={user} title="poem" path={path}>
      <div className="v f fic">
        {buttons.length > 0 ? buttons : <p>No poems yet.</p>}
        <a className="btn" href="/poem/add">
          Add Poem
        </a>
      </div>
    </Layout>
  );
}
