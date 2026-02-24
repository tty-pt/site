import React from "https://esm.sh/react@18";
import { Layout } from "../../../ssr/ui.tsx";

export default function PoemList({ user, path, poems }: { user: string | null; path: string; poems: string[] }) {
  const buttons = poems.map((poem) => (
    <a key={poem} href={`/poem/${poem}/`} className="no-underline px-4 py-2 border border-gray-200 rounded shadow-sm bg-gray-50 hover:brightness-105">
      {poem}
    </a>
  ));

  return (
    <Layout user={user} title="poem" path={path}>
      <div className="flex flex-col items-center gap-2">
        {buttons.length > 0 ? (
          <div className="flex flex-wrap justify-center gap-2">{buttons}</div>
        ) : (
          <p>No poems yet.</p>
        )}
        <a href="/poem/add" className="no-underline px-4 py-2 border border-gray-200 rounded shadow-sm bg-gray-50 hover:brightness-105">
          Add Poem
        </a>
      </div>
    </Layout>
  );
}
