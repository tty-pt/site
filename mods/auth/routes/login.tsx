import { Handlers, PageProps } from "$fresh/server.ts";
import type { State } from "#/routes/_middleware.ts";
import Login from "@/auth/Login.tsx";

export const handler: Handlers<unknown, State> = {
  GET(req, ctx) {
    const ret = new URL(req.url).searchParams.get("ret") ?? "/";
    return ctx.render({ user: ctx.state.user, ret });
  },
};

export default function LoginPage({ data }: PageProps<{ user: string | null; ret: string }>) {
  return <Login user={data.user} ret={data.ret} />;
}
