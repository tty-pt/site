import React from "https://esm.sh/react@18";
import { Layout } from "../../../ssr/ui.tsx";

export default function PoemAdd({ user, path }: { user: string | null; path: string }) {
  return (
    <Layout user={user} title="Add Poem" path={path}>
      <form action="/poem/add" method="POST" encType="multipart/form-data" className="flex flex-col gap-2">
        <label className="flex flex-col text-xs">
          ID: <input required name="id" className="border border-[#2c2c2c] p-2" />
        </label>
        <label className="flex flex-col text-xs">
          File: <input required type="file" name="file" className="border border-[#2c2c2c] p-2" />
        </label>
        <button type="submit" className="bg-gray-50 border border-gray-200 px-2 py-1 hover:brightness-105 shadow-sm">Upload</button>
      </form>
    </Layout>
  );
}
