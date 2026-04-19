import { Handlers, PageProps } from "$fresh/server.ts";
import type { State } from "#/routes/_middleware.ts";
import PoemDetail from "@/poem/PoemDetail.tsx";
import { ErrorPage, parseErrorBody } from "@/ssr/ui.tsx";

interface PoemDetailData {
  user: string | null;
  id: string;
  title: string;
  lang: string;
  owner: boolean;
  error?: string;
  status?: number;
}

export const handler: Handlers<PoemDetailData, State> = {
  async GET(_req, ctx) {
    return ctx.renderNotFound();
  },

  async POST(req, ctx) {
    const body = await req.text();
    const err = parseErrorBody(body);
    if (err) {
      return ctx.render(
        { user: ctx.state.user, id: ctx.params.id, title: "", lang: "pt_PT", owner: false, ...err },
        { status: err.status },
      );
    }
    const data = JSON.parse(body);
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
  if (data.error) {
    return <ErrorPage status={data.status ?? 500} message={data.error} user={data.user} path={`/poem/${data.id}`} />;
  }
  return <PoemDetail user={data.user} id={data.id} title={data.title} lang={data.lang} owner={data.owner} />;
}
