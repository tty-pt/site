import { Field, FormPage, authPath } from "../ssr/ui.tsx";

interface RegisterProps {
  user: string | null;
}

export default function Register({ user }: RegisterProps) {
  return (
    <FormPage user={user} title="Register" path={authPath("register")} icon="📝">
      <form action="/auth/register" method="POST" className="flex flex-col gap-4">
        <Field label="Username:">
          <input required name="username" />
        </Field>
        <Field label="Password:">
          <input required type="password" name="password" />
        </Field>
        <Field label="Confirm:">
          <input required type="password" name="password2" />
        </Field>
        <Field label="Email:">
          <input required type="email" name="email" />
        </Field>
        <button type="submit">Register</button>
      </form>
    </FormPage>
  );
}
