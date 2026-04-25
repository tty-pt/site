import { Handlers, PageProps } from "$fresh/server.ts";
import type { State } from "#/routes/_middleware.ts";
import Login from "@/auth/Login.tsx";
import { readPostedForm } from "@/ssr/ui.tsx";

interface LoginData {
  user: string | null;
  ret: string;
  error?: string;
}

export const handler: Handlers<LoginData, State> = {
  GET(req, ctx) {
    const ret = new URL(req.url).searchParams.get("ret") ?? "/";
    return ctx.render({ user: ctx.state.user, ret });
  },

  async POST(req, ctx) {
    const params = await readPostedForm(req);
    const error = params.get("error") ?? undefined;
    const ret = params.get("ret") ?? "/";
    const status = Number(params.get("status") ?? "401");
    return ctx.render({ user: ctx.state.user, ret, error }, { status });
  },
};

export default function LoginPage({ data }: PageProps<LoginData>) {
  return <Login user={data.user} ret={data.ret} error={data.error} />;
}
