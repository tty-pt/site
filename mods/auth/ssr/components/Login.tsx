import React from "https://esm.sh/react@18";
import { Layout } from "../ui.tsx";

export default function Login({ user, path }: { user: string | null; path: string }) {
  return (
    <Layout user={user} title="Login" path={path}>
      <form action="/login" method="POST" className="v f fic">
        <label>
          Username: <input required name="username" />
        </label>
        <label>
          Password: <input required type="password" name="password" />
        </label>
        <input type="hidden" name="ret" value="/" />
        <button>Login</button>
      </form>
    </Layout>
  );
}
