import { Handlers, PageProps } from "$fresh/server.ts";
import type { State } from "#/routes/_middleware.ts";
import PoemDetail from "@/poem/PoemDetail.tsx";

interface PoemDetailData {
  user: string | null;
  id: string;
  title: string;
  lang: string;
  owner: boolean;
}

export const handler: Handlers<PoemDetailData, State> = {
  async GET(_req, ctx) {
    return ctx.renderNotFound();
  },

  async POST(req, ctx) {
    const data = JSON.parse(await req.text());
    return ctx.render({
      user: ctx.state.user,
      id: ctx.params.id,
      title: data.title ?? "",
      lang: data.lang ?? "pt_PT",
      owner: data.owner === true,
    });
  },
};

export default function PoemPage({ data }: PageProps<PoemDetailData>) {
  return <PoemDetail user={data.user} id={data.id} title={data.title} lang={data.lang} owner={data.owner} />;
}
