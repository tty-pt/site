import { Layout } from "../ssr/ui.tsx";

export default function Login({ user }: { user: string | null }) {
  return (
    <Layout user={user} title="Login" path="/auth/login">
      <form action="/auth/login" method="POST">
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
