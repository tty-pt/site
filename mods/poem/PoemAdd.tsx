import { Layout } from "../ssr/ui.tsx";

export default function PoemAdd({ user, path }: { user: string | null; path: string }) {
  return (
    <Layout user={user} title="Add Poem" path={path}>
      <form action="/poem/add" method="POST" encType="multipart/form-data">
        <label>
          ID: <input required name="id" />
        </label>
        <label>
          Title: <input name="title" />
        </label>
        <label>
          File: <input required type="file" name="file" />
        </label>
        <button type="submit">Upload</button>
      </form>
    </Layout>
  );
}
