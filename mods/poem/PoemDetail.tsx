import { Layout } from "@/ssr/ui.tsx";

export default function PoemDetail({ user, id, html }: { user: string | null; id: string; html: string }) {
  return (
    <Layout user={user} title={`poem: ${id}`} path={`/poem/${id}`}>
      <div className="flex flex-col items-center gap-2">
        {html ? (
          <div dangerouslySetInnerHTML={{ __html: html }} />
        ) : (
          <p>No content yet.</p>
        )}
      </div>
    </Layout>
  );
}
