import { EmptyState, Layout, ItemMenu } from "@/ssr/ui.tsx";
import { moduleItemPath } from "@/ssr/ui.tsx";
import PoemView from "#/islands/PoemView.tsx";

export interface PoemDetailProps {
  user: string | null;
  id: string;
  title: string;
  lang: string;
  owner: boolean;
}

function PoemContent({ id, lang }: { id: string; lang: string }) {
  if (!lang) {
    return <EmptyState message="No content yet." />;
  }

  return <PoemView id={id} lang={lang} />;
}

export default function PoemDetail({ user, id, title, lang, owner }: PoemDetailProps) {
  return (
    <Layout
      user={user}
      title={title || `poem: ${id}`}
      path={moduleItemPath("poem", id)}
      icon="📜"
      menuItems={<ItemMenu module="poem" id={id} isOwner={owner} />}
    >
      <PoemContent id={id} lang={lang} />
    </Layout>
  );
}
