import React from "https://esm.sh/react@18";
import { Layout } from "../../../ssr/ui.tsx";

export default function PoemDetail({ user, path, id, html }: { user: string | null; path: string; id: string; html: string }) {
  return (
    <Layout user={user} title={`poem: ${id}`} path={path}>
      <div className="flex flex-col items-center gap-2">
        <div dangerouslySetInnerHTML={{ __html: html }} />
      </div>
    </Layout>
  );
}
