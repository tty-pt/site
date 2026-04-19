import { Layout } from "../ssr/ui.tsx";

export default function Login({ user, ret }: { user: string | null; ret: string }) {
  return (
    <Layout user={user} title="Login" path="/auth/login" icon="🔑">
      <form action="/auth/login" method="POST">
        <label>
          Username: <input required name="username" />
        </label>
        <label>
          Password: <input required type="password" name="password" />
        </label>
        <input type="hidden" name="ret" value={ret} />
        <button type="submit">Login</button>
      </form>
    </Layout>
  );
}
