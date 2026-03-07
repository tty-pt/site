import { Layout } from "../ssr/ui.tsx";

export default function IndexAdd({
  module,
  user,
}: { module: string, user: string | null }) {
  const path = `/${module}/add`;

  return (
    <Layout user={user} title="Add Item" path={path}>
      <form action={path} method="POST" encType="multipart/form-data">
        <label>
          <span>Title:</span>
          <input name="title" />
        </label>
        <button type="submit">Add</button>
      </form>
    </Layout>
  );
}
