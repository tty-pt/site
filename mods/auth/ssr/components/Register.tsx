import React from "https://esm.sh/react@18";
import { Layout } from "../../../ssr/ui.tsx";

export default function Register({ user, path }: { user: string | null; path: string }) {
  return (
    <Layout user={user} title="Register" path={path}>
      <form action="/register" method="POST">
        <label>
          Username: <input required name="username" />
        </label>
        <label>
          Password: <input required type="password" name="password" />
        </label>
        <label>
          Confirm: <input required type="password" name="password2" />
        </label>
        <label>
          Email: <input required type="email" name="email" />
        </label>
        <button type="submit">Register</button>
      </form>
    </Layout>
  );
}
