import { Layout } from "@/ssr/ui.tsx";

export default function PoemDetail({ user, id, title, html, owner }: { user: string | null; id: string; title: string; html: string; owner: boolean }) {
  return (
    <Layout user={user} title={title || `poem: ${id}`} path={`/poem/${id}`}>
      <div className="flex flex-col items-center gap-2">
        {html ? (
          <div dangerouslySetInnerHTML={{ __html: html }} />
        ) : (
          <p>No content yet.</p>
        )}
        {owner && (
          <div class="flex gap-2">
            <a href={`/poem/${id}/edit`}>Edit</a>
            <a href={`/poem/${id}/delete`}>Delete</a>
          </div>
        )}
      </div>
    </Layout>
  );
}
