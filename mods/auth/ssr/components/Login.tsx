import React from "https://esm.sh/react@18";
import { Layout } from "../../../ssr/ui.tsx";

export default function Login({ user, path }: { user: string | null; path: string }) {
  return (
    <Layout user={user} title="Login" path={path}>
      <form action="/login" method="POST" className="flex flex-col gap-2">
        <label className="flex flex-col text-xs">
          Username: <input required name="username" className="border border-[#2c2c2c] p-2" />
        </label>
        <label className="flex flex-col text-xs">
          Password: <input required type="password" name="password" className="border border-[#2c2c2c] p-2" />
        </label>
        <input type="hidden" name="ret" value="/" />
        <button type="submit" className="bg-gray-50 border border-gray-200 px-2 py-1 hover:brightness-105 shadow-sm">Login</button>
      </form>
    </Layout>
  );
}
