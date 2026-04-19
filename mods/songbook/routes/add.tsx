import { Handlers, PageProps } from "$fresh/server.ts";
import { Layout } from "@/ssr/ui.tsx";
import type { State } from "#/routes/_middleware.ts";

interface AddData {
  user: string | null;
  choir: string;
}

export const handler: Handlers<AddData, State> = {
  GET(req, ctx) {
    if (!ctx.state.user) {
      const reqUrl = new URL(req.url);
      const forwardedHost = req.headers.get("X-Forwarded-Host");
      if (forwardedHost) reqUrl.host = forwardedHost;
      const ret = encodeURIComponent(reqUrl.pathname + reqUrl.search);
      return Response.redirect(new URL(`/auth/login?ret=${ret}`, reqUrl), 303);
    }
    const choir = new URL(req.url).searchParams.get("choir") ?? "";
    return ctx.render({ user: ctx.state.user, choir });
  },
};

export default function SongbookAdd({ data }: PageProps<AddData>) {
  return (
    <Layout user={data.user} title="Add Songbook" path="/songbook/add" icon="📖">
      <form action="/songbook/add" method="POST" encType="multipart/form-data">
        <label>
          <span>Title:</span>
          <input name="title" required />
        </label>
        {data.choir && <input type="hidden" name="choir" value={data.choir} />}
        <button type="submit">Add</button>
      </form>
    </Layout>
  );
}
