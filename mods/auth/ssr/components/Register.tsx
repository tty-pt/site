import React from "https://esm.sh/react@18";
import { Layout } from "../ui.tsx";

export default function Register({ user, path }: { user: string | null; path: string }) {
  return (
    <Layout user={user} title="Register" path={path}>
      <form action="/register" method="POST" className="v f fic">
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
        <button>Register</button>
      </form>
    </Layout>
  );
}
