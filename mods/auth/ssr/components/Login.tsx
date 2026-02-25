import React from "https://esm.sh/react@18";
import { Layout } from "../../../ssr/ui.tsx";

export default function Login({ user, path }: { user: string | null; path: string }) {
  return (
    <Layout user={user} title="Login" path={path}>
      <form action="/login" method="POST">
        <label>
          Username: <input required name="username" />
        </label>
        <label>
          Password: <input required type="password" name="password" />
        </label>
        <input type="hidden" name="ret" value="/" />
        <button type="submit">Login</button>
      </form>
    </Layout>
  );
}
