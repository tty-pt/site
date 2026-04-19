import { Handlers, PageProps } from "$fresh/server.ts";
import { IndexAdd } from "@/index/IndexAdd.tsx";
import type { State } from "#/routes/_middleware.ts";

interface AddData {
  user: string | null;
  extraFields: { name: string; value: string }[];
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
    const extraFields = choir ? [{ name: "choir", value: choir }] : [];
    return ctx.render({ user: ctx.state.user, extraFields });
  },
};

export default function SongbookAdd({ data }: PageProps<AddData>) {
  return <IndexAdd user={data.user} module="songbook" extraFields={data.extraFields} />;
}
