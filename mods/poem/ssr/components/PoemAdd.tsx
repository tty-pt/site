import React from "https://esm.sh/react@18";
import { Layout } from "../ui.tsx";

export default function PoemAdd({ user, path }: { user: string | null; path: string }) {
  return (
    <Layout user={user} title="Add Poem" path={path}>
      <form action="/poem/add" method="POST" encType="multipart/form-data" className="v f fic">
        <label>
          ID: <input required name="id" />
        </label>
        <label>
          File: <input required type="file" name="file" />
        </label>
        <button>Upload</button>
      </form>
    </Layout>
  );
}
