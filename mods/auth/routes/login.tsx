import { Handlers, PageProps } from "$fresh/server.ts";
import type { State } from "#/routes/_middleware.ts";
import Login from "@/auth/Login.tsx";

export const handler: Handlers<unknown, State> = {
  GET(_req, ctx) {
    return ctx.render({ user: ctx.state.user });
  },
};

export default function LoginPage({ data }: PageProps<{ user: string | null }>) {
  return <Login user={data.user} />;
}
