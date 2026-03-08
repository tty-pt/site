import { Handlers, PageProps } from "$fresh/server.ts";
import type { State } from "#/routes/_middleware.ts";
import PoemDetail from "@/poem/PoemDetail.tsx";

export const handler: Handlers<unknown, State> = {
  async GET(_req, ctx) {
    return ctx.renderNotFound();
  },

  async POST(req, ctx) {
    const content = await req.text();
    const id = ctx.params.id;
    return ctx.render({ user: ctx.state.user, id, html: content });
  },
};

export default function PoemPage({ data }: PageProps<{ user: string | null; id: string; html: string }>) {
  return <PoemDetail user={data.user} id={data.id} html={data.html} />;
}
