import React from "https://esm.sh/react@18";
import { Layout } from "../../../ssr/ui.tsx";

export default function Register({ user, path }: { user: string | null; path: string }) {
  return (
    <Layout user={user} title="Register" path={path}>
      <form action="/register" method="POST" className="flex flex-col gap-2">
        <label className="flex flex-col text-xs">
          Username: <input required name="username" className="border border-[#2c2c2c] p-2" />
        </label>
        <label className="flex flex-col text-xs">
          Password: <input required type="password" name="password" className="border border-[#2c2c2c] p-2" />
        </label>
        <label className="flex flex-col text-xs">
          Confirm: <input required type="password" name="password2" className="border border-[#2c2c2c] p-2" />
        </label>
        <label className="flex flex-col text-xs">
          Email: <input required type="email" name="email" className="border border-[#2c2c2c] p-2" />
        </label>
        <button type="submit" className="bg-gray-50 border border-gray-200 px-2 py-1 hover:brightness-105 shadow-sm">Register</button>
      </form>
    </Layout>
  );
}
