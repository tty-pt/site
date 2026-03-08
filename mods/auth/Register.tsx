import { Layout } from "../ssr/ui.tsx";

export default function Register({ user }: { user: string | null }) {
  return (
    <Layout user={user} title="Register" path="/auth/register">
      <form action="/auth/register" method="POST">
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
