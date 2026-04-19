import { Layout, ItemMenu } from "@/ssr/ui.tsx";

export default function PoemDetail({ user, id, title, html, owner }: { user: string | null; id: string; title: string; html: string; owner: boolean }) {
  return (
    <Layout user={user} title={title || `poem: ${id}`} path={`/poem/${id}`} icon="📜"
      menuItems={<ItemMenu module="poem" id={id} isOwner={owner} />}>
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
