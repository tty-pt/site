import { Layout, ItemMenu } from "@/ssr/ui.tsx";
import PoemView from "#/islands/PoemView.tsx";

export default function PoemDetail({ user, id, title, lang, owner }: { user: string | null; id: string; title: string; lang: string; owner: boolean }) {
  return (
    <Layout user={user} title={title || `poem: ${id}`} path={`/poem/${id}`} icon="📜"
      menuItems={<ItemMenu module="poem" id={id} isOwner={owner} />}>
      {lang ? (
        <PoemView id={id} lang={lang} />
      ) : (
        <p>No content yet.</p>
      )}
    </Layout>
  );
}
