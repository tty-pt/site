import React from "https://esm.sh/react@18";
import { Layout } from "../../../ssr/ui.tsx";

export default function ChordAdd({ user, path }: { user: string | null; path: string }) {
  return (
    <Layout user={user} title="Add Chord" path={path}>
      <form action="/chords/add" method="POST" encType="multipart/form-data">
        <label>
          ID: <input required name="id" />
        </label>
        <label>
          Title: <input name="title" />
        </label>
        <label>
          Type: <input name="type" placeholder="e.g., Communion, Thanksgiving" />
        </label>
        <label>
          Chord Data:
          <textarea required name="data" rows={10} cols={50} placeholder="Enter chord chart with lyrics"></textarea>
        </label>
        <button type="submit">Upload</button>
      </form>
    </Layout>
  );
}
