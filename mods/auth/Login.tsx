import { Field, FormPage, authPath } from "../ssr/ui.tsx";

interface LoginProps {
  user: string | null;
  ret: string;
  error?: string;
}

export default function Login({ user, ret, error }: LoginProps) {
  return (
    <FormPage user={user} title="Login" path={authPath("login")} icon="🔑">
      {error && <p class="error">{error}</p>}
      <form action="/auth/login" method="POST" className="flex flex-col gap-4">
        <Field label="Username:">
          <input required name="username" />
        </Field>
        <Field label="Password:">
          <input required type="password" name="password" />
        </Field>
        <input type="hidden" name="ret" value={ret} />
        <button type="submit">Login</button>
      </form>
    </FormPage>
  );
}
