import { Layout } from "@/ssr/ui.tsx";

export default function PoemAdd({ user }: { user: string | null }) {
  return (
    <Layout user={user} title="Add Poem" path="/poem/add">
      <form action="/poem/add" method="POST" encType="multipart/form-data">
        <label>
          Title: <input required name="title" />
        </label>
        <label>
          File: <input type="file" name="file" />
        </label>
        <button type="submit">Upload</button>
      </form>
    </Layout>
  );
}
