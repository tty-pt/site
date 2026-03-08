import { Handlers, PageProps } from "$fresh/server.ts";
import type { State } from "#/routes/_middleware.ts";
import Register from "@/auth/Register.tsx";

export const handler: Handlers<unknown, State> = {
  GET(_req, ctx) {
    return ctx.render({ user: ctx.state.user });
  },
};

export default function RegisterPage({ data }: PageProps<{ user: string | null }>) {
  return <Register user={data.user} />;
}
